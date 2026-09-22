#include "pages/motor_debug/MotorDebugPage.h"

#include "ui/common/AppComboBox.h"
#include "ui/common/UiPrimitives.h"
#include "widgets/plot/QwtCurvePlotWidget.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <utility>

namespace
{

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
        m_title =
            rov::makeLabel(QStringLiteral("曲线窗口 %1").arg(number), QStringLiteral("bodyValue"));
        toolbar->addWidget(m_title);
        m_variable = new QComboBox;
        m_variable->setMinimumWidth(150);
        toolbar->addWidget(m_variable, 1);
        m_clear = rov::makeButton(QStringLiteral("清空变量"), QStringLiteral("softButton"));
        toolbar->addWidget(m_clear);
        m_fit = rov::makeButton(QStringLiteral("自动适配"), QStringLiteral("softButton"));
        toolbar->addWidget(m_fit);
        m_remove = rov::makeButton(QStringLiteral("删除"), QStringLiteral("softButton"));
        toolbar->addWidget(m_remove);
        root->addLayout(toolbar);

        m_plot = new rov::QwtCurvePlotWidget;
        root->addWidget(m_plot, 1);
        connect(m_fit, &QPushButton::clicked, m_plot, &rov::QwtCurvePlotWidget::fitToData);
        connect(m_clear, &QPushButton::clicked, this,
                [this]()
                {
                    m_selectedIndexes.clear();
                    refreshPlot(true);
                });
    }

    QComboBox *variableCombo() const
    {
        return m_variable;
    }
    QPushButton *removeButton() const
    {
        return m_remove;
    }
    int selectedIndex() const
    {
        return m_selectedIndex;
    }

    void setSeriesCatalog(const QVector<rov::DebugSeries> &series,
                          const QVector<int> &preferredIndexes)
    {
        m_catalog = series;
        m_variable->blockSignals(true);
        m_variable->clear();
        for (const auto &item : m_catalog)
            m_variable->addItem(QStringLiteral("%1  [%2]").arg(item.name, item.unit));
        m_selectedIndexes.clear();
        for (const int index : preferredIndexes)
        {
            if (index >= 0 && index < m_catalog.size() && !m_selectedIndexes.contains(index))
                m_selectedIndexes.append(index);
        }
        if (m_selectedIndexes.isEmpty() && !m_catalog.isEmpty())
            m_selectedIndexes.append(0);
        if (!m_selectedIndexes.isEmpty())
            m_selectedIndex = m_selectedIndexes.first();
        else
            m_selectedIndex = -1;
        if (m_selectedIndex >= 0)
            m_variable->setCurrentIndex(m_selectedIndex);
        m_variable->blockSignals(false);
        refreshPlot(true);
    }

    void selectSeries(const int index)
    {
        if (index < 0 || index >= m_catalog.size())
            return;
        m_selectedIndex = index;
        if (!m_selectedIndexes.contains(index))
            m_selectedIndexes.append(index);
        refreshPlot(false);
    }

    void updateSeries(const QVector<rov::DebugSeries> &series)
    {
        m_catalog = series;
        refreshPlot(false);
    }

    void fitToData()
    {
        m_plot->fitToData();
    }

  private:
    void refreshPlot(const bool autoFit)
    {
        QVector<rov::DebugSeries> selected;
        for (const int index : std::as_const(m_selectedIndexes))
        {
            if (index >= 0 && index < m_catalog.size())
                selected.append(m_catalog.at(index));
        }
        m_plot->setSeries(selected, autoFit);
    }

    QLabel *m_title = nullptr;
    QComboBox *m_variable = nullptr;
    QPushButton *m_clear = nullptr;
    QPushButton *m_fit = nullptr;
    QPushButton *m_remove = nullptr;
    rov::QwtCurvePlotWidget *m_plot = nullptr;
    QVector<rov::DebugSeries> m_catalog;
    QVector<int> m_selectedIndexes;
    int m_selectedIndex = -1;
};

QVector<int> indexesForIds(const QVector<rov::DebugSeries> &series, const QStringList &ids)
{
    QVector<int> indexes;
    for (const QString &id : ids)
    {
        bool found = false;
        for (int index = 0; index < series.size(); ++index)
        {
            if (!found && series.at(index).id == id)
            {
                indexes.append(index);
                found = true;
            }
        }
    }
    return indexes;
}

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
                                   QStringLiteral("节点反馈")));

    auto *curveCard = new CardWidget(QStringLiteral("实时曲线工作区"), IconKind::Waveform);
    curveCard->contentLayout()->setContentsMargins(10, 8, 10, 10);
    curveCard->contentLayout()->setSpacing(7);
    auto *curveToolbar = new QHBoxLayout;
    curveToolbar->setSpacing(7);
    curveToolbar->addWidget(
        makeLabel(QStringLiteral("每个窗口可叠加变量并独立缩放"), QStringLiteral("mutedLabel")));
    curveToolbar->addStretch();
    auto *fitAll = makeButton(QStringLiteral("全部自动适配"), QStringLiteral("softButton"));
    curveToolbar->addWidget(fitAll);
    auto *addIabc = makeButton(QStringLiteral("＋ IABC 组"), QStringLiteral("softButton"));
    curveToolbar->addWidget(addIabc);
    auto *addSpeed = makeButton(QStringLiteral("＋ 速度组"), QStringLiteral("softButton"));
    curveToolbar->addWidget(addSpeed);
    auto *addCurve = makeButton(QStringLiteral("＋ 自定义窗口"), QStringLiteral("primaryButton"));
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
    m_motorSelect->addItems(
        {QStringLiteral("Node1 · 推进器 1（FL）"), QStringLiteral("Node2 · 推进器 2（FR）"),
         QStringLiteral("Node3 · 推进器 3（ML）"), QStringLiteral("Node4 · 推进器 4（MR）"),
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
                                        QStringLiteral("观测器增益"),
                                        QStringLiteral("电流限值（A）")};
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
    sampleRate->addItems(
        {QStringLiteral("1 kHz"), QStringLiteral("5 kHz"), QStringLiteral("10 kHz")});
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
    connect(addIabc, &QPushButton::clicked, this,
            [this]()
            {
                addPresetWindow({QStringLiteral("phase_current_u"),
                                 QStringLiteral("phase_current_v"),
                                 QStringLiteral("phase_current_w")});
            });
    connect(
        addSpeed, &QPushButton::clicked, this,
        [this]() {
            addPresetWindow({QStringLiteral("speed_rpm"), QStringLiteral("pll_electrical_speed")});
        });
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
            [this](const int value)
            { m_speedValue->setText(QStringLiteral("%1 rpm").arg(value)); });
    connect(applySpeed, &QPushButton::clicked, this,
            [this]()
            {
                MotorSpeedControlRequest request;
                request.motorId = m_snapshot.selectedMotorId;
                request.nodeId = static_cast<quint8>(m_motorSelect->currentIndex() + 1);
                request.targetRpm = m_speedSlider->value();
                request.enabled = m_running;
                request.runCommand = false;
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
                request.nodeId = static_cast<quint8>(m_motorSelect->currentIndex() + 1);
                request.targetRpm = m_speedSlider->value();
                request.enabled = m_running;
                // 启动同时下发当前目标转速；停止使用 RUN_VECTOR，确保固件执行停机。
                request.runCommand = !m_running;
                emit speedControlRequested(request);
                logRequest(m_running ? QStringLiteral("请求启动电机")
                                     : QStringLiteral("请求停止电机"));
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

    MotorDebugSnapshot initialSnapshot;
    initialSnapshot.motorStamp.freshness = DataFreshness::Offline;
    initialSnapshot.state = QStringLiteral("离线");
    initialSnapshot.fault = QStringLiteral("离线");
    setSnapshot(initialSnapshot);
    addCurveWindow(0);
}

void MotorDebugPage::setSnapshot(const MotorDebugSnapshot &snapshot)
{
    m_snapshot = snapshot;
    m_availableSeries = snapshot.series;
    refreshView();
    refreshCurveWindows();
}

quint8 MotorDebugPage::selectedNodeId() const
{
    if (m_motorSelect == nullptr)
        return 1;
    return static_cast<quint8>(qBound(0, m_motorSelect->currentIndex(), 7) + 1);
}

void MotorDebugPage::addCurveWindow(const int seriesIndex)
{
    auto *curve = new CurveWindowWidget(m_curveWindows.size() + 1);
    QVector<int> indexes;
    if (seriesIndex == 0)
        indexes = indexesForIds(m_availableSeries, {QStringLiteral("phase_current_u"),
                                                    QStringLiteral("phase_current_v"),
                                                    QStringLiteral("phase_current_w")});
    if (indexes.isEmpty() && seriesIndex >= 0 && seriesIndex < m_availableSeries.size())
        indexes.append(seriesIndex);
    curve->setSeriesCatalog(m_availableSeries, indexes);
    m_curveWindows.append(curve);
    connect(curve->variableCombo(), qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, curve](const int index) { curve->selectSeries(index); });
    connect(curve->removeButton(), &QPushButton::clicked, this,
            [this, curve]() { removeCurveWindow(curve); });
    relayoutCurveWindows();
}

void MotorDebugPage::addPresetWindow(const QStringList &seriesIds)
{
    auto *curve = new CurveWindowWidget(m_curveWindows.size() + 1);
    curve->setSeriesCatalog(m_availableSeries, indexesForIds(m_availableSeries, seriesIds));
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
    const bool available = m_snapshot.motorStamp.validity == DataValidity::Valid &&
                           m_snapshot.motorStamp.freshness != DataFreshness::Offline;
    m_stateValue->setText(available && !m_snapshot.state.isEmpty() ? m_snapshot.state
                                                                   : QStringLiteral("离线"));
    m_rpmValue->setText(available ? QStringLiteral("%1 rpm").arg(m_snapshot.rpm, 0, 'f', 0)
                                  : QStringLiteral("--"));
    m_currentValue->setText(available ? QStringLiteral("%1 A").arg(m_snapshot.currentA, 0, 'f', 4)
                                      : QStringLiteral("--"));
    m_voltageValue->setText(available ? QStringLiteral("%1 V").arg(m_snapshot.voltageV, 0, 'f', 1)
                                      : QStringLiteral("--"));
    m_temperatureValue->setText(
        available ? QStringLiteral("%1 °C").arg(m_snapshot.temperatureC, 0, 'f', 3)
                  : QStringLiteral("--"));
    m_faultValue->setText(available ? m_snapshot.fault : QStringLiteral("离线"));
    if (!m_snapshot.recentLog.isEmpty() && m_requestLog != nullptr)
        m_requestLog->setText(m_snapshot.recentLog.join(QStringLiteral("\n")));
}

void MotorDebugPage::logRequest(const QString &message)
{
    if (m_requestLog != nullptr)
        m_requestLog->setText(QStringLiteral("最近请求：%1 · 设备离线时不下发").arg(message));
}

} // namespace rov
