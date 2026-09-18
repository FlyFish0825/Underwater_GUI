#include "communication/protocol/CanFlowControlProtocol.h"

#include <QCoreApplication>
#include <QDebug>

using namespace rov;

namespace
{

void appendLe16(QByteArray &bytes, const quint16 value)
{
    bytes.append(static_cast<char>(value & 0xFFU));
    bytes.append(static_cast<char>((value >> 8U) & 0xFFU));
}

void appendLe32(QByteArray &bytes, const quint32 value)
{
    bytes.append(static_cast<char>(value & 0xFFU));
    bytes.append(static_cast<char>((value >> 8U) & 0xFFU));
    bytes.append(static_cast<char>((value >> 16U) & 0xFFU));
    bytes.append(static_cast<char>((value >> 24U) & 0xFFU));
}

bool require(const bool condition, const char *message)
{
    if (!condition)
        qCritical() << message;
    return condition;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (!require(canFlowCrc16(QByteArrayLiteral("123456789")) == 0x29B1U,
                 "CRC16-CCITT 校验向量失败"))
        return 1;

    CanFlowFrame begin;
    begin.command = CanFlowCommand::Begin;
    begin.sequence = 7;
    appendLe32(begin.payload, 128);
    const QByteArray beginPacket = encodeCanFlowFrame(begin);
    if (!require(beginPacket.size() == 24, "BEGIN 总长度错误")
        || !require(beginPacket.startsWith(QByteArray::fromHex("AA590106")), "BEGIN 帧头错误")
        || !require(beginPacket.endsWith(QByteArray::fromHex("59AA")), "BEGIN 帧尾错误"))
        return 1;

    CanFlowFrame ackFrame;
    ackFrame.command = CanFlowCommand::FlowAck;
    ackFrame.flags = 0x02;
    ackFrame.sequence = 11;
    appendLe32(ackFrame.payload, 3);
    appendLe16(ackFrame.payload, 4);
    appendLe16(ackFrame.payload, 28);
    ackFrame.payload.append(static_cast<char>(CanFlowStatus::Ok));
    ackFrame.payload.append(5, '\0'); // 固件当前 ACK PAYLOAD_LEN 固定为 14。
    const QByteArray ackPacket = encodeCanFlowFrame(ackFrame);

    CanFlowDecoder decoder;
    const QByteArray joined = beginPacket + ackPacket;
    if (!require(decoder.feed(joined.left(9)).isEmpty(), "半包不应提前产出帧"))
        return 1;
    const QVector<CanFlowFrame> decoded = decoder.feed(joined.mid(9));
    if (!require(decoded.size() == 2, "粘包未拆成两帧"))
        return 1;
    CanFlowAck ack;
    if (!require(decodeCanFlowAck(decoded.at(1), ack), "FLOW_ACK 解析失败")
        || !require(ack.frameSequence == 11 && ack.acknowledgedBlock == 3
                        && ack.creditReturn == 4 && ack.freeBlocks == 28
                        && ack.status == CanFlowStatus::Ok,
                    "FLOW_ACK 字段错误"))
        return 1;

    CanFlowFrame data;
    data.command = CanFlowCommand::DataBlock;
    data.payload = QByteArray::fromHex("0001000000000000000000000004AA551122");
    const QByteArray dataPacket = encodeCanFlowFrame(data);
    if (!require(decoder.feed(dataPacket).size() == 1,
                 "AA59 数据中的 AA55 不应破坏 AA59 解码"))
        return 1;

    QByteArray corrupted = ackPacket;
    corrupted[18] = static_cast<char>(static_cast<quint8>(corrupted.at(18)) ^ 0x01U);
    if (!require(decoder.feed(corrupted).isEmpty(), "CRC 错误帧不应通过")
        || !require(decoder.lastError().contains(QStringLiteral("CRC16")),
                    "CRC 错误没有诊断信息"))
        return 1;

    qInfo() << "CanFlowControlProtocolTest: PASS";
    return 0;
}
