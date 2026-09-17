#pragma once

#include "contracts/firmware/FirmwareContract.h"
#include "communication/service/BootloaderCommunicationService.h"

#include <QWidget>
#include <QStringList>

class QLabel;
class QComboBox;
class QPushButton;
class QTableWidget;

namespace rov
{

class FirmwarePage final : public QWidget
{
    Q_OBJECT

  public:
    explicit FirmwarePage(QWidget *parent = nullptr);

    void setSnapshot(const FirmwareSnapshot &snapshot);

  signals:
    void firmwareFileSelected(const FirmwareFileRequest &request);
    void upgradeRequested(const FirmwareUpgradeRequest &request);
    void verifyRequested();

  private:
    void refreshView();
    void refreshSerialDevices();
    void toggleSerialConnection();
    void logRequest(const QString &message);

    FirmwareSnapshot m_snapshot;
    QLabel *m_fileName = nullptr;
    QLabel *m_fileVersion = nullptr;
    QLabel *m_fileSize = nullptr;
    QLabel *m_checksum = nullptr;
    QLabel *m_description = nullptr;
    QLabel *m_requestLog = nullptr;
    QTableWidget *m_nodeTable = nullptr;
    QComboBox *m_serialDeviceCombo = nullptr;
    QLabel *m_serialStatus = nullptr;
    QPushButton *m_serialConnectButton = nullptr;
    QVector<SerialDeviceInfo> m_serialDevices;
    BootloaderCommunicationService *m_communication = nullptr;
    QStringList m_runtimeLog;
};

} // namespace rov
