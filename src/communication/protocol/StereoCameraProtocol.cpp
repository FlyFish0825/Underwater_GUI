#include "communication/protocol/StereoCameraProtocol.h"
#include <QtEndian>
#include <utility>

namespace rov
{
bool StereoCameraParser::readHeader(QString *error)
{
    const auto *p = reinterpret_cast<const uchar *>(m_buffer.constData());
    const quint32 length = qFromBigEndian<quint32>(p + 32);
    m_header.sequence = qFromBigEndian<quint64>(p + 8);
    m_header.receiptNs = qFromBigEndian<quint64>(p + 16);
    m_header.width = qFromBigEndian<quint32>(p + 24);
    m_header.height = qFromBigEndian<quint32>(p + 28);
    if (m_buffer.left(4) != QByteArrayLiteral("UWSC") || qFromBigEndian<quint16>(p + 4) != 1 ||
        qFromBigEndian<quint16>(p + 6) != 1 || qFromBigEndian<quint32>(p + 36) != 0 ||
        length == 0 || length > MaxJpegSize || m_header.width < 2 || m_header.width > 8192 ||
        (m_header.width % 2) != 0 || m_header.height == 0 || m_header.height > 4096)
    {
        if (error)
            *error = QStringLiteral("双目相机帧头无效（版本、长度或尺寸不符合 UWSC v1）");
        clear();
        return false;
    }
    m_expectedSize = HeaderSize + static_cast<int>(length);
    m_haveHeader = true;
    return true;
}

bool StereoCameraParser::feed(const QByteArray &bytes, const Receiver &receiver, QString *error)
{
    int offset = 0;
    // Copy only enough for the current header/payload, even for many glued frames.
    while (offset < bytes.size())
    {
        const int count = qMin(m_expectedSize - m_buffer.size(), bytes.size() - offset);
        m_buffer.append(bytes.constData() + offset, count);
        offset += count;
        if (m_buffer.size() != m_expectedSize)
            continue;
        if (!m_haveHeader && !readHeader(error))
            return false;
        if (m_buffer.size() == m_expectedSize && m_haveHeader)
        {
            StereoCameraPacket packet = m_header;
            packet.jpeg = m_buffer.mid(HeaderSize);
            clear();
            receiver(std::move(packet));
        }
    }
    return true;
}

void StereoCameraParser::clear()
{
    m_buffer.clear();
    m_header = {};
    m_expectedSize = HeaderSize;
    m_haveHeader = false;
}
} // namespace rov
