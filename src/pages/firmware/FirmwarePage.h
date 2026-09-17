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
class QFrame;
class QTextBrowser;
class QPushButton;
class QTimer;
class QTableWidget;

namespace rov
{

class BootloaderCommandDialog;
class FirmwareHistoryDialog;

class FirmwarePage final : public QWidget
{
    Q_OBJECT

  public:
    explicit FirmwarePage(QWidget *parent = nullptr);

    void setSnapshot(const FirmwareSnapshot &snapshot);

    // 连接栏由主窗口统一放置，避免把“仅连接检查”混在 Bootloader 页面内容中。
    QWidget *connectionBar() const;

    // 主窗口退出前调用，销毁脱离页面布局显示的独立窗口。
    void closeAuxiliaryWindows();

  signals:
    void firmwareFileSelected(const FirmwareFileRequest &request);
    void upgradeRequested(const FirmwareUpgradeRequest &request);
    void verifyRequested();
    // 高级命令窗口订阅此信号，实时显示与主页面一致的调试日志。
    void debugLogAppended(const QString &message);

  private:
    void refreshView();
    void refreshSerialDevices();
    void toggleSerialConnection();
    void browseFirmwareFile();
    bool loadFirmwareFile(const QString &path);
    void selectNode(int index);
    void selectTableRow(int row, int column);
    void sendCommonCommand(BootCommand command, const QString &label, quint8 byte2 = 0,
                           const QByteArray &params = QByteArray());
    void showCommandCenter();
    void handleBootResponse(const BootResponse &response);
    void handlePeerMessage(const PeerControlMessage &message);
    void showHistory();
    void logRequest(const QString &message);
    void renderRuntimeLog();

    FirmwareSnapshot m_snapshot;
    QLabel *m_fileName = nullptr;
    QLabel *m_fileVersion = nullptr;
    QLabel *m_fileSize = nullptr;
    QLabel *m_checksum = nullptr;
    QLabel *m_description = nullptr;
    QLabel *m_dropTitle = nullptr;
    QLabel *m_dropHint = nullptr;
    QTextBrowser *m_requestLog = nullptr;
    QFrame *m_dropZone = nullptr;
    QFrame *m_connectionBar = nullptr;
    QTableWidget *m_nodeTable = nullptr;
    QComboBox *m_serialDeviceCombo = nullptr;
    QLabel *m_serialStatus = nullptr;
    QPushButton *m_serialConnectButton = nullptr;
    QVector<SerialDeviceInfo> m_serialDevices;
    BootloaderCommunicationService *m_communication = nullptr;
    BootloaderService *m_bootloader = nullptr;
    // 高级命令窗口关闭后自动清空，支持重复打开。
    QPointer<BootloaderCommandDialog> m_commandDialog;
    QStringList m_runtimeLog;
    QString m_firmwarePath;
    QTimer *m_heartbeatWatchdog = nullptr;
    QTimer *m_deviceScanTimer = nullptr;
    quint64 m_heartbeatCount = 0;
    QString m_deviceSignature;
    FirmwareHistoryStore m_historyStore;
    // QPointer 会在对话框关闭销毁后自动变为空，支持历史窗口反复打开。
    QPointer<FirmwareHistoryDialog> m_historyDialog;
    QComboBox *m_targetNodeCombo = nullptr;
    QLabel *m_stateNode = nullptr;
    QLabel *m_stateDevice = nullptr;
    QLabel *m_stateBootloader = nullptr;
    QLabel *m_stateApp = nullptr;
    QLabel *m_stateConfig = nullptr;
    QLabel *m_stateStatus = nullptr;
    QLabel *m_stateError = nullptr;
    QLabel *m_stateProgress = nullptr;
};

} // namespace rov
