#pragma once

#include "contracts/firmware/FirmwareContract.h"
#include "communication/service/BootloaderCommunicationService.h"
#include "communication/bootloader/BootloaderService.h"
#include "data/store/FirmwareHistoryStore.h"

#include <QWidget>
#include <QPointer>
#include <QStringList>

class QLabel;
class QComboBox;
class QBoxLayout;
class QCheckBox;
class QFrame;
class QLineEdit;
class QResizeEvent;
class QTextBrowser;
class QPushButton;
class QTimer;
class QTableWidget;

namespace rov
{

class BootloaderCommandDialog;
class FirmwareHistoryDialog;
class FirmwareLogRecordingDialog;
class BootloaderDownloadController;
class AppProgressBar;

class FirmwarePage final : public QWidget
{
    Q_OBJECT

  public:
    explicit FirmwarePage(QWidget *parent = nullptr);

    void setSnapshot(const FirmwareSnapshot &snapshot);

    // 连接栏由主窗口统一放置，避免把“仅连接检查”混在 Bootloader 页面内容中。
    QWidget *connectionBar() const;

    // 供主窗口的数据服务订阅同一条 USB CDC → AA55 CAN 网关帧流。
    BootloaderCommunicationService *communicationService() const;

    // 主窗口退出前调用，销毁脱离页面布局显示的独立窗口。
    void closeAuxiliaryWindows();

  signals:
    void firmwareFileSelected(const FirmwareFileRequest &request);
    void upgradeRequested(const FirmwareUpgradeRequest &request);
    void verifyRequested();
    // 高级命令窗口订阅此信号，实时显示与主页面一致的调试日志。
    void debugLogAppended(const QString &message);

  protected:
    void resizeEvent(QResizeEvent *event) override;

  private:
    void refreshView();
    void refreshSerialDevices();
    void toggleSerialConnection();
    void browseFirmwareFile();
    bool loadFirmwareFile(const QString &path);
    void selectNode(int index);
    void selectTableRow(int row, int column);
    void setUpgradeMode(int mode);
    void sendCommonCommand(BootCommand command, const QString &label, quint8 byte2 = 0,
                           const QByteArray &params = QByteArray());
    void showCommandCenter();
    void handleBootResponse(const BootResponse &response);
    void handlePeerMessage(const PeerControlMessage &message);
    void startFirmwareDownload();
    void cancelFirmwareDownload();
    void updateDownloadProgress(quint8 target, int percent, quint16 sequence, int totalPackets);
    void finishFirmwareDownload(bool success, const QString &message);
    void showHistory();
    void toggleLogRecording();
    void showRecordedLogs();
    void exportRecordedLogs();
    void logRequest(const QString &message);
    void renderRuntimeLog();
    bool matchesRuntimeLogFilter(const QString &message) const;

    FirmwareSnapshot m_snapshot;
    QBoxLayout *m_mainRowLayout = nullptr;
    QLabel *m_fileName = nullptr;
    QLabel *m_fileVersion = nullptr;
    QLabel *m_fileSize = nullptr;
    QLabel *m_checksum = nullptr;
    QLabel *m_description = nullptr;
    QLabel *m_dropTitle = nullptr;
    QLabel *m_dropHint = nullptr;
    QTextBrowser *m_requestLog = nullptr;
    QComboBox *m_logFilter = nullptr;
    QComboBox *m_logNodeFilter = nullptr;
    QLineEdit *m_logCanIdFilter = nullptr;
    QLineEdit *m_logSearchFilter = nullptr;
    QPushButton *m_logFollowButton = nullptr;
    QPushButton *m_logPauseButton = nullptr;
    bool m_logFollowing = true;
    bool m_logPaused = false;
    QFrame *m_dropZone = nullptr;
    QFrame *m_connectionBar = nullptr;
    QFrame *m_multiModePanel = nullptr;
    QFrame *m_protocolModePanel = nullptr;
    QLabel *m_demoBanner = nullptr;
    QTableWidget *m_nodeTable = nullptr;
    QComboBox *m_serialDeviceCombo = nullptr;
    QComboBox *m_transferModeCombo = nullptr;
    QLabel *m_serialStatus = nullptr;
    QPushButton *m_serialConnectButton = nullptr;
    QVector<SerialDeviceInfo> m_serialDevices;
    BootloaderCommunicationService *m_communication = nullptr;
    BootloaderService *m_bootloader = nullptr;
    BootloaderDownloadController *m_downloadController = nullptr;
    // 高级命令窗口关闭后自动清空，支持重复打开。
    QPointer<BootloaderCommandDialog> m_commandDialog;
    QStringList m_runtimeLog;
    QString m_firmwarePath;
    QTimer *m_heartbeatWatchdog = nullptr;
    QTimer *m_deviceScanTimer = nullptr;
    QTimer *m_bootProbeTimer = nullptr;
    quint8 m_bootProbeTarget = 0;
    quint64 m_heartbeatCount = 0;
    QString m_deviceSignature;
    FirmwareHistoryStore m_historyStore;
    // QPointer 会在对话框关闭销毁后自动变为空，支持历史窗口反复打开。
    QPointer<FirmwareHistoryDialog> m_historyDialog;
    QPointer<FirmwareLogRecordingDialog> m_recordingDialog;
    QPushButton *m_logRecordButton = nullptr;
    QPushButton *m_viewRecordedButton = nullptr;
    QPushButton *m_exportRecordedButton = nullptr;
    QStringList m_recordedLogs;
    bool m_logRecording = false;
    QComboBox *m_targetNodeCombo = nullptr;
    QComboBox *m_canaryNodeCombo = nullptr;
    QComboBox *m_guardNodeCombo = nullptr;
    QVector<QCheckBox *> m_multiNodeChecks;
    int m_upgradeMode = 0;
    QLabel *m_stateNode = nullptr;
    QLabel *m_stateDevice = nullptr;
    QLabel *m_stateBootloader = nullptr;
    QLabel *m_stateApp = nullptr;
    QLabel *m_stateConfig = nullptr;
    QLabel *m_stateStatus = nullptr;
    QLabel *m_stateError = nullptr;
    QLabel *m_stateProgress = nullptr;
    QVector<AppProgressBar *> m_progressBars;
    QVector<QLabel *> m_progressStates;
    QPushButton *m_updateButton = nullptr;
};

} // namespace rov
