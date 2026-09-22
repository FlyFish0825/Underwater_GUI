#include "data/recording/ResearchDataRecorder.h"

#include "communication/protocol/ObserverMotorProtocol.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

namespace rov
{

namespace
{

void put(QJsonObject &object, const char *key, const QString &value)
{
    object.insert(QString::fromLatin1(key), value);
}

void put(QJsonObject &object, const char *key, const double value)
{
    object.insert(QString::fromLatin1(key), value);
}

void put(QJsonObject &object, const char *key, const qint64 value)
{
    object.insert(QString::fromLatin1(key), static_cast<double>(value));
}

void put(QJsonObject &object, const char *key, const int value)
{
    object.insert(QString::fromLatin1(key), value);
}

void put(QJsonObject &object, const char *key, const bool value)
{
    object.insert(QString::fromLatin1(key), value);
}

QString typeName(const ResearchDataRecorder::RecordType type)
{
    switch (type)
    {
    case ResearchDataRecorder::RecordType::Frame:
        return QStringLiteral("can_frame");
    case ResearchDataRecorder::RecordType::Dashboard:
        return QStringLiteral("dashboard_snapshot");
    case ResearchDataRecorder::RecordType::Input:
        return QStringLiteral("control_input");
    case ResearchDataRecorder::RecordType::Event:
        return QStringLiteral("event");
    }
    return QStringLiteral("unknown");
}

QString quoteCsv(const QString &value)
{
    QString result = value;
    result.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(result);
}

} // namespace

ResearchDataRecorder::ResearchDataRecorder(QObject *parent) : QThread(parent) {}

ResearchDataRecorder::~ResearchDataRecorder()
{
    stopRecording();
}

bool ResearchDataRecorder::startRecording(const QString &path, QString *error)
{
    if (error != nullptr)
        error->clear();

    QFileInfo requested(path);
    if (path.trimmed().isEmpty() || requested.absolutePath().isEmpty() ||
        !QDir(requested.absolutePath()).exists())
    {
        if (error != nullptr)
            *error = QStringLiteral("记录目录不存在");
        return false;
    }

    QMutexLocker locker(&m_mutex);
    if (m_accepting || isRunning())
    {
        if (error != nullptr)
            *error = QStringLiteral("已有记录会话正在运行");
        return false;
    }

    QString basePath = requested.absoluteFilePath();
    if (basePath.endsWith(QStringLiteral(".jsonl"), Qt::CaseInsensitive) ||
        basePath.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
    {
        basePath.chop(basePath.endsWith(QStringLiteral(".jsonl"), Qt::CaseInsensitive)
                          ? QStringLiteral(".jsonl").size()
                          : QStringLiteral(".csv").size());
    }
    m_sessionPath = basePath + QStringLiteral(".jsonl");
    m_lastDirectory = QFileInfo(m_sessionPath).absolutePath();
    m_queue.clear();
    m_accepted = 0;
    m_dropped = 0;
    m_stopRequested = false;
    m_accepting = true;
    m_startedUtc = QDateTime::currentDateTimeUtc();
    m_elapsed.start();
    start();
    return true;
}

void ResearchDataRecorder::stopRecording()
{
    {
        QMutexLocker locker(&m_mutex);
        if (!m_accepting && !isRunning())
            return;
        m_accepting = false;
        m_stopRequested = true;
        m_wait.wakeAll();
        while (!m_workerStarted)
            m_wait.wait(&m_mutex, 10);
    }
    if (QThread::currentThread() != this)
        wait();
}

bool ResearchDataRecorder::isRecording() const
{
    QMutexLocker locker(&m_mutex);
    return m_accepting || isRunning();
}

QString ResearchDataRecorder::lastDirectory() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastDirectory;
}

ResearchDataRecorder::PendingRecord ResearchDataRecorder::makeBaseRecord(
    const RecordType type, const QString &source) const
{
    PendingRecord record;
    record.type = type;
    record.source = source;
    record.utc = QDateTime::currentDateTimeUtc();
    record.monotonicUs = m_elapsed.isValid() ? m_elapsed.nsecsElapsed() / 1000 : 0;
    return record;
}

bool ResearchDataRecorder::enqueue(PendingRecord record)
{
    QMutexLocker locker(&m_mutex);
    if (!m_accepting || m_stopRequested)
        return false;
    if (m_queue.size() >= kQueueLimit)
    {
        ++m_dropped;
        if ((m_dropped % 1000U) == 1U)
            emit statusChanged(true, m_accepted, m_dropped, m_sessionPath);
        return false;
    }
    m_queue.enqueue(std::move(record));
    ++m_accepted;
    m_wait.wakeOne();
    return true;
}

void ResearchDataRecorder::recordFrame(const CanGatewayFrame &frame, const QString &direction)
{
    PendingRecord record = makeBaseRecord(RecordType::Frame, QStringLiteral("can"));
    record.frame = frame;
    record.source = direction;
    enqueue(std::move(record));
}

void ResearchDataRecorder::recordDashboardSnapshot(const DashboardSnapshot &snapshot)
{
    PendingRecord record = makeBaseRecord(RecordType::Dashboard, QStringLiteral("dashboard"));
    record.snapshot = snapshot;
    enqueue(std::move(record));
}

void ResearchDataRecorder::recordControlInput(const SixDofControlRequest &request)
{
    PendingRecord record = makeBaseRecord(RecordType::Input, QStringLiteral("dashboard"));
    record.input = request;
    enqueue(std::move(record));
}

void ResearchDataRecorder::recordEvent(const QString &eventType, const QString &message)
{
    PendingRecord record = makeBaseRecord(RecordType::Event, QStringLiteral("ui"));
    record.eventType = eventType;
    record.message = message;
    enqueue(std::move(record));
}

QJsonObject ResearchDataRecorder::toJson(const PendingRecord &record)
{
    QJsonObject object;
    put(object, "schema_version", 1);
    put(object, "type", typeName(record.type));
    put(object, "timestamp_utc", record.utc.toString(Qt::ISODateWithMs));
    put(object, "monotonic_us", record.monotonicUs);
    put(object, "source", record.source);

    if (record.type == RecordType::Frame)
    {
        put(object, "direction", record.source);
        put(object, "sequence", static_cast<int>(record.frame.sequence));
        put(object, "can_id", static_cast<int>(record.frame.canId));
        put(object, "flags", static_cast<int>(record.frame.flags));
        put(object, "data_hex", QString::fromLatin1(record.frame.data.toHex(' ').toUpper()));

        ObserverMotorProtocol::DecodedFrame decoded;
        QString decodeError;
        if (ObserverMotorProtocol::decode(record.frame, decoded, &decodeError))
        {
            put(object, "motor_node_id", static_cast<int>(decoded.nodeId));
            switch (decoded.kind)
            {
            case ObserverMotorProtocol::FrameKind::Control:
                put(object, "motor_command", static_cast<int>(decoded.control.command));
                put(object, "motor_node_mask", static_cast<int>(decoded.control.nodeMask));
                put(object, "motor_run_mask", static_cast<int>(decoded.control.runMask));
                for (int i = 0; i < 8; ++i)
                    put(object,
                        QStringLiteral("motor_target_rpm_%1").arg(i + 1).toLatin1().constData(),
                        static_cast<int>(decoded.control.speedsRpm[static_cast<size_t>(i)]));
                break;
            case ObserverMotorProtocol::FrameKind::Feedback:
                put(object, "motor_speed_rpm", static_cast<int>(decoded.feedback.speedRpm));
                put(object, "motor_bus_current_a", decoded.feedback.busCurrentA);
                put(object, "motor_bus_voltage_v", decoded.feedback.busVoltageV);
                put(object, "motor_temperature_c", decoded.feedback.temperatureC);
                put(object, "motor_state",
                    ObserverMotorProtocol::stateText(decoded.feedback.state));
                put(object, "motor_feedback_sequence", static_cast<int>(decoded.feedback.sequence));
                break;
            case ObserverMotorProtocol::FrameKind::Debug:
                put(object, "motor_speed_rpm", static_cast<int>(decoded.debug.speedRpm));
                put(object, "motor_iq_a", decoded.debug.iqA);
                put(object, "motor_bus_voltage_v", decoded.debug.busVoltageV);
                put(object, "motor_temperature_c", decoded.debug.temperatureC);
                put(object, "phase_current_u_a", decoded.debug.phaseCurrentU_A);
                put(object, "phase_current_v_a", decoded.debug.phaseCurrentV_A);
                put(object, "phase_current_w_a", decoded.debug.phaseCurrentW_A);
                put(object, "motor_id_a", decoded.debug.idA);
                put(object, "motor_ud_v", decoded.debug.udV);
                put(object, "motor_uq_v", decoded.debug.uqV);
                put(object, "pll_speed_rad_s",
                    static_cast<int>(decoded.debug.pllElectricalSpeedRadPerSec));
                put(object, "observer_angle_deg", decoded.debug.observerElectricalAngleDeg);
                put(object, "motor_status_flags", static_cast<int>(decoded.debug.statusFlags));
                break;
            case ObserverMotorProtocol::FrameKind::Heartbeat:
                put(object, "motor_node_id", static_cast<int>(decoded.heartbeat.nodeId));
                put(object, "motor_temperature_c",
                    static_cast<int>(decoded.heartbeat.temperatureC));
                put(object, "motor_feedback_sequence",
                    static_cast<int>(decoded.heartbeat.feedbackSequence));
                put(object, "motor_state",
                    ObserverMotorProtocol::stateText(decoded.heartbeat.state));
                break;
            case ObserverMotorProtocol::FrameKind::ReservedReply:
                break;
            }
        }
        else
        {
            put(object, "decode_error", decodeError);
        }
    }
    else if (record.type == RecordType::Dashboard)
    {
        put(object, "depth_m", record.snapshot.depthM);
        put(object, "roll_deg", record.snapshot.rollDeg);
        put(object, "pitch_deg", record.snapshot.pitchDeg);
        put(object, "yaw_deg", record.snapshot.yawDeg);
        put(object, "bus_voltage_v", record.snapshot.busVoltageV);
        put(object, "internal_temperature_c", record.snapshot.internalTemperatureC);
        put(object, "armed", record.snapshot.armed);
        put(object, "connected", record.snapshot.connected);
        put(object, "alarm_count", record.snapshot.alarmCount);
        QJsonArray thrusters;
        for (const ThrusterTelemetry &thruster : record.snapshot.thrusters)
        {
            QJsonObject item;
            put(item, "id", thruster.id);
            put(item, "rpm", thruster.rpm);
            put(item, "current_a", thruster.currentA);
            put(item, "temperature_c", thruster.temperatureC);
            thrusters.append(item);
        }
        object.insert(QStringLiteral("thrusters"), thrusters);
        // IMU and pressure keys are intentionally null until their real protocol is connected.
        object.insert(QStringLiteral("imu"), QJsonValue(QJsonValue::Null));
        object.insert(QStringLiteral("pressure_pa"), QJsonValue(QJsonValue::Null));
    }
    else if (record.type == RecordType::Input)
    {
        put(object, "input_surge", record.input.surge);
        put(object, "input_sway", record.input.sway);
        put(object, "input_heave", record.input.heave);
        put(object, "input_roll", record.input.roll);
        put(object, "input_pitch", record.input.pitch);
        put(object, "input_yaw", record.input.yaw);
    }
    else
    {
        put(object, "event_type", record.eventType);
        put(object, "event_message", record.message);
    }
    return object;
}

QByteArray ResearchDataRecorder::csvHeader()
{
    return QByteArrayLiteral(
        "schema_version,type,timestamp_utc,monotonic_us,source,direction,sequence,can_id,flags,"
        "data_hex,"
        "motor_node_id,motor_command,motor_node_mask,motor_run_mask,motor_speed_rpm,"
        "motor_bus_current_a,motor_iq_a,"
        "motor_bus_voltage_v,motor_temperature_c,motor_state,motor_feedback_sequence,"
        "motor_target_rpm_1,motor_target_rpm_2,motor_target_rpm_3,motor_target_rpm_4,"
        "motor_target_rpm_5,motor_target_rpm_6,motor_target_rpm_7,motor_target_rpm_8,"
        "phase_current_u_a,phase_current_v_a,phase_current_w_a,motor_id_a,motor_ud_v,motor_uq_v,"
        "pll_speed_rad_s,observer_angle_deg,motor_status_flags,depth_m,roll_deg,pitch_deg,yaw_deg,"
        "bus_voltage_v,internal_temperature_c,armed,connected,alarm_count,pressure_pa,"
        "imu_accel_x,imu_accel_y,imu_accel_z,imu_gyro_x,imu_gyro_y,imu_gyro_z,"
        "input_surge,input_sway,input_heave,input_roll,input_pitch,input_yaw,event_type,event_"
        "message,decode_error\n");
}

QString ResearchDataRecorder::csvValue(const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull())
        return QString();
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 15);
    return value.toString();
}

QByteArray ResearchDataRecorder::csvRow(const QJsonObject &object)
{
    static const QStringList columns =
        QString::fromLatin1(csvHeader()).trimmed().split(QLatin1Char(','));
    QStringList values;
    values.reserve(columns.size());
    for (const QString &column : columns)
        values.append(quoteCsv(csvValue(object.value(column))));
    return (values.join(QLatin1Char(',')) + QLatin1Char('\n')).toUtf8();
}

void ResearchDataRecorder::writeBatch(QFile &jsonl, QFile &csv, const QVector<PendingRecord> &batch,
                                      quint64 &written, QString &error)
{
    QByteArray jsonBytes;
    QByteArray csvBytes;
    jsonBytes.reserve(batch.size() * 300);
    csvBytes.reserve(batch.size() * 300);
    for (const PendingRecord &record : batch)
    {
        const QJsonObject object = toJson(record);
        jsonBytes += QJsonDocument(object).toJson(QJsonDocument::Compact);
        jsonBytes += '\n';
        csvBytes += csvRow(object);
        ++written;
    }
    if (jsonl.write(jsonBytes) != jsonBytes.size() || csv.write(csvBytes) != csvBytes.size())
    {
        error = QStringLiteral("记录文件写入失败");
        return;
    }
}

void ResearchDataRecorder::run()
{
    QString sessionPath;
    QDateTime startedUtc;
    quint64 accepted = 0;
    quint64 dropped = 0;
    {
        QMutexLocker locker(&m_mutex);
        sessionPath = m_sessionPath;
        startedUtc = m_startedUtc;
        m_workerStarted = true;
        m_wait.wakeAll();
    }

    QFile jsonl(sessionPath);
    QFile csv(QFileInfo(sessionPath).path() + QLatin1Char('/') +
              QFileInfo(sessionPath).completeBaseName() + QStringLiteral(".csv"));
    if (!jsonl.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        !csv.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        emit errorOccurred(QStringLiteral("无法创建科研记录文件：%1").arg(sessionPath));
        QMutexLocker locker(&m_mutex);
        m_accepting = false;
        m_workerStarted = false;
        m_stopRequested = true;
        m_wait.wakeAll();
        emit statusChanged(false, 0, 0, sessionPath);
        return;
    }
    csv.write(csvHeader());

    quint64 written = 0;
    QString writeError;
    QElapsedTimer flushTimer;
    QElapsedTimer statusTimer;
    flushTimer.start();
    statusTimer.start();
    for (;;)
    {
        QVector<PendingRecord> batch;
        {
            QMutexLocker locker(&m_mutex);
            while (m_queue.isEmpty() && !m_stopRequested)
                m_wait.wait(&m_mutex);
            while (!m_queue.isEmpty() && batch.size() < 256)
                batch.append(m_queue.dequeue());
            accepted = m_accepted;
            dropped = m_dropped;
            if (batch.isEmpty() && m_stopRequested)
                break;
        }
        writeBatch(jsonl, csv, batch, written, writeError);
        if (!writeError.isEmpty())
        {
            emit errorOccurred(writeError);
            break;
        }
        if (flushTimer.elapsed() >= 250)
        {
            jsonl.flush();
            csv.flush();
            flushTimer.restart();
        }
        if (statusTimer.elapsed() >= 250)
        {
            emit statusChanged(true, accepted, dropped, sessionPath);
            statusTimer.restart();
        }
    }

    jsonl.flush();
    csv.flush();
    jsonl.close();
    csv.close();

    QFile meta(QFileInfo(sessionPath).path() + QLatin1Char('/') +
               QFileInfo(sessionPath).completeBaseName() + QStringLiteral(".meta.json"));
    if (meta.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QJsonObject object;
        put(object, "schema_version", 1);
        put(object, "format", QStringLiteral("JSONL + CSV"));
        put(object, "started_utc", startedUtc.toString(Qt::ISODateWithMs));
        put(object, "finished_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        put(object, "accepted_records", static_cast<qint64>(accepted));
        put(object, "written_records", static_cast<qint64>(written));
        put(object, "dropped_records", static_cast<qint64>(dropped));
        put(object, "queue_limit", kQueueLimit);
        put(object, "timestamp_policy",
            QStringLiteral("UTC ISO-8601 + session monotonic microseconds"));
        put(object, "imu_depth_policy",
            QStringLiteral("真实协议接入前保留空值；原始 CAN 帧不丢弃"));
        meta.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
        meta.close();
    }

    {
        QMutexLocker locker(&m_mutex);
        m_accepting = false;
        m_workerStarted = false;
        m_stopRequested = false;
        m_queue.clear();
    }
    emit statusChanged(false, accepted, dropped, sessionPath);
}

} // namespace rov
