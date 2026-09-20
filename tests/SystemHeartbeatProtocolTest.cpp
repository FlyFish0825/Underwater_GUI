#include "communication/protocol/SystemHeartbeatProtocol.h"

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

QByteArray makeHeartbeat(const quint32 sequence,
                         const quint32 timestamp,
                         const QByteArray &payload)
{
    QByteArray packet;
    packet.append('\xAA');
    packet.append('\x58');
    packet.append('\x01');
    packet.append('\x01');
    packet.append('\x00');
    packet.append('\x00');
    appendLe32(packet, sequence);
    appendLe16(packet, static_cast<quint16>(payload.size()));
    appendLe32(packet, timestamp);
    packet.append(payload);
    const quint16 crc = SystemHeartbeatDecoder::crc16Ccitt(packet.mid(1));
    appendLe16(packet, crc);
    packet.append('\x58');
    packet.append('\xAA');
    return packet;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    SystemHeartbeatDecoder decoder;
    const QByteArray current = makeHeartbeat(7, 123456, QByteArray::fromHex("0A141E2832"));

    if (!require(current.size() == 25, "当前心跳长度错误")
        || !require(decoder.feed(current.left(11)).isEmpty(), "半包不应提前产出心跳")
        || !require(decoder.feed(current.mid(11)).size() == 1, "25 字节心跳解析失败"))
        return 1;

    const auto decoded = decoder.feed(current);
    if (!require(decoded.size() == 1, "重复输入心跳解析失败")
        || !require(decoded.at(0).sequence == 7
                    && decoded.at(0).timestampUs == 123456
                    && decoded.at(0).inputBufferPercent == 10
                    && decoded.at(0).outputBufferPercent == 20
                    && decoded.at(0).canRxBufferPercent == 30
                    && decoded.at(0).canTxBufferPercent == 40
                    && decoded.at(0).flowBufferPercent == 50,
                    "25 字节心跳字段错误"))
        return 1;

    const QByteArray legacy = makeHeartbeat(8, 654321, QByteArray());
    if (!require(legacy.size() == 20, "旧版心跳长度错误")
        || !require(decoder.feed(legacy).size() == 1, "旧版 20 字节心跳兼容失败"))
        return 1;

    qInfo() << "SystemHeartbeatProtocolTest: PASS";
    return 0;
}
