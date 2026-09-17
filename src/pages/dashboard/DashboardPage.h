#pragma once

#include "contracts/dashboard/DashboardContract.h"

#include <QWidget>

class QLabel;

namespace rov
{

class DashboardPage final : public QWidget
{
    Q_OBJECT

  public:
    explicit DashboardPage(QWidget *parent = nullptr);

    void setSnapshot(const DashboardSnapshot &snapshot);

  signals:
    void armRequested();
    void disarmRequested();
    void holdPositionRequested();
    void surfaceRequested();
    void manualControlRequested(const SixDofControlRequest &request);
    void thrustLimitRequested(const ThrustLimitRequest &request);

  private:
    void refreshView();
    void logRequest(const QString &message);

    DashboardSnapshot m_snapshot;
    QLabel *m_depthValue = nullptr;
    QLabel *m_rollValue = nullptr;
    QLabel *m_pitchValue = nullptr;
    QLabel *m_yawValue = nullptr;
    QLabel *m_voltageValue = nullptr;
    QLabel *m_modeValue = nullptr;
    QLabel *m_armValue = nullptr;
    QLabel *m_leakValue = nullptr;
    QLabel *m_temperatureValue = nullptr;
    QLabel *m_alarmValue = nullptr;
    QLabel *m_controlPermission = nullptr;
    QLabel *m_requestLog = nullptr;
};

} // namespace rov
