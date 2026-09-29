#include "communication/protocol/ObserverMotorProtocol.h"

#include <cstring>
#include <cmath>
#include <QtEndian>

namespace rov
{

namespace ObserverMotorProtocol
{

namespace
{

quint16 readLe16(const QByteArray &bytes, const int offset)
{
    return static_cast<quint16>(static_cast<quint8>(bytes.at(offset)))
           | static_cast<quint16>(static_cast<quint8>(bytes.at(offset + 1))) << 8U;
}

qint16 readSignedLe16(const QByteArray &bytes, const int offset)
{
    return static_cast<qint16>(readLe16(bytes, offset));
}

quint32 readLe24(const QByteArray &bytes, const int offset)
{
    return static_cast<quint32>(static_cast<quint8>(bytes.at(offset)))
           | static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 1))) << 8U
           | static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 2))) << 16U;
}

float readFloat(const QByteArray &bytes, const int offset)
{
    const quint32 bits = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(bytes.constData() + offset));
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool validCalibrationRequest(const quint8 action, const quint8 mask)
{
    if (action < 0x01U || action > 0x08U || mask == 0U)
        return false;
    // Stop and RAM reads may address several nodes; powered starts may not.
    return action == 0x06U || action == 0x07U || (mask & (mask - 1U)) == 0U;
}

void appendLe16(QByteArray &bytes, const quint16 value)
{
    bytes.append(static_cast<char>(value & 0xFFU));
    bytes.append(static_cast<char>((value >> 8U) & 0xFFU));
}

quint8 nodeFromCanId(const quint32 canId, const quint32 base)
{
    return static_cast<quint8>(canId - base);
}

bool isCanFd(const CanGatewayFrame &frame)
{
    // bit0=扩展帧、bit1=CAN FD、bit2=BRS；电机协议只接受标准数据帧。
    // 当前总线发送 0x02（FD、关闭 BRS），接收端保留兼容未来的 0x06。
    return frame.flags == kCanFdFlags || frame.flags == kCanFdBrsFlags;
}

bool isClassicCan(const CanGatewayFrame &frame)
{
    return (frame.flags & 0x07U) == kClassicCanFlags;
}

bool validCommand(const quint8 value)
{
    return value == static_cast<quint8>(Command::SpeedVector)
           || value == static_cast<quint8>(Command::RunVector)
           || value == static_cast<quint8>(Command::DebugSelect)
           || value == static_cast<quint8>(Command::StatusOnce)
           || value == static_cast<quint8>(Command::Calibration);
}

bool fail(QString *error, const QString &message)
{
    if (error != nullptr)
        *error = message;
    return false;
}

} // namespace

quint8 crc8(const QByteArray &bytes)
{
    quint8 value = 0;
    for (const char byte : bytes)
    {
        value ^= static_cast<quint8>(byte);
        for (int bit = 0; bit < 8; ++bit)
            value = (value & 0x80U) ? static_cast<quint8>((value << 1U) ^ 0x07U)
                                    : static_cast<quint8>(value << 1U);
    }
    return value;
}

QString stateText(const MotorState state)
{
    switch (state)
    {
    case MotorState::Idle:
        return QStringLiteral("IDLE");
    case MotorState::OpenLoop:
        return QStringLiteral("OPEN_LOOP");
    case MotorState::ClosedLoop:
        return QStringLiteral("CLOSED_LOOP");
    }
    return QStringLiteral("UNKNOWN");
}

bool decode(const CanGatewayFrame &frame, DecodedFrame &decoded, QString *error)
{
    if (error != nullptr)
        error->clear();

    if (frame.canId == kControlCanId)
    {
        if (!isCanFd(frame) || frame.data.size() != 24)
            return fail(error, QStringLiteral("Observer_Motor 控制帧必须是 CAN FD、24 字节"));
        const QByteArray &data = frame.data;
        if (static_cast<quint8>(data.at(0)) != kVersion)
            return fail(error, QStringLiteral("Observer_Motor 控制帧版本不支持"));
        const quint8 commandValue = static_cast<quint8>(data.at(1));
        if (!validCommand(commandValue))
            return fail(error, QStringLiteral("Observer_Motor 控制命令未知"));
        decoded.kind = FrameKind::Control;
        decoded.control.command = static_cast<Command>(commandValue);
        decoded.control.nodeMask = static_cast<quint8>(data.at(2));
        decoded.control.runMask = static_cast<quint8>(data.at(3));
        decoded.control.sequence = readLe16(data, 4);
        decoded.control.flags = readLe16(data, 6);
        for (int index = 0; index < 8; ++index)
            decoded.control.speedsRpm[static_cast<size_t>(index)] = readSignedLe16(data, 8 + index * 2);
        if (decoded.control.flags != 0)
            return fail(error, QStringLiteral("Observer_Motor 控制帧保留 Flags 必须为 0"));
        if (decoded.control.command == Command::Calibration)
        {
            if (!validCalibrationRequest(decoded.control.runMask, decoded.control.nodeMask))
                return fail(error, QStringLiteral("参数辨识 Action 或 NodeMask 无效"));
            for (int index = 6; index < 24; ++index)
                if (data.at(index) != 0)
                    return fail(error, QStringLiteral("参数辨识保留字节 6～23 必须为 0"));
        }
        return true;
    }

    if (frame.canId >= kReservedReplyBaseCanId + kFirstNodeId
        && frame.canId <= kReservedReplyBaseCanId + kLastNodeId)
    {
        if (!isClassicCan(frame) || frame.data.size() != 8)
            return fail(error, QStringLiteral("Observer_Motor 保留应答必须是 Classic CAN、8 字节"));
        decoded.kind = FrameKind::ReservedReply;
        decoded.nodeId = nodeFromCanId(frame.canId, kReservedReplyBaseCanId);
        return true;
    }

    if (frame.canId >= kFeedbackBaseCanId + kFirstNodeId
        && frame.canId <= kFeedbackBaseCanId + kLastNodeId)
    {
        if (!isCanFd(frame) || frame.data.size() != 12)
            return fail(error, QStringLiteral("Observer_Motor 普通反馈必须是 CAN FD、12 字节"));
        decoded.kind = FrameKind::Feedback;
        decoded.nodeId = nodeFromCanId(frame.canId, kFeedbackBaseCanId);
        decoded.feedback.nodeId = decoded.nodeId;
        decoded.feedback.speedRpm = readSignedLe16(frame.data, 0);
        decoded.feedback.busCurrentA =
            static_cast<double>(readLe16(frame.data, 2)) * kFeedbackBusCurrentLsbA;
        decoded.feedback.busVoltageV = static_cast<double>(readLe16(frame.data, 4)) / 100.0;
        decoded.feedback.temperatureC = kFeedbackTemperatureMinC
                                        + static_cast<double>(readLe16(frame.data, 6))
                                              * kFeedbackTemperatureLsbC;
        decoded.feedback.state = static_cast<MotorState>(static_cast<quint8>(frame.data.at(8)));
        decoded.feedback.currentCalibrationDone = (static_cast<quint8>(frame.data.at(9)) & 0x01U) != 0;
        decoded.feedback.speedLoopEnabled = (static_cast<quint8>(frame.data.at(9)) & 0x02U) != 0;
        decoded.feedback.voltageLimited = (static_cast<quint8>(frame.data.at(9)) & 0x04U) != 0;
        decoded.feedback.sequence = static_cast<quint8>(frame.data.at(10));
        return true;
    }

    if (frame.canId >= kHeartbeatBaseCanId + kFirstNodeId
        && frame.canId <= kHeartbeatBaseCanId + kLastNodeId)
    {
        if (!isClassicCan(frame) || frame.data.size() != 8)
            return fail(error, QStringLiteral("Observer_Motor 心跳必须是 Classic CAN、8 字节"));
        if (crc8(frame.data.left(7)) != static_cast<quint8>(frame.data.at(7)))
            return fail(error, QStringLiteral("Observer_Motor 心跳 CRC8 错误"));
        decoded.kind = FrameKind::Heartbeat;
        decoded.nodeId = nodeFromCanId(frame.canId, kHeartbeatBaseCanId);
        decoded.heartbeat.nodeId = decoded.nodeId;
        if (static_cast<quint8>(frame.data.at(0)) != decoded.nodeId)
            return fail(error, QStringLiteral("Observer_Motor 心跳 NodeID 与 CAN ID 不一致"));
        decoded.heartbeat.state = static_cast<MotorState>(static_cast<quint8>(frame.data.at(1)));
        decoded.heartbeat.currentCalibrationDone = static_cast<quint8>(frame.data.at(2)) != 0;
        decoded.heartbeat.debugMode = static_cast<quint8>(frame.data.at(3)) != 0;
        decoded.heartbeat.temperatureC = static_cast<qint8>(static_cast<quint8>(frame.data.at(4)));
        decoded.heartbeat.voltageLimited = static_cast<quint8>(frame.data.at(5)) != 0;
        decoded.heartbeat.feedbackSequence = static_cast<quint8>(frame.data.at(6));
        return true;
    }

    if (frame.canId >= kDebugBaseCanId + kFirstNodeId
        && frame.canId <= kDebugBaseCanId + kLastNodeId)
    {
        if (!isCanFd(frame) || frame.data.size() != 64)
            return fail(error, QStringLiteral("Observer_Motor 调试反馈必须是 CAN FD、64 字节"));
        decoded.kind = FrameKind::Debug;
        decoded.nodeId = nodeFromCanId(frame.canId, kDebugBaseCanId);
        decoded.debug.nodeId = decoded.nodeId;
        decoded.debug.speedRpm = readSignedLe16(frame.data, 0);
        decoded.debug.pllElectricalSpeedRadPerSec = readSignedLe16(frame.data, 2);
        decoded.debug.phaseCurrentU_A = static_cast<double>(readSignedLe16(frame.data, 4)) / 100.0;
        decoded.debug.phaseCurrentV_A = static_cast<double>(readSignedLe16(frame.data, 6)) / 100.0;
        decoded.debug.phaseCurrentW_A = static_cast<double>(readSignedLe16(frame.data, 8)) / 100.0;
        decoded.debug.idA = static_cast<double>(readSignedLe16(frame.data, 10)) / 100.0;
        decoded.debug.iqA = static_cast<double>(readSignedLe16(frame.data, 12)) / 100.0;
        decoded.debug.udV = static_cast<double>(readSignedLe16(frame.data, 14)) / 100.0;
        decoded.debug.uqV = static_cast<double>(readSignedLe16(frame.data, 16)) / 100.0;
        decoded.debug.busVoltageV = static_cast<double>(readLe16(frame.data, 18)) / 100.0;
        decoded.debug.temperatureC = static_cast<double>(readSignedLe16(frame.data, 20)) / 10.0;
        decoded.debug.observerElectricalAngleDeg = static_cast<double>(readSignedLe16(frame.data, 22)) / 100.0;
        decoded.debug.state = static_cast<MotorState>(static_cast<quint8>(frame.data.at(24)));
        decoded.debug.statusFlags = readLe24(frame.data, 25);
        decoded.debug.sequence = static_cast<quint8>(frame.data.at(28));
        return true;
    }

    if (frame.canId >= kCalibrationBaseCanId + kFirstNodeId
        && frame.canId <= kCalibrationBaseCanId + kLastNodeId)
    {
        if (!isCanFd(frame) || frame.data.size() != 64)
            return fail(error, QStringLiteral("Observer_Motor 参数辨识反馈必须是 CAN FD、64 字节"));
        if (static_cast<quint8>(frame.data.at(0)) != kVersion
            || static_cast<quint8>(frame.data.at(1)) != nodeFromCanId(frame.canId, kCalibrationBaseCanId))
            return fail(error, QStringLiteral("参数辨识反馈版本或 NodeID 与 CAN ID 不匹配"));
        decoded.kind = FrameKind::Calibration;
        decoded.nodeId = nodeFromCanId(frame.canId, kCalibrationBaseCanId);
        decoded.calibration.nodeId = decoded.nodeId;
        decoded.calibration.event = static_cast<quint8>(frame.data.at(2));
        decoded.calibration.action = static_cast<quint8>(frame.data.at(3));
        decoded.calibration.stage = static_cast<quint8>(frame.data.at(4));
        decoded.calibration.phase = static_cast<quint8>(frame.data.at(5));
        decoded.calibration.error = static_cast<quint8>(frame.data.at(6));
        decoded.calibration.validMask = static_cast<quint8>(frame.data.at(7));
        decoded.calibration.sequence = readLe16(frame.data, 8);
        decoded.calibration.hardwareError = static_cast<quint8>(frame.data.at(10));
        if (decoded.calibration.event < 0x01U || decoded.calibration.event > 0x07U
            || decoded.calibration.action < 0x01U || decoded.calibration.action > 0x08U
            || decoded.calibration.stage > 0x03U || decoded.calibration.phase > 0x02U
            || decoded.calibration.validMask > 0x0FU)
            return fail(error, QStringLiteral("参数辨识反馈的事件、阶段或有效位非法"));
        decoded.calibration.rsOhm = readFloat(frame.data, 12);
        decoded.calibration.rAbOhm = readFloat(frame.data, 16);
        decoded.calibration.rBcOhm = readFloat(frame.data, 20);
        decoded.calibration.rCaOhm = readFloat(frame.data, 24);
        decoded.calibration.rAOhm = readFloat(frame.data, 28);
        decoded.calibration.rBOhm = readFloat(frame.data, 32);
        decoded.calibration.rCOhm = readFloat(frame.data, 36);
        decoded.calibration.lsAbUh = readFloat(frame.data, 40);
        decoded.calibration.lsBcUh = readFloat(frame.data, 44);
        decoded.calibration.lsCaUh = readFloat(frame.data, 48);
        const auto &result = decoded.calibration;
        const auto positive = [](const double value) { return std::isfinite(value) && value > 0.0; };
        if (((result.validMask & 0x01U) != 0U
             && (!positive(result.rsOhm) || !positive(result.rAbOhm)
                 || !positive(result.rBcOhm) || !positive(result.rCaOhm)
                 || !positive(result.rAOhm) || !positive(result.rBOhm) || !positive(result.rCOhm)))
            || ((result.validMask & 0x02U) != 0U && !positive(result.lsAbUh))
            || ((result.validMask & 0x04U) != 0U && !positive(result.lsBcUh))
            || ((result.validMask & 0x08U) != 0U && !positive(result.lsCaUh)))
            return fail(error, QStringLiteral("参数辨识有效位对应的结果不是有限正数"));
        return true;
    }

    return false;
}

QByteArray encodeControl(const ControlFrame &control, QString *error)
{
    if (error != nullptr)
        error->clear();
    const quint8 command = static_cast<quint8>(control.command);
    if (!validCommand(command))
    {
        fail(error, QStringLiteral("Observer_Motor 控制命令未知"));
        return {};
    }
    if (control.flags != 0)
    {
        fail(error, QStringLiteral("Observer_Motor 控制帧保留 Flags 必须为 0"));
        return {};
    }
    if (control.command == Command::Calibration)
    {
        if (!validCalibrationRequest(control.runMask, control.nodeMask))
        {
            fail(error, QStringLiteral("参数辨识 Action 或 NodeMask 无效"));
            return {};
        }
        for (const qint16 speed : control.speedsRpm)
            if (speed != 0)
            {
                fail(error, QStringLiteral("参数辨识保留字节 8～23 必须为 0"));
                return {};
            }
    }
    if (command == static_cast<quint8>(Command::SpeedVector))
    {
        for (int index = 0; index < 8; ++index)
        {
            if ((control.nodeMask & (1U << index)) != 0
                && (control.speedsRpm[static_cast<size_t>(index)] < -10000
                    || control.speedsRpm[static_cast<size_t>(index)] > 10000))
            {
                fail(error, QStringLiteral("Node%1 速度超出 -10000～10000 rpm").arg(index + 1));
                return {};
            }
        }
    }
    QByteArray data;
    data.reserve(24);
    data.append(static_cast<char>(kVersion));
    data.append(static_cast<char>(command));
    data.append(static_cast<char>(control.nodeMask));
    data.append(static_cast<char>(control.runMask));
    appendLe16(data, control.sequence);
    appendLe16(data, control.flags);
    for (const qint16 speed : control.speedsRpm)
        appendLe16(data, static_cast<quint16>(speed));
    return data;
}

QByteArray encodeDebugSelect(const quint8 nodeMask, const bool enabled, const quint16 sequence,
                             QString *error)
{
    if (enabled && (nodeMask == 0 || (nodeMask & (nodeMask - 1U)) != 0))
    {
        fail(error, QStringLiteral("DEBUG_SELECT 启用时 NodeMask 必须是单 bit"));
        return {};
    }
    ControlFrame control;
    control.command = Command::DebugSelect;
    control.nodeMask = enabled ? nodeMask : 0;
    control.runMask = enabled ? 1 : 0;
    control.sequence = sequence;
    return encodeControl(control, error);
}

QByteArray encodeStatusOnce(const quint8 nodeMask, const quint16 sequence, QString *error)
{
    ControlFrame control;
    control.command = Command::StatusOnce;
    control.nodeMask = nodeMask;
    control.sequence = sequence;
    return encodeControl(control, error);
}

QByteArray encodeCalibration(const CalibrationAction action, const quint8 nodeMask,
                             const quint16 sequence, QString *error)
{
    const quint8 actionValue = static_cast<quint8>(action);
    if (!validCalibrationRequest(actionValue, nodeMask))
    {
        fail(error, QStringLiteral("参数辨识启动须选择单节点；停止和读取允许多节点"));
        return {};
    }
    if (actionValue < 0x01U || actionValue > 0x08U)
    {
        fail(error, QStringLiteral("参数辨识 Action 不受支持"));
        return {};
    }
    ControlFrame control;
    control.command = Command::Calibration;
    control.nodeMask = nodeMask;
    control.runMask = actionValue; // 0x40 复用 Byte3 作为 Calibration Action。
    control.sequence = sequence;
    return encodeControl(control, error);
}

QByteArray encodeEnterBootloader()
{
    return QByteArray::fromHex("010400000000007B");
}

} // namespace ObserverMotorProtocol

} // namespace rov
