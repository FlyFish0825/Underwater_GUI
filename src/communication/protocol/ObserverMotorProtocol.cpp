#include "communication/protocol/ObserverMotorProtocol.h"

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
           || value == static_cast<quint8>(Command::StatusOnce);
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

QByteArray encodeEnterBootloader()
{
    return QByteArray::fromHex("010400000000007B");
}

} // namespace ObserverMotorProtocol

} // namespace rov
