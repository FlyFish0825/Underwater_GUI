#include "widgets/plot/QwtCurvePlotWidget.h"

#include <qwt_plot.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_layout.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_zoomer.h>
#include <qwt_legend.h>
#include <qwt_scale_div.h>
#include <qwt_scale_widget.h>

#include <QColor>
#include <QEvent>
#include <QPen>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>

namespace rov
{

class QwtCurvePlotWidget::Plot final : public QwtPlot
{
  public:
    explicit Plot(QWidget *parent = nullptr) : QwtPlot(parent)
    {
        setCanvas(new QwtPlotCanvas);
        canvas()->setStyleSheet(QStringLiteral("background: #ffffff;"));
        // Keep the canvas edges aligned with the bottom and left scales even
        // when the X/Y ranges and tick label widths are different.
        plotLayout()->setAlignCanvasToScales(true);
        setAxisTitle(QwtPlot::xBottom, QStringLiteral("时间 (s)"));
        setAxisTitle(QwtPlot::yLeft, QStringLiteral("值"));
        setAutoReplot(false);
        insertLegend(new QwtLegend, QwtPlot::BottomLegend);
    }
};

QwtCurvePlotWidget::QwtCurvePlotWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(420, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_plot = new Plot(this);
    m_plot->setToolTip(QStringLiteral(
        "左键拖拽框选放大；绘图区滚轮缩放双轴；悬停 X/Y 轴滚轮可单独缩放；"
        "右键返回上一级；自动适配恢复默认范围"));
    layout->addWidget(m_plot);

    if (auto *xAxis = m_plot->axisWidget(QwtPlot::xBottom))
        xAxis->installEventFilter(this);
    if (auto *yAxis = m_plot->axisWidget(QwtPlot::yLeft))
        yAxis->installEventFilter(this);

    m_zoomer = new QwtPlotZoomer(m_plot->canvas());
    m_zoomer->setRubberBand(QwtPicker::RectRubberBand);
    m_zoomer->setRubberBandPen(QPen(QColor(QStringLiteral("#1687ee")), 1.5, Qt::DashLine));
    m_zoomer->setTrackerMode(QwtPicker::ActiveOnly);
    m_zoomer->setMousePattern(QwtEventPattern::MouseSelect1, Qt::LeftButton);
    m_zoomer->setMousePattern(QwtEventPattern::MouseSelect2, Qt::NoButton);
    m_zoomer->setMousePattern(QwtEventPattern::MouseSelect3, Qt::RightButton);

    m_magnifier = new QwtPlotMagnifier(m_plot->canvas());
    m_magnifier->setWheelFactor(0.9);
    m_magnifier->setWheelModifiers(Qt::NoModifier);
    m_magnifier->setMouseButton(Qt::NoButton);

    m_xMagnifier = new QwtPlotMagnifier(m_plot->canvas());
    m_xMagnifier->setWheelFactor(0.9);
    m_xMagnifier->setWheelModifiers(Qt::ShiftModifier);
    m_xMagnifier->setAxisEnabled(QwtPlot::yLeft, false);
    m_xMagnifier->setMouseButton(Qt::NoButton);

    m_yMagnifier = new QwtPlotMagnifier(m_plot->canvas());
    m_yMagnifier->setWheelFactor(0.9);
    m_yMagnifier->setWheelModifiers(Qt::ControlModifier);
    m_yMagnifier->setAxisEnabled(QwtPlot::xBottom, false);
    m_yMagnifier->setMouseButton(Qt::NoButton);
}

bool QwtCurvePlotWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel && m_plot != nullptr)
    {
        int axis = -1;
        if (watched == static_cast<QObject *>(m_plot->axisWidget(QwtPlot::xBottom)))
            axis = QwtPlot::xBottom;
        else if (watched == static_cast<QObject *>(m_plot->axisWidget(QwtPlot::yLeft)))
            axis = QwtPlot::yLeft;

        if (axis >= 0)
        {
            auto *wheel = static_cast<QWheelEvent *>(event);
            int delta = wheel->angleDelta().y();
            if (delta == 0)
                delta = wheel->pixelDelta().y();
            if (delta == 0)
                return true;

            const QRect axisRect = static_cast<QWidget *>(watched)->rect();
            const auto scale = m_plot->axisScaleDiv(axis);
            const double lower = scale.lowerBound();
            const double upper = scale.upperBound();
            const double span = upper - lower;
            if (!qIsFinite(lower) || !qIsFinite(upper) || span <= 0.0 || axisRect.isEmpty())
                return true;

            const double steps = static_cast<double>(delta) / 120.0;
            const double factor = qPow(0.9, steps);
            const QPoint position = wheel->position().toPoint();
            const double fraction = axis == QwtPlot::xBottom
                                        ? qBound(0.0, static_cast<double>(position.x()) /
                                                           qMax(1, axisRect.width()),
                                                 1.0)
                                        : qBound(0.0, static_cast<double>(position.y()) /
                                                           qMax(1, axisRect.height()),
                                                 1.0);
            const double anchor = axis == QwtPlot::xBottom
                                      ? lower + span * fraction
                                      : upper - span * fraction;
            const double nextLower = anchor - (anchor - lower) * factor;
            const double nextUpper = anchor + (upper - anchor) * factor;
            if (qIsFinite(nextLower) && qIsFinite(nextUpper) && nextUpper > nextLower)
            {
                m_plot->setAxisScale(axis, nextLower, nextUpper);
                m_plot->replot();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void QwtCurvePlotWidget::setSeries(const QVector<DebugSeries> &series, const bool autoFit)
{
    // Remove the old plot items through Qwt so their legend entries are
    // removed together with the curves before the replacement series arrive.
    m_plot->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
    m_curves.clear();

    const QVector<QColor> colors = {QColor(QStringLiteral("#1687ee")),
                                    QColor(QStringLiteral("#e26d35")),
                                    QColor(QStringLiteral("#24a148")),
                                    QColor(QStringLiteral("#8c5bd9")),
                                    QColor(QStringLiteral("#d59b16")),
                                    QColor(QStringLiteral("#0f9fa8"))};
    for (int index = 0; index < series.size(); ++index)
    {
        const DebugSeries &item = series.at(index);
        auto *curve = new QwtPlotCurve(item.name + QStringLiteral(" [") + item.unit
                                       + QStringLiteral("]"));
        curve->setPen(QPen(colors.at(index % colors.size()), 1.8));
        curve->setRenderHint(QwtPlotItem::RenderAntialiased, false);
        curve->setPaintAttribute(QwtPlotCurve::FilterPoints, true);
        const double rate = item.sampleRateHz > 0.0 ? item.sampleRateHz : 1000.0;
        QVector<double> x;
        x.reserve(item.samples.size());
        for (int sample = 0; sample < item.samples.size(); ++sample)
            x.append(static_cast<double>(sample) / rate);
        curve->setSamples(x, item.samples);
        curve->attach(m_plot);
        m_curves.append(curve);
    }

    m_plot->updateLegend();
    m_plot->legend()->setVisible(series.size() > 1);
    if (autoFit)
        fitToData();
    else if (m_followLatest)
        applyDisplayWindow();
    else if (series.isEmpty())
    {
        m_plot->setAxisScale(QwtPlot::xBottom, 0.0, 1.0);
        m_plot->setAxisScale(QwtPlot::yLeft, -1.0, 1.0);
    }
    m_plot->replot();
}

void QwtCurvePlotWidget::fitToData()
{
    m_plot->setAxisAutoScale(QwtPlot::yLeft);
    applyDisplayWindow();
    m_plot->replot();
    m_zoomer->setZoomBase(false);
}

void QwtCurvePlotWidget::setDisplayWindowSeconds(const double seconds)
{
    m_displayWindowSeconds = qBound(1.0, seconds, 3600.0);
    applyDisplayWindow();
    m_plot->replot();
}

void QwtCurvePlotWidget::setFollowLatest(const bool follow)
{
    m_followLatest = follow;
    if (m_followLatest)
    {
        applyDisplayWindow();
        m_plot->replot();
    }
}

void QwtCurvePlotWidget::applyDisplayWindow()
{
    if (m_plot == nullptr)
        return;

    double latest = 0.0;
    const auto curves = m_plot->itemList(QwtPlotItem::Rtti_PlotCurve);
    for (QwtPlotItem *item : curves)
    {
        auto *curve = static_cast<QwtPlotCurve *>(item);
        const int sampleCount = curve->data() != nullptr ? curve->data()->size() : 0;
        if (sampleCount <= 0)
            continue;

        const double lastX = curve->data()->sample(sampleCount - 1).x();
        if (qIsFinite(lastX))
            latest = qMax(latest, lastX);
    }

    if (latest <= 0.0)
    {
        m_plot->setAxisScale(QwtPlot::xBottom, 0.0, qMax(1.0, m_displayWindowSeconds));
        return;
    }

    const double upper = latest;
    const double lower = qMax(0.0, upper - m_displayWindowSeconds);
    m_plot->setAxisScale(QwtPlot::xBottom, lower, qMax(1.0, upper));
}

} // namespace rov
