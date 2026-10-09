#pragma once

#include <QByteArray>
#include <QString>
#include <functional>

namespace rov
{
struct StereoCameraPacket
{
    quint64 sequence = 0;
    quint64 receiptNs = 0; // Nano ROS receipt time, not exposure or Windows time.
    quint32 width = 0;
    quint32 height = 0;
    QByteArray jpeg;
};

// UWSC v1: strict framing. A protocol failure requires a new TCP connection.
class StereoCameraParser
{
  public:
    static constexpr int HeaderSize = 40;
    static constexpr int MaxJpegSize = 8 * 1024 * 1024;
    static constexpr int MaxBufferSize = HeaderSize + MaxJpegSize;
    using Receiver = std::function<void(StereoCameraPacket)>;

    bool feed(const QByteArray &bytes, const Receiver &receiver, QString *error);
    void clear();
    int bufferedBytes() const
    {
        return m_buffer.size();
    }

  private:
    bool readHeader(QString *error);
    QByteArray m_buffer;
    StereoCameraPacket m_header;
    int m_expectedSize = HeaderSize;
    bool m_haveHeader = false;
};
} // namespace rov
