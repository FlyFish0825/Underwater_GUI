#pragma once

#include "communication/bootloader/BootloaderTypes.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

namespace rov
{

class BootloaderService;

/**
 * @brief 使用 CAN_FD_IAP 自治协议执行 7+1 多节点升级。
 *
 * Host 只负责进入 Bootloader、建立 Session、广播首轮镜像和发送 WRITE_END。
 * 缺包修复、Coordinator 选举、Guard 更新以及最终 Commit 由节点间协议完成。
 */
class BootloaderAutonomousUpgrade final : public QObject
{
    Q_OBJECT

  public:
    explicit BootloaderAutonomousUpgrade(BootloaderService *service,
                                         QObject *parent = nullptr);

    bool start(const QVector<quint8> &selectedNodes, quint8 guardNode,
               const QString &firmwarePath, bool canFd = false);
    void cancel();
    bool isRunning() const { return m_running; }

  signals:
    void nodeProgress(quint8 node, int percent);
    void nodeStateChanged(quint8 node, const QString &state);
    void logMessage(const QString &message);
    void finished(bool success, const QString &message);

  private slots:
    void handleHostResponse(const BootResponse &response);
    void handlePeerMessage(const PeerControlMessage &message);
    void handleDataProgress(int completedPackets, int totalPackets);
    void handleDataFinished(bool success, const QString &message);
    void handleTimeout();

  private:
    enum class Phase
    {
        Idle,
        WaitingBoot,
        Session,
        SessionCrc,
        Guard,
        Erase,
        Write,
        Streaming,
        WriteEnd,
        Recovery,
        WaitingCommit,
    };

    bool loadFirmware(const QString &firmwarePath);
    bool sendControl(BootCommand command, quint8 byte2 = 0,
                     const QByteArray &params = QByteArray());
    bool allResponsesReceived() const;
    bool isSelected(quint8 node) const;
    bool isPrimary(quint8 node) const;
    void resetResponseSet();
    void armTimeout(int milliseconds, const QString &description);
    void beginSession();
    void sendSessionCrc();
    void sendGuard();
    void sendErase();
    void sendWrite();
    void sendData();
    void sendWriteEnd();
    void beginRecovery();
    void finish(bool success, const QString &message);
    void fail(const QString &message);
    void setAllPrimaryStates(const QString &state);

    BootloaderService *m_service = nullptr;
    QTimer m_timer;
    Phase m_phase = Phase::Idle;
    QVector<quint8> m_targets;
    QSet<quint8> m_responses;
    QSet<quint8> m_commitAcks;
    QString m_firmwarePath;
    QByteArray m_firmware;
    quint32 m_crc32 = 0;
    quint16 m_sessionId = 0;
    quint16 m_totalPackets = 0;
    quint8 m_guardNode = 0;
    bool m_canFd = false;
    bool m_running = false;
    bool m_commitExecuteSeen = false;
    QString m_waitDescription;
};

} // namespace rov
