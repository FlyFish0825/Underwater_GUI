#include "data/services/SensorDataService.h"
#include <QRandomGenerator>
#include <cmath>

namespace rov
{
namespace
{
constexpr qint64 kStaleMs = 2500; // Depth; do not change the established MS5837 policy.
constexpr qint64 kImuStaleMs = 500;
constexpr qint64 kImuRateWindowMs = 2000;
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
    m_imuReads.clear();
    for (auto &samples : m_imuRates) samples.clear();
    m_imuDeliveryAgeMs = {{0, 0, 0}};
    m_imuRateStartedMs = -1;
    m_depthReads.clear();
    m_depthSampling = DepthSamplingChange{};
    m_state = SensorSnapshot{};
    m_state.connected = connected;
    m_depthStartup = connected && m_depthStartupEnabled ? DepthStartup::WaitInfo : DepthStartup::Idle;
    m_depthStartupDeadlineMs = m_clock.elapsed() + 10000;
    m_lastSeen = {{-1, -1}};
    m_statusSeen = {{-1, -1}};
    m_depthDeliveryAgeMs = 0;
    m_depthAwaitingSample = false;
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

void SensorDataService::setDepthStartupEnabled(const bool enabled)
{
    if (m_depthStartupEnabled == enabled) return;
    m_depthStartupEnabled = enabled;
    m_depthStartup = enabled && m_state.connected ? DepthStartup::WaitInfo : DepthStartup::Idle;
    m_depthStartupDeadlineMs = m_clock.elapsed() + 10000;
}

bool SensorDataService::request(const SensorRequest &r)
{
    if (r.target != kImuSensor && r.target != kDepthSensor) return false;
    if (r.target == kDepthSensor && m_depthSampling.active)
    {
        m_state.devices[1].lastCommand = QStringLiteral("OSR/频率联动设置进行中，请等待回读确认");
        emit snapshotChanged(snapshot());
        return false;
    }
    if (r.samplingOsr || r.samplingRateHz)
    {
        const int maximum = depth02baMaxRateHz(r.samplingOsr);
        const bool paired = r.target == kDepthSensor && r.operation == SensorOperation::SetParameter
            && (r.parameterId == 0x0001 || r.parameterId == 0x0101);
        const int expected = r.parameterId == 0x0001 ? r.samplingRateHz : r.samplingOsr;
        if (!m_state.connected || !m_sender || m_pending.contains(r.target) || !paired
            || maximum == 0 || r.samplingRateHz < 1 || r.samplingRateHz > maximum
            || !r.value.isValid() || r.value.toDouble() != expected)
        {
            m_state.devices.at(r.target - 1).lastCommand = QStringLiteral("组合未提交：请检查连接、忙状态及 OSR 对应的合法频率");
            emit snapshotChanged(snapshot());
            return false;
        }
        // Reserve this target for one explicit user intent, never for an editor valueChanged signal.
        m_depthReads.clear();
        m_depthSampling = DepthSamplingChange{};
        m_depthSampling.active = true;
        m_depthSampling.osr = r.samplingOsr;
        m_depthSampling.rateHz = r.samplingRateHz;
        for (quint16 id : {quint16(0x0105), quint16(0x0101), quint16(0x0001)})
        {
            SensorRequest read; read.target = kDepthSensor;
            read.operation = SensorOperation::GetParameter; read.parameterId = id;
            m_depthSampling.steps.append(read);
        }
        m_state.devices[1].lastCommand = QStringLiteral("正在核对设备当前型号、OSR 和频率，尚未写入");
        emit snapshotChanged(snapshot());
        return true;
    }
    return sendRequest(r, false);
}

bool SensorDataService::sendRequest(const SensorRequest &r, const bool automatic)
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
    else if (r.target == kImuSensor && r.operation == SensorOperation::SetParameter
             && (device.status & SensorStatus::PinBlocked))
        error = QStringLiteral("IMU 配置发送受引脚保护（PIN_BLOCKED），未发送；接收和只读查询不受影响");
    if (!error.isEmpty())
    {
        device.lastCommand = error;
        emit snapshotChanged(snapshot());
        return false;
    }
    if (r.target == kImuSensor && !automatic) m_imuReads.clear();
    if (r.target == kImuSensor && r.operation == SensorOperation::SetParameter && r.parameterId == 1)
    {
        m_state.imuRateCheck = ImuRateCheck::NotRequested;
        m_state.imuRequestedRateHz = r.value.toInt();
        m_state.imuRateObservedHz = -1;
        m_state.imuRateMessage = QStringLiteral("等待频率请求回复，尚未开始观测");
    }
    // Explicit requests take priority over remaining discovery reads; never queue a user write.
    if (r.target == kDepthSensor && !automatic)
    {
        m_depthReads.clear();
        // An accepted explicit reference/model/stream action cancels startup automation.
        if (r.operation == SensorOperation::ZeroDepth || r.operation == SensorOperation::StopStream
            || (r.operation == SensorOperation::SetParameter
                && (r.parameterId == 0x0103 || r.parameterId == 0x0105)))
            m_depthStartup = DepthStartup::Idle;
    }
    SensorFrame frame;
    frame.command = quint8(r.operation); frame.target = r.target;
    frame.flags = 1; frame.sequence = ++m_sequence; frame.payload = payload;
    Pending pending;
    pending.request = r; pending.payload = payload; pending.sequence = frame.sequence;
    pending.automatic = automatic;
    pending.deadlineMs = m_clock.elapsed() + (r.operation == SensorOperation::Calibrate ? 45000 : 4000);
    // Install pending before calling sender: synchronous test transports may reply immediately.
    m_pending.insert(r.target, pending);
    device.pending = true;
    device.lastRequestHex = QString::fromLatin1(encodeSensorFrame(frame).toHex(' ').toUpper());
    device.lastReplyHex.clear();
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
        const bool ready = d.online && (status & SensorStatus::PromValid)
                           && (status & SensorStatus::ModelConfirmed)
                           && !(status & SensorStatus::ConfigUnknown);
        if (!ready) m_state.pressureValid = m_state.temperatureValid = m_state.depthValid = false;
        if (!d.online || !(status & SensorStatus::RawValid) || !(status & SensorStatus::PromValid))
            m_state.depthRawValid = false;
        if (!(status & SensorStatus::PressureValid)) m_state.pressureValid = false;
        if (!(status & SensorStatus::TemperatureValid)) m_state.temperatureValid = false;
        if (!(status & SensorStatus::PressureValid) || !(status & SensorStatus::DepthValid)
            || !(status & SensorStatus::ZeroValid))
            m_state.depthValid = false;
        m_state.zeroValid = (status & SensorStatus::ZeroValid) != 0;
        if (!ready || !m_state.zeroValid) m_state.surfacePressureValid = false;
        // A status-only frame may invalidate P0, but cannot supply a new numeric P0.
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
        const qint64 staleMs = frame.target == kImuSensor ? kImuStaleMs : kStaleMs;
        if (deliveryAgeUs > staleMs * 1000 || deliveryAgeUs < -100000) return;
        // A pre-configuration queued sample must not restore the old model/zero values.
        if (frame.target == kDepthSensor && m_depthAwaitingSample
            && qint32(frame.timestampUs - m_depthNotBeforeUs) < 0) return;
        // All telemetry groups share one monotonically increasing sequence per TARGET.
        // A late status frame must not overwrite newer raw/attitude data (including u32 wrap).
        const quint16 key = quint16(frame.target) << 8U;
        const auto fingerprint = qMakePair(frame.sequence, frame.timestampUs);
        if (m_lastStream.contains(key)
            && qint32(frame.sequence - m_lastStream.value(key).first) <= 0) return;
        if (consumeTelemetry(frame, qMax<qint64>(0, deliveryAgeUs / 1000)))
            m_lastStream.insert(key, fingerprint);
        else emit protocolError(QStringLiteral("AA5B 遥测长度、数据类型或数值非法，未更新快照"));
        return;
    }
    if (frame.flags != 2 && frame.flags != 6) return;
    const auto it = m_pending.constFind(frame.target);
    if (it == m_pending.cend() || frame.sequence != it->sequence
        || frame.command != quint8(quint8(it->request.operation) + 0x40U))
        return; // Late, duplicate, wrong-command or wrong-target replies cannot complete another request.
    m_state.devices.at(frame.target - 1).lastReplyHex =
        QString::fromLatin1(encodeSensorFrame(frame).toHex(' ').toUpper());
    if (frame.payload.isEmpty() || quint8(frame.payload.at(0)) > quint8(SensorResult::PinBlocked))
    {
        finish(frame.target, SensorResult::IoError, QStringLiteral("设备回复格式非法"));
        return;
    }
    const SensorRequest completedRequest = it->request; // Signals below may re-enter the service.
    const auto result = SensorResult(quint8(frame.payload.at(0)));
    const bool good = result == SensorResult::Ok || result == SensorResult::Unconfirmed;
    // Older common documentation uses FLAGS=06 for UNCONFIRMED; the measured firmware
    // uses 02. Accept both for result 7 only; it is never promoted to OK.
    if ((result == SensorResult::Ok && frame.flags != 2) || (!good && frame.flags != 6)
        || (!good && frame.payload.size() != 1)
        || (good && !consumeReply(frame, result)))
    {
        finish(frame.target, SensorResult::IoError, QStringLiteral("设备回复内容与当前请求不匹配"));
        return;
    }
    // A feedback consumer may have disconnected during consumeReply().
    if (!m_pending.contains(frame.target) || m_pending.value(frame.target).sequence != frame.sequence) return;
    if (result == SensorResult::Ok && frame.target == kDepthSensor
        && (completedRequest.operation == SensorOperation::SetParameter
            || completedRequest.operation == SensorOperation::ZeroDepth))
    {
        // Wait for data actually produced after the acknowledged RAM configuration.
        m_state.depthRawValid = m_state.pressureValid = m_state.temperatureValid = m_state.depthValid = false;
        m_state.surfacePressureValid = false;
        m_depthSeen = -1;
        m_depthNotBeforeUs = frame.timestampUs;
        m_depthAwaitingSample = true;
        if (completedRequest.operation == SensorOperation::ZeroDepth || completedRequest.parameterId == 0x0105)
            m_state.zeroValid = false;
    }
    m_clockAnchorSeen.at(frame.target - 1) = m_clock.elapsed();
    m_clockAnchorUs.at(frame.target - 1) = frame.timestampUs;
    finish(frame.target, result);
}

bool SensorDataService::consumeTelemetry(const SensorFrame &f, const qint64 deliveryAgeMs)
{
    const QByteArray &p = f.payload;
    const qint64 now = m_clock.elapsed();
    if (f.command == 0x83)
    {
        if (p.size() != 20) return false;
        applyStatus(f.target, SensorWire::read32(p, 0));
        auto &d = m_state.devices.at(f.target - 1);
        d.statusKnown = true;
        d.sampleSequence = SensorWire::read32(p, 4);
        const quint32 age = SensorWire::read32(p, 8);
        d.sampleAgeMs = age == 0xFFFFFFFFU ? -1 : qint64(age) + deliveryAgeMs;
        m_statusSeen.at(f.target - 1) = now;
        d.goodFrames = SensorWire::read32(p, 12); d.errors = SensorWire::read32(p, 16);
        if (f.flags == 8) d.sequence = f.sequence; // A GET_STATUS reply is not a stream sample.
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
        m_imuDeliveryAgeMs[0] = deliveryAgeMs;
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
        if (m_state.quaternionValid) { m_quatSeen = now; m_imuDeliveryAgeMs[1] = deliveryAgeMs; }
        if (m_state.eulerValid) { m_eulerSeen = now; m_imuDeliveryAgeMs[2] = deliveryAgeMs; }
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
        m_state.depthRawValid = (flags & SensorStatus::Online) && (flags & SensorStatus::PromValid)
                                && (flags & SensorStatus::RawValid);
        const bool ready = (flags & SensorStatus::Online) && (flags & SensorStatus::PromValid)
                           && (flags & SensorStatus::ModelConfirmed)
                           && !(flags & SensorStatus::ConfigUnknown);
        m_state.surfacePressureValid = ready && m_state.zeroValid;
        m_state.pressureValid = ready && (flags & SensorStatus::PressureValid);
        m_state.temperatureValid = ready && (flags & SensorStatus::TemperatureValid);
        m_state.depthValid = m_state.pressureValid && m_state.zeroValid && (flags & SensorStatus::DepthValid);
        m_depthSeen = now; m_state.depthTimestampUs = f.timestampUs;
        m_depthDeliveryAgeMs = deliveryAgeMs;
        m_depthAwaitingSample = false;
    }
    else if (f.target == kImuSensor && f.command == 0x85)
    {
        // Native IMU air-pressure data is deliberately NOT mapped to underwater depth.
        return p.size() == 20 && finiteFloats(p, 4, 4);
    }
    else return false;
    if (f.target == kImuSensor && ((f.command == 0x80 && m_state.rawValid)
        || (f.command == 0x81 && (m_state.quaternionValid || m_state.eulerValid))))
    {
        auto &samples = m_imuRates[f.command == 0x80 ? 0 : 1];
        // Same-ms combined attitude frames are distinct; a repeated raw timestamp is not
        // another raw conversion. Numeric equality is never used to infer freshness.
        const qint32 advance = samples.isEmpty() ? 1 : qint32(f.timestampUs - samples.last().timestampUs);
        if (advance > 0 || (advance == 0 && f.command == 0x81))
        {
            if (samples.size() == 512) samples.removeFirst();
            samples.append({now, f.timestampUs});
        }
    }
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
        if (f.target == kImuSensor && quint8(p.at(2)) != 0) return false; // Not a 6/9-axis selector.
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
        if (f.target == kImuSensor && result != SensorResult::Unconfirmed) return false;
        quint16 id = 0; QVariant value;
        if (!decodeSensorParameter(p.mid(1), id, value) || id != pending.request.parameterId) return false;
        SensorRequest validation = pending.request;
        validation.operation = SensorOperation::SetParameter; validation.value = value;
        QByteArray expected; QString error;
        if (!makeSensorRequestPayload(validation, expected, error) || expected != p.mid(1)) return false;
        if (operation == SensorOperation::SetParameter && pending.payload != p.mid(1)) return false;
        SensorParameterFeedback feedback;
        feedback.target = f.target; feedback.parameterId = id; feedback.value = value;
        feedback.confirmed = f.target == kDepthSensor && result == SensorResult::Ok;
        if (f.target == kImuSensor)
            feedback.message = operation == SensorOperation::SetParameter
                ? QStringLiteral("UART 已下发 / 未确认生效（UNCONFIRMED）")
                : QStringLiteral("最后下发缓存 / 未确认（UNCONFIRMED；非设备读回）");
        if (feedback.confirmed && f.target == kDepthSensor)
        {
            if (id == 0x0105) d.model = quint8(value.toUInt());
            if (m_depthSampling.active)
            {
                if (id == 0x0101) m_depthSampling.currentOsr = quint16(value.toUInt());
                if (id == 0x0001) m_depthSampling.currentRateHz = quint16(value.toUInt());
            }
        }
        emit parameterReceived(feedback);
        return true;
    }
    return p.size() == 1;
}

void SensorDataService::finish(const quint8 target, SensorResult result, const QString &detail)
{
    if (!m_pending.contains(target)) return;
    const Pending p = m_pending.take(target);
    auto &d = m_state.devices.at(target - 1);
    d.pending = false;
    QString completionDetail = detail;
    const bool samplingStep = target == kDepthSensor && m_depthSampling.active;
    if (samplingStep)
    {
        auto &change = m_depthSampling;
        if (result == SensorResult::Ok && change.steps.isEmpty())
        {
            if (!change.applying)
            {
                const int currentMaximum = depth02baMaxRateHz(change.currentOsr);
                if (d.model != 2 || !currentMaximum || change.currentRateHz < 1
                    || change.currentRateHz > currentMaximum)
                {
                    result = SensorResult::BadValue;
                    completionDetail = QStringLiteral("设备型号须为 02BA，且当前 OSR/频率须已有效读回；未写入组合");
                }
                else
                {
                    const auto append = [&change](SensorOperation op, quint16 id, quint16 value = 0) {
                        SensorRequest r; r.target = kDepthSensor; r.operation = op;
                        r.parameterId = id; r.value = value; change.steps.append(r);
                    };
                    const auto rate = [&]() {
                        if (change.currentRateHz != change.rateHz)
                            append(SensorOperation::SetParameter, 0x0001, change.rateHz);
                    };
                    const auto osr = [&]() {
                        if (change.currentOsr != change.osr)
                            append(SensorOperation::SetParameter, 0x0101, change.osr);
                    };
                    // Every intermediate pair fits: lower rate before raising OSR;
                    // lower OSR before a rate that cannot fit the current OSR.
                    if (change.rateHz <= currentMaximum) { rate(); osr(); }
                    else { osr(); rate(); }
                    append(SensorOperation::GetParameter, 0x0101);
                    append(SensorOperation::GetParameter, 0x0001);
                    change.applying = true;
                }
            }
            else
            {
                change.active = false;
                if (change.currentOsr != change.osr || change.currentRateHz != change.rateHz)
                {
                    result = SensorResult::IoError;
                    completionDetail = QStringLiteral("组合回读不一致，未确认整体完成");
                }
                else completionDetail = QStringLiteral("组合已回读确认：OSR %1 / %2 Hz")
                                            .arg(change.osr).arg(change.rateHz);
            }
        }
        if (result != SensorResult::Ok)
        {
            change.active = false; change.steps.clear();
            completionDetail = QStringLiteral("联动设置中断：%1；未继续执行，已生效步骤不自动回滚")
                .arg(completionDetail.isEmpty() ? sensorResultText(result) : completionDetail);
        }
        else if (change.active) completionDetail = QStringLiteral("OSR/频率联动设置进行中（当前步骤已确认）");
    }
    if (target == kDepthSensor && !samplingStep)
    {
        if (p.automatic && result != SensorResult::Ok && result != SensorResult::NotReady)
        {
            m_depthReads.clear(); // Stop on transport/protocol failure; no retry loop.
            m_depthStartup = DepthStartup::Idle;
        }
        const bool discovery = p.request.operation == SensorOperation::GetInfo;
        const bool zeroChanged = p.request.operation == SensorOperation::ZeroDepth
            || (p.request.operation == SensorOperation::SetParameter && p.request.parameterId == 0x0105);
        if (result == SensorResult::Ok && (discovery || zeroChanged))
        {
            m_depthReads.clear();
            SensorRequest read; read.target = kDepthSensor; read.operation = SensorOperation::GetStatus;
            m_depthReads.append(read);
            read.operation = SensorOperation::GetParameter;
            const QVector<quint16> ids = discovery
                ? QVector<quint16>{0x0001, 0x0101, 0x0102, 0x0103, 0x0104, 0x0105}
                : QVector<quint16>{0x0103};
            for (const quint16 id : ids) { read.parameterId = id; m_depthReads.append(read); }
        }
    }
    if (target == kImuSensor)
    {
        if (p.automatic && result != SensorResult::Ok && result != SensorResult::Unconfirmed
            && result != SensorResult::NotReady) m_imuReads.clear();
        if (p.request.operation == SensorOperation::GetInfo && result == SensorResult::Ok)
        {
            m_imuReads.clear();
            SensorRequest read; read.target = kImuSensor; read.operation = SensorOperation::GetStatus;
            m_imuReads.append(read); read.operation = SensorOperation::GetParameter;
            for (quint16 id : {quint16(1), quint16(3)}) { read.parameterId = id; m_imuReads.append(read); }
        }
        if (p.request.operation == SensorOperation::SetParameter && p.request.parameterId == 1)
        {
            m_state.imuRequestedRateHz = p.request.value.toInt();
            m_state.imuRateObservedHz = -1;
            if (result == SensorResult::Unconfirmed)
            {
                m_imuRates[0].clear(); // Never verify using pre-SET traffic or a parameter GET cache.
                m_imuRateStartedMs = m_clock.elapsed();
                m_imuRateNotBeforeUs = m_clockAnchorUs[0];
                m_state.imuRateCheck = ImuRateCheck::Observing;
                m_state.imuRateMessage = QStringLiteral("已下发 %1 Hz，正在观察约 2 s 原始流；尚未确认生效")
                    .arg(m_state.imuRequestedRateHz);
            }
            else
            {
                m_state.imuRateCheck = ImuRateCheck::Cancelled;
                m_state.imuRateMessage = QStringLiteral("未启动频率验证：%1")
                    .arg(completionDetail.isEmpty() ? sensorResultText(result) : completionDetail);
            }
        }
        if (p.request.operation == SensorOperation::StopStream && result == SensorResult::Ok)
        {
            for (auto &samples : m_imuRates) samples.clear();
            if (m_state.imuRateCheck == ImuRateCheck::Observing)
            {
                m_state.imuRateCheck = ImuRateCheck::Cancelled;
                m_state.imuRateMessage = QStringLiteral("已停止 IMU 上传，本次频率验证取消；深度计不受影响");
            }
        }
    }
    d.lastCommand = QStringLiteral("SEQ %1 · %2").arg(p.sequence).arg(completionDetail.isEmpty() ? sensorResultText(result) : completionDetail);
    if (result == SensorResult::PinBlocked) applyStatus(target, d.status | SensorStatus::PinBlocked);
    if ((p.request.operation == SensorOperation::GetParameter || p.request.operation == SensorOperation::SetParameter)
        && result != SensorResult::Ok && result != SensorResult::Unconfirmed)
    {
        SensorParameterFeedback feedback;
        feedback.target = target; feedback.parameterId = p.request.parameterId;
        feedback.message = target == kImuSensor && result == SensorResult::NotReady
            ? QStringLiteral("尚无该参数的下发缓存（NOT_READY，通信正常）")
            : target == kDepthSensor && p.request.parameterId == 0x0103
                           && result == SensorResult::NotReady
            ? QStringLiteral("未采集水面零点（NOT_READY，通信正常）")
            : (completionDetail.isEmpty() ? sensorResultText(result) : completionDetail);
        emit parameterReceived(feedback);
    }
    emit commandFinished(target, p.sequence, result, d.lastCommand);
    emit snapshotChanged(snapshot());
}

SensorSnapshot SensorDataService::snapshot() const
{
    SensorSnapshot s = m_state;
    s.devices[1].pending = s.devices[1].pending || m_depthSampling.active;
    const qint64 now = m_clock.elapsed();
    const auto age = [now](qint64 then) { return then < 0 ? qint64(-1) : now - then; };
    const auto fresh = [now](qint64 then) { return then >= 0 && now - then <= kStaleMs; };
    for (int i = 0; i < 2; ++i)
    {
        auto &d = s.devices.at(i);
        d.online = s.connected && d.online && fresh(m_lastSeen.at(i));
        if (d.sampleAgeMs >= 0 && m_statusSeen.at(i) >= 0) d.sampleAgeMs += age(m_statusSeen.at(i));
    }
    s.rawAgeMs = m_rawSeen < 0 ? -1 : age(m_rawSeen) + m_imuDeliveryAgeMs[0];
    s.quaternionAgeMs = m_quatSeen < 0 ? -1 : age(m_quatSeen) + m_imuDeliveryAgeMs[1];
    s.attitudeAgeMs = m_eulerSeen < 0 ? -1 : age(m_eulerSeen) + m_imuDeliveryAgeMs[2];
    s.depthAgeMs = m_depthSeen < 0 ? -1 : age(m_depthSeen) + m_depthDeliveryAgeMs;
    const bool depthFresh = s.depthAgeMs >= 0 && s.depthAgeMs <= kStaleMs;
    s.rawValid = s.rawValid && s.devices[0].online && s.rawAgeMs >= 0 && s.rawAgeMs <= kImuStaleMs;
    s.quaternionValid = s.quaternionValid && s.devices[0].online && s.quaternionAgeMs >= 0 && s.quaternionAgeMs <= kImuStaleMs;
    s.eulerValid = s.eulerValid && s.devices[0].online && s.attitudeAgeMs >= 0 && s.attitudeAgeMs <= kImuStaleMs;
    s.imuRawRateHz = s.rawValid ? imuReceivedRateHz(0) : -1;
    s.imuAttitudeRateHz = s.quaternionValid || s.eulerValid ? imuReceivedRateHz(1) : -1;
    s.depthRawValid = s.depthRawValid && s.devices[1].online && depthFresh;
    s.pressureValid = s.pressureValid && s.devices[1].online && depthFresh;
    s.temperatureValid = s.temperatureValid && s.devices[1].online && depthFresh;
    s.depthValid = s.depthValid && s.devices[1].online && depthFresh;
    s.zeroValid = s.zeroValid && s.devices[1].online;
    s.surfacePressureValid = s.surfacePressureValid && s.zeroValid && depthFresh;
    return s;
}
void SensorDataService::processDepthStartup()
{
    if (m_depthStartup == DepthStartup::Idle) return;
    if (m_clock.elapsed() >= m_depthStartupDeadlineMs)
    {
        m_depthStartup = DepthStartup::Idle;
        m_state.devices[1].lastCommand = QStringLiteral("启动水面初始化未完成：未取得有效压力；可在详情中手动校准");
        return;
    }
    const SensorSnapshot live = snapshot();
    const auto &d = live.devices[1];
    if (!d.infoKnown) return; // No guessed descriptor/model, and no command before the handshake.
    if (m_depthStartup == DepthStartup::WaitInfo)
    {
        m_depthStartup = DepthStartup::WaitPressure; // Set before sender; test senders may reply inline.
        if (d.model != 2)
        {
            if (!(d.capabilities & (1U << 11)))
            {
                m_depthStartup = DepthStartup::Idle;
                m_state.devices[1].lastCommand = QStringLiteral("设备不支持固定 02BA 初始化；未自动写入");
                return;
            }
            SensorRequest model; model.target = kDepthSensor;
            model.operation = SensorOperation::SetParameter; model.parameterId = 0x0105; model.value = 2;
            if (!sendRequest(model, true)) m_depthStartup = DepthStartup::Idle;
            return; // Wait for ACK and a sample under the confirmed model.
        }
    }
    if (d.model != 2) return;
    if (live.zeroValid)
    {
        m_depthStartup = DepthStartup::Idle; // Reconnecting underwater must not erase an existing zero.
        return;
    }
    if (!live.pressureValid) return;
    if (!(d.capabilities & (1U << 10)) || !std::isfinite(live.pressurePa)
        || live.pressurePa < 10000.0 || live.pressurePa > 200000.0)
    {
        m_depthStartup = DepthStartup::Idle;
        m_state.devices[1].lastCommand = QStringLiteral("当前压力不满足水面归零条件；未自动写入零点");
        return;
    }
    // ZERO_DEPTH records the latest real pressure on the device, not a PC-side fabricated 0 m.
    // One attempt per connection. Failure/timeout never triggers repeated calibration.
    m_depthStartup = DepthStartup::Idle;
    SensorRequest zero; zero.target = kDepthSensor; zero.operation = SensorOperation::ZeroDepth;
    sendRequest(zero, true);
}

double SensorDataService::imuReceivedRateHz(const int group) const
{
    const auto &samples = m_imuRates.at(group);
    const qint64 cutoff = m_clock.elapsed() - kImuRateWindowMs;
    int first = 0;
    while (first < samples.size() && samples[first].receivedMs < cutoff) ++first;
    if (samples.size() - first < 2) return -1;
    const auto &a = samples[first]; const auto &b = samples.last();
    const quint32 span = b.timestampUs - a.timestampUs; // u32 wrap, source timestamps retained.
    if (span < 500000U || b.receivedMs - a.receivedMs < 500) return -1;
    return (samples.size() - first - 1) * 1000000.0 / span;
}

void SensorDataService::refreshImuRates()
{
    const qint64 now = m_clock.elapsed();
    for (auto &samples : m_imuRates)
        while (!samples.isEmpty() && now - samples.first().receivedMs > kImuRateWindowMs)
            samples.removeFirst();
    if (m_state.imuRateCheck != ImuRateCheck::Observing || now - m_imuRateStartedMs < kImuRateWindowMs) return;
    const ImuRateSample *first = nullptr, *last = nullptr;
    int count = 0;
    for (const auto &sample : m_imuRates[0])
    {
        if (sample.receivedMs < m_imuRateStartedMs || sample.receivedMs > m_imuRateStartedMs + kImuRateWindowMs
            || qint32(sample.timestampUs - m_imuRateNotBeforeUs) < 0) continue;
        if (!first) first = &sample;
        last = &sample; ++count;
    }
    const quint32 span = count > 1 ? last->timestampUs - first->timestampUs : 0;
    if (span < 1500000U || !first || last->receivedMs - first->receivedMs < 1500 || !snapshot().rawValid)
    {
        m_state.imuRateCheck = ImuRateCheck::Insufficient;
        m_state.imuRateMessage = QStringLiteral("未确认：约 2 s 内新鲜原始帧不足，请检查 IMU 上传/链路；不会自动重发设置");
        return;
    }
    const double hz = (count - 1) * 1000000.0 / span;
    m_state.imuRateObservedHz = hz;
    // Host observation tolerance, not device accuracy: at least 1 Hz or 10% of request.
    const bool matches = std::abs(hz - m_state.imuRequestedRateHz) <= qMax(1.0, m_state.imuRequestedRateHz * 0.10);
    m_state.imuRateCheck = matches ? ImuRateCheck::Matches : ImuRateCheck::Differs;
    m_state.imuRateMessage = matches
        ? QStringLiteral("观测一致：请求 %1 Hz，原始实收 %2 Hz（仅流量验证，参数仍无原生 ACK）").arg(m_state.imuRequestedRateHz).arg(hz,0,'f',1)
        : QStringLiteral("未确认生效：请求 %1 Hz，原始实收 %2 Hz（可能未生效或链路丢帧；不自动重发）").arg(m_state.imuRequestedRateHz).arg(hz,0,'f',1);
}

void SensorDataService::refresh()
{
    const auto targets = m_pending.keys();
    for (const quint8 target : targets)
        if (m_pending.contains(target) && m_clock.elapsed() >= m_pending.value(target).deadlineMs)
            finish(target, SensorResult::Timeout);
    if (m_state.connected && !m_pending.contains(kImuSensor) && !m_imuReads.isEmpty())
    {
        const SensorRequest read = m_imuReads.takeFirst();
        if (!sendRequest(read, true)) m_imuReads.clear();
    }
    refreshImuRates();
    if (m_state.connected && !m_pending.contains(kDepthSensor) && m_depthSampling.active)
    {
        const SensorRequest step = m_depthSampling.steps.takeFirst();
        if (!sendRequest(step, true)) m_depthSampling = DepthSamplingChange{};
    }
    else if (m_state.connected && !m_pending.contains(kDepthSensor) && !m_depthReads.isEmpty())
    {
        const SensorRequest read = m_depthReads.takeFirst();
        if (!sendRequest(read, true)) m_depthReads.clear();
    }
    else if (m_state.connected && !m_pending.contains(kDepthSensor))
        processDepthStartup();
    emit snapshotChanged(snapshot());
}
} // namespace rov
