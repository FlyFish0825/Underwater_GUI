#pragma once

#include "communication/protocol/CanGatewayProtocol.h"
#include "communication/protocol/SystemHeartbeatProtocol.h"
#include "communication/transport/SerialTransport.h"

#include <QObject>

namespace rov
{

class BootloaderCommunicationService final : public QObject
{
    Q_OBJECT

  public:
    explicit BootloaderCommunicationService(QObject *parent = nullptr);

    QVector<SerialDeviceInfo> enumerateDevices() const;
    bool open(const SerialDeviceInfo &device);
    void close();
    bool isOpen() const;
    bool sendCanFrame(const CanGatewayFrame &frame);

  signals:
    void rawBytesReceived(const QByteArray &bytes);
    void frameReceived(const CanGatewayFrame &frame);
    void heartbeatReceived(const SystemHeartbeat &heartbeat);
    void opened(const QString &portName);
    void closed();
    void errorOccurred(const QString &message);

  private:
    SerialTransport *m_transport = nullptr;
    CanGatewayDecoder m_decoder;
    SystemHeartbeatDecoder m_heartbeatDecoder;
};

} // namespace rov
