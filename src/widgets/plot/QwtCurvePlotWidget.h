#pragma once

#include "contracts/motor_debug/MotorDebugContract.h"

#include <QPoint>
#include <QSize>
#include <QVector>
#include <QWidget>

class QwtPlotCurve;
class QwtPlotMagnifier;
class QwtPlotZoomer;

namespace rov
{

/**
 * @brief Qt Widgets adapter for a bounded group of motor debug curves.
 *
 * The widget owns only Qwt display objects. Samples arrive as a value-type
 * snapshot, so transport and protocol code never depends on Qwt or QWidget.
 */
class QwtCurvePlotWidget final : public QWidget
{
    Q_OBJECT

  public:
    explicit QwtCurvePlotWidget(QWidget *parent = nullptr);

    void setSeries(const QVector<DebugSeries> &series, bool autoFit);
    void fitToData();
    void setDisplayWindowSeconds(double seconds);
    void setFollowLatest(bool follow);

  signals:
    // Manual zooming and axis scaling pause the moving viewport so the next
    // snapshot cannot immediately overwrite the user's view.
    void followLatestChanged(bool follow);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    class Plot;
    Plot *m_plot = nullptr;
    QwtPlotZoomer *m_zoomer = nullptr;
    QwtPlotMagnifier *m_magnifier = nullptr;
    QwtPlotMagnifier *m_xMagnifier = nullptr;
    QwtPlotMagnifier *m_yMagnifier = nullptr;
    QVector<QwtPlotCurve *> m_curves;
    double m_displayWindowSeconds = 10.0;
    bool m_followLatest = true;

    void applyDisplayWindow();
    void pauseFollowForManualView();
};

} // namespace rov
