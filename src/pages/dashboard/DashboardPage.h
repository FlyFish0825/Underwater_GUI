#pragma once

#include "contracts/dashboard/DashboardContract.h"

#include <QVector>
#include <QWidget>

class QCheckBox;
class QLabel;
class QSlider;

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
    void manualControlEnableRequested(const ManualControlEnableRequest &request);
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
    QLabel *m_alarmSummary = nullptr;
    QLabel *m_controlPermission = nullptr;
    QLabel *m_requestLog = nullptr;
    QLabel *m_stateUpdate = nullptr;
    QLabel *m_totalThrusterValue = nullptr;
    QLabel *m_onlineThrusterValue = nullptr;
    QLabel *m_offlineThrusterValue = nullptr;
    QLabel *m_warningValue = nullptr;
    QLabel *m_averageRpmValue = nullptr;
    QLabel *m_averageCurrentValue = nullptr;
    QLabel *m_averageTemperatureValue = nullptr;
    QCheckBox *m_enableControl = nullptr;
    QSlider *m_thrustLimitSlider = nullptr;
    QLabel *m_thrustLimitValue = nullptr;
    QWidget *m_rpmChart = nullptr;
    QWidget *m_currentChart = nullptr;
    QWidget *m_temperatureChart = nullptr;
    QVector<QLabel *> m_thrusterRpmValues;
    QVector<QLabel *> m_thrusterCurrentValues;
    QVector<QLabel *> m_thrusterTemperatureValues;
    QVector<QLabel *> m_thrusterStatusValues;
};

} // namespace rov
