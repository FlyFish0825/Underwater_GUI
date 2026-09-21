#pragma once

#include "contracts/motor_debug/MotorDebugContract.h"

#include <QVector>
#include <QWidget>

class QwtPlotCurve;
class QwtPlotPanner;
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

  private:
    class Plot;
    Plot *m_plot = nullptr;
    QwtPlotZoomer *m_zoomer = nullptr;
    QwtPlotPanner *m_panner = nullptr;
    QVector<QwtPlotCurve *> m_curves;
};

} // namespace rov
