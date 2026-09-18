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
    connect(m_communication, &BootloaderCommunicationService::flowTransferProgress,
            this, &BootloaderService::dataWindowProgress);
    connect(m_communication, &BootloaderCommunicationService::flowTransferFinished,
            this, &BootloaderService::dataWindowFinished);
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
    // 控制帧沿用底层调试工具的固定 SEQ=0；网关不会用该字段匹配响应，
    // 但固定值可兼容旧版 H750 网关固件。
    frame.sequence = 0;
    frame.canId = BootloaderProtocol::hostControlCanId;
    frame.flags = 0;
    frame.data = payload;
    if (!m_communication->sendCanFrame(frame))
        return false;
    emit commandSent(command, target);
    return true;
}

bool BootloaderService::sendDataPacket(quint8 target, quint16 sequence,
                                       const QByteArray &payload, quint16 session, const bool canFd)
{
    if (m_communication == nullptr)
        return false;

    const QByteArray packet = BootloaderProtocol::encodeDataPacket(target, sequence, payload, session);
    if (packet.isEmpty())
        return false;

    if (canFd)
    {
        CanGatewayFrame frame;
        frame.sequence = m_gatewaySequence++;
        frame.canId = 0x100U;
        frame.flags = 0x06U; // 标准 ID + CAN FD + BRS
        frame.data = packet;
        return m_communication->sendCanFrame(frame);
    }

    for (quint8 index = 0; index < 8; ++index)
    {
        if (!sendClassicDataFragment(target, sequence, payload, session, index))
            return false;
    }
    return true;
}

bool BootloaderService::sendClassicDataFragment(const quint8 target, const quint16 sequence,
                                                const QByteArray &payload,
                                                const quint16 session,
                                                const quint8 fragmentIndex)
{
    if (m_communication == nullptr || fragmentIndex >= 8)
        return false;

    const QByteArray packet = BootloaderProtocol::encodeDataPacket(target, sequence,
                                                                    payload, session);
    if (packet.size() != BootloaderProtocol::dataPacketSize)
        return false;
    const QByteArray fragment = packet.mid(static_cast<int>(fragmentIndex) * 8, 8);
    if (fragment.size() != 8)
        return false;

    CanGatewayFrame frame;
    frame.sequence = 0;
    frame.canId = 0x100U + static_cast<quint32>(fragmentIndex);
    frame.flags = 0; // 标准 Classic CAN 数据帧
    frame.data = fragment;
    return m_communication->sendCanFrame(frame);
}

bool BootloaderService::startDataWindow(const quint8 target, const quint16 firstSequence,
                                        const QVector<QByteArray> &payloads,
                                        const quint16 session, const bool canFd)
{
    if (m_communication == nullptr || payloads.isEmpty())
        return false;

    QVector<CanGatewayFrame> blocks;
    blocks.reserve(payloads.size());
    for (int index = 0; index < payloads.size(); ++index)
    {
        const quint32 sequence = static_cast<quint32>(firstSequence)
                                 + static_cast<quint32>(index);
        if (sequence > 0xFFFFU)
            return false;
        const QByteArray packet = BootloaderProtocol::encodeDataPacket(
            target, static_cast<quint16>(sequence), payloads.at(index), session);
        if (packet.size() != BootloaderProtocol::dataPacketSize)
            return false;

        CanGatewayFrame block;
        block.canId = 0x100U;
        block.flags = canFd ? 0x06U : 0x00U;
        block.data = packet;
        blocks.append(block);
    }
    return m_communication->startFlowTransfer(blocks);
}

void BootloaderService::cancelDataWindow()
{
    if (m_communication != nullptr)
        m_communication->cancelFlowTransfer();
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
