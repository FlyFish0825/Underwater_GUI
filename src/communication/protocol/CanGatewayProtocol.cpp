#include "communication/protocol/CanGatewayProtocol.h"

#include <QtGlobal>

namespace rov
{

quint8 CanGatewayDecoder::crc8(const QByteArray &bytes)
{
    quint8 crc = 0;
    for (const auto value : bytes)
    {
        crc ^= static_cast<quint8>(value);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80U) ? static_cast<quint8>((crc << 1U) ^ 0x07U)
                                : static_cast<quint8>(crc << 1U);
    }
    return crc;
}

QVector<CanGatewayFrame> CanGatewayDecoder::feed(const QByteArray &bytes)
{
    QVector<CanGatewayFrame> frames;
    m_lastError.clear();
    m_buffer.append(bytes);
    while (true)
    {
        const int header = m_buffer.indexOf(QByteArray::fromHex("AA55"));
        if (header < 0)
        {
            if (m_buffer.size() > 1)
                m_buffer = m_buffer.right(1);
            break;
        }
        if (header > 0)
            m_buffer.remove(0, header);
        if (m_buffer.size() < 3)
            break;

        const int bodyLength = static_cast<quint8>(m_buffer.at(2));
        if (bodyLength < 8 || bodyLength > 72)
        {
            m_lastError = QStringLiteral("非法 BODY_LEN=%1").arg(bodyLength);
            m_buffer.remove(0, 2);
            continue;
        }
        const int packetLength = bodyLength + 6;
        if (m_buffer.size() < packetLength)
            break;

        if (static_cast<quint8>(m_buffer.at(bodyLength + 4)) != 0x55
            || static_cast<quint8>(m_buffer.at(bodyLength + 5)) != 0xAA)
        {
            m_lastError = QStringLiteral("帧尾错误");
            m_buffer.remove(0, 2);
            continue;
        }
        const QByteArray checked = m_buffer.mid(2, bodyLength + 1);
        const quint8 expected = crc8(checked);
        const quint8 actual = static_cast<quint8>(m_buffer.at(bodyLength + 3));
        if (expected != actual)
        {
            m_lastError = QStringLiteral("CRC 错误（期望 %1，收到 %2）")
                              .arg(expected, 2, 16, QLatin1Char('0'))
                              .arg(actual, 2, 16, QLatin1Char('0'))
                              .toUpper();
            m_buffer.remove(0, 2);
            continue;
        }
        const QByteArray body = m_buffer.mid(3, bodyLength);
        const quint8 dataLength = static_cast<quint8>(body.at(7));
        const bool compactBody = bodyLength == 8 + dataLength;
        const bool fixedBody = bodyLength == 72;
        if ((!compactBody && !fixedBody) || dataLength > 64)
        {
            m_lastError = QStringLiteral("BODY_LEN 与 LEN 不匹配");
            m_buffer.remove(0, 2);
            continue;
        }
        CanGatewayFrame frame;
        frame.sequence = static_cast<quint16>(static_cast<quint8>(body.at(0)))
                         | static_cast<quint16>(static_cast<quint8>(body.at(1))) << 8U;
        frame.canId = static_cast<quint32>(static_cast<quint8>(body.at(2)))
                      | static_cast<quint32>(static_cast<quint8>(body.at(3))) << 8U
                      | static_cast<quint32>(static_cast<quint8>(body.at(4))) << 16U
                      | static_cast<quint32>(static_cast<quint8>(body.at(5))) << 24U;
        frame.flags = static_cast<quint8>(body.at(6));
        frame.data = body.mid(8, dataLength);
        frames.append(frame);
        m_buffer.remove(0, packetLength);
    }
    return frames;
}

void CanGatewayDecoder::reset()
{
    m_buffer.clear();
    m_lastError.clear();
}

int CanGatewayDecoder::bufferedBytes() const
{
    return m_buffer.size();
}

QString CanGatewayDecoder::lastError() const
{
    return m_lastError;
}

QByteArray encodeCanGatewayFrame(const CanGatewayFrame &frame)
{
    if (frame.data.size() > 64 || frame.sequence > 0xFFFFU || frame.canId > 0x1FFFFFFFU)
        return {};
    QByteArray body;
    body.append(static_cast<char>(frame.sequence & 0xFFU));
    body.append(static_cast<char>((frame.sequence >> 8U) & 0xFFU));
    body.append(static_cast<char>(frame.canId & 0xFFU));
    body.append(static_cast<char>((frame.canId >> 8U) & 0xFFU));
    body.append(static_cast<char>((frame.canId >> 16U) & 0xFFU));
    body.append(static_cast<char>((frame.canId >> 24U) & 0xFFU));
    body.append(static_cast<char>(frame.flags));
    body.append(static_cast<char>(frame.data.size()));
    body.append(frame.data);
    QByteArray packet = QByteArray::fromHex("AA55");
    packet.append(static_cast<char>(body.size()));
    packet.append(body);
    packet.append(static_cast<char>(CanGatewayDecoder::crc8(packet.mid(2))));
    packet.append(QByteArray::fromHex("55AA"));
    return packet;
}

QString describeCanGatewayFrame(const CanGatewayFrame &frame)
{
    return QStringLiteral("CAN %1  %2 字节  SEQ=%3  FLAGS=0x%4  DATA=%5")
        .arg(QStringLiteral("0x%1").arg(frame.canId, 8, 16, QLatin1Char('0')).toUpper())
        .arg(frame.data.size())
        .arg(frame.sequence)
        .arg(frame.flags, 2, 16, QLatin1Char('0'))
        .arg(QString(frame.data.toHex(' ').toUpper()));
}

} // namespace rov
