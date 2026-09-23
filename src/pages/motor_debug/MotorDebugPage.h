#pragma once

#include "contracts/motor_debug/MotorDebugContract.h"

#include <QHash>
#include <QPoint>
#include <QStringList>
#include <QWidget>

class QLabel;
class QComboBox;
class QCheckBox;
class QDialog;
class QDoubleSpinBox;
class QGridLayout;
class QPushButton;
class QSlider;
class QSpinBox;
class QSplitter;
class QTimer;
class QVBoxLayout;

namespace rov
{

class CardWidget;

class MotorDebugPage final : public QWidget
{
    Q_OBJECT

  public:
    explicit MotorDebugPage(QWidget *parent = nullptr);
    ~MotorDebugPage() override;

    void setSnapshot(const MotorDebugSnapshot &snapshot);
    quint8 selectedNodeId() const;

  signals:
    void parameterWriteRequested(const MotorParameterRequest &request);
    void captureRequested(const MotorCaptureRequest &request);
    void speedControlRequested(const MotorSpeedControlRequest &request);
    void historyLimitChanged(int limit);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void addCurveWindow(int seriesIndex = 0);
    void addPresetWindow(const QStringList &seriesIds);
    void removeCurveWindow(QWidget *window);
    void relayoutCurveWindows();
    void updateCurveAreaLayout();
    void refreshCurveWindows();
    void autoFitAllCurves();
    void setHistoryLimit(int limit);
    void detachCurveWindow(QWidget *window, const QPoint &globalPos, const QPoint &dragOffset);
    void restoreCurveWindow(QWidget *window);
    void sendSpeedControl(bool runCommand, bool enabled);
    void refreshView();

    MotorDebugSnapshot m_snapshot;
    QVector<DebugSeries> m_availableSeries;
    QVector<QWidget *> m_curveWindows;
    QGridLayout *m_curveGrid = nullptr;
    CardWidget *m_curveCard = nullptr;
    QWidget *m_curveCardHost = nullptr;
    QSplitter *m_curveSplitter = nullptr;
    QHash<QWidget *, QDialog *> m_curveDialogs;
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
    QSpinBox *m_speedInput = nullptr;
    QPushButton *m_runButton = nullptr;
    QTimer *m_speedDispatchTimer = nullptr;
    QComboBox *m_historyLimit = nullptr;
    QPoint m_curveDragStartGlobal;
    QPoint m_curveDragOffset;
    QWidget *m_curveDragWindow = nullptr;
    bool m_curveDragActive = false;
    bool m_curveSystemMoveActive = false;
    bool m_curveAreaCollapsed = false;
    int m_curveExpandedHeight = 500;
    bool m_running = false;
};

} // namespace rov
