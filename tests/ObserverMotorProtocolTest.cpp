#include "communication/protocol/ObserverMotorProtocol.h"

#include <QCoreApplication>
#include <QDebug>

using namespace rov;
using namespace rov::ObserverMotorProtocol;

namespace
{

void appendLe16(QByteArray &bytes, const qint16 value)
{
    bytes.append(static_cast<char>(static_cast<quint16>(value) & 0xFFU));
    bytes.append(static_cast<char>((static_cast<quint16>(value) >> 8U) & 0xFFU));
}

bool require(const bool condition, const char *message)
{
    if (!condition)
        qCritical() << message;
    return condition;
}

CanGatewayFrame frame(const quint32 canId, const quint8 flags, const QByteArray &data)
{
    CanGatewayFrame result;
    result.canId = canId;
    result.flags = flags;
    result.data = data;
    return result;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    ControlFrame control;
    control.command = Command::SpeedVector;
    control.nodeMask = 0x07;
    control.runMask = 0x07;
    control.sequence = 1;
    control.speedsRpm = {1000, -1500, 3000, 0, 0, 0, 0, 0};
    const QByteArray controlData = encodeControl(control);
    if (!require(controlData == QByteArray::fromHex(
                         "0110070701000000E80324FAB80B00000000000000000000"),
                 "控制帧编码错误"))
        return 1;

    DecodedFrame decoded;
    if (!require(decode(frame(kControlCanId, kCanFdBrsFlags, controlData), decoded),
                 "控制帧解析失败")
        || !require(decoded.control.speedsRpm[1] == -1500, "控制帧小端有符号速度错误"))
        return 1;

    QByteArray feedback;
    feedback.reserve(12);
    appendLe16(feedback, 1234);
    appendLe16(feedback, -250);
    appendLe16(feedback, 2410);
    appendLe16(feedback, 283);
    feedback.resize(12);
    feedback[8] = static_cast<char>(MotorState::ClosedLoop);
    feedback[9] = 0x07;
    feedback[10] = 9;
    if (!require(decode(frame(0x201, kCanFdBrsFlags, feedback), decoded),
                 "普通反馈解析失败")
        || !require(decoded.feedback.speedRpm == 1234 && qFuzzyCompare(decoded.feedback.iqA, -2.5)
                        && qFuzzyCompare(decoded.feedback.busVoltageV, 24.1)
                        && qFuzzyCompare(decoded.feedback.temperatureC, 28.3),
                    "普通反馈缩放错误"))
        return 1;

    QByteArray heartbeat = QByteArray::fromHex("010201011C010900");
    heartbeat[7] = static_cast<char>(crc8(heartbeat.left(7)));
    if (!require(decode(frame(0x281, kClassicCanFlags, heartbeat), decoded),
                 "心跳解析失败")
        || !require(decoded.heartbeat.nodeId == 1 && decoded.heartbeat.debugMode,
                    "心跳字段错误"))
        return 1;

    QByteArray debug;
    debug.reserve(64);
    appendLe16(debug, 1000);
    appendLe16(debug, 120);
    appendLe16(debug, 100);
    appendLe16(debug, -100);
    appendLe16(debug, 0);
    appendLe16(debug, 10);
    appendLe16(debug, 20);
    appendLe16(debug, 30);
    appendLe16(debug, 40);
    appendLe16(debug, 2410);
    appendLe16(debug, 283);
    appendLe16(debug, 1234);
    debug.resize(64);
    for (int index = 24; index < debug.size(); ++index)
        debug[index] = '\0';
    debug[24] = static_cast<char>(MotorState::OpenLoop);
    debug[25] = 0x07;
    debug[28] = 10;
    if (!require(decode(frame(0x301, kCanFdBrsFlags, debug), decoded),
                 "调试反馈解析失败")
        || !require(decoded.debug.nodeId == 1 && qFuzzyCompare(decoded.debug.udV, 0.3)
                        && decoded.debug.statusFlags == 0x07,
                    "调试字段错误"))
        return 1;

    if (!require(encodeEnterBootloader() == QByteArray::fromHex("010400000000007B"),
                 "Bootloader 进入帧错误"))
        return 1;

    qInfo() << "ObserverMotorProtocolTest: PASS";
    return 0;
}
