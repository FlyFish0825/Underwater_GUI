#include "pages/motor_debug/MotorDebugPage.h"

#include "ui/common/AppComboBox.h"
#include "ui/common/UiPrimitives.h"
#include "widgets/plot/QwtCurvePlotWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <utility>

namespace
{

class CurveFloatingDialog final : public QDialog
{
  public:
    explicit CurveFloatingDialog(QWidget *owner)
        : QDialog(owner, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
                             | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint)
    {
        setAttribute(Qt::WA_NativeWindow, true);
        setAttribute(Qt::WA_QuitOnClose, false);
        setWindowModality(Qt::NonModal);
        setSizeGripEnabled(true);
    }
};

class CurveWindowWidget final : public QFrame
{
  public:
    explicit CurveWindowWidget(const int number, QWidget *parent = nullptr) : QFrame(parent)
    {
        setObjectName(QStringLiteral("card"));
        setMinimumHeight(228);
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(8, 6, 8, 6);
        root->setSpacing(5);

        auto *toolbar = new QHBoxLayout;
        toolbar->setSpacing(6);
        m_title =
            rov::makeLabel(QStringLiteral("曲线窗口 %1").arg(number), QStringLiteral("bodyValue"));
        m_title->setMinimumWidth(96);
        m_title->setCursor(Qt::OpenHandCursor);
        m_title->setToolTip(QStringLiteral("拖动此处可将当前曲线窗口独立浮动"));
        toolbar->addWidget(m_title);
        toolbar->addWidget(rov::makeLabel(QStringLiteral("实时显示窗口"), QStringLiteral("mutedLabel")));
        m_displayWindow = new rov::AppComboBox;
        m_displayWindow->setMinimumWidth(76);
        m_displayWindow->addItem(QStringLiteral("5 秒"), 5.0);
        m_displayWindow->addItem(QStringLiteral("10 秒"), 10.0);
        m_displayWindow->addItem(QStringLiteral("30 秒"), 30.0);
        m_displayWindow->addItem(QStringLiteral("60 秒"), 60.0);
        m_displayWindow->addItem(QStringLiteral("120 秒"), 120.0);
        m_displayWindow->setCurrentIndex(1);
        toolbar->addWidget(m_displayWindow);
        m_followLatest = new QCheckBox(QStringLiteral("跟随最新数据"));
        m_followLatest->setChecked(true);
        m_followLatest->setToolTip(QStringLiteral("开启后曲线会持续跟随最新数据；关闭后可停留查看当前视图"));
        toolbar->addWidget(m_followLatest);
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
        connect(m_displayWindow, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](const int index)
                {
                    if (index >= 0)
                        m_plot->setDisplayWindowSeconds(
                            m_displayWindow->itemData(index).toDouble());
                });
        connect(m_followLatest, &QCheckBox::toggled, m_plot,
                &rov::QwtCurvePlotWidget::setFollowLatest);
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
    QLabel *dragLabel() const
    {
        return m_title;
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
        // A combo-box selection represents the variable shown by this
        // window. Preset groups can still create multi-series windows, but
        // choosing a variable here must replace the previous selection.
        m_selectedIndexes = {index};
        // A new variable has a different natural range; fit it once. Future
        // snapshot refreshes keep the user's manually adjusted view.
        refreshPlot(true);
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

    void setDisplayWindowSeconds(const double seconds)
    {
        m_plot->setDisplayWindowSeconds(seconds);
    }

    void setFollowLatest(const bool follow)
    {
        m_plot->setFollowLatest(follow);
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
    rov::AppComboBox *m_displayWindow = nullptr;
    QCheckBox *m_followLatest = nullptr;
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
    root->setContentsMargins(10, 6, 10, 6);
    root->setSpacing(6);
    auto *pageHeader = makePageHeader(QStringLiteral("电机调试"),
                                      QStringLiteral("多窗口曲线观察、转速控制与参数调节。"),
                                      QStringLiteral("实时节点反馈"));
    pageHeader->setObjectName(QStringLiteral("motorDebugPageHeader"));

    auto *captureCard = new CardWidget(QStringLiteral("采集与分析"), IconKind::File);
    captureCard->contentLayout()->setContentsMargins(10, 6, 10, 6);
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

    auto *pageTopLayout = new QHBoxLayout;
    pageTopLayout->setContentsMargins(0, 0, 0, 0);
    pageTopLayout->setSpacing(10);
    pageTopLayout->addWidget(pageHeader, 0, Qt::AlignTop);
    pageTopLayout->addWidget(captureCard, 1, Qt::AlignTop);
    root->addLayout(pageTopLayout);

    auto *curveCard = new CardWidget(QStringLiteral("实时曲线工作区"), IconKind::Waveform);
    m_curveCard = curveCard;
    curveCard->contentLayout()->setContentsMargins(10, 6, 10, 8);
    curveCard->contentLayout()->setSpacing(5);
    auto *curveHeader = curveCard->headerLayout();
    curveHeader->insertWidget(
        qMax(0, curveHeader->count() - 1),
        makeLabel(QStringLiteral("每个窗口可叠加变量并独立缩放"),
                  QStringLiteral("mutedLabel")));
    auto *fitAll = makeButton(QStringLiteral("全部自动适配"), QStringLiteral("softButton"));
    curveHeader->addWidget(fitAll);
    auto *addIabc = makeButton(QStringLiteral("＋ IABC 组"), QStringLiteral("softButton"));
    curveHeader->addWidget(addIabc);
    auto *addSpeed = makeButton(QStringLiteral("＋ 速度组"), QStringLiteral("softButton"));
    curveHeader->addWidget(addSpeed);
    auto *addCurve = makeButton(QStringLiteral("＋ 自定义窗口"), QStringLiteral("primaryButton"));
    curveHeader->addWidget(addCurve);
    curveHeader->addWidget(makeLabel(QStringLiteral("缓冲区"), QStringLiteral("mutedLabel")));
    auto *historyLimit = new AppComboBox;
    historyLimit->setMinimumWidth(88);
    historyLimit->addItem(QStringLiteral("250 点"), 250);
    historyLimit->addItem(QStringLiteral("500 点"), 500);
    historyLimit->addItem(QStringLiteral("1000 点"), 1000);
    historyLimit->addItem(QStringLiteral("2000 点"), 2000);
    historyLimit->addItem(QStringLiteral("5000 点"), 5000);
    historyLimit->setCurrentIndex(1);
    curveHeader->addWidget(historyLimit);
    m_curveGrid = new QGridLayout;
    m_curveGrid->setContentsMargins(0, 0, 0, 0);
    m_curveGrid->setHorizontalSpacing(8);
    m_curveGrid->setVerticalSpacing(8);
    m_curveGrid->setColumnStretch(0, 1);
    m_curveGrid->setColumnStretch(1, 1);
    curveCard->contentLayout()->addLayout(m_curveGrid);

    auto *statusCard = new CardWidget(QStringLiteral("电机状态与参数"), IconKind::Motor);
    statusCard->contentLayout()->setContentsMargins(8, 6, 8, 8);
    statusCard->contentLayout()->setSpacing(4);
    auto *parameterToggle = makeButton(QStringLiteral("调参模式"), QStringLiteral("softButton"));
    parameterToggle->setCheckable(true);
    parameterToggle->setToolTip(QStringLiteral("打开高级电机参数调节；日常使用建议保持关闭"));
    statusCard->headerLayout()->addWidget(parameterToggle);

    auto *statusArea = new QHBoxLayout;
    statusArea->setContentsMargins(0, 0, 0, 0);
    statusArea->setSpacing(12);
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
    statusLayout->addWidget(makeMetricLabel(QStringLiteral("转速")), 1, 2);
    statusLayout->addWidget(m_rpmValue, 1, 3);
    statusLayout->addWidget(makeMetricLabel(QStringLiteral("电流")), 2, 0);
    statusLayout->addWidget(m_currentValue, 2, 1);
    statusLayout->addWidget(makeMetricLabel(QStringLiteral("电压")), 2, 2);
    statusLayout->addWidget(m_voltageValue, 2, 3);
    statusLayout->addWidget(makeMetricLabel(QStringLiteral("温度")), 3, 0);
    statusLayout->addWidget(m_temperatureValue, 3, 1);
    statusLayout->addWidget(makeMetricLabel(QStringLiteral("故障")), 3, 2);
    statusLayout->addWidget(m_faultValue, 3, 3);

    statusArea->addLayout(statusLayout, 1);

    auto *parameterPanel = new QWidget;
    parameterPanel->setObjectName(QStringLiteral("motorParameterPanel"));
    auto *parameterLayout = new QGridLayout(parameterPanel);
    parameterLayout->setContentsMargins(0, 0, 0, 0);
    parameterLayout->setHorizontalSpacing(6);
    parameterLayout->setVerticalSpacing(3);
    parameterLayout->setColumnStretch(1, 1);
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
        parameterLayout->addWidget(makeMetricLabel(parameterNames.at(index)), index, 0);
        parameterLayout->addWidget(boxes[index], index, 1);
    }
    auto *apply = makeButton(QStringLiteral("应用参数"), QStringLiteral("primaryButton"));
    parameterLayout->addWidget(apply, 4, 0, 1, 2);
    auto *reset = makeButton(QStringLiteral("恢复默认"), QStringLiteral("softButton"));
    parameterLayout->addWidget(reset, 5, 0, 1, 2);
    parameterPanel->setVisible(false);
    statusArea->addWidget(parameterPanel, 1);
    statusCard->contentLayout()->addLayout(statusArea);
    connect(parameterToggle, &QPushButton::toggled, this,
            [parameterToggle, parameterPanel](const bool enabled)
            {
                parameterPanel->setVisible(enabled);
                parameterToggle->setText(enabled ? QStringLiteral("关闭调参")
                                                 : QStringLiteral("调参模式"));
            });

    auto *speedCard = new CardWidget(QStringLiteral("基础转速控制"), IconKind::Settings);
    speedCard->contentLayout()->setContentsMargins(10, 8, 10, 8);
    speedCard->contentLayout()->setSpacing(6);
    auto *speedHeader = new QHBoxLayout;
    speedHeader->addWidget(makeLabel(QStringLiteral("目标转速"), QStringLiteral("mutedLabel")));
    m_speedInput = new QSpinBox;
    m_speedInput->setRange(-10000, 10000);
    m_speedInput->setSingleStep(100);
    m_speedInput->setSuffix(QStringLiteral(" rpm"));
    m_speedInput->setAlignment(Qt::AlignRight);
    m_speedInput->setKeyboardTracking(false);
    m_speedInput->setMinimumWidth(128);
    speedHeader->addWidget(m_speedInput);
    speedHeader->addStretch();
    speedCard->contentLayout()->addLayout(speedHeader);
    m_speedSlider = new QSlider(Qt::Horizontal);
    m_speedSlider->setRange(-10000, 10000);
    m_speedSlider->setSingleStep(100);
    m_speedSlider->setPageStep(1000);
    m_speedSlider->setValue(0);
    m_speedDispatchTimer = new QTimer(this);
    m_speedDispatchTimer->setSingleShot(true);
    m_speedDispatchTimer->setInterval(80);
    connect(m_speedDispatchTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_running)
                    sendSpeedControl(false, true);
            });
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

    auto *controlSplitter = new QSplitter(Qt::Horizontal);
    controlSplitter->setObjectName(QStringLiteral("motorDebugControlAreaSplitter"));
    controlSplitter->setChildrenCollapsible(false);
    controlSplitter->setHandleWidth(8);
    controlSplitter->addWidget(statusCard);
    controlSplitter->addWidget(speedCard);
    controlSplitter->setStretchFactor(0, 1);
    controlSplitter->setStretchFactor(1, 1);
    controlSplitter->setSizes({430, 520});
    controlSplitter->setMinimumHeight(220);

    m_curveCardHost = new QWidget;
    auto *curveCardHostLayout = new QVBoxLayout(m_curveCardHost);
    curveCardHostLayout->setContentsMargins(0, 0, 0, 0);
    curveCardHostLayout->addWidget(curveCard);

    m_curveSplitter = new QSplitter(Qt::Vertical);
    m_curveSplitter->setObjectName(QStringLiteral("motorDebugVerticalSplitter"));
    m_curveSplitter->setChildrenCollapsible(false);
    m_curveSplitter->setHandleWidth(8);
    m_curveSplitter->addWidget(m_curveCardHost);
    m_curveSplitter->addWidget(controlSplitter);
    m_curveSplitter->setStretchFactor(0, 1);
    m_curveSplitter->setStretchFactor(1, 0);
    m_curveSplitter->setSizes({500, 250});
    root->addWidget(m_curveSplitter, 1);

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
    connect(historyLimit, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, historyLimit](const int index)
            {
                if (index >= 0)
                    emit historyLimitChanged(historyLimit->itemData(index).toInt());
            });
    connect(m_motorSelect, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](const int index)
            {
                if (index < 0)
                    return;
                m_snapshot.selectedMotorId = QStringLiteral("thruster%1").arg(index + 1);
                m_snapshot.selectedMotorLabel = m_motorSelect->currentText();
            });
    connect(m_speedSlider, &QSlider::valueChanged, this,
            [this](const int value)
            {
                if (m_speedInput != nullptr)
                {
                    const QSignalBlocker blocker(m_speedInput);
                    m_speedInput->setValue(value);
                }
                if (m_running && m_speedDispatchTimer != nullptr)
                {
                    m_speedDispatchTimer->start();
                }
            });
    connect(m_speedInput, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](const int value)
            {
                if (m_speedSlider != nullptr)
                {
                    const QSignalBlocker blocker(m_speedSlider);
                    m_speedSlider->setValue(value);
                }
            });
    connect(m_speedInput, &QSpinBox::editingFinished, this,
            [this]()
            {
                if (m_speedDispatchTimer != nullptr)
                    m_speedDispatchTimer->stop();
                sendSpeedControl(false, m_running);
            });
    connect(m_speedSlider, &QSlider::sliderReleased, this,
            [this]()
            {
                if (m_running)
                {
                    if (m_speedDispatchTimer != nullptr)
                        m_speedDispatchTimer->stop();
                    sendSpeedControl(false, true);
                }
            });
    connect(applySpeed, &QPushButton::clicked, this,
            [this]() { sendSpeedControl(false, m_running); });
    connect(m_runButton, &QPushButton::clicked, this,
            [this]()
            {
                m_running = !m_running;
                m_runButton->setText(m_running ? QStringLiteral("停止") : QStringLiteral("启动"));
                // 启动同时下发当前目标转速；停止使用 RUN_VECTOR，确保固件执行停机。
                sendSpeedControl(!m_running, m_running);
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
            });
    connect(reset, &QPushButton::clicked, this,
            [this]()
            {
                m_kp->setValue(0.20);
                m_ki->setValue(0.05);
                m_observerGain->setValue(0.10);
                m_currentLimit->setValue(20.0);
            });
    connect(capture, &QPushButton::clicked, this,
            [this, sampleRate, duration]()
            {
                MotorCaptureRequest request;
                request.motorId = m_snapshot.selectedMotorId;
                request.sampleRateHz = sampleRate->currentText().split(' ').first().toInt();
                request.durationSeconds = duration->currentText().split(' ').first().toInt();
                emit captureRequested(request);
            });

    MotorDebugSnapshot initialSnapshot;
    initialSnapshot.motorStamp.freshness = DataFreshness::Offline;
    initialSnapshot.state = QStringLiteral("离线");
    initialSnapshot.fault = QStringLiteral("离线");
    setSnapshot(initialSnapshot);
    addCurveWindow(0);
}

MotorDebugPage::~MotorDebugPage()
{
    const auto floatingWindows = m_curveDialogs;
    for (auto it = floatingWindows.cbegin(); it != floatingWindows.cend(); ++it)
    {
        if (it.key() == nullptr || it.value() == nullptr)
            continue;
        it.value()->layout()->removeWidget(it.key());
        it.key()->setParent(this);
        delete it.value();
    }
    m_curveDialogs.clear();
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
    curve->dragLabel()->installEventFilter(this);
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
    curve->dragLabel()->installEventFilter(this);
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
        return;
    if (m_curveDialogs.contains(window))
        restoreCurveWindow(window);
    m_curveGrid->removeWidget(window);
    m_curveWindows.removeOne(window);
    window->deleteLater();
    relayoutCurveWindows();
}

void MotorDebugPage::relayoutCurveWindows()
{
    QVector<QWidget *> dockedWindows;
    for (QWidget *window : m_curveWindows)
    {
        if (!m_curveDialogs.contains(window))
            dockedWindows.append(window);
    }

    if (dockedWindows.size() == 1)
    {
        m_curveGrid->addWidget(dockedWindows.first(), 0, 0, 1, 2);
    }
    else
    {
        for (int index = 0; index < dockedWindows.size(); ++index)
            m_curveGrid->addWidget(dockedWindows.at(index), index / 2, index % 2);
    }
    updateCurveAreaLayout();
}

void MotorDebugPage::updateCurveAreaLayout()
{
    if (m_curveCard == nullptr || m_curveCardHost == nullptr || m_curveSplitter == nullptr)
        return;

    int dockedCount = 0;
    for (QWidget *window : m_curveWindows)
    {
        if (!m_curveDialogs.contains(window))
            ++dockedCount;
    }

    QWidget *content = m_curveCard->contentLayout()->parentWidget();
    if (dockedCount == 0)
    {
        if (!m_curveAreaCollapsed)
        {
            const QList<int> sizes = m_curveSplitter->sizes();
            if (!sizes.isEmpty() && sizes.first() > 0)
                m_curveExpandedHeight = sizes.first();
        }
        if (content != nullptr)
            content->setVisible(false);

        const int headerHeight = qMax(42, m_curveCard->headerLayout()->parentWidget()->sizeHint().height());
        // Keep the area visually collapsed, but leave the splitter handle usable
        // so the user can manually open the empty curve area.
        m_curveCardHost->setMaximumHeight(QWIDGETSIZE_MAX);
        const int totalHeight = qMax(headerHeight + 1, m_curveSplitter->height());
        m_curveSplitter->setSizes({headerHeight, qMax(1, totalHeight - headerHeight)});
        m_curveAreaCollapsed = true;
        return;
    }

    if (content != nullptr)
        content->setVisible(true);
    m_curveCardHost->setMaximumHeight(QWIDGETSIZE_MAX);
    if (m_curveAreaCollapsed)
    {
        const int totalHeight = qMax(m_curveExpandedHeight + 1, m_curveSplitter->height());
        const int curveHeight = qBound(1, m_curveExpandedHeight, totalHeight - 1);
        m_curveSplitter->setSizes({curveHeight, qMax(1, totalHeight - curveHeight)});
    }
    m_curveAreaCollapsed = false;
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
}

void MotorDebugPage::detachCurveWindow(QWidget *window, const QPoint &globalPos,
                                       const QPoint &dragOffset)
{
    if (window == nullptr || m_curveDialogs.contains(window))
        return;

    auto *curve = static_cast<CurveWindowWidget *>(window);
    QWidget *owner = QApplication::activeWindow();
    auto *dialog = new CurveFloatingDialog(owner);
    dialog->setWindowTitle(QStringLiteral("%1").arg(curve->dragLabel()->text()));
    dialog->setAttribute(Qt::WA_DeleteOnClose, false);
    dialog->setModal(false);
    const QSize sourceSize = window->size();
    const QSize initialSize = QSize(qMax(420, sourceSize.width() + 16),
                                    qMax(260, sourceSize.height() + 16));
    dialog->resize(initialSize);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(8, 8, 8, 8);
    m_curveGrid->removeWidget(window);
    window->setParent(dialog);
    layout->addWidget(window);
    dialog->resize(initialSize);
    m_curveDialogs.insert(window, dialog);
    relayoutCurveWindows();
    dialog->move(globalPos - dragOffset);
    connect(dialog, &QDialog::finished, this,
            [this, window](const int) { restoreCurveWindow(window); });
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    m_curveSystemMoveActive = dialog->windowHandle() != nullptr &&
                              dialog->windowHandle()->startSystemMove();
}

void MotorDebugPage::restoreCurveWindow(QWidget *window)
{
    auto it = m_curveDialogs.find(window);
    if (it == m_curveDialogs.end())
        return;

    QDialog *dialog = it.value();
    m_curveDialogs.erase(it);
    dialog->layout()->removeWidget(window);
    window->setParent(m_curveCard);
    relayoutCurveWindows();
    dialog->hide();
    dialog->deleteLater();
}

void MotorDebugPage::sendSpeedControl(const bool runCommand, const bool enabled)
{
    MotorSpeedControlRequest request;
    request.motorId = m_snapshot.selectedMotorId;
    request.nodeId = static_cast<quint8>(m_motorSelect->currentIndex() + 1);
    request.targetRpm = m_speedInput != nullptr ? m_speedInput->value() : m_speedSlider->value();
    request.enabled = enabled;
    request.runCommand = runCommand;
    emit speedControlRequested(request);
}

bool MotorDebugPage::eventFilter(QObject *watched, QEvent *event)
{
    QWidget *dragWindow = nullptr;
    for (QWidget *widget : m_curveWindows)
    {
        auto *curve = static_cast<CurveWindowWidget *>(widget);
        if (watched == curve->dragLabel())
        {
            dragWindow = widget;
            break;
        }
    }

    const bool curveTitle = dragWindow != nullptr &&
                            (!m_curveDialogs.contains(dragWindow) ||
                             (m_curveDragWindow == dragWindow && m_curveDragActive));
    if (curveTitle)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton)
            {
                m_curveDragStartGlobal = mouse->globalPos();
                m_curveDragWindow = dragWindow;
                if (m_curveDialogs.contains(dragWindow))
                    m_curveDragOffset = m_curveDragStartGlobal
                                        - m_curveDialogs.value(dragWindow)
                                              ->frameGeometry()
                                              .topLeft();
                else
                m_curveDragOffset =
                        m_curveDragStartGlobal - dragWindow->mapToGlobal(QPoint(0, 0));
                m_curveDragActive = false;
                m_curveSystemMoveActive = false;
                static_cast<CurveWindowWidget *>(dragWindow)->dragLabel()->setCursor(
                    Qt::ClosedHandCursor);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove)
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if ((mouse->buttons() & Qt::LeftButton) != 0)
            {
                const bool pastThreshold =
                    (mouse->globalPos() - m_curveDragStartGlobal).manhattanLength() >=
                    QApplication::startDragDistance();
                if (!m_curveDragActive && pastThreshold)
                {
                    if (!m_curveDialogs.contains(m_curveDragWindow))
                    {
                        detachCurveWindow(m_curveDragWindow, mouse->globalPos(), m_curveDragOffset);
                        m_curveDragActive = m_curveDialogs.contains(m_curveDragWindow);
                    }
                    else
                        m_curveDragActive = true;
                }
                if (m_curveDragActive && m_curveDragWindow != nullptr &&
                    m_curveDialogs.contains(m_curveDragWindow))
                {
                    if (m_curveSystemMoveActive)
                        return true;
                    m_curveDialogs.value(m_curveDragWindow)->move(
                        mouse->globalPos() - m_curveDragOffset);
                    return true;
                }
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease)
        {
            m_curveDragActive = false;
            m_curveSystemMoveActive = false;
            if (m_curveDragWindow != nullptr)
                static_cast<CurveWindowWidget *>(m_curveDragWindow)
                    ->dragLabel()
                    ->setCursor(Qt::OpenHandCursor);
            m_curveDragWindow = nullptr;
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
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
}

} // namespace rov
