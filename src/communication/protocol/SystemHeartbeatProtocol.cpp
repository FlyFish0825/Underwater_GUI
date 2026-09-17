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
    constexpr int packetLength = 20;
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
        if (m_buffer.size() < packetLength)
            break;
        if (static_cast<quint8>(m_buffer.at(2)) != 0x01
            || static_cast<quint8>(m_buffer.at(3)) != 0x01
            || static_cast<quint8>(m_buffer.at(4)) != 0x00
            || static_cast<quint8>(m_buffer.at(5)) != 0x00
            || static_cast<quint8>(m_buffer.at(10)) != 0x00
            || static_cast<quint8>(m_buffer.at(11)) != 0x00
            || static_cast<quint8>(m_buffer.at(18)) != 0x58
            || static_cast<quint8>(m_buffer.at(19)) != 0xAA)
        {
            m_buffer.remove(0, 1);
            continue;
        }
        const quint16 expected = crc16Ccitt(m_buffer.mid(1, 15));
        const quint16 actual = static_cast<quint16>(static_cast<quint8>(m_buffer.at(16)))
                               | static_cast<quint16>(static_cast<quint8>(m_buffer.at(17))) << 8U;
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
