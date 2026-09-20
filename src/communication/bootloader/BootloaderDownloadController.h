#pragma once

#include "communication/bootloader/BootloaderService.h"

#include <QByteArray>
#include <QObject>
#include <QTimer>

namespace rov
{

/**
 * @brief Legacy 单节点固件下载状态机。
 *
 * 严格按照底层协议文档执行：ENTER_BOOT（无 ACK 等待）→ ERASE → WRITE →
 * DATA → WRITE_END → VERIFY → Trial JUMP_APP → APP ENTER_BOOT 返回验证。
 * 试运行期间由下位机 Bootloader 开启 IWDG；只有 APP 能主动复位回
 * Bootloader 后，Bootloader 才会恢复 app_valid。上位机不发送普通 JUMP_APP
 * 直接切换新 APP，避免异常镜像使节点无法回到 Bootloader。数据面固定使用 Session=0，并由
 * BootloaderService 根据用户选择，将每个 64 字节 DATA 包通过 AA59 交给
 * H750：Classic CAN 由网关拆成 0x100~0x107，CAN FD+BRS 发送为一帧。
 */
class BootloaderDownloadController final : public QObject
{
    Q_OBJECT

  public:
    explicit BootloaderDownloadController(BootloaderService *service,
                                          QObject *parent = nullptr);

    bool start(quint8 target, const QString &firmwarePath, bool canFd = false);
    void cancel();
    bool isRunning() const { return m_phase != Phase::Idle; }
    int totalPackets() const { return m_totalPackets; }

  signals:
    void phaseChanged(const QString &phase);
    void progressChanged(quint8 target, int percent, quint16 sequence, int totalPackets);
    void logMessage(const QString &message);
    void finished(bool success, const QString &message);

  private:
    enum class Phase
    {
        Idle,
        WaitingBoot,
        Erasing,
        Writing,
        Streaming,
        WaitingGatewayFlow,
        WaitingWindow,
        Ending,
        Verifying,
        TrialBooting,
        TrialReturning,
        TrialChecking,
    };

    void step();
    void handleResponse(const BootResponse &response);
    void sendErase();
    void sendWrite();
    void sendWriteEnd();
    void sendVerify();
    void sendTrialJump();
    void sendTrialEnterBoot();
    void queryTrialResult();
    void pumpData();
    void queryWindow();
    void handleWindow(const BootResponse &response);
    void handleDataWindowProgress(int completedPackets, int totalPackets);
    void handleDataWindowFinished(bool success, const QString &message);
    bool sendControl(BootCommand command, quint8 byte2 = 0,
                     const QByteArray &params = QByteArray());
    void armResponseTimeout(int milliseconds, const QString &phase);
    void fail(const QString &message);
    void complete(const QString &message);

    BootloaderService *m_service = nullptr;
    QTimer m_stepTimer;
    QTimer m_responseTimer;
    Phase m_phase = Phase::Idle;
    quint8 m_target = 0;
    bool m_canFd = false;
    QByteArray m_firmware;
    QString m_firmwarePath;
    quint32 m_crc32 = 0;
    quint16 m_sequence = 0;
    int m_totalPackets = 0;
    int m_windowPackets = 0;
    int m_windowStart = 0;
    int m_windowEnd = 0;
    int m_committed = 0;
    int m_windowAttempts = 0;
    bool m_windowQueried = false;
    quint16 m_pendingWindowStart = 0;
    quint16 m_pendingWindowEnd = 0;
    bool m_hasDeferredWindowResponse = false;
    BootResponse m_deferredWindowResponse;
    int m_trialEnterBootAttempts = 0;
    int m_trialProbeAttempts = 0;
};

} // namespace rov
