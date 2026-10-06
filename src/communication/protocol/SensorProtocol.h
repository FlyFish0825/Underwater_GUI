#pragma once

#include "contracts/sensors/SensorContract.h"
#include <QByteArray>
#include <QVector>

namespace rov
{
struct SensorFrame
{
    quint8 command = 0;
    quint8 flags = 1;
    quint8 target = kImuSensor;
    quint32 sequence = 0;
    quint32 timestampUs = 0;
    QByteArray payload;
};

namespace SensorWire
{
constexpr int kMaxPayload = 58;
constexpr int kOverhead = 20;
quint16 read16(const QByteArray &data, int offset);
quint32 read32(const QByteArray &data, int offset);
float readFloat(const QByteArray &data, int offset);
void append16(QByteArray &data, quint16 value);
void append32(QByteArray &data, quint32 value);
void appendFloat(QByteArray &data, float value);
quint16 crc16(const QByteArray &data);
}

QByteArray encodeSensorFrame(const SensorFrame &frame);
bool decodeSensorFrame(const QByteArray &packet, SensorFrame &frame, QString *error = nullptr);
bool makeSensorRequestPayload(const SensorRequest &request, QByteArray &payload, QString &error);
bool decodeSensorParameter(const QByteArray &tuple, quint16 &id, QVariant &value);
QString sensorResultText(SensorResult result);

// Bounded incremental decoder for tests and alternative transports. Production
// USB stream must FIRST be split by the shared family router, not fed to every parser.
class SensorDecoder
{
  public:
    QVector<SensorFrame> feed(const QByteArray &bytes);
    void reset();
    quint64 errorCount() const { return m_errors; }
    int bufferedBytes() const { return m_buffer.size(); }
  private:
    QByteArray m_buffer;
    quint64 m_errors = 0;
};
} // namespace rov

Q_DECLARE_METATYPE(rov::SensorFrame)
