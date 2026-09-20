#pragma once

#include <QByteArray>
#include <QVector>

namespace rov
{

struct SystemHeartbeat
{
    quint32 sequence = 0;
    quint32 timestampUs = 0;
    quint8 inputBufferPercent = 0;
    quint8 outputBufferPercent = 0;
    quint8 canRxBufferPercent = 0;
    quint8 canTxBufferPercent = 0;
    quint8 flowBufferPercent = 0;
};

class SystemHeartbeatDecoder final
{
  public:
    QVector<SystemHeartbeat> feed(const QByteArray &bytes);
    void reset();

    static quint16 crc16Ccitt(const QByteArray &bytes);

  private:
    QByteArray m_buffer;
};

} // namespace rov
