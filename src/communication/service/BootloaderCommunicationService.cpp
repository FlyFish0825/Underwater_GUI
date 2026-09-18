#include "communication/service/BootloaderCommunicationService.h"

namespace rov
{

namespace
{

void appendLe16(QByteArray &bytes, const quint16 value)
{
    bytes.append(static_cast<char>(value & 0xFFU));
    bytes.append(static_cast<char>((value >> 8U) & 0xFFU));
}

void appendLe32(QByteArray &bytes, const quint32 value)
{
    bytes.append(static_cast<char>(value & 0xFFU));
    bytes.append(static_cast<char>((value >> 8U) & 0xFFU));
    bytes.append(static_cast<char>((value >> 16U) & 0xFFU));
    bytes.append(static_cast<char>((value >> 24U) & 0xFFU));
}

bool isAllowedFlowFlags(const quint8 flags)
{
    return flags == 0x00U || flags == 0x01U || flags == 0x02U
           || flags == 0x03U || flags == 0x06U || flags == 0x07U;
}

} // namespace

BootloaderCommunicationService::BootloaderCommunicationService(QObject *parent)
    : QObject(parent), m_transport(new SerialTransport(this)), m_flowTimeout(this)
{
    m_flowTimeout.setSingleShot(true);
    connect(&m_flowTimeout, &QTimer::timeout, this,
            [this]()
            {
                finishFlowTransfer(false, QStringLiteral("等待 AA59 FLOW_ACK 超时"));
            });
    connect(m_transport, &SerialTransport::bytesReceived, this,
            [this](const QByteArray &bytes)
            {
                emit rawBytesReceived(bytes);
                processReceivedBytes(bytes);
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
    m_flowDecoder.reset();
    m_heartbeatDecoder.reset();
    m_receiveBuffer.clear();
    cancelFlowTransfer();
    return m_transport->open(device);
}

void BootloaderCommunicationService::close()
{
    if (isFlowTransferActive())
        finishFlowTransfer(false, QStringLiteral("串口关闭，AA59 传输已停止"));
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

void BootloaderCommunicationService::processReceivedBytes(const QByteArray &bytes)
{
    m_receiveBuffer.append(bytes);
    while (true)
    {
        const int start = m_receiveBuffer.indexOf(static_cast<char>(0xAA));
        if (start < 0)
        {
            m_receiveBuffer.clear();
            return;
        }
        if (start > 0)
            m_receiveBuffer.remove(0, start);
        if (m_receiveBuffer.size() < 2)
            return;

        const quint8 family = static_cast<quint8>(m_receiveBuffer.at(1));
        int packetLength = 0;
        quint8 expectedTail = 0;
        if (family == 0x55U)
        {
            if (m_receiveBuffer.size() < 3)
                return;
            const int bodyLength = static_cast<quint8>(m_receiveBuffer.at(2));
            if (bodyLength < 8 || bodyLength > 72)
            {
                m_receiveBuffer.remove(0, 1);
                continue;
            }
            packetLength = bodyLength + 6;
            expectedTail = 0x55U;
        }
        else if (family == 0x58U)
        {
            packetLength = 20;
            expectedTail = 0x58U;
        }
        else if (family == 0x59U)
        {
            if (m_receiveBuffer.size() < 12)
                return;
            const int payloadLength = static_cast<quint8>(m_receiveBuffer.at(10))
                                      | (static_cast<int>(static_cast<quint8>(m_receiveBuffer.at(11))) << 8);
            if (payloadLength > 4096)
            {
                m_receiveBuffer.remove(0, 1);
                continue;
            }
            packetLength = 20 + payloadLength;
            expectedTail = 0x59U;
        }
        else
        {
            m_receiveBuffer.remove(0, 1);
            continue;
        }

        if (m_receiveBuffer.size() < packetLength)
            return;
        if (static_cast<quint8>(m_receiveBuffer.at(packetLength - 2)) != expectedTail
            || static_cast<quint8>(m_receiveBuffer.at(packetLength - 1)) != 0xAAU)
        {
            m_receiveBuffer.remove(0, 1);
            continue;
        }

        const QByteArray packet = m_receiveBuffer.left(packetLength);
        m_receiveBuffer.remove(0, packetLength);
        if (family == 0x55U)
        {
            for (const auto &frame : m_decoder.feed(packet))
                emit frameReceived(frame);
            if (!m_decoder.lastError().isEmpty())
                emit errorOccurred(m_decoder.lastError());
        }
        else if (family == 0x58U)
        {
            for (const auto &heartbeat : m_heartbeatDecoder.feed(packet))
                emit heartbeatReceived(heartbeat);
        }
        else
        {
            for (const auto &frame : m_flowDecoder.feed(packet))
                handleFlowFrame(frame);
            if (!m_flowDecoder.lastError().isEmpty())
                emit errorOccurred(m_flowDecoder.lastError());
        }
    }
}

bool BootloaderCommunicationService::validateFlowBlock(const CanGatewayFrame &block,
                                                       QString &error) const
{
    if (!isAllowedFlowFlags(block.flags))
    {
        error = QStringLiteral("AA59 CAN_FLAGS=0x%1 不受支持")
                    .arg(block.flags, 2, 16, QLatin1Char('0')).toUpper();
        return false;
    }
    if (block.data.isEmpty() || block.data.size() > 64)
    {
        error = QStringLiteral("AA59 数据块长度必须为 1～64 字节");
        return false;
    }
    const bool extended = (block.flags & 0x01U) != 0;
    const bool canFd = (block.flags & 0x02U) != 0;
    const quint32 maximumId = extended ? 0x1FFFFFFFU : 0x7FFU;
    if (block.canId > maximumId)
    {
        error = QStringLiteral("AA59 CAN ID 超出范围");
        return false;
    }
    if (!canFd)
    {
        const quint32 fragments = static_cast<quint32>((block.data.size() + 7) / 8);
        if (block.canId + fragments - 1U > maximumId)
        {
            error = QStringLiteral("Classic CAN 拆分后的最后一个 CAN ID 超出范围");
            return false;
        }
    }
    return true;
}

bool BootloaderCommunicationService::startFlowTransfer(const QVector<CanGatewayFrame> &blocks)
{
    if (!isOpen())
    {
        emit errorOccurred(QStringLiteral("串口尚未连接，无法开始 AA59 传输"));
        return false;
    }
    if (isFlowTransferActive())
    {
        emit errorOccurred(QStringLiteral("已有 AA59 传输正在进行"));
        return false;
    }
    if (blocks.isEmpty())
    {
        emit errorOccurred(QStringLiteral("AA59 传输没有数据块"));
        return false;
    }

    quint64 totalBytes = 0;
    for (const auto &block : blocks)
    {
        QString error;
        if (!validateFlowBlock(block, error))
        {
            emit errorOccurred(error);
            return false;
        }
        totalBytes += static_cast<quint64>(block.data.size());
    }
    if (totalBytes > 0xFFFFFFFFULL)
    {
        emit errorOccurred(QStringLiteral("AA59 传输总长度超过 uint32 范围"));
        return false;
    }

    m_flowBlocks = blocks;
    m_seenFlowAckSequences.clear();
    m_flowSentBytes = 0;
    m_flowTotalBytes = static_cast<quint32>(totalBytes);
    m_lastAcknowledgedBlock = 0xFFFFFFFFU;
    m_nextFlowBlock = 0;
    m_flowCredit = 0;
    m_flowState = FlowState::WaitingBeginAck;

    QByteArray payload;
    appendLe32(payload, m_flowTotalBytes);
    if (!sendFlowFrame(CanFlowCommand::Begin, payload))
    {
        finishFlowTransfer(false, QStringLiteral("发送 AA59 BEGIN 失败"));
        return false;
    }
    m_flowTimeout.start(2000);
    return true;
}

void BootloaderCommunicationService::cancelFlowTransfer()
{
    if (!isFlowTransferActive())
        return;
    finishFlowTransfer(false, QStringLiteral("AA59 传输已取消"));
}

bool BootloaderCommunicationService::isFlowTransferActive() const
{
    return m_flowState != FlowState::Idle;
}

bool BootloaderCommunicationService::sendFlowFrame(const CanFlowCommand command,
                                                   const QByteArray &payload)
{
    CanFlowFrame frame;
    frame.command = command;
    frame.sequence = m_flowFrameSequence++;
    frame.payload = payload;
    const QByteArray packet = encodeCanFlowFrame(frame);
    return !packet.isEmpty() && m_transport->writeBytes(packet);
}

void BootloaderCommunicationService::pumpFlowBlocks()
{
    if (m_flowState != FlowState::SendingBlocks)
        return;

    while (m_flowCredit > 0 && m_nextFlowBlock < m_flowBlocks.size())
    {
        const CanGatewayFrame &block = m_flowBlocks.at(m_nextFlowBlock);
        QByteArray payload;
        payload.reserve(14 + block.data.size());
        appendLe32(payload, block.canId);
        payload.append(static_cast<char>(block.flags));
        appendLe32(payload, static_cast<quint32>(m_nextFlowBlock));
        appendLe32(payload, m_flowSentBytes);
        payload.append(static_cast<char>(block.data.size()));
        payload.append(block.data);
        if (!sendFlowFrame(CanFlowCommand::DataBlock, payload))
        {
            finishFlowTransfer(false,
                               QStringLiteral("发送 AA59 DATA_BLOCK %1 失败")
                                   .arg(m_nextFlowBlock));
            return;
        }
        --m_flowCredit;
        m_flowSentBytes += static_cast<quint32>(block.data.size());
        ++m_nextFlowBlock;
    }

    if (m_nextFlowBlock == m_flowBlocks.size())
        m_flowState = FlowState::WaitingFinalBlockAck;
    // 只要还有尚未确认的块，就持续等待累计 ACK。每个合法 ACK 都会重置超时。
    m_flowTimeout.start(2000);
}

void BootloaderCommunicationService::handleFlowFrame(const CanFlowFrame &frame)
{
    CanFlowAck ack;
    if (!decodeCanFlowAck(frame, ack) || !isFlowTransferActive())
        return;
    if (m_seenFlowAckSequences.contains(ack.frameSequence))
        return; // 重复 ACK 的 credit_return 绝不能重复累计。
    m_seenFlowAckSequences.insert(ack.frameSequence);
    handleFlowAck(ack);
}

void BootloaderCommunicationService::handleFlowAck(const CanFlowAck &ack)
{
    if (ack.status != CanFlowStatus::Ok)
    {
        finishFlowTransfer(false,
                           QStringLiteral("AA59 网关拒绝传输：%1（状态码 0x%2）")
                               .arg(canFlowStatusText(ack.status))
                               .arg(static_cast<quint8>(ack.status), 2, 16,
                                    QLatin1Char('0')).toUpper());
        return;
    }

    if (m_flowState == FlowState::WaitingBeginAck)
    {
        // BEGIN 回复中的 credit_return 当前固定为 0。初始 16 个额度是协议参数，
        // 必须由上位机在 BEGIN 成功后设置，不能用 free_blocks 代替。
        m_flowCredit = 16;
        m_flowState = FlowState::SendingBlocks;
        m_flowTimeout.stop();
        pumpFlowBlocks();
        return;
    }

    if (m_flowState == FlowState::WaitingEndAck)
    {
        finishFlowTransfer(true,
                           QStringLiteral("AA59 传输完成：%1 个逻辑块，%2 字节")
                               .arg(m_flowBlocks.size()).arg(m_flowTotalBytes));
        return;
    }

    if (m_flowState != FlowState::SendingBlocks
        && m_flowState != FlowState::WaitingFinalBlockAck)
        return;

    if (ack.acknowledgedBlock != 0xFFFFFFFFU
        && ack.acknowledgedBlock >= static_cast<quint32>(m_flowBlocks.size()))
    {
        finishFlowTransfer(false, QStringLiteral("AA59 ACK 的块编号超出本次传输范围"));
        return;
    }
    if (ack.creditReturn > 16U || m_flowCredit + ack.creditReturn > 16)
    {
        finishFlowTransfer(false, QStringLiteral("AA59 ACK 返回了非法 Credit"));
        return;
    }

    m_flowCredit += ack.creditReturn;
    if (ack.acknowledgedBlock != 0xFFFFFFFFU
        && (m_lastAcknowledgedBlock == 0xFFFFFFFFU
            || ack.acknowledgedBlock > m_lastAcknowledgedBlock))
    {
        m_lastAcknowledgedBlock = ack.acknowledgedBlock;
        emit flowTransferProgress(static_cast<int>(m_lastAcknowledgedBlock + 1U),
                                  m_flowBlocks.size());
    }

    const quint32 finalBlock = static_cast<quint32>(m_flowBlocks.size() - 1);
    if (m_nextFlowBlock == m_flowBlocks.size()
        && m_lastAcknowledgedBlock == finalBlock)
    {
        m_flowTimeout.stop();
        m_flowState = FlowState::WaitingEndAck;
        if (!sendFlowFrame(CanFlowCommand::End))
        {
            finishFlowTransfer(false, QStringLiteral("发送 AA59 END 失败"));
            return;
        }
        m_flowTimeout.start(2000);
        return;
    }

    m_flowState = FlowState::SendingBlocks;
    pumpFlowBlocks();
}

void BootloaderCommunicationService::finishFlowTransfer(const bool success,
                                                        const QString &message)
{
    if (!isFlowTransferActive())
        return;
    m_flowTimeout.stop();
    m_flowState = FlowState::Idle;
    m_flowCredit = 0;
    m_nextFlowBlock = 0;
    m_flowSentBytes = 0;
    m_flowTotalBytes = 0;
    m_lastAcknowledgedBlock = 0xFFFFFFFFU;
    m_seenFlowAckSequences.clear();
    m_flowBlocks.clear();
    emit flowTransferFinished(success, message);
}

} // namespace rov
