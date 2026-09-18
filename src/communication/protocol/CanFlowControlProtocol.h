#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace rov
{

enum class CanFlowCommand : quint8
{
    Begin = 0x06,
    DataBlock = 0x10,
    End = 0x11,
    FlowAck = 0x80,
};

enum class CanFlowStatus : quint8
{
    Ok = 0x00,
    Busy = 0x01,
    QueueFull = 0x02,
    InvalidBlock = 0x03,
    ForwardFailed = 0x04,
    InternalError = 0x05,
};

struct CanFlowFrame
{
    quint8 version = 1;
    CanFlowCommand command = CanFlowCommand::Begin;
    quint8 flags = 0;
    quint8 target = 0;
    quint32 sequence = 0;
    quint32 timestampUs = 0;
    QByteArray payload;
};

struct CanFlowAck
{
    quint32 frameSequence = 0;
    quint32 acknowledgedBlock = 0xFFFFFFFFU;
    quint16 creditReturn = 0;
    quint16 freeBlocks = 0;
    CanFlowStatus status = CanFlowStatus::InternalError;
};

/**
 * @brief AA59 字节流解码器。
 *
 * USB CDC 可能把一帧拆成多次读取，也可能一次返回多帧。本解码器始终按
 * 帧头、PAYLOAD_LEN、CRC16 和帧尾累计拆包，不依赖单次读取边界。
 */
class CanFlowDecoder final
{
  public:
    QVector<CanFlowFrame> feed(const QByteArray &bytes);
    void reset();
    QString lastError() const;

  private:
    QByteArray m_buffer;
    QString m_lastError;
};

quint16 canFlowCrc16(const QByteArray &bytes);
QByteArray encodeCanFlowFrame(const CanFlowFrame &frame);
bool decodeCanFlowAck(const CanFlowFrame &frame, CanFlowAck &ack);
QString canFlowStatusText(CanFlowStatus status);

} // namespace rov
