#pragma once

#include "contracts/motor_debug/MotorDebugContract.h"

#include <QWidget>

class QLabel;
class QDoubleSpinBox;

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

  private:
    void refreshView();
    void logRequest(const QString &message);

    MotorDebugSnapshot m_snapshot;
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
    QLabel *m_requestLog = nullptr;
};

} // namespace rov
