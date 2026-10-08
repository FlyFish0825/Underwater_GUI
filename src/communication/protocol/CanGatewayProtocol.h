#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace rov
{

struct CanGatewayFrame
{
    quint16 sequence = 0;
    quint32 canId = 0;
    quint8 flags = 0;
    QByteArray data;
    // AA55 RX-only metadata. Zero is a valid device timestamp; legacy/TX has none.
    bool hasTimestamp = false;
    quint32 timestampUs = 0; // Gateway FIFO-read/packet-build time, ms tick * 1000.
    qint64 timestampExtendedUs = -1; // Filled by the connection service; -1 = unavailable.
};

namespace CanGatewayWire
{
constexpr int kMaxBody = 76; // New RX: 12 + 64. Legacy RX/TX: 8 + 64.
constexpr int kMaxPacket = kMaxBody + 6;
}

// Connection-local nearest-epoch expansion, not UTC and not an ordering key.
// Feed CRC/shape-validated timestamps only; reset on disconnect/reconnect.
// Forward gaps and reordering must be less than half the u32 range (~35.8 min).
class CanGatewayTimestampUnwrapper final
{
  public:
    qint64 extend(quint32 rawUs);
    void reset() { m_highWaterUs = -1; }
  private:
    qint64 m_highWaterUs = -1;
};

class CanGatewayDecoder final
{
  public:
    QVector<CanGatewayFrame> feed(const QByteArray &bytes);
    void reset();
    int bufferedBytes() const;
    QString lastError() const;

    static quint8 crc8(const QByteArray &bytes);

  private:
    QByteArray m_buffer;
    QString m_lastError;
};

// Host-to-gateway encoder is deliberately legacy-only, even for a received frame.
QByteArray encodeCanGatewayFrame(const CanGatewayFrame &frame);
// Atomic decode: failure leaves out unchanged. No time/connection state here.
bool decodeCanGatewayFrame(const QByteArray &packet, CanGatewayFrame &out,
                           QString *error = nullptr);
QString describeCanGatewayFrame(const CanGatewayFrame &frame);

} // namespace rov
