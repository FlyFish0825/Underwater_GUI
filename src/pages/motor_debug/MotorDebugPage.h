#pragma once

#include "contracts/motor_debug/MotorDebugContract.h"

#include <QWidget>

class QLabel;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QPushButton;
class QSlider;

namespace rov
{

class MotorDebugPage final : public QWidget
{
    Q_OBJECT

  public:
    explicit MotorDebugPage(QWidget *parent = nullptr);

    void setSnapshot(const MotorDebugSnapshot &snapshot);

  signals:
    void parameterWriteRequested(const MotorParameterRequest &request);
    void captureRequested(const MotorCaptureRequest &request);
    void speedControlRequested(const MotorSpeedControlRequest &request);

  private:
    void addCurveWindow(int seriesIndex = 0);
    void removeCurveWindow(QWidget *window);
    void relayoutCurveWindows();
    void refreshCurveWindows();
    void autoFitAllCurves();
    void refreshView();
    void logRequest(const QString &message);

    MotorDebugSnapshot m_snapshot;
    QVector<DebugSeries> m_availableSeries;
    QVector<QWidget *> m_curveWindows;
    QGridLayout *m_curveGrid = nullptr;
    QComboBox *m_motorSelect = nullptr;
    QLabel *m_stateValue = nullptr;
    QLabel *m_rpmValue = nullptr;
    QLabel *m_currentValue = nullptr;
    QLabel *m_voltageValue = nullptr;
    QLabel *m_temperatureValue = nullptr;
    QLabel *m_faultValue = nullptr;
    QDoubleSpinBox *m_kp = nullptr;
    QDoubleSpinBox *m_ki = nullptr;
    QDoubleSpinBox *m_observerGain = nullptr;
    QDoubleSpinBox *m_currentLimit = nullptr;
    QSlider *m_speedSlider = nullptr;
    QLabel *m_speedValue = nullptr;
    QPushButton *m_runButton = nullptr;
    QLabel *m_requestLog = nullptr;
    bool m_running = false;
};

} // namespace rov
