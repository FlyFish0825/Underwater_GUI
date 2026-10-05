#include "communication/bootloader/BootloaderAutonomousUpgrade.h"

#include "communication/bootloader/BootloaderProtocol.h"
#include "communication/bootloader/BootloaderService.h"

#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStringList>

namespace rov
{

namespace
{
constexpr quint8 kBroadcastNode = 0xFFU;
constexpr quint8 kSessionFlags = 0x07U; // Peer Recovery + Guard Rollback + Coordinated Commit
constexpr int kBootSettleMs = 1200;
constexpr int kControlTimeoutMs = 7000;
constexpr int kEraseTimeoutMs = 30000;
constexpr int kRecoveryTimeoutMs = 180000;
constexpr int kCommitSettleMs = 8000;

void appendLe32(QByteArray &bytes, quint32 value)
{
    bytes.append(static_cast<char>(value & 0xFFU));
    bytes.append(static_cast<char>((value >> 8U) & 0xFFU));
    bytes.append(static_cast<char>((value >> 16U) & 0xFFU));
    bytes.append(static_cast<char>((value >> 24U) & 0xFFU));
}

QString nodeListText(const QVector<quint8> &nodes)
{
    QStringList values;
    for (const quint8 node : nodes)
        values.append(QStringLiteral("Node%1").arg(node));
    return values.join(QStringLiteral("、"));
}
} // namespace

BootloaderAutonomousUpgrade::BootloaderAutonomousUpgrade(BootloaderService *service,
                                                         QObject *parent)
    : QObject(parent), m_service(service)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &BootloaderAutonomousUpgrade::handleTimeout);
    if (m_service != nullptr)
    {
        connect(m_service, &BootloaderService::hostResponseReceived, this,
                &BootloaderAutonomousUpgrade::handleHostResponse);
        connect(m_service, &BootloaderService::peerMessageReceived, this,
                &BootloaderAutonomousUpgrade::handlePeerMessage);
        connect(m_service, &BootloaderService::dataWindowProgress, this,
                &BootloaderAutonomousUpgrade::handleDataProgress);
        connect(m_service, &BootloaderService::dataWindowFinished, this,
                &BootloaderAutonomousUpgrade::handleDataFinished);
    }
}

bool BootloaderAutonomousUpgrade::start(const QVector<quint8> &selectedNodes,
                                        const quint8 guardNode,
                                        const QString &firmwarePath, const bool canFd)
{
    if (m_running || m_service == nullptr || firmwarePath.isEmpty())
        return false;

    QSet<quint8> unique;
    for (const quint8 node : selectedNodes)
    {
        if (node < 1U || node > 8U || unique.contains(node))
            return false;
        unique.insert(node);
    }
    // CAN_FD_IAP 的 7+1 流程要求完整成员集合，Guard 由其中一个节点承担。
    if (unique.size() != 8 || guardNode < 1U || guardNode > 8U
        || !unique.contains(guardNode))
        return false;
    if (!loadFirmware(firmwarePath))
        return false;

    m_targets = selectedNodes;
    m_guardNode = guardNode;
    m_canFd = canFd;
    m_sessionId = static_cast<quint16>(QRandomGenerator::global()->bounded(1, 0x10000));
    if (m_sessionId == 0)
        m_sessionId = 1;
    m_commitAcks.clear();
    m_commitExecuteSeen = false;
    m_running = true;
    m_phase = Phase::WaitingBoot;
    resetResponseSet();

    for (const quint8 node : m_targets)
    {
        emit nodeProgress(node, 0);
        emit nodeStateChanged(node, node == m_guardNode ? QStringLiteral("准备成为 Guard")
                                                        : QStringLiteral("准备进入 Bootloader"));
    }
    emit logMessage(QStringLiteral("启动 7+1 并行升级：Guard=Node%1，目标=%2，Session=0x%3")
                        .arg(m_guardNode)
                        .arg(nodeListText(m_targets))
                        .arg(m_sessionId, 4, 16, QLatin1Char('0')).toUpper());
    if (!sendControl(BootCommand::EnterBoot))
    {
        fail(QStringLiteral("发送广播 ENTER_BOOT 失败"));
        return false;
    }
    armTimeout(kBootSettleMs, QStringLiteral("等待所有节点进入 Bootloader"));
    return true;
}

bool BootloaderAutonomousUpgrade::loadFirmware(const QString &firmwarePath)
{
    if (QFileInfo(firmwarePath).suffix().compare(QStringLiteral("bin"), Qt::CaseInsensitive) != 0)
        return false;
    QFile file(firmwarePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray image = file.readAll();
    file.close();
    if (image.isEmpty() || image.size() > 106 * 1024)
        return false;
    const int packets = (image.size() + BootloaderProtocol::dataPayloadSize - 1)
                        / BootloaderProtocol::dataPayloadSize;
    if (packets <= 0 || packets > 0xFFFF)
        return false;
    m_firmwarePath = QFileInfo(firmwarePath).absoluteFilePath();
    m_firmware = image;
    m_crc32 = BootloaderProtocol::crc32Mpeg2(m_firmware);
    m_totalPackets = static_cast<quint16>(packets);
    return true;
}

bool BootloaderAutonomousUpgrade::sendControl(const BootCommand command, const quint8 byte2,
                                              const QByteArray &params)
{
    if (m_service == nullptr || !m_service->sendHostCommand(kBroadcastNode, command, byte2, params))
        return false;
    const QString raw = BootloaderProtocol::encodeHostControl(kBroadcastNode, command, byte2, params)
                            .toHex(' ')
                            .toUpper();
    emit logMessage(QStringLiteral("TX 广播 · %1 · CAN=0x000 · DATA=%2")
                        .arg(bootCommandName(command), QString(raw)));
    return true;
}

bool BootloaderAutonomousUpgrade::allResponsesReceived() const
{
    for (const quint8 node : m_targets)
    {
        if (!m_responses.contains(node))
            return false;
    }
    return true;
}

bool BootloaderAutonomousUpgrade::isSelected(const quint8 node) const
{
    return m_targets.contains(node);
}

bool BootloaderAutonomousUpgrade::isPrimary(const quint8 node) const
{
    return isSelected(node) && node != m_guardNode;
}

void BootloaderAutonomousUpgrade::resetResponseSet()
{
    m_responses.clear();
}

void BootloaderAutonomousUpgrade::armTimeout(const int milliseconds, const QString &description)
{
    m_waitDescription = description;
    m_timer.start(milliseconds);
}

void BootloaderAutonomousUpgrade::beginSession()
{
    if (!m_running || m_phase != Phase::WaitingBoot)
        return;
    m_phase = Phase::Session;
    resetResponseSet();
    QByteArray params;
    params.append(static_cast<char>(m_sessionId & 0xFFU));
    params.append(static_cast<char>((m_sessionId >> 8U) & 0xFFU));
    params.append('\0');
    params.append('\0');
    if (!sendControl(BootCommand::SessionBegin, kSessionFlags, params))
    {
        fail(QStringLiteral("发送 SESSION_BEGIN 失败"));
        return;
    }
    armTimeout(kControlTimeoutMs, QStringLiteral("等待 8 个节点确认 Session"));
}

void BootloaderAutonomousUpgrade::sendSessionCrc()
{
    m_phase = Phase::SessionCrc;
    resetResponseSet();
    QByteArray params;
    appendLe32(params, m_crc32);
    if (!sendControl(BootCommand::SessionCrc32, 0, params))
    {
        fail(QStringLiteral("发送 SESSION_CRC32 失败"));
        return;
    }
    armTimeout(kControlTimeoutMs, QStringLiteral("等待节点确认镜像 CRC32"));
}

void BootloaderAutonomousUpgrade::sendGuard()
{
    m_phase = Phase::Guard;
    resetResponseSet();
    if (!sendControl(BootCommand::SetGuard, m_guardNode))
    {
        fail(QStringLiteral("发送 SET_GUARD 失败"));
        return;
    }
    armTimeout(kControlTimeoutMs, QStringLiteral("等待节点确认 Guard"));
}

void BootloaderAutonomousUpgrade::sendErase()
{
    m_phase = Phase::Erase;
    resetResponseSet();
    setAllPrimaryStates(QStringLiteral("并行擦除中"));
    if (!sendControl(BootCommand::Erase))
    {
        fail(QStringLiteral("发送广播 ERASE 失败"));
        return;
    }
    armTimeout(kEraseTimeoutMs, QStringLiteral("等待 7 个普通节点完成擦除"));
}

void BootloaderAutonomousUpgrade::sendWrite()
{
    m_phase = Phase::Write;
    resetResponseSet();
    QByteArray params;
    appendLe32(params, static_cast<quint32>(m_firmware.size()));
    if (!sendControl(BootCommand::Write, 0, params))
    {
        fail(QStringLiteral("发送广播 WRITE 失败"));
        return;
    }
    armTimeout(kControlTimeoutMs, QStringLiteral("等待 7 个普通节点建立写入会话"));
}

void BootloaderAutonomousUpgrade::sendData()
{
    m_phase = Phase::Streaming;
    setAllPrimaryStates(QStringLiteral("7 节点并行接收数据"));
    QVector<QByteArray> payloads;
    payloads.reserve(m_totalPackets);
    for (int sequence = 0; sequence < m_totalPackets; ++sequence)
        payloads.append(m_firmware.mid(sequence * BootloaderProtocol::dataPayloadSize,
                                       BootloaderProtocol::dataPayloadSize));
    emit logMessage(QStringLiteral("开始广播 DATA：%1 个逻辑包，%2")
                        .arg(m_totalPackets)
                        .arg(m_canFd ? QStringLiteral("CAN FD+BRS")
                                     : QStringLiteral("Classic CAN 8 分片")));
    if (!m_service->startDataWindow(kBroadcastNode, 0, payloads, m_sessionId, m_canFd))
    {
        fail(QStringLiteral("无法启动广播 DATA 流控"));
        return;
    }
    m_timer.stop();
}

void BootloaderAutonomousUpgrade::sendWriteEnd()
{
    m_phase = Phase::WriteEnd;
    resetResponseSet();
    if (!sendControl(BootCommand::WriteEnd))
    {
        fail(QStringLiteral("发送广播 WRITE_END 失败"));
        return;
    }
    armTimeout(kControlTimeoutMs, QStringLiteral("等待节点进入自治恢复"));
}

void BootloaderAutonomousUpgrade::beginRecovery()
{
    m_phase = Phase::Recovery;
    setAllPrimaryStates(QStringLiteral("设备自治修复与校验中"));
    emit nodeStateChanged(m_guardNode, QStringLiteral("Guard 保留旧固件"));
    emit logMessage(QStringLiteral("WRITE_END 已完成，后续由 CAN_FD_IAP 节点自治执行缺包修复、Guard 更新和 Commit"));
    armTimeout(kRecoveryTimeoutMs, QStringLiteral("等待节点自治升级完成"));
}

void BootloaderAutonomousUpgrade::handleHostResponse(const BootResponse &response)
{
    if (!m_running || !isSelected(response.nodeId))
        return;

    if (response.status == BootStatus::Error)
    {
        fail(QStringLiteral("Node%1 在 %2 阶段报错 0x%3")
                 .arg(response.nodeId)
                 .arg(bootCommandName(response.command))
                 .arg(response.errorCode, 2, 16, QLatin1Char('0')).toUpper());
        return;
    }

    switch (m_phase)
    {
    case Phase::Session:
        if (response.command != BootCommand::SessionBegin || response.status != BootStatus::Ready
            || response.data.size() < 2)
            return;
        if (static_cast<quint8>(response.data.at(0)) != (m_sessionId & 0xFFU)
            || static_cast<quint8>(response.data.at(1)) != ((m_sessionId >> 8U) & 0xFFU))
            return;
        m_responses.insert(response.nodeId);
        emit nodeStateChanged(response.nodeId, QStringLiteral("已加入升级 Session"));
        if (allResponsesReceived())
            sendSessionCrc();
        break;
    case Phase::SessionCrc:
        if (response.command != BootCommand::SessionCrc32 || response.status != BootStatus::Ready)
            return;
        m_responses.insert(response.nodeId);
        if (allResponsesReceived())
            sendGuard();
        break;
    case Phase::Guard:
        if (response.command != BootCommand::SetGuard
            || (response.status != BootStatus::Ready && response.status != BootStatus::Guard))
            return;
        m_responses.insert(response.nodeId);
        emit nodeStateChanged(response.nodeId, response.nodeId == m_guardNode
                                              ? QStringLiteral("Guard 已就位")
                                              : QStringLiteral("等待并行升级"));
        if (allResponsesReceived())
            sendErase();
        break;
    case Phase::Erase:
        if (response.command != BootCommand::Erase)
            return;
        if (response.nodeId == m_guardNode && response.status == BootStatus::Guard)
        {
            m_responses.insert(response.nodeId);
            emit nodeStateChanged(response.nodeId, QStringLiteral("Guard 守护中"));
        }
        else if (isPrimary(response.nodeId) && response.status == BootStatus::Ready)
        {
            m_responses.insert(response.nodeId);
            emit nodeStateChanged(response.nodeId, QStringLiteral("擦除完成"));
        }
        if (allResponsesReceived())
            sendWrite();
        break;
    case Phase::Write:
        if (response.command != BootCommand::Write)
            return;
        if (response.nodeId == m_guardNode && response.status == BootStatus::Guard)
        {
            m_responses.insert(response.nodeId);
            emit nodeStateChanged(response.nodeId, QStringLiteral("Guard 守护中"));
        }
        else if (isPrimary(response.nodeId) && response.status == BootStatus::Write)
        {
            m_responses.insert(response.nodeId);
            emit nodeStateChanged(response.nodeId, QStringLiteral("写入会话已建立"));
        }
        if (allResponsesReceived())
            sendData();
        break;
    case Phase::WriteEnd:
        if (response.command != BootCommand::WriteEnd)
            return;
        if (response.nodeId == m_guardNode && response.status == BootStatus::Guard)
            m_responses.insert(response.nodeId);
        else if (isPrimary(response.nodeId)
                 && (response.status == BootStatus::Verify || response.status == BootStatus::Repair))
            m_responses.insert(response.nodeId);
        if (allResponsesReceived())
            beginRecovery();
        break;
    case Phase::WaitingCommit:
    case Phase::Recovery:
    case Phase::Idle:
    case Phase::WaitingBoot:
    case Phase::Streaming:
        break;
    }
}

void BootloaderAutonomousUpgrade::handlePeerMessage(const PeerControlMessage &message)
{
    if (!m_running || !isSelected(message.source) || message.session != m_sessionId)
        return;

    if (message.command == BootCommand::RecoveryFailed)
    {
        fail(QStringLiteral("Node%1 报告自治恢复失败（轮次 %2）")
                 .arg(message.source)
                 .arg(message.value));
        return;
    }
    if (m_phase != Phase::Recovery && m_phase != Phase::WaitingCommit)
        return;

    if (message.command == BootCommand::VerifyResult)
    {
        emit nodeStateChanged(message.source, QStringLiteral("节点校验完成"));
    }
    else if (message.command == BootCommand::CommitAck)
    {
        if (message.value != 0)
        {
            m_commitAcks.insert(message.source);
            emit nodeStateChanged(message.source, QStringLiteral("提交准备完成"));
        }
        // Coordinator 本地直接置位 ACK 位图，不会再给自己发送一条 Peer ACK；
        // Host 因此最多收到其余节点的 N-1 条 COMMIT_ACK。
        const int expectedPeerAcks = qMax(1, m_targets.size() - 1);
        if (m_commitAcks.size() >= expectedPeerAcks)
        {
            m_phase = Phase::WaitingCommit;
            setAllPrimaryStates(QStringLiteral("等待统一提交"));
            armTimeout(kCommitSettleMs, QStringLiteral("等待所有节点统一提交"));
        }
    }
    else if (message.command == BootCommand::CommitExecute)
    {
        m_commitExecuteSeen = true;
        m_phase = Phase::WaitingCommit;
        setAllPrimaryStates(QStringLiteral("统一提交并返回 APP"));
        armTimeout(kCommitSettleMs, QStringLiteral("等待节点完成统一提交"));
    }
    else if (message.command == BootCommand::RecoveryReady)
    {
        emit nodeStateChanged(message.source, QStringLiteral("自治恢复完成"));
    }
}

void BootloaderAutonomousUpgrade::handleDataProgress(const int completedPackets,
                                                      const int totalPackets)
{
    if (!m_running || m_phase != Phase::Streaming || totalPackets <= 0)
        return;
    const int percent = qBound(0, completedPackets * 100 / totalPackets, 100);
    for (const quint8 node : m_targets)
    {
        if (isPrimary(node))
            emit nodeProgress(node, percent);
    }
}

void BootloaderAutonomousUpgrade::handleDataFinished(const bool success, const QString &message)
{
    if (!m_running || m_phase != Phase::Streaming)
        return;
    if (!success)
    {
        fail(QStringLiteral("广播 DATA 流控失败：%1").arg(message));
        return;
    }
    for (const quint8 node : m_targets)
    {
        if (isPrimary(node))
        {
            emit nodeProgress(node, 100);
            emit nodeStateChanged(node, QStringLiteral("首轮数据发送完成"));
        }
    }
    sendWriteEnd();
}

void BootloaderAutonomousUpgrade::handleTimeout()
{
    if (!m_running)
        return;
    if (m_phase == Phase::WaitingBoot)
    {
        beginSession();
        return;
    }
    if (m_phase == Phase::Recovery || m_phase == Phase::WaitingCommit)
    {
        if (m_commitExecuteSeen || m_commitAcks.size() >= qMax(1, m_targets.size() - 1))
        {
            finish(true, QStringLiteral("7+1 并行升级已完成，节点已执行统一提交"));
            return;
        }
    }
    fail(QStringLiteral("%1超时；已收到 %2/%3 个节点响应")
             .arg(m_waitDescription)
             .arg(m_responses.size())
             .arg(m_targets.size()));
}

void BootloaderAutonomousUpgrade::setAllPrimaryStates(const QString &state)
{
    for (const quint8 node : m_targets)
    {
        if (isPrimary(node))
            emit nodeStateChanged(node, state);
    }
}

void BootloaderAutonomousUpgrade::cancel()
{
    if (!m_running)
        return;
    if (m_service != nullptr)
    {
        m_service->cancelDataWindow();
        m_service->sendHostCommand(kBroadcastNode, BootCommand::Abort);
    }
    finish(false, QStringLiteral("用户取消 7+1 并行升级"));
}

void BootloaderAutonomousUpgrade::finish(const bool success, const QString &message)
{
    if (!m_running)
        return;
    m_timer.stop();
    m_running = false;
    m_phase = Phase::Idle;
    emit finished(success, message);
}

void BootloaderAutonomousUpgrade::fail(const QString &message)
{
    emit logMessage(QStringLiteral("7+1 并行升级失败：%1").arg(message));
    if (m_service != nullptr && m_running)
    {
        m_service->cancelDataWindow();
        m_service->sendHostCommand(kBroadcastNode, BootCommand::Abort);
    }
    finish(false, message);
}

} // namespace rov
