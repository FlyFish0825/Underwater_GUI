#include "communication/protocol/CanFlowControlProtocol.h"

namespace rov
{

namespace
{

quint16 readLe16(const QByteArray &bytes, const int offset)
{
    return static_cast<quint8>(bytes.at(offset))
           | (static_cast<quint16>(static_cast<quint8>(bytes.at(offset + 1))) << 8U);
}

quint32 readLe32(const QByteArray &bytes, const int offset)
{
    return static_cast<quint8>(bytes.at(offset))
           | (static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 1))) << 8U)
           | (static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 2))) << 16U)
           | (static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 3))) << 24U);
}

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

} // namespace

quint16 canFlowCrc16(const QByteArray &bytes)
{
    quint16 crc = 0xFFFFU;
    for (const char value : bytes)
    {
        crc ^= static_cast<quint16>(static_cast<quint8>(value)) << 8U;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000U) ? static_cast<quint16>((crc << 1U) ^ 0x1021U)
                                  : static_cast<quint16>(crc << 1U);
    }
    return crc;
}

QByteArray encodeCanFlowFrame(const CanFlowFrame &frame)
{
    if (frame.version != 1 || frame.payload.size() > 0xFFFF)
        return {};

    QByteArray packet;
    packet.reserve(20 + frame.payload.size());
    packet.append(static_cast<char>(0xAA));
    packet.append(static_cast<char>(0x59));
    packet.append(static_cast<char>(frame.version));
    packet.append(static_cast<char>(frame.command));
    packet.append(static_cast<char>(frame.flags));
    packet.append(static_cast<char>(frame.target));
    appendLe32(packet, frame.sequence);
    appendLe16(packet, static_cast<quint16>(frame.payload.size()));
    appendLe32(packet, frame.timestampUs);
    packet.append(frame.payload);
    const quint16 crc = canFlowCrc16(packet.mid(1, 15 + frame.payload.size()));
    appendLe16(packet, crc);
    packet.append(static_cast<char>(0x59));
    packet.append(static_cast<char>(0xAA));
    return packet;
}

QVector<CanFlowFrame> CanFlowDecoder::feed(const QByteArray &bytes)
{
    QVector<CanFlowFrame> frames;
    m_lastError.clear();
    m_buffer.append(bytes);
    while (true)
    {
        const int header = m_buffer.indexOf(QByteArray::fromHex("AA59"));
        if (header < 0)
        {
            if (m_buffer.size() > 1)
                m_buffer = m_buffer.right(1);
            break;
        }
        if (header > 0)
            m_buffer.remove(0, header);
        if (m_buffer.size() < 16)
            break;

        const quint16 payloadLength = readLe16(m_buffer, 10);
        // 当前最大 DATA_BLOCK 只有 78 字节。保留一定扩展空间，同时避免坏包
        // 声明超大长度后让接收器长期等待。
        if (payloadLength > 4096)
        {
            m_lastError = QStringLiteral("AA59 PAYLOAD_LEN 非法：%1").arg(payloadLength);
            m_buffer.remove(0, 2);
            continue;
        }
        const int packetLength = 20 + payloadLength;
        if (m_buffer.size() < packetLength)
            break;
        if (static_cast<quint8>(m_buffer.at(packetLength - 2)) != 0x59U
            || static_cast<quint8>(m_buffer.at(packetLength - 1)) != 0xAAU)
        {
            m_lastError = QStringLiteral("AA59 帧尾错误");
            m_buffer.remove(0, 2);
            continue;
        }

        const quint16 expected = canFlowCrc16(m_buffer.mid(1, 15 + payloadLength));
        const quint16 actual = readLe16(m_buffer, 16 + payloadLength);
        if (expected != actual)
        {
            m_lastError = QStringLiteral("AA59 CRC16 错误（期望 0x%1，收到 0x%2）")
                              .arg(expected, 4, 16, QLatin1Char('0'))
                              .arg(actual, 4, 16, QLatin1Char('0'))
                              .toUpper();
            m_buffer.remove(0, 2);
            continue;
        }

        CanFlowFrame frame;
        frame.version = static_cast<quint8>(m_buffer.at(2));
        frame.command = static_cast<CanFlowCommand>(static_cast<quint8>(m_buffer.at(3)));
        frame.flags = static_cast<quint8>(m_buffer.at(4));
        frame.target = static_cast<quint8>(m_buffer.at(5));
        frame.sequence = readLe32(m_buffer, 6);
        frame.timestampUs = readLe32(m_buffer, 12);
        frame.payload = m_buffer.mid(16, payloadLength);
        if (frame.version == 1)
            frames.append(frame);
        else
            m_lastError = QStringLiteral("不支持的 AA59 版本：%1").arg(frame.version);
        m_buffer.remove(0, packetLength);
    }
    return frames;
}

void CanFlowDecoder::reset()
{
    m_buffer.clear();
    m_lastError.clear();
}

QString CanFlowDecoder::lastError() const
{
    return m_lastError;
}

bool decodeCanFlowAck(const CanFlowFrame &frame, CanFlowAck &ack)
{
    // 固件文档声明 ACK PAYLOAD_LEN=14；当前有效字段只使用前 9 字节，
    // 后面均为保留区。接受至少 9 字节，兼容保留区长度调整。
    if (frame.command != CanFlowCommand::FlowAck || frame.version != 1
        || frame.payload.size() < 9)
        return false;
    ack.frameSequence = frame.sequence;
    ack.acknowledgedBlock = readLe32(frame.payload, 0);
    ack.creditReturn = readLe16(frame.payload, 4);
    ack.freeBlocks = readLe16(frame.payload, 6);
    ack.status = static_cast<CanFlowStatus>(static_cast<quint8>(frame.payload.at(8)));
    return true;
}

QString canFlowStatusText(const CanFlowStatus status)
{
    switch (status)
    {
    case CanFlowStatus::Ok:
        return QStringLiteral("正常");
    case CanFlowStatus::Busy:
        return QStringLiteral("设备忙（上一会话尚未结束）");
    case CanFlowStatus::QueueFull:
        return QStringLiteral("队列已满（发送端违反 Credit）");
    case CanFlowStatus::InvalidBlock:
        return QStringLiteral("数据块非法（编号、偏移、长度或 CRC 错误）");
    case CanFlowStatus::ForwardFailed:
        return QStringLiteral("CAN 转发失败");
    case CanFlowStatus::InternalError:
        return QStringLiteral("网关内部错误");
    }
    return QStringLiteral("未知状态 0x%1")
        .arg(static_cast<quint8>(status), 2, 16, QLatin1Char('0')).toUpper();
}

} // namespace rov
