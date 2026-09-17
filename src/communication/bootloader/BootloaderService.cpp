#include "communication/bootloader/BootloaderService.h"

namespace rov
{

BootloaderService::BootloaderService(BootloaderCommunicationService *communication, QObject *parent)
    : QObject(parent), m_communication(communication)
{
    Q_ASSERT(m_communication != nullptr);
    connect(m_communication, &BootloaderCommunicationService::frameReceived, this,
            [this](const CanGatewayFrame &frame)
            {
                BootResponse response;
                if (BootloaderProtocol::decodeHostResponse(frame.canId, frame.data, response))
                {
                    emit hostResponseReceived(response);
                    return;
                }
                PeerControlMessage peer;
                if (BootloaderProtocol::decodePeerControl(frame.canId, frame.data, peer))
                    emit peerMessageReceived(peer);
            });
    connect(m_communication, &BootloaderCommunicationService::errorOccurred, this,
            &BootloaderService::errorOccurred);
}

bool BootloaderService::sendHostCommand(quint8 target, BootCommand command, quint8 byte2,
                                        const QByteArray &params)
{
    if (m_communication == nullptr)
        return false;
    const QByteArray payload = BootloaderProtocol::encodeHostControl(target, command, byte2, params);
    if (payload.isEmpty())
        return false;
    CanGatewayFrame frame;
    frame.sequence = 0;
    frame.canId = BootloaderProtocol::hostControlCanId;
    frame.flags = 0;
    frame.data = payload;
    if (!m_communication->sendCanFrame(frame))
        return false;
    emit commandSent(command, target);
    return true;
}

bool BootloaderService::sendPeerCommand(quint8 target, BootCommand command, quint8 source,
                                        quint16 session, quint16 value)
{
    if (m_communication == nullptr)
        return false;
    const QByteArray payload = BootloaderProtocol::encodePeerControl(target, command, source,
                                                                     session, value);
    CanGatewayFrame frame;
    frame.sequence = 0;
    frame.canId = 0x600U + source;
    frame.flags = 0;
    frame.data = payload;
    if (!m_communication->sendCanFrame(frame))
        return false;
    emit commandSent(command, target);
    return true;
}

} // namespace rov
