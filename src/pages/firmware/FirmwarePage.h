#pragma once

#include "contracts/firmware/FirmwareContract.h"

#include <QWidget>

class QLabel;
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
    void logRequest(const QString &message);

    FirmwareSnapshot m_snapshot;
    QLabel *m_fileName = nullptr;
    QLabel *m_fileVersion = nullptr;
    QLabel *m_fileSize = nullptr;
    QLabel *m_checksum = nullptr;
    QLabel *m_description = nullptr;
    QLabel *m_requestLog = nullptr;
    QTableWidget *m_nodeTable = nullptr;
};

} // namespace rov
