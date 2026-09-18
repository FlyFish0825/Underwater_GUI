#pragma once

#include "contracts/dashboard/DashboardContract.h"

#include <QHBoxLayout>
#include <QList>
#include <QVBoxLayout>
#include <QWidget>

class QLabel;
class QGridLayout;
class QPushButton;
class QResizeEvent;

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
    void resizeEvent(QResizeEvent *event) override;
    void updateResponsiveLayout();
    void refreshView();
    void logRequest(const QString &message);

    DashboardSnapshot m_snapshot;
    QVBoxLayout *m_rootLayout = nullptr;
    QHBoxLayout *m_topRowLayout = nullptr;
    QVBoxLayout *m_stateColumnLayout = nullptr;
    QHBoxLayout *m_bottomRowLayout = nullptr;
    QGridLayout *m_overviewGridLayout = nullptr;
    QGridLayout *m_stateGridLayout = nullptr;
    QGridLayout *m_axisGridLayout = nullptr;
    QList<QVBoxLayout *> m_cardContentLayouts;
    QList<QHBoxLayout *> m_cardHeaderLayouts;
    QList<QPushButton *> m_dashboardButtons;
    QList<QWidget *> m_bottomCards;
    QWidget *m_rovTopView = nullptr;
    QWidget *m_stateCard = nullptr;
    QWidget *m_controlCard = nullptr;
    bool m_compactLayout = false;
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
