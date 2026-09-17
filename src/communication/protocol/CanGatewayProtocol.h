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

QByteArray encodeCanGatewayFrame(const CanGatewayFrame &frame);
QString describeCanGatewayFrame(const CanGatewayFrame &frame);

} // namespace rov
