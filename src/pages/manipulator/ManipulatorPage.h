#pragma once

#include "contracts/manipulator/ManipulatorContract.h"

#include <QWidget>

class QLabel;
class QTableWidget;

namespace rov
{

class ManipulatorPage final : public QWidget
{
    Q_OBJECT

  public:
    explicit ManipulatorPage(QWidget *parent = nullptr);

    void setSnapshot(const ManipulatorSnapshot &snapshot);

  signals:
    void jointTargetRequested(const JointTargetRequest &request);
    void gripperRequested(const GripperRequest &request);
    void homeRequested();
    void stopRequested();

  private:
    void refreshView();
    void logRequest(const QString &message);

    ManipulatorSnapshot m_snapshot;
    QLabel *m_systemState = nullptr;
    QLabel *m_controlMode = nullptr;
    QLabel *m_faultStatus = nullptr;
    QLabel *m_requestLog = nullptr;
    QTableWidget *m_jointTable = nullptr;
};

} // namespace rov
