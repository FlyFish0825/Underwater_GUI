#include "communication/bootloader/BootloaderUpgradeSequence.h"

#include "communication/bootloader/BootloaderDownloadController.h"

#include <QSet>

namespace rov
{

BootloaderUpgradeSequence::BootloaderUpgradeSequence(
    BootloaderDownloadController *downloadController, QObject *parent)
    : QObject(parent), m_controller(downloadController)
{
    if (m_controller != nullptr)
        connect(m_controller, &BootloaderDownloadController::finished, this,
                &BootloaderUpgradeSequence::handleNodeFinished);
}

bool BootloaderUpgradeSequence::start(const QVector<quint8> &selectedNodes,
                                      const quint8 canaryNode, const quint8 guardNode,
                                      const QString &firmwarePath, const bool canFd)
{
    if (m_running || m_controller == nullptr || m_controller->isRunning() || firmwarePath.isEmpty())
        return false;

    const QVector<quint8> ordered = orderedTargets(selectedNodes, canaryNode, guardNode);
    if (ordered.isEmpty())
        return false;

    m_queue = ordered;
    m_firmwarePath = firmwarePath;
    m_canFd = canFd;
    m_nextIndex = 0;
    m_cancelRequested = false;
    m_hasDeferredFinished = false;
    m_running = true;
    return startNext();
}

QVector<quint8> BootloaderUpgradeSequence::orderedTargets(
    const QVector<quint8> &selectedNodes, const quint8 canaryNode, const quint8 guardNode)
{
    if (canaryNode < 1U || canaryNode > 8U || guardNode < 1U || guardNode > 8U
        || canaryNode == guardNode)
        return {};
    QSet<quint8> selected;
    for (const quint8 node : selectedNodes)
    {
        if (node < 1U || node > 8U || selected.contains(node))
            return {};
        selected.insert(node);
    }
    if (!selected.contains(canaryNode) || !selected.contains(guardNode))
        return {};
    QVector<quint8> ordered{canaryNode};
    for (quint8 node = 1; node <= 8U; ++node)
    {
        if (selected.contains(node) && node != canaryNode && node != guardNode)
            ordered.append(node);
    }
    ordered.append(guardNode);
    return ordered;
}

void BootloaderUpgradeSequence::cancel()
{
    if (!m_running)
        return;
    m_cancelRequested = true;
    if (m_controller != nullptr && m_controller->isRunning())
        m_controller->cancel();
}

quint8 BootloaderUpgradeSequence::currentTarget() const
{
    return m_currentTarget;
}

bool BootloaderUpgradeSequence::startNext()
{
    if (!m_running || m_cancelRequested)
    {
        m_running = false;
        emit finished(false, QStringLiteral("多节点升级已取消"));
        return false;
    }
    if (m_nextIndex >= m_queue.size())
    {
        m_running = false;
        emit finished(true, QStringLiteral("所选节点均已完成升级"));
        return true;
    }

    m_currentTarget = m_queue.at(m_nextIndex);
    ++m_nextIndex;
    m_startingNode = true;
    emit nodeStarted(m_currentTarget, m_nextIndex, m_queue.size());
    if (m_cancelRequested)
    {
        m_startingNode = false;
        m_running = false;
        emit finished(false, QStringLiteral("多节点升级已取消"));
        return false;
    }
    m_hasDeferredFinished = false;
    const bool accepted = m_controller->start(m_currentTarget, m_firmwarePath, m_canFd);
    m_startingNode = false;
    if (!accepted)
    {
        m_running = false;
        emit finished(false, QStringLiteral("Node%1 启动下载失败，队列已停止")
                                  .arg(m_currentTarget));
        return false;
    }
    if (m_hasDeferredFinished)
    {
        const bool success = m_deferredSuccess;
        const QString message = m_deferredMessage;
        m_hasDeferredFinished = false;
        handleNodeFinished(success, message);
    }
    return true;
}

void BootloaderUpgradeSequence::handleNodeFinished(const bool success, const QString &message)
{
    if (m_startingNode)
    {
        m_hasDeferredFinished = true;
        m_deferredSuccess = success;
        m_deferredMessage = message;
        return;
    }
    if (!m_running)
        return;
    emit nodeFinished(m_currentTarget, success, message);
    if (!success)
    {
        m_running = false;
        emit finished(false, QStringLiteral("Node%1 升级失败，后续节点未启动：%2")
                                  .arg(m_currentTarget)
                                  .arg(message));
        return;
    }
    if (m_cancelRequested)
    {
        m_running = false;
        emit finished(false, QStringLiteral("Node%1 已完成；多节点升级已取消")
                                  .arg(m_currentTarget));
        return;
    }
    startNext();
}

} // namespace rov
