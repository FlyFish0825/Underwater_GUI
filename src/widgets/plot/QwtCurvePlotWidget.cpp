#include "widgets/plot/QwtCurvePlotWidget.h"

#include <qwt_plot.h>
#include <qwt_plot_canvas.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_zoomer.h>
#include <qwt_legend.h>

#include <QColor>
#include <QPen>
#include <QVBoxLayout>

namespace rov
{

class QwtCurvePlotWidget::Plot final : public QwtPlot
{
  public:
    explicit Plot(QWidget *parent = nullptr) : QwtPlot(parent)
    {
        setCanvas(new QwtPlotCanvas);
        canvas()->setStyleSheet(QStringLiteral("background: #ffffff;"));
        setAxisTitle(QwtPlot::xBottom, QStringLiteral("时间 (s)"));
        setAxisTitle(QwtPlot::yLeft, QStringLiteral("值"));
        setAutoReplot(false);
        insertLegend(new QwtLegend, QwtPlot::BottomLegend);
    }
};

QwtCurvePlotWidget::QwtCurvePlotWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(420, 250);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_plot = new Plot(this);
    layout->addWidget(m_plot);

    m_zoomer = new QwtPlotZoomer(m_plot->canvas());
    m_zoomer->setRubberBand(QwtPicker::RectRubberBand);
    m_zoomer->setTrackerMode(QwtPicker::AlwaysOn);
    m_panner = new QwtPlotPanner(m_plot->canvas());
    m_panner->setMouseButton(Qt::MiddleButton);
}

void QwtCurvePlotWidget::setSeries(const QVector<DebugSeries> &series, const bool autoFit)
{
    qDeleteAll(m_curves);
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

    m_plot->legend()->setVisible(series.size() > 1);
    if (autoFit || !series.isEmpty())
        fitToData();
    else
    {
        m_plot->setAxisScale(QwtPlot::xBottom, 0.0, 1.0);
        m_plot->setAxisScale(QwtPlot::yLeft, -1.0, 1.0);
    }
    m_plot->replot();
}

void QwtCurvePlotWidget::fitToData()
{
    m_plot->setAxisAutoScale(QwtPlot::xBottom);
    m_plot->setAxisAutoScale(QwtPlot::yLeft);
    m_plot->replot();
}

} // namespace rov
