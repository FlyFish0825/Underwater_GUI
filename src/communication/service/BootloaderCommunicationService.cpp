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

} // namespace rov
