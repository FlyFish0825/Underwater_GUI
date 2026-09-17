#include "communication/service/BootloaderCommunicationService.h"

namespace rov
{

BootloaderCommunicationService::BootloaderCommunicationService(QObject *parent)
    : QObject(parent), m_transport(new SerialTransport(this))
{
    connect(m_transport, &SerialTransport::bytesReceived, this,
            [this](const QByteArray &bytes)
            {
                emit rawBytesReceived(bytes);
                for (const auto &frame : m_decoder.feed(bytes))
                    emit frameReceived(frame);
                for (const auto &heartbeat : m_heartbeatDecoder.feed(bytes))
                    emit heartbeatReceived(heartbeat);
                if (!m_decoder.lastError().isEmpty())
                    emit errorOccurred(m_decoder.lastError());
            });
    connect(m_transport, &SerialTransport::opened, this, &BootloaderCommunicationService::opened);
    connect(m_transport, &SerialTransport::closed, this, &BootloaderCommunicationService::closed);
    connect(m_transport, &SerialTransport::errorOccurred, this,
            &BootloaderCommunicationService::errorOccurred);
}

QVector<SerialDeviceInfo> BootloaderCommunicationService::enumerateDevices() const
{
    return SerialTransport::enumerate();
}

bool BootloaderCommunicationService::open(const SerialDeviceInfo &device)
{
    m_decoder.reset();
    m_heartbeatDecoder.reset();
    return m_transport->open(device);
}

void BootloaderCommunicationService::close()
{
    m_transport->close();
}

bool BootloaderCommunicationService::isOpen() const
{
    return m_transport->isOpen();
}

bool BootloaderCommunicationService::sendCanFrame(const CanGatewayFrame &frame)
{
    const QByteArray packet = encodeCanGatewayFrame(frame);
    if (packet.isEmpty())
    {
        emit errorOccurred(QStringLiteral("CAN 帧参数非法，无法封装"));
        return false;
    }
    return m_transport->writeBytes(packet);
}

} // namespace rov
