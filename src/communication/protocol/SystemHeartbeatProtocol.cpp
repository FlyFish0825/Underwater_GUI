#include "communication/protocol/SystemHeartbeatProtocol.h"

namespace rov
{

quint16 SystemHeartbeatDecoder::crc16Ccitt(const QByteArray &bytes)
{
    quint16 crc = 0xFFFFU;
    for (const auto value : bytes)
    {
        crc ^= static_cast<quint16>(static_cast<quint8>(value)) << 8U;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000U) ? static_cast<quint16>((crc << 1U) ^ 0x1021U)
                                  : static_cast<quint16>(crc << 1U);
    }
    return crc;
}

QVector<SystemHeartbeat> SystemHeartbeatDecoder::feed(const QByteArray &bytes)
{
    QVector<SystemHeartbeat> result;
    m_buffer.append(bytes);
    while (true)
    {
        const int header = m_buffer.indexOf(QByteArray::fromHex("AA58"));
        if (header < 0)
        {
            if (m_buffer.size() > 1)
                m_buffer = m_buffer.right(1);
            break;
        }
        if (header > 0)
            m_buffer.remove(0, header);
        if (m_buffer.size() < 12)
            break;
        const int payloadLength = static_cast<quint8>(m_buffer.at(10))
                                  | (static_cast<int>(static_cast<quint8>(m_buffer.at(11))) << 8);
        /* 0 字节是旧版兼容格式，5 字节是当前固件的缓冲区状态格式。 */
        if (payloadLength != 0 && payloadLength != 5)
        {
            m_buffer.remove(0, 1);
            continue;
        }
        const int packetLength = 20 + payloadLength;
        if (m_buffer.size() < packetLength)
            break;
        if (static_cast<quint8>(m_buffer.at(2)) != 0x01
            || static_cast<quint8>(m_buffer.at(3)) != 0x01
            || static_cast<quint8>(m_buffer.at(4)) != 0x00
            || static_cast<quint8>(m_buffer.at(5)) != 0x00
            || static_cast<quint8>(m_buffer.at(packetLength - 2)) != 0x58
            || static_cast<quint8>(m_buffer.at(packetLength - 1)) != 0xAA)
        {
            m_buffer.remove(0, 1);
            continue;
        }
        const quint16 expected = crc16Ccitt(m_buffer.mid(1, 15 + payloadLength));
        const int crcIndex = 16 + payloadLength;
        const quint16 actual = static_cast<quint16>(static_cast<quint8>(m_buffer.at(crcIndex)))
                               | static_cast<quint16>(static_cast<quint8>(m_buffer.at(crcIndex + 1))) << 8U;
        if (expected != actual)
        {
            m_buffer.remove(0, 1);
            continue;
        }
        SystemHeartbeat heartbeat;
        heartbeat.sequence = static_cast<quint32>(static_cast<quint8>(m_buffer.at(6)))
                             | static_cast<quint32>(static_cast<quint8>(m_buffer.at(7))) << 8U
                             | static_cast<quint32>(static_cast<quint8>(m_buffer.at(8))) << 16U
                             | static_cast<quint32>(static_cast<quint8>(m_buffer.at(9))) << 24U;
        heartbeat.timestampUs = static_cast<quint32>(static_cast<quint8>(m_buffer.at(12)))
                                | static_cast<quint32>(static_cast<quint8>(m_buffer.at(13))) << 8U
                                | static_cast<quint32>(static_cast<quint8>(m_buffer.at(14))) << 16U
                                | static_cast<quint32>(static_cast<quint8>(m_buffer.at(15))) << 24U;
        if (payloadLength == 5)
        {
            heartbeat.inputBufferPercent = static_cast<quint8>(m_buffer.at(16));
            heartbeat.outputBufferPercent = static_cast<quint8>(m_buffer.at(17));
            heartbeat.canRxBufferPercent = static_cast<quint8>(m_buffer.at(18));
            heartbeat.canTxBufferPercent = static_cast<quint8>(m_buffer.at(19));
            heartbeat.flowBufferPercent = static_cast<quint8>(m_buffer.at(20));
        }
        result.append(heartbeat);
        m_buffer.remove(0, packetLength);
    }
    return result;
}

void SystemHeartbeatDecoder::reset()
{
    m_buffer.clear();
}

} // namespace rov
