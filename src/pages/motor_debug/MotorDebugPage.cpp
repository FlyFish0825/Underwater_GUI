#include "pages/motor_debug/MotorDebugPage.h"

#include "preview/PreviewData.h"
#include "ui/common/AppComboBox.h"
#include "ui/common/UiPrimitives.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

namespace
{

QString axisNumber(const double value)
{
    const int decimals = std::abs(value) >= 100.0 ? 0 : 2;
    return QString::number(value, 'f', decimals);
}

class CurvePlotWidget final : public QWidget
{
  public:
    explicit CurvePlotWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(420, 250);
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setToolTip(QStringLiteral("在下方 X 轴或左侧 Y 轴滚轮缩放；点击自动适配恢复比例"));
    }

    void setSeries(const rov::DebugSeries &series, const bool autoFit)
    {
        m_series = series;
        if (autoFit || !m_hasView)
            fitToData();
        update();
    }

    void fitToData()
    {
        if (m_series.samples.isEmpty())
        {
            m_yCenter = 0.0;
            m_yHalfRange = 1.0;
            m_hasView = true;
            update();
            return;
        }

        double minimum = m_series.samples.first();
        double maximum = minimum;
        for (const double sample : m_series.samples)
        {
            minimum = qMin(minimum, sample);
            maximum = qMax(maximum, sample);
        }
        const double span = qMax(1.0, maximum - minimum);
        m_yCenter = (maximum + minimum) / 2.0;
        m_yHalfRange = qMax(0.5, span * 0.60);
        m_xWindowSeconds = kDefaultWindowSeconds;
        m_hasView = true;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(QStringLiteral("#fbfdff")));

        const QRectF plot = plotRect();
        painter.fillRect(plot, Qt::white);
        painter.setPen(QPen(QColor(QStringLiteral("#e2eaf2")), 1));
        for (int index = 0; index <= 6; ++index)
        {
            const qreal y = plot.top() + plot.height() * index / 6.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        for (int index = 0; index <= 10; ++index)
        {
            const qreal x = plot.left() + plot.width() * index / 10.0;
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }

        painter.setPen(QPen(QColor(QStringLiteral("#768ba1")), 1.2));
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
        painter.drawLine(plot.topLeft(), plot.bottomLeft());
        painter.setPen(QColor(QStringLiteral("#5d7186")));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 8));
        painter.drawText(QRectF(4, 2, width() - 8, 16), Qt::AlignLeft,
                         QStringLiteral("%1  [%2]").arg(m_series.name, m_series.unit));

        for (int index = 0; index <= 6; ++index)
        {
            const double value = m_yCenter + m_yHalfRange
                                 - (2.0 * m_yHalfRange * index / 6.0);
            const qreal y = plot.top() + plot.height() * index / 6.0;
            painter.drawText(QRectF(0, y - 8, plot.left() - 7, 16), Qt::AlignRight,
                             axisNumber(value));
        }
        painter.drawText(QRectF(plot.left() - 5, plot.bottom() + 7, 38, 16), Qt::AlignLeft,
                         QStringLiteral("0"));
        painter.drawText(QRectF(plot.center().x() - 20, plot.bottom() + 7, 48, 16),
                         Qt::AlignCenter, axisNumber(m_xWindowSeconds / 2.0));
        painter.drawText(QRectF(plot.right() - 36, plot.bottom() + 7, 40, 16), Qt::AlignRight,
                         QStringLiteral("%1 s").arg(axisNumber(m_xWindowSeconds)));

        if (!m_series.samples.isEmpty())
        {
            const int visibleCount = qBound(
                2, qRound(static_cast<double>(m_series.samples.size())
                          * m_xWindowSeconds / kDataWindowSeconds),
                m_series.samples.size());
            const int firstSample = m_series.samples.size() - visibleCount;
            QPainterPath path;
            for (int index = 0; index < visibleCount; ++index)
            {
                const double value = m_series.samples.at(firstSample + index);
                const qreal x = plot.left() + plot.width() * index / qMax(1, visibleCount - 1);
                const qreal y = plot.center().y()
                                - (value - m_yCenter) / (2.0 * m_yHalfRange) * plot.height();
                if (index == 0)
                    path.moveTo(x, y);
                else
                    path.lineTo(x, y);
            }
            painter.setPen(QPen(QColor(QStringLiteral("#1687ee")), 2.0));
            painter.drawPath(path);
        }

        if (plot.contains(m_cursorPosition))
        {
            painter.setPen(QPen(QColor(QStringLiteral("#9eb3c7")), 1, Qt::DashLine));
            painter.drawLine(QPointF(m_cursorPosition.x(), plot.top()),
                             QPointF(m_cursorPosition.x(), plot.bottom()));
            painter.drawLine(QPointF(plot.left(), m_cursorPosition.y()),
                             QPointF(plot.right(), m_cursorPosition.y()));

            const double value = m_yCenter
                                 + (plot.center().y() - m_cursorPosition.y()) / plot.height()
                                       * 2.0 * m_yHalfRange;
            const double seconds = (m_cursorPosition.x() - plot.left()) / plot.width()
                                   * m_xWindowSeconds;
            painter.setPen(QColor(QStringLiteral("#34536f")));
            painter.drawText(QRectF(plot.left() + 8, plot.top() + 6, plot.width() - 16, 18),
                             Qt::AlignLeft,
                             QStringLiteral("t=%1 s   y=%2 %3")
                                 .arg(axisNumber(seconds), axisNumber(value), m_series.unit));
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        m_cursorPosition = event->pos();
        update();
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_cursorPosition = QPoint(-1, -1);
        update();
        QWidget::leaveEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        const QRectF plot = plotRect();
        const QPoint position = event->position().toPoint();
        const int steps = event->angleDelta().y() > 0 ? 1 : -1;
        const double factor = steps > 0 ? 0.85 : 1.0 / 0.85;
        if (position.x() < plot.left() && position.y() >= plot.top()
            && position.y() <= plot.bottom())
        {
            const double cursorValue = m_yCenter
                                        + (plot.center().y() - position.y()) / plot.height()
                                              * 2.0 * m_yHalfRange;
            m_yCenter = cursorValue + (m_yCenter - cursorValue) * factor;
            m_yHalfRange = qBound(0.001, m_yHalfRange * factor, 1000000.0);
            m_hasView = true;
            update();
            event->accept();
            return;
        }
        if (position.y() > plot.bottom() && position.x() >= plot.left()
            && position.x() <= plot.right())
        {
            m_xWindowSeconds = qBound(0.2, m_xWindowSeconds * factor, 60.0);
            m_hasView = true;
            update();
            event->accept();
            return;
        }
        event->ignore();
    }

  private:
    QRectF plotRect() const
    {
        const qreal plotWidth = qMax<qreal>(80.0, width() - 76.0);
        const qreal plotHeight = qMax<qreal>(80.0, height() - 55.0);
        return QRectF(58.0, 18.0, plotWidth, plotHeight);
    }

    static constexpr double kDataWindowSeconds = 10.0;
    static constexpr double kDefaultWindowSeconds = 10.0;
    rov::DebugSeries m_series;
    QPoint m_cursorPosition{-1, -1};
    double m_xWindowSeconds = kDefaultWindowSeconds;
    double m_yCenter = 0.0;
    double m_yHalfRange = 1.0;
    bool m_hasView = false;
};

class CurveWindowWidget final : public QFrame
{
  public:
    explicit CurveWindowWidget(const int number, QWidget *parent = nullptr) : QFrame(parent)
    {
        setObjectName(QStringLiteral("card"));
        setMinimumHeight(278);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(10, 8, 10, 8);
        root->setSpacing(6);

        auto *toolbar = new QHBoxLayout;
        toolbar->setSpacing(6);
        m_title = rov::makeLabel(QStringLiteral("曲线窗口 %1").arg(number),
                                 QStringLiteral("bodyValue"));
        toolbar->addWidget(m_title);
        m_variable = new QComboBox;
        m_variable->setMinimumWidth(150);
        toolbar->addWidget(m_variable, 1);
        m_fit = rov::makeButton(QStringLiteral("自动适配"), QStringLiteral("softButton"));
        toolbar->addWidget(m_fit);
        m_remove = rov::makeButton(QStringLiteral("删除"), QStringLiteral("softButton"));
        toolbar->addWidget(m_remove);
        root->addLayout(toolbar);

        m_plot = new CurvePlotWidget;
        root->addWidget(m_plot, 1);
        connect(m_fit, &QPushButton::clicked, m_plot, &CurvePlotWidget::fitToData);
    }

    QComboBox *variableCombo() const { return m_variable; }
    QPushButton *removeButton() const { return m_remove; }
    int selectedIndex() const { return m_selectedIndex; }

    void setSeriesCatalog(const QVector<rov::DebugSeries> &series, const int preferredIndex)
    {
        const int oldIndex = m_selectedIndex;
        m_catalog = series;
        const int selected = m_catalog.isEmpty()
                                 ? -1
                                 : qBound(0, preferredIndex, m_catalog.size() - 1);
        m_variable->blockSignals(true);
        m_variable->clear();
        for (const auto &item : m_catalog)
            m_variable->addItem(QStringLiteral("%1  [%2]").arg(item.name, item.unit));
        if (selected >= 0)
            m_variable->setCurrentIndex(selected);
        m_variable->blockSignals(false);
        m_selectedIndex = selected;
        if (selected >= 0)
            m_plot->setSeries(m_catalog.at(selected), oldIndex != selected || !m_hasCatalog);
        m_hasCatalog = true;
    }

    void selectSeries(const int index)
    {
        if (index < 0 || index >= m_catalog.size())
            return;
        m_selectedIndex = index;
        m_plot->setSeries(m_catalog.at(index), true);
    }

    void updateSeries(const QVector<rov::DebugSeries> &series)
    {
        m_catalog = series;
        if (m_selectedIndex >= 0 && m_selectedIndex < m_catalog.size())
            m_plot->setSeries(m_catalog.at(m_selectedIndex), false);
    }

    void fitToData() { m_plot->fitToData(); }

  private:
    QLabel *m_title = nullptr;
    QComboBox *m_variable = nullptr;
    QPushButton *m_fit = nullptr;
    QPushButton *m_remove = nullptr;
    CurvePlotWidget *m_plot = nullptr;
    QVector<rov::DebugSeries> m_catalog;
    int m_selectedIndex = -1;
    bool m_hasCatalog = false;
};

QDoubleSpinBox *parameterBox(const double value)
{
    auto *box = new QDoubleSpinBox;
    box->setRange(0.0, 100.0);
    box->setDecimals(2);
    box->setSingleStep(0.01);
    box->setValue(value);
    return box;
}

} // namespace

namespace rov
{

MotorDebugPage::MotorDebugPage(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(8);
    root->addWidget(makePageHeader(QStringLiteral("电机调试"),
                                   QStringLiteral("多窗口曲线观察、转速控制与参数调节。"),
                                   QStringLiteral("演示 · 预览数据")));

    auto *curveCard = new CardWidget(QStringLiteral("实时曲线工作区"), IconKind::Waveform);
    curveCard->contentLayout()->setContentsMargins(10, 8, 10, 10);
    curveCard->contentLayout()->setSpacing(7);
    auto *curveToolbar = new QHBoxLayout;
    curveToolbar->setSpacing(7);
    curveToolbar->addWidget(makeLabel(QStringLiteral("每个窗口独立选择变量和缩放比例"),
                                      QStringLiteral("mutedLabel")));
    curveToolbar->addStretch();
    auto *fitAll = makeButton(QStringLiteral("全部自动适配"), QStringLiteral("softButton"));
    curveToolbar->addWidget(fitAll);
    auto *addCurve = makeButton(QStringLiteral("＋ 新增曲线"), QStringLiteral("primaryButton"));
    curveToolbar->addWidget(addCurve);
    curveCard->contentLayout()->addLayout(curveToolbar);
    m_curveGrid = new QGridLayout;
    m_curveGrid->setContentsMargins(0, 0, 0, 0);
    m_curveGrid->setHorizontalSpacing(8);
    m_curveGrid->setVerticalSpacing(8);
    m_curveGrid->setColumnStretch(0, 1);
    m_curveGrid->setColumnStretch(1, 1);
    curveCard->contentLayout()->addLayout(m_curveGrid);
    root->addWidget(curveCard, 1);

    auto *lowerRow = new QHBoxLayout;
    lowerRow->setSpacing(8);

    auto *statusCard = new CardWidget(QStringLiteral("电机状态与参数"), IconKind::Motor);
    statusCard->contentLayout()->setContentsMargins(8, 6, 8, 8);
    statusCard->contentLayout()->setSpacing(4);
    auto *statusLayout = new QGridLayout;
    statusLayout->setHorizontalSpacing(6);
    statusLayout->setVerticalSpacing(3);
    statusLayout->setColumnStretch(1, 1);
    statusLayout->setColumnStretch(3, 1);
    statusLayout->addWidget(makeLabel(QStringLiteral("电机"), QStringLiteral("mutedLabel")), 0, 0);
    m_motorSelect = new AppComboBox;
    m_motorSelect->addItems({QStringLiteral("Node1 · 推进器 1（FL）"),
                             QStringLiteral("Node2 · 推进器 2（FR）"),
                             QStringLiteral("Node3 · 推进器 3（ML）"),
                             QStringLiteral("Node4 · 推进器 4（MR）"),
                             QStringLiteral("Node5 · 推进器 5"), QStringLiteral("Node6 · 推进器 6"),
                             QStringLiteral("Node7 · 推进器 7"), QStringLiteral("Node8 · 推进器 8")});
    statusLayout->addWidget(m_motorSelect, 0, 1, 1, 3);
    m_stateValue = makeStatusPill(QStringLiteral("运行中"), QStringLiteral("statusGood"));
    m_rpmValue = makeMetricValue(QStringLiteral("--"));
    m_currentValue = makeMetricValue(QStringLiteral("--"));
    m_voltageValue = makeMetricValue(QStringLiteral("--"));
    m_temperatureValue = makeMetricValue(QStringLiteral("--"));
    m_faultValue = makeStatusPill(QStringLiteral("--"), QStringLiteral("statusIdle"));
    statusLayout->addWidget(makeMetricLabel(QStringLiteral("状态")), 1, 0);
    statusLayout->addWidget(m_stateValue, 1, 1);
    statusLayout->addWidget(makeMetricLabel(QStringLiteral("转速")), 2, 0);
    statusLayout->addWidget(m_rpmValue, 2, 1);
    statusLayout->addWidget(makeMetricLabel(QStringLiteral("电流")), 3, 0);
    statusLayout->addWidget(m_currentValue, 3, 1);
    statusLayout->addWidget(makeMetricLabel(QStringLiteral("电压")), 4, 0);
    statusLayout->addWidget(m_voltageValue, 4, 1);
    statusLayout->addWidget(makeMetricLabel(QStringLiteral("温度")), 5, 0);
    statusLayout->addWidget(m_temperatureValue, 5, 1);
    statusLayout->addWidget(makeMetricLabel(QStringLiteral("故障")), 6, 0);
    statusLayout->addWidget(m_faultValue, 6, 1);
    m_kp = parameterBox(0.20);
    m_ki = parameterBox(0.05);
    m_observerGain = parameterBox(0.10);
    m_currentLimit = parameterBox(20.0);
    const QStringList parameterNames = {QStringLiteral("电流 Kp"), QStringLiteral("电流 Ki"),
                                        QStringLiteral("观测器增益"), QStringLiteral("电流限值（A）")};
    QDoubleSpinBox *boxes[] = {m_kp, m_ki, m_observerGain, m_currentLimit};
    for (int index = 0; index < 4; ++index)
    {
        statusLayout->addWidget(makeMetricLabel(parameterNames.at(index)), index + 1, 2);
        statusLayout->addWidget(boxes[index], index + 1, 3);
    }
    auto *apply = makeButton(QStringLiteral("应用参数"), QStringLiteral("primaryButton"));
    statusLayout->addWidget(apply, 5, 2, 1, 2);
    auto *reset = makeButton(QStringLiteral("恢复默认"), QStringLiteral("softButton"));
    statusLayout->addWidget(reset, 6, 2, 1, 2);
    statusCard->contentLayout()->addLayout(statusLayout);
    lowerRow->addWidget(statusCard, 5);

    auto *rightColumn = new QVBoxLayout;
    rightColumn->setSpacing(8);
    auto *speedCard = new CardWidget(QStringLiteral("基础转速控制"), IconKind::Settings);
    speedCard->contentLayout()->setContentsMargins(10, 8, 10, 8);
    speedCard->contentLayout()->setSpacing(6);
    auto *speedHeader = new QHBoxLayout;
    speedHeader->addWidget(makeLabel(QStringLiteral("目标转速"), QStringLiteral("mutedLabel")));
    m_speedValue = makeMetricValue(QStringLiteral("0 rpm"));
    speedHeader->addWidget(m_speedValue);
    speedHeader->addStretch();
    speedCard->contentLayout()->addLayout(speedHeader);
    m_speedSlider = new QSlider(Qt::Horizontal);
    m_speedSlider->setRange(-10000, 10000);
    m_speedSlider->setSingleStep(100);
    m_speedSlider->setPageStep(1000);
    m_speedSlider->setValue(0);
    speedCard->contentLayout()->addWidget(m_speedSlider);
    auto *speedScale = new QHBoxLayout;
    speedScale->addWidget(makeLabel(QStringLiteral("-10000"), QStringLiteral("mutedLabel")));
    speedScale->addStretch();
    speedScale->addWidget(makeLabel(QStringLiteral("0"), QStringLiteral("mutedLabel")));
    speedScale->addStretch();
    speedScale->addWidget(makeLabel(QStringLiteral("+10000 rpm"), QStringLiteral("mutedLabel")));
    speedCard->contentLayout()->addLayout(speedScale);
    auto *speedButtons = new QHBoxLayout;
    auto *applySpeed = makeButton(QStringLiteral("下发速度"), QStringLiteral("primaryButton"));
    m_runButton = makeButton(QStringLiteral("启动"), QStringLiteral("softButton"));
    speedButtons->addWidget(applySpeed);
    speedButtons->addWidget(m_runButton);
    speedCard->contentLayout()->addLayout(speedButtons);
    rightColumn->addWidget(speedCard);

    auto *captureCard = new CardWidget(QStringLiteral("采集与分析"), IconKind::File);
    captureCard->contentLayout()->setContentsMargins(10, 8, 10, 8);
    auto *captureLayout = new QHBoxLayout;
    auto *sampleRate = new AppComboBox;
    sampleRate->addItems({QStringLiteral("1 kHz"), QStringLiteral("5 kHz"), QStringLiteral("10 kHz")});
    auto *duration = new AppComboBox;
    duration->addItems({QStringLiteral("10 s"), QStringLiteral("30 s"), QStringLiteral("60 s")});
    captureLayout->addWidget(makeMetricLabel(QStringLiteral("采样率")));
    captureLayout->addWidget(sampleRate);
    captureLayout->addWidget(makeMetricLabel(QStringLiteral("时长")));
    captureLayout->addWidget(duration);
    captureLayout->addStretch();
    auto *capture = makeButton(QStringLiteral("立即采集"), QStringLiteral("softButton"));
    captureLayout->addWidget(capture);
    captureCard->contentLayout()->addLayout(captureLayout);
    rightColumn->addWidget(captureCard);

    auto *logCard = new CardWidget(QStringLiteral("最近数据 / 日志"), IconKind::List);
    logCard->contentLayout()->setContentsMargins(10, 8, 10, 8);
    m_requestLog = makeLabel(QStringLiteral("暂无请求。"), QStringLiteral("mutedLabel"));
    m_requestLog->setWordWrap(true);
    logCard->contentLayout()->addWidget(m_requestLog);
    rightColumn->addWidget(logCard, 1);
    lowerRow->addLayout(rightColumn, 6);
    root->addLayout(lowerRow);

    connect(addCurve, &QPushButton::clicked, this, [this]() { addCurveWindow(); });
    connect(fitAll, &QPushButton::clicked, this, &MotorDebugPage::autoFitAllCurves);
    connect(m_motorSelect, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int index)
            {
                if (index < 0)
                    return;
                m_snapshot.selectedMotorId = QStringLiteral("thruster%1").arg(index + 1);
                m_snapshot.selectedMotorLabel = m_motorSelect->currentText();
                logRequest(QStringLiteral("已选择 %1").arg(m_motorSelect->currentText()));
            });
    connect(m_speedSlider, &QSlider::valueChanged, this,
            [this](const int value) { m_speedValue->setText(QStringLiteral("%1 rpm").arg(value)); });
    connect(applySpeed, &QPushButton::clicked, this,
            [this]()
            {
                MotorSpeedControlRequest request;
                request.motorId = m_snapshot.selectedMotorId;
                request.targetRpm = m_speedSlider->value();
                request.enabled = m_running;
                emit speedControlRequested(request);
                logRequest(QStringLiteral("下发目标转速：%1 rpm").arg(request.targetRpm));
            });
    connect(m_runButton, &QPushButton::clicked, this,
            [this]()
            {
                m_running = !m_running;
                m_runButton->setText(m_running ? QStringLiteral("停止") : QStringLiteral("启动"));
                MotorSpeedControlRequest request;
                request.motorId = m_snapshot.selectedMotorId;
                request.targetRpm = m_speedSlider->value();
                request.enabled = m_running;
                emit speedControlRequested(request);
                logRequest(m_running ? QStringLiteral("请求启动电机") : QStringLiteral("请求停止电机"));
            });
    connect(apply, &QPushButton::clicked, this,
            [this]()
            {
                MotorParameterRequest request;
                request.motorId = m_snapshot.selectedMotorId;
                request.currentKp = m_kp->value();
                request.currentKi = m_ki->value();
                request.observerGain = m_observerGain->value();
                request.currentLimitA = m_currentLimit->value();
                emit parameterWriteRequested(request);
                logRequest(QStringLiteral("请求写入参数：%1").arg(request.motorId));
            });
    connect(reset, &QPushButton::clicked, this,
            [this]()
            {
                m_kp->setValue(0.20);
                m_ki->setValue(0.05);
                m_observerGain->setValue(0.10);
                m_currentLimit->setValue(20.0);
                logRequest(QStringLiteral("已恢复默认参数"));
            });
    connect(capture, &QPushButton::clicked, this,
            [this, sampleRate, duration]()
            {
                MotorCaptureRequest request;
                request.motorId = m_snapshot.selectedMotorId;
                request.sampleRateHz = sampleRate->currentText().split(' ').first().toInt();
                request.durationSeconds = duration->currentText().split(' ').first().toInt();
                emit captureRequested(request);
                logRequest(QStringLiteral("请求采集：%1 Hz / %2 秒")
                               .arg(request.sampleRateHz)
                               .arg(request.durationSeconds));
            });

    setSnapshot(motorDebugPreview());
    addCurveWindow(0);
}

void MotorDebugPage::setSnapshot(const MotorDebugSnapshot &snapshot)
{
    m_snapshot = snapshot;
    m_availableSeries = snapshot.series;
    refreshView();
    refreshCurveWindows();
}

void MotorDebugPage::addCurveWindow(const int seriesIndex)
{
    auto *curve = new CurveWindowWidget(m_curveWindows.size() + 1);
    curve->setSeriesCatalog(m_availableSeries, seriesIndex);
    m_curveWindows.append(curve);
    connect(curve->variableCombo(), qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, curve](const int index) { curve->selectSeries(index); });
    connect(curve->removeButton(), &QPushButton::clicked, this,
            [this, curve]() { removeCurveWindow(curve); });
    relayoutCurveWindows();
}

void MotorDebugPage::removeCurveWindow(QWidget *window)
{
    if (m_curveWindows.size() <= 1)
    {
        logRequest(QStringLiteral("至少保留一个曲线窗口"));
        return;
    }
    m_curveGrid->removeWidget(window);
    m_curveWindows.removeOne(window);
    window->deleteLater();
    relayoutCurveWindows();
}

void MotorDebugPage::relayoutCurveWindows()
{
    if (m_curveWindows.size() == 1)
    {
        m_curveGrid->addWidget(m_curveWindows.first(), 0, 0, 1, 2);
        return;
    }
    for (int index = 0; index < m_curveWindows.size(); ++index)
        m_curveGrid->addWidget(m_curveWindows.at(index), index / 2, index % 2);
}

void MotorDebugPage::refreshCurveWindows()
{
    for (QWidget *widget : m_curveWindows)
        static_cast<CurveWindowWidget *>(widget)->updateSeries(m_availableSeries);
}

void MotorDebugPage::autoFitAllCurves()
{
    for (QWidget *widget : m_curveWindows)
        static_cast<CurveWindowWidget *>(widget)->fitToData();
    logRequest(QStringLiteral("已自动适配全部曲线比例"));
}

void MotorDebugPage::refreshView()
{
    if (m_stateValue == nullptr)
        return;
    m_stateValue->setText(m_snapshot.state);
    m_rpmValue->setText(QStringLiteral("%1 rpm").arg(m_snapshot.rpm, 0, 'f', 0));
    m_currentValue->setText(QStringLiteral("%1 A").arg(m_snapshot.currentA, 0, 'f', 1));
    m_voltageValue->setText(QStringLiteral("%1 V").arg(m_snapshot.voltageV, 0, 'f', 1));
    m_temperatureValue->setText(QStringLiteral("%1 °C").arg(m_snapshot.temperatureC, 0, 'f', 1));
    m_faultValue->setText(m_snapshot.fault);
    if (!m_snapshot.recentLog.isEmpty() && m_requestLog != nullptr)
        m_requestLog->setText(m_snapshot.recentLog.join(QStringLiteral("\n")));
}

void MotorDebugPage::logRequest(const QString &message)
{
    if (m_requestLog != nullptr)
        m_requestLog->setText(QStringLiteral("最近请求：%1 · 演示模式未写入设备").arg(message));
}

} // namespace rov
