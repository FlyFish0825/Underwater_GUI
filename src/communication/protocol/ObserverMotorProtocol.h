#pragma once

#include "communication/protocol/CanGatewayProtocol.h"

#include <array>

namespace rov
{

namespace ObserverMotorProtocol
{

constexpr quint32 kControlCanId = 0x100U;
constexpr quint32 kReservedReplyBaseCanId = 0x180U;
constexpr quint32 kFeedbackBaseCanId = 0x200U;
constexpr quint32 kHeartbeatBaseCanId = 0x280U;
constexpr quint32 kDebugBaseCanId = 0x300U;
constexpr quint32 kCalibrationBaseCanId = 0x340U;
constexpr quint8 kFirstNodeId = 1U;
constexpr quint8 kLastNodeId = 8U;
constexpr quint8 kVersion = 0x01U;
constexpr quint8 kClassicCanFlags = 0x00U;
// 当前 Observer_Motor 测试总线为 1 Mbit/s CAN FD，关闭 BRS。
constexpr quint8 kCanFdFlags = 0x02U;
// 兼容旧调用方/测试夹具；接收端仍允许将来切换到 CAN FD+BRS。
constexpr quint8 kCanFdBrsFlags = 0x06U;
constexpr double kFeedbackBusCurrentMaxA = 10.0;
constexpr double kFeedbackBusCurrentLsbA = kFeedbackBusCurrentMaxA / 65535.0;
constexpr double kFeedbackTemperatureMinC = -20.0;
constexpr double kFeedbackTemperatureMaxC = 150.0;
constexpr double kFeedbackTemperatureLsbC =
    (kFeedbackTemperatureMaxC - kFeedbackTemperatureMinC) / 65535.0;

enum class Command : quint8
{
    SpeedVector = 0x10,
    RunVector = 0x11,
    DebugSelect = 0x20,
    StatusOnce = 0x30,
    Calibration = 0x40,
};

enum class CalibrationAction : quint8
{
    Rs = 0x01,
    LsAll = 0x02,
    LsAb = 0x03,
    LsBc = 0x04,
    LsCa = 0x05,
    Stop = 0x06,
    Read = 0x07,
    RsLs = 0x08,
};

enum class FrameKind
{
    Control,
    ReservedReply,
    Feedback,
    Heartbeat,
    Debug,
    Calibration,
};

enum class MotorState : quint8
{
    Idle = 0,
    OpenLoop = 1,
    ClosedLoop = 2,
};

struct ControlFrame
{
    Command command = Command::SpeedVector;
    quint8 nodeMask = 0;
    quint8 runMask = 0;
    quint16 sequence = 0;
    quint16 flags = 0;
    std::array<qint16, 8> speedsRpm{};
};

struct FeedbackFrame
{
    quint8 nodeId = 0;
    qint16 speedRpm = 0;
    // Byte2..3: estimated bus current, unsigned 0..10 A full scale.
    double busCurrentA = 0.0;
    // Byte4..5: bus voltage, 0.01 V per LSB.
    double busVoltageV = 0.0;
    // Byte6..7: STM32G431 internal temperature, unsigned -20..150 C full scale.
    double temperatureC = 0.0;
    MotorState state = MotorState::Idle;
    bool currentCalibrationDone = false;
    bool speedLoopEnabled = false;
    bool voltageLimited = false;
    quint8 sequence = 0;
};

struct HeartbeatFrame
{
    quint8 nodeId = 0;
    MotorState state = MotorState::Idle;
    bool currentCalibrationDone = false;
    bool debugMode = false;
    qint8 temperatureC = 0;
    bool voltageLimited = false;
    quint8 feedbackSequence = 0;
};

struct DebugFrame
{
    quint8 nodeId = 0;
    qint16 speedRpm = 0;
    qint16 pllElectricalSpeedRadPerSec = 0;
    double phaseCurrentU_A = 0.0;
    double phaseCurrentV_A = 0.0;
    double phaseCurrentW_A = 0.0;
    double idA = 0.0;
    double iqA = 0.0;
    double udV = 0.0;
    double uqV = 0.0;
    double busVoltageV = 0.0;
    double temperatureC = 0.0;
    double observerElectricalAngleDeg = 0.0;
    MotorState state = MotorState::Idle;
    quint32 statusFlags = 0;
    quint8 sequence = 0;
};

struct CalibrationFrame
{
    quint8 nodeId = 0;
    quint8 event = 0;
    quint8 action = 0;
    quint8 stage = 0;
    quint8 phase = 0;
    quint8 error = 0;
    quint8 validMask = 0;
    quint16 sequence = 0;
    quint8 hardwareError = 0;
    double rsOhm = 0.0;
    double rAbOhm = 0.0;
    double rBcOhm = 0.0;
    double rCaOhm = 0.0;
    double rAOhm = 0.0;
    double rBOhm = 0.0;
    double rCOhm = 0.0;
    double lsAbUh = 0.0;
    double lsBcUh = 0.0;
    double lsCaUh = 0.0;
};

struct DecodedFrame
{
    FrameKind kind = FrameKind::ReservedReply;
    quint8 nodeId = 0;
    ControlFrame control;
    FeedbackFrame feedback;
    HeartbeatFrame heartbeat;
    DebugFrame debug;
    CalibrationFrame calibration;
};

bool decode(const CanGatewayFrame &frame, DecodedFrame &decoded, QString *error = nullptr);

QByteArray encodeControl(const ControlFrame &control, QString *error = nullptr);
QByteArray encodeDebugSelect(quint8 nodeMask, bool enabled, quint16 sequence,
                             QString *error = nullptr);
QByteArray encodeStatusOnce(quint8 nodeMask, quint16 sequence, QString *error = nullptr);
QByteArray encodeCalibration(CalibrationAction action, quint8 nodeMask, quint16 sequence,
                             QString *error = nullptr);
QByteArray encodeEnterBootloader();

quint8 crc8(const QByteArray &bytes);
QString stateText(MotorState state);

} // namespace ObserverMotorProtocol

} // namespace rov
