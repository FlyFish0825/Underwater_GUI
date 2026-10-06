#include "data/services/SensorDataService.h"
#include <QRandomGenerator>
#include <cmath>

namespace rov
{
namespace
{
constexpr qint64 kStaleMs = 2500;
constexpr double kRadiansToDegrees = 57.295779513082320876;
QString fixedText(const QByteArray &bytes)
{
    const int end = bytes.indexOf('\0');
    const QString text = QString::fromLatin1(end < 0 ? bytes : bytes.left(end)).trimmed();
    return text.isEmpty() ? QStringLiteral("--") : text;
}
bool finiteFloats(const QByteArray &bytes, const int offset, const int count)
{
    for (int i = 0; i < count; ++i)
        if (!std::isfinite(SensorWire::readFloat(bytes, offset + 4 * i))) return false;
    return true;
}
}

SensorDataService::SensorDataService(QObject *parent) : QObject(parent), m_refresh(this)
{
    m_clock.start();
    m_sequence = QRandomGenerator::global()->generate();
    m_refresh.setInterval(50);
    connect(&m_refresh, &QTimer::timeout, this, &SensorDataService::refresh);
    m_refresh.start();
}
void SensorDataService::setSender(std::function<bool(const SensorFrame &)> sender)
{
    m_sender = std::move(sender);
}
void SensorDataService::setConnected(const bool connected)
{
    if (m_state.connected == connected) return;
    const auto oldPending = m_pending;
    m_pending.clear();
    m_state = SensorSnapshot{};
    m_state.connected = connected;
    m_lastSeen = {{-1, -1}};
    m_clockAnchorSeen = {{-1, -1}};
    m_clockAnchorUs = {{0, 0}};
    m_rawSeen = m_quatSeen = m_eulerSeen = m_depthSeen = -1;
    m_lastStream.clear();
    for (auto it = oldPending.cbegin(); it != oldPending.cend(); ++it)
    {
        const QString text = QStringLiteral("连接已重置，未确认命令结果；不会自动重发");
        m_state.devices.at(it.key() - 1).lastCommand = text;
        emit commandFinished(it.key(), it.value().sequence, SensorResult::IoError, text);
    }
    emit snapshotChanged(snapshot());
}

bool SensorDataService::request(const SensorRequest &r)
{
    if (r.target != kImuSensor && r.target != kDepthSensor) return false;
    auto &device = m_state.devices.at(r.target - 1);
    QString error;
    QByteArray payload;
    if (!m_state.connected || !m_sender)
        error = QStringLiteral("网关未连接，命令未发送");
    else if (m_pending.contains(r.target))
        error = QStringLiteral("该传感器已有请求待确认，请等待结果");
    else if (!makeSensorRequestPayload(r, payload, error)) {}
    if (!error.isEmpty())
    {
        device.lastCommand = error;
        emit snapshotChanged(snapshot());
        return false;
    }
    SensorFrame frame;
    frame.command = quint8(r.operation); frame.target = r.target;
    frame.flags = 1; frame.sequence = ++m_sequence; frame.payload = payload;
    Pending pending;
    pending.request = r; pending.payload = payload; pending.sequence = frame.sequence;
    pending.deadlineMs = m_clock.elapsed() + (r.operation == SensorOperation::Calibrate ? 45000 : 4000);
    // Install pending before calling sender: synchronous test transports may reply immediately.
    m_pending.insert(r.target, pending);
    device.pending = true;
    device.lastCommand = QStringLiteral("SEQ %1 · 等待设备回复…").arg(frame.sequence);
    if (!m_sender(frame))
    {
        if (m_pending.contains(r.target))
            finish(r.target, SensorResult::IoError, QStringLiteral("发送失败或固件传输忙，命令未提交"));
        return false;
    }
    emit snapshotChanged(snapshot());
    return true;
}

void SensorDataService::applyStatus(const quint8 target, const quint32 status)
{
    auto &d = m_state.devices.at(target - 1);
    d.status = status;
    d.online = (status & SensorStatus::Online) != 0;
    m_lastSeen.at(target - 1) = m_clock.elapsed();
    if (target == kImuSensor)
    {
        if (!d.online)
            m_state.rawValid = m_state.quaternionValid = m_state.eulerValid = false;
        if (!(status & SensorStatus::RawValid)) m_state.rawValid = false;
        if (!(status & SensorStatus::QuaternionValid)) m_state.quaternionValid = false;
        if (!(status & SensorStatus::EulerValid)) m_state.eulerValid = false;
    }
    else
    {
        if (!d.online) m_state.pressureValid = m_state.temperatureValid = m_state.depthValid = false;
        if (!(status & SensorStatus::PressureValid)) m_state.pressureValid = false;
        if (!(status & SensorStatus::TemperatureValid)) m_state.temperatureValid = false;
        if (!(status & SensorStatus::DepthValid)) m_state.depthValid = false;
        m_state.zeroValid = (status & SensorStatus::ZeroValid) != 0;
    }
}

void SensorDataService::handleFrame(const SensorFrame &frame)
{
    if (!m_state.connected || (frame.target != kImuSensor && frame.target != kDepthSensor)) return;
    if (frame.flags == 8)
    {
        const int sensorIndex = frame.target - 1;
        if (m_clockAnchorSeen.at(sensorIndex) < 0) return;
        const quint32 expectedNow = m_clockAnchorUs.at(sensorIndex)
            + quint32((m_clock.elapsed() - m_clockAnchorSeen.at(sensorIndex)) * 1000);
        const qint32 deliveryAgeUs = qint32(expectedNow - frame.timestampUs);
        if (deliveryAgeUs > kStaleMs * 1000 || deliveryAgeUs < -100000) return;
        const quint16 key = (quint16(frame.target) << 8U) | frame.command;
        const auto fingerprint = qMakePair(frame.sequence, frame.timestampUs);
        if (m_lastStream.contains(key) && m_lastStream.value(key) == fingerprint) return;
        if (consumeTelemetry(frame)) m_lastStream.insert(key, fingerprint);
        else emit protocolError(QStringLiteral("AA5B 遥测长度、数据类型或数值非法，未更新快照"));
        return;
    }
    if (frame.flags != 2 && frame.flags != 6) return;
    const auto it = m_pending.constFind(frame.target);
    if (it == m_pending.cend() || frame.sequence != it->sequence
        || frame.command != quint8(quint8(it->request.operation) + 0x40U))
        return; // Late, duplicate, wrong-command or wrong-target replies cannot complete another request.
    if (frame.payload.isEmpty() || quint8(frame.payload.at(0)) > quint8(SensorResult::PinBlocked))
    {
        finish(frame.target, SensorResult::IoError, QStringLiteral("设备回复格式非法"));
        return;
    }
    const auto result = SensorResult(quint8(frame.payload.at(0)));
    const bool good = result == SensorResult::Ok || result == SensorResult::Unconfirmed;
    if ((good && frame.flags != 2) || (!good && frame.flags != 6)
        || (good && !consumeReply(frame, result)))
    {
        finish(frame.target, SensorResult::IoError, QStringLiteral("设备回复内容与当前请求不匹配"));
        return;
    }
    m_clockAnchorSeen.at(frame.target - 1) = m_clock.elapsed();
    m_clockAnchorUs.at(frame.target - 1) = frame.timestampUs;
    finish(frame.target, result);
}

bool SensorDataService::consumeTelemetry(const SensorFrame &f)
{
    const QByteArray &p = f.payload;
    const qint64 now = m_clock.elapsed();
    if (f.command == 0x83)
    {
        if (p.size() != 20) return false;
        applyStatus(f.target, SensorWire::read32(p, 0));
        auto &d = m_state.devices.at(f.target - 1);
        d.sequence = SensorWire::read32(p, 4);
        d.goodFrames = SensorWire::read32(p, 12); d.errors = SensorWire::read32(p, 16);
        return true;
    }
    if (f.target == kImuSensor && f.command == 0x80)
    {
        if (p.size() != 40 || !finiteFloats(p, 4, 9)) return false;
        applyStatus(f.target, SensorWire::read32(p, 0));
        for (int i = 0; i < 3; ++i)
        {
            m_state.accelG.at(i) = SensorWire::readFloat(p, 4 + 4 * i);
            m_state.gyroRadS.at(i) = SensorWire::readFloat(p, 16 + 4 * i);
            m_state.magProtocolUnits.at(i) = SensorWire::readFloat(p, 28 + 4 * i);
        }
        const quint32 flags = m_state.devices[0].status;
        m_state.rawValid = (flags & SensorStatus::RawValid) && (flags & SensorStatus::Online);
        m_rawSeen = now; m_state.rawTimestampUs = f.timestampUs;
    }
    else if (f.target == kImuSensor && f.command == 0x81)
    {
        if (p.size() != 32 || !finiteFloats(p, 4, 7)) return false;
        const quint32 flags = SensorWire::read32(p, 0);
        double norm2 = 0;
        for (int i = 0; i < 4; ++i)
        {
            const double value = SensorWire::readFloat(p, 4 + 4 * i);
            norm2 += value * value;
        }
        if ((flags & SensorStatus::QuaternionValid) && (norm2 < 0.25 || norm2 > 2.25)) return false;
        applyStatus(f.target, flags);
        for (int i = 0; i < 4; ++i) m_state.quaternionWxyz.at(i) = SensorWire::readFloat(p, 4 + 4 * i);
        for (int i = 0; i < 3; ++i) m_state.eulerDeg.at(i) = SensorWire::readFloat(p, 20 + 4 * i) * kRadiansToDegrees;
        const bool live = (flags & SensorStatus::Online) != 0;
        m_state.quaternionValid = live && (flags & SensorStatus::QuaternionValid);
        m_state.eulerValid = live && (flags & SensorStatus::EulerValid);
        if (m_state.quaternionValid) m_quatSeen = now;
        if (m_state.eulerValid) m_eulerSeen = now;
        m_state.attitudeTimestampUs = f.timestampUs;
    }
    else if (f.target == kDepthSensor && f.command == 0x82)
    {
        if (p.size() != 32 || !finiteFloats(p, 4, 5)) return false;
        const quint32 flags = SensorWire::read32(p, 0);
        applyStatus(f.target, flags);
        m_state.pressurePa = SensorWire::readFloat(p, 4);
        m_state.temperatureC = SensorWire::readFloat(p, 8);
        m_state.depthRawM = SensorWire::readFloat(p, 12);
        m_state.depthFilteredM = SensorWire::readFloat(p, 16);
        m_state.surfacePressurePa = SensorWire::readFloat(p, 20);
        m_state.rawAdcD1 = SensorWire::read32(p, 24); m_state.rawAdcD2 = SensorWire::read32(p, 28);
        const bool ready = (flags & SensorStatus::Online) && (flags & SensorStatus::PromValid)
                           && (flags & SensorStatus::ModelConfirmed);
        m_state.pressureValid = ready && (flags & SensorStatus::PressureValid);
        m_state.temperatureValid = ready && (flags & SensorStatus::TemperatureValid);
        m_state.depthValid = m_state.pressureValid && m_state.zeroValid && (flags & SensorStatus::DepthValid);
        m_depthSeen = now; m_state.depthTimestampUs = f.timestampUs;
    }
    else if (f.target == kImuSensor && f.command == 0x85)
    {
        // Native IMU air-pressure data is deliberately NOT mapped to underwater depth.
        return p.size() == 20 && finiteFloats(p, 4, 4);
    }
    else return false;
    m_state.devices.at(f.target - 1).sequence = f.sequence;
    return true;
}

bool SensorDataService::consumeReply(const SensorFrame &f, const SensorResult result)
{
    const auto pending = m_pending.value(f.target);
    const auto operation = pending.request.operation;
    const QByteArray &p = f.payload;
    auto &d = m_state.devices.at(f.target - 1);
    if (operation == SensorOperation::GetInfo)
    {
        if (p.size() != 31 || quint8(p.at(1)) != f.target || result != SensorResult::Ok) return false;
        d.model = quint8(p.at(2)); d.capabilities = SensorWire::read32(p, 3);
        d.name = fixedText(p.mid(7, 16)); d.firmware = fixedText(p.mid(23, 8)); d.infoKnown = true;
        return true;
    }
    if (operation == SensorOperation::GetStatus)
    {
        if (p.size() != 21 || result != SensorResult::Ok) return false;
        SensorFrame status = f; status.command = 0x83; status.payload = p.mid(1);
        return consumeTelemetry(status);
    }
    if (operation == SensorOperation::GetParameter || operation == SensorOperation::SetParameter)
    {
        quint16 id = 0; QVariant value;
        if (!decodeSensorParameter(p.mid(1), id, value) || id != pending.request.parameterId) return false;
        SensorRequest validation = pending.request;
        validation.operation = SensorOperation::SetParameter; validation.value = value;
        QByteArray expected; QString error;
        if (!makeSensorRequestPayload(validation, expected, error) || expected != p.mid(1)) return false;
        if (operation == SensorOperation::SetParameter && pending.payload != p.mid(1)) return false;
        SensorParameterFeedback feedback;
        feedback.target = f.target; feedback.parameterId = id; feedback.value = value;
        feedback.confirmed = result == SensorResult::Ok;
        emit parameterReceived(feedback);
        return true;
    }
    return p.size() == 1;
}

void SensorDataService::finish(const quint8 target, const SensorResult result, const QString &detail)
{
    if (!m_pending.contains(target)) return;
    const Pending p = m_pending.take(target);
    auto &d = m_state.devices.at(target - 1);
    d.pending = false;
    d.lastCommand = QStringLiteral("SEQ %1 · %2").arg(p.sequence).arg(detail.isEmpty() ? sensorResultText(result) : detail);
    if (result == SensorResult::PinBlocked) applyStatus(target, d.status | SensorStatus::PinBlocked);
    emit commandFinished(target, p.sequence, result, d.lastCommand);
    emit snapshotChanged(snapshot());
}

SensorSnapshot SensorDataService::snapshot() const
{
    SensorSnapshot s = m_state;
    const qint64 now = m_clock.elapsed();
    const auto age = [now](qint64 then) { return then < 0 ? qint64(-1) : now - then; };
    const auto fresh = [now](qint64 then) { return then >= 0 && now - then <= kStaleMs; };
    for (int i = 0; i < 2; ++i) s.devices.at(i).online = s.connected && s.devices.at(i).online && fresh(m_lastSeen.at(i));
    s.rawAgeMs = age(m_rawSeen); s.attitudeAgeMs = age(m_eulerSeen); s.depthAgeMs = age(m_depthSeen);
    s.rawValid = s.rawValid && s.devices[0].online && fresh(m_rawSeen);
    s.quaternionValid = s.quaternionValid && s.devices[0].online && fresh(m_quatSeen);
    s.eulerValid = s.eulerValid && s.devices[0].online && fresh(m_eulerSeen);
    s.pressureValid = s.pressureValid && s.devices[1].online && fresh(m_depthSeen);
    s.temperatureValid = s.temperatureValid && s.devices[1].online && fresh(m_depthSeen);
    s.depthValid = s.depthValid && s.devices[1].online && fresh(m_depthSeen);
    return s;
}
void SensorDataService::refresh()
{
    const auto targets = m_pending.keys();
    for (const quint8 target : targets)
        if (m_pending.contains(target) && m_clock.elapsed() >= m_pending.value(target).deadlineMs)
            finish(target, SensorResult::Timeout);
    emit snapshotChanged(snapshot());
}
} // namespace rov
