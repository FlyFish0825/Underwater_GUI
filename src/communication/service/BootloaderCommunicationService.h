#pragma once

#include "communication/protocol/CanGatewayProtocol.h"
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

  signals:
    void rawBytesReceived(const QByteArray &bytes);
    void frameReceived(const CanGatewayFrame &frame);
    void opened(const QString &portName);
    void closed();
    void errorOccurred(const QString &message);

  private:
    SerialTransport *m_transport = nullptr;
    CanGatewayDecoder m_decoder;
};

} // namespace rov
