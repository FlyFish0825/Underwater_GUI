#include "pages/motor_debug/MotorDebugPage.h"

#include "preview/PreviewData.h"
#include "ui/common/UiPrimitives.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{

class WaveformWidget final : public QWidget
{
  public:
    explicit WaveformWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(360, 210);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setSeries(const QVector<rov::DebugSeries> &series)
    {
        m_series = series;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), Qt::white);
        const QRectF plot = QRectF(42, 12, width() - 60, height() - 46);
        painter.setPen(QPen(QColor(QStringLiteral("#dce6ef")), 1));
        for (int i = 0; i <= 6; ++i)
        {
            const qreal y = plot.top() + plot.height() * i / 6.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        for (int i = 0; i <= 8; ++i)
        {
            const qreal x = plot.left() + plot.width() * i / 8.0;
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        painter.setPen(QPen(QColor(QStringLiteral("#849ab1")), 1));
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
        painter.drawLine(plot.topLeft(), plot.bottomLeft());
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9));
        painter.drawText(QRectF(0, plot.top() - 8, 35, 18), Qt::AlignRight, QStringLiteral("60"));
        painter.drawText(QRectF(0, plot.center().y() - 8, 35, 18), Qt::AlignRight,
                         QStringLiteral("0"));
        painter.drawText(QRectF(0, plot.bottom() - 8, 35, 18), Qt::AlignRight,
                         QStringLiteral("-60"));
        painter.drawText(QRectF(plot.left(), plot.bottom() + 9, plot.width(), 20), Qt::AlignCenter,
                         QStringLiteral("时间（秒）"));
        const QColor colors[] = {
            QColor(QStringLiteral("#1687ee")), QColor(QStringLiteral("#ef4444")),
            QColor(QStringLiteral("#16a05d")), QColor(QStringLiteral("#f39a18"))};
        for (int s = 0; s < m_series.size(); ++s)
        {
            const auto &data = m_series.at(s).samples;
            if (data.isEmpty())
            {
                continue;
            }
            QPainterPath path;
            for (int i = 0; i < data.size(); ++i)
            {
                const qreal x = plot.left() + plot.width() * i / qMax(1, data.size() - 1);
                const qreal normalized = qBound(-60.0, data.at(i), 60.0);
                const qreal y = plot.center().y() - normalized / 120.0 * plot.height();
                if (i == 0)
                {
                    path.moveTo(x, y);
                }
                else
                {
                    path.lineTo(x, y);
                }
            }
            painter.setPen(QPen(colors[s % 4], 1.6));
            painter.drawPath(path);
        }
    }

  private:
    QVector<rov::DebugSeries> m_series;
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
                                   QStringLiteral("单电机调节与波形分析。"),
                                   QStringLiteral("演示 · 预览数据")));

    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(8);
    auto *waveformCard = new CardWidget(QStringLiteral("实时波形"), IconKind::Waveform);
    waveformCard->contentLayout()->setContentsMargins(10, 8, 10, 10);
    waveformCard->contentLayout()->setSpacing(6);
    auto *controls = new QHBoxLayout;
    controls->addWidget(makeLabel(QStringLiteral("时间刻度"), QStringLiteral("mutedLabel")));
    auto *scale = new QComboBox;
    scale->addItems(
        {QStringLiteral("1 秒/格"), QStringLiteral("500 毫秒/格"), QStringLiteral("100 毫秒/格")});
    controls->addWidget(scale);
    controls->addSpacing(8);
    controls->addWidget(makeLabel(QStringLiteral("触发"), QStringLiteral("mutedLabel")));
    auto *trigger = new QComboBox;
    trigger->addItems({QStringLiteral("关闭"), QStringLiteral("转速"), QStringLiteral("电流")});
    controls->addWidget(trigger);
    controls->addStretch();
    controls->addWidget(makeButton(QStringLiteral("暂停"), QStringLiteral("softButton")));
    controls->addWidget(makeButton(QStringLiteral("记录"), QStringLiteral("softButton")));
    controls->addWidget(makeButton(QStringLiteral("导出"), QStringLiteral("softButton")));
    waveformCard->contentLayout()->addLayout(controls);
    auto *waveform = new WaveformWidget;
    waveformCard->contentLayout()->addWidget(waveform, 1);
    auto *legend = new QHBoxLayout;
    const QStringList legendNames = {QStringLiteral("相电流（A）"), QStringLiteral("母线电压（V）"),
                                     QStringLiteral("转速（×100）"), QStringLiteral("观测器误差")};
    for (const QString &name : legendNames)
    {
        legend->addWidget(
            makeLabel(QStringLiteral("□  %1").arg(name), QStringLiteral("mutedLabel")));
    }
    legend->addStretch();
    waveformCard->contentLayout()->addLayout(legend);
    topRow->addWidget(waveformCard, 6);

    auto *side = new QVBoxLayout;
    side->setSpacing(8);
    auto *signalsCard = new CardWidget(QStringLiteral("信号选择"), IconKind::Settings);
    signalsCard->contentLayout()->setContentsMargins(8, 6, 8, 8);
    signalsCard->contentLayout()->setSpacing(4);
    auto *signalGrid = new QGridLayout;
    signalGrid->setSpacing(4);
    const QStringList groups = {QStringLiteral("电流信号"), QStringLiteral("电压信号"),
                                QStringLiteral("观测器信号"), QStringLiteral("控制信号")};
    const QStringList signalNames = {QStringLiteral("A 相电流"), QStringLiteral("母线电压"),
                                     QStringLiteral("估计转速"), QStringLiteral("PWM 指令")};
    for (int i = 0; i < groups.size(); ++i)
    {
        auto *group = new QFrame;
        group->setObjectName(QStringLiteral("card"));
        auto *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(6, 4, 6, 4);
        groupLayout->setSpacing(1);
        groupLayout->addWidget(makeLabel(groups.at(i), QStringLiteral("bodyValue")));
        for (int j = 0; j < 3; ++j)
        {
            auto *check = new QCheckBox(
                j == 0 ? signalNames.at(i)
                       : QStringLiteral("%1 %2").arg(groups.at(i).left(7)).arg(j + 1));
            check->setChecked(j == 0);
            groupLayout->addWidget(check);
        }
        signalGrid->addWidget(group, i / 2, i % 2);
    }
    signalsCard->contentLayout()->addLayout(signalGrid);
    side->addWidget(signalsCard);

    auto *statusCard = new CardWidget(QStringLiteral("电机状态与参数"), IconKind::Motor);
    statusCard->contentLayout()->setContentsMargins(8, 6, 8, 8);
    statusCard->contentLayout()->setSpacing(4);
    auto *statusLayout = new QGridLayout;
    statusLayout->setHorizontalSpacing(6);
    statusLayout->setVerticalSpacing(3);
    statusLayout->setColumnStretch(1, 1);
    statusLayout->setColumnStretch(3, 1);
    statusLayout->addWidget(makeLabel(QStringLiteral("电机"), QStringLiteral("mutedLabel")), 0, 0);
    auto *motorSelect = new QComboBox;
    motorSelect->addItems({QStringLiteral("推进器 1（FL）"), QStringLiteral("推进器 2（FR）"),
                           QStringLiteral("推进器 3（ML）"), QStringLiteral("推进器 4（MR）")});
    statusLayout->addWidget(motorSelect, 0, 1, 1, 3);
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
    for (int i = 0; i < 4; ++i)
    {
        statusLayout->addWidget(makeMetricLabel(parameterNames.at(i)), i + 1, 2);
        statusLayout->addWidget(boxes[i], i + 1, 3);
    }
    auto *apply = makeButton(QStringLiteral("应用参数"), QStringLiteral("primaryButton"));
    statusLayout->addWidget(apply, 5, 2, 1, 2);
    auto *reset = makeButton(QStringLiteral("恢复默认"), QStringLiteral("softButton"));
    statusLayout->addWidget(reset, 6, 2, 1, 2);
    statusCard->contentLayout()->addLayout(statusLayout);
    side->addWidget(statusCard);
    topRow->addLayout(side, 4);
    root->addLayout(topRow, 1);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->setSpacing(8);
    auto *captureCard = new CardWidget(QStringLiteral("采集与分析"), IconKind::File);
    captureCard->setMaximumHeight(124);
    captureCard->contentLayout()->setContentsMargins(10, 8, 10, 8);
    auto *captureLayout = new QHBoxLayout;
    auto *sampleRate = new QComboBox;
    sampleRate->addItems(
        {QStringLiteral("1 kHz"), QStringLiteral("5 kHz"), QStringLiteral("10 kHz")});
    auto *duration = new QComboBox;
    duration->addItems({QStringLiteral("10 s"), QStringLiteral("30 s"), QStringLiteral("60 s")});
    captureLayout->addWidget(makeMetricLabel(QStringLiteral("采样率")));
    captureLayout->addWidget(sampleRate);
    captureLayout->addWidget(makeMetricLabel(QStringLiteral("记录时长")));
    captureLayout->addWidget(duration);
    captureLayout->addStretch();
    auto *capture = makeButton(QStringLiteral("立即采集"), QStringLiteral("softButton"));
    captureLayout->addWidget(capture);
    captureCard->contentLayout()->addLayout(captureLayout);
    bottomRow->addWidget(captureCard, 3);

    auto *logCard = new CardWidget(QStringLiteral("最近数据 / 日志"), IconKind::List);
    logCard->setMaximumHeight(124);
    logCard->contentLayout()->setContentsMargins(10, 8, 10, 8);
    m_requestLog = makeLabel(QStringLiteral("暂无请求。"), QStringLiteral("mutedLabel"));
    m_requestLog->setWordWrap(true);
    logCard->contentLayout()->addWidget(m_requestLog);
    bottomRow->addWidget(logCard, 2);
    root->addLayout(bottomRow);

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
                logRequest(QStringLiteral("已在演示模式恢复默认参数"));
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
    waveform->setSeries(m_snapshot.series);
}

void MotorDebugPage::setSnapshot(const MotorDebugSnapshot &snapshot)
{
    m_snapshot = snapshot;
    refreshView();
}

void MotorDebugPage::refreshView()
{
    if (m_stateValue == nullptr)
    {
        return;
    }
    m_stateValue->setText(m_snapshot.state);
    m_rpmValue->setText(QStringLiteral("%1 rpm").arg(m_snapshot.rpm, 0, 'f', 0));
    m_currentValue->setText(QStringLiteral("%1 A").arg(m_snapshot.currentA, 0, 'f', 1));
    m_voltageValue->setText(QStringLiteral("%1 V").arg(m_snapshot.voltageV, 0, 'f', 1));
    m_temperatureValue->setText(QStringLiteral("%1 °C").arg(m_snapshot.temperatureC, 0, 'f', 1));
    m_faultValue->setText(m_snapshot.fault);
    if (!m_snapshot.recentLog.isEmpty() && m_requestLog != nullptr)
    {
        m_requestLog->setText(m_snapshot.recentLog.join(QStringLiteral("\n")));
    }
}

void MotorDebugPage::logRequest(const QString &message)
{
    if (m_requestLog != nullptr)
    {
        m_requestLog->setText(QStringLiteral("最近请求：%1 · 未写入设备").arg(message));
    }
}

} // namespace rov
