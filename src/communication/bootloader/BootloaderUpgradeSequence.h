#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>
#include <QVector>

namespace rov
{

class BootloaderDownloadController;

/** Serially reuses the established single-node download state machine. */
class BootloaderUpgradeSequence final : public QObject
{
    Q_OBJECT

  public:
    explicit BootloaderUpgradeSequence(BootloaderDownloadController *downloadController,
                                       QObject *parent = nullptr);

    bool start(const QVector<quint8> &selectedNodes, quint8 canaryNode, quint8 guardNode,
               const QString &firmwarePath, bool canFd = false);
    static QVector<quint8> orderedTargets(const QVector<quint8> &selectedNodes,
                                          quint8 canaryNode, quint8 guardNode);
    void cancel();
    bool isRunning() const { return m_running; }
    quint8 currentTarget() const;

  signals:
    void nodeStarted(quint8 target, int position, int count);
    void nodeFinished(quint8 target, bool success, const QString &message);
    void finished(bool success, const QString &message);

  private slots:
    void handleNodeFinished(bool success, const QString &message);

  private:
    bool startNext();

    BootloaderDownloadController *m_controller = nullptr;
    QVector<quint8> m_queue;
    QString m_firmwarePath;
    bool m_canFd = false;
    bool m_running = false;
    bool m_cancelRequested = false;
    bool m_startingNode = false;
    bool m_hasDeferredFinished = false;
    bool m_deferredSuccess = false;
    QString m_deferredMessage;
    int m_nextIndex = 0;
    quint8 m_currentTarget = 0;
};

} // namespace rov
