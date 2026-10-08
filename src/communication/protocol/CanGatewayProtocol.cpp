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

qint64 CanGatewayTimestampUnwrapper::extend(const quint32 rawUs)
{
    if (m_highWaterUs < 0) return m_highWaterUs = rawUs;
    const quint32 delta = rawUs - quint32(m_highWaterUs);
    if (delta == 0x80000000U) return -1; // Exactly half a cycle is ambiguous.
    const qint64 step = delta < 0x80000000U ? qint64(delta) : qint64(delta) - (qint64(1) << 32);
    const qint64 value = m_highWaterUs + step;
    if (value < 0) return -1; // Delayed pre-first-epoch sample: preserve raw, do not guess.
    if (value > m_highWaterUs) m_highWaterUs = value;
    return value; // A delayed frame never moves the unwrap anchor backwards.
}

bool decodeCanGatewayFrame(const QByteArray &packet, CanGatewayFrame &out, QString *error)
{
    if (error) error->clear();
    const auto fail = [error](const QString &text) { if (error) *error = text; return false; };
    if (packet.size() < 14 || packet.left(2) != QByteArray::fromHex("AA55"))
        return fail(QStringLiteral("AA55 帧头或最小长度错误"));
    const int bodyLength = quint8(packet.at(2));
    if (bodyLength < 8 || bodyLength > CanGatewayWire::kMaxBody || packet.size() != bodyLength + 6)
        return fail(QStringLiteral("AA55 BODY_LEN 或总长度错误"));
    if (packet.right(2) != QByteArray::fromHex("55AA"))
        return fail(QStringLiteral("AA55 帧尾错误"));
    if (CanGatewayDecoder::crc8(packet.mid(2, bodyLength + 1)) != quint8(packet.at(bodyLength + 3)))
        return fail(QStringLiteral("AA55 CRC8 错误"));
    if (quint8(packet.at(9)) == 0x80U)
        return fail(QStringLiteral("AA55 配置回复不能当作 CAN 数据"));
    const int n = quint8(packet.at(10));
    const bool timestamped = bodyLength == 12 + n;
    const bool compact = bodyLength == 8 + n;
    // Keep historical fixed-72-byte bodies, except the inherently ambiguous N=60:
    // exact 12+N takes priority per the supplied new uplink specification.
    const bool legacyFixed = bodyLength == 72;
    if (n > 64 || (!timestamped && !compact && !legacyFixed))
        return fail(QStringLiteral("AA55 BODY_LEN 与 LEN 不匹配"));
    const auto le16 = [&packet](int o) {
        return quint16(quint8(packet.at(o))) | (quint16(quint8(packet.at(o + 1))) << 8U);
    };
    const auto le32 = [&le16](int o) { return quint32(le16(o)) | (quint32(le16(o + 2)) << 16U); };
    CanGatewayFrame frame;
    frame.sequence = le16(3); frame.canId = le32(5); frame.flags = quint8(packet.at(9));
    frame.data = packet.mid(11, n); // Never expose the trailer as CAN payload.
    frame.hasTimestamp = timestamped;
    if (timestamped) frame.timestampUs = le32(11 + n);
    out = frame;
    return true;
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
            m_buffer = m_buffer.endsWith(char(0xAA)) ? QByteArray(1, char(0xAA)) : QByteArray();
            break;
        }
        if (header > 0) m_buffer.remove(0, header);
        if (m_buffer.size() < 3) break;
        const int bodyLength = quint8(m_buffer.at(2));
        if (bodyLength < 8 || bodyLength > CanGatewayWire::kMaxBody)
        {
            m_lastError = QStringLiteral("非法 BODY_LEN=%1").arg(bodyLength);
            m_buffer.remove(0, 1); continue;
        }
        const int packetLength = bodyLength + 6;
        if (m_buffer.size() < packetLength) break;
        CanGatewayFrame frame;
        QString error;
        if (!decodeCanGatewayFrame(m_buffer.left(packetLength), frame, &error))
        {
            m_lastError = error;
            m_buffer.remove(0, 1); continue;
        }
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
