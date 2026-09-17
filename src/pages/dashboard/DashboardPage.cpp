#include "pages/dashboard/DashboardPage.h"

#include "preview/PreviewData.h"
#include "ui/common/UiPrimitives.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>

namespace
{

class RovTopView final : public QWidget
{
  public:
    explicit RovTopView(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(280, 280);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), Qt::white);
        const QColor blue(QStringLiteral("#1687ee"));
        const QColor dark(QStringLiteral("#203b60"));
        const QColor light(QStringLiteral("#eaf2f9"));
        const QPointF center(width() * 0.50, height() * 0.52);
        const qreal bodyW = qMin(width() * 0.27, 132.0);
        const qreal bodyH = qMin(height() * 0.64, 270.0);
        const QRectF body(center.x() - bodyW / 2.0, center.y() - bodyH / 2.0, bodyW, bodyH);

        painter.setPen(QPen(dark, 2));
        painter.setBrush(QColor(QStringLiteral("#f4f7fa")));
        painter.drawRoundedRect(body, bodyW / 2.0, bodyW / 2.0);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(QStringLiteral("#8ba0b8")), 1.4));
        painter.drawEllipse(QPointF(center.x(), body.top() + 28), 7, 7);
        painter.drawEllipse(QPointF(center.x(), body.bottom() - 30), 5, 5);
        painter.drawLine(center.x(), body.top() + 50, center.x(), body.bottom() - 52);
        painter.drawLine(body.left() + 18, center.y(), body.right() - 18, center.y());

        const QPointF positions[] = {QPointF(body.left() - 48, body.top() + 30),
                                     QPointF(body.right() + 48, body.top() + 30),
                                     QPointF(body.left() - 55, center.y()),
                                     QPointF(body.right() + 55, center.y()),
                                     QPointF(body.left() - 48, body.bottom() - 30),
                                     QPointF(body.right() + 48, body.bottom() - 30)};
        const QStringList labels = {QStringLiteral("FL"), QStringLiteral("FR"),
                                    QStringLiteral("ML"), QStringLiteral("MR"),
                                    QStringLiteral("RL"), QStringLiteral("RR")};
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        for (int i = 0; i < 6; ++i)
        {
            const QPointF p = positions[i];
            painter.setPen(QPen(QColor(QStringLiteral("#a5b7c9")), 1));
            painter.drawLine(QPointF(p.x() < center.x() ? body.left() : body.right(), p.y()), p);
            painter.setPen(QPen(QColor(QStringLiteral("#223c5f")), 1.3));
            painter.setBrush(QColor(QStringLiteral("#313d4b")));
            painter.drawEllipse(p, 21, 21);
            painter.setBrush(QColor(QStringLiteral("#1f97ea")));
            painter.drawEllipse(p, 10, 10);
            painter.setPen(QPen(QColor(QStringLiteral("#85c9f5")), 1.1));
            painter.drawLine(p + QPointF(-7, -7), p + QPointF(7, 7));
            painter.drawLine(p + QPointF(-7, 7), p + QPointF(7, -7));
            painter.setPen(dark);
            const QRectF labelRect(p.x() < center.x() ? p.x() - 41 : p.x() + 25, p.y() - 10, 28,
                                   20);
            painter.drawText(labelRect, Qt::AlignCenter, labels.at(i));
            painter.setBrush(QColor(QStringLiteral("#10a85e")));
            painter.drawEllipse(p + QPointF(p.x() < center.x() ? -29 : 29, -17), 4, 4);
        }
        painter.setPen(QPen(QColor(QStringLiteral("#728aa5")), 1.2));
        painter.drawLine(center.x(), body.top() - 28, center.x(), body.top() - 7);
        painter.drawLine(center.x(), body.top() - 28, center.x() - 5, body.top() - 20);
        painter.drawLine(center.x(), body.top() - 28, center.x() + 5, body.top() - 20);
        painter.drawText(QRectF(center.x() - 25, body.top() - 52, 50, 18), Qt::AlignCenter,
                         QStringLiteral("前方"));
        painter.drawLine(center.x(), body.bottom() + 7, center.x(), body.bottom() + 28);
        painter.drawLine(center.x(), body.bottom() + 28, center.x() - 5, body.bottom() + 20);
        painter.drawLine(center.x(), body.bottom() + 28, center.x() + 5, body.bottom() + 20);
        painter.drawText(QRectF(center.x() - 25, body.bottom() + 34, 50, 18), Qt::AlignCenter,
                         QStringLiteral("后方"));
    }
};

QWidget *metricTile(const QString &label, QLabel *&value, const QString &initial)
{
    auto *tile = new QFrame;
    tile->setObjectName(QStringLiteral("card"));
    auto *layout = new QVBoxLayout(tile);
    layout->setContentsMargins(12, 9, 12, 9);
    layout->setSpacing(3);
    layout->addWidget(rov::makeMetricLabel(label));
    value = rov::makeMetricValue(initial);
    layout->addWidget(value);
    return tile;
}

QWidget *thrusterTile(const rov::ThrusterTelemetry &item)
{
    auto *tile = new QFrame;
    tile->setObjectName(QStringLiteral("card"));
    auto *layout = new QVBoxLayout(tile);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(2);
    auto *title = new QLabel(QStringLiteral("推进器 %1").arg(item.label));
    title->setStyleSheet(QStringLiteral("font-weight: 650; color: #18365b;"));
    layout->addWidget(title);
    auto *line = new QHBoxLayout;
    line->addWidget(rov::makeMetricLabel(QStringLiteral("转速")));
    line->addStretch();
    line->addWidget(rov::makeLabel(QString::number(item.rpm, 'f', 0), QStringLiteral("bodyValue")));
    layout->addLayout(line);
    line = new QHBoxLayout;
    line->addWidget(rov::makeMetricLabel(QStringLiteral("电流")));
    line->addStretch();
    line->addWidget(rov::makeLabel(QStringLiteral("%1 A").arg(item.currentA, 0, 'f', 1),
                                   QStringLiteral("bodyValue")));
    layout->addLayout(line);
    line = new QHBoxLayout;
    line->addWidget(rov::makeMetricLabel(QStringLiteral("温度")));
    line->addStretch();
    line->addWidget(rov::makeLabel(QStringLiteral("%1 °C").arg(item.temperatureC, 0, 'f', 0),
                                   QStringLiteral("bodyValue")));
    layout->addLayout(line);
    return tile;
}

} // namespace

namespace rov
{

DashboardPage::DashboardPage(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(10);
    root->addWidget(makePageHeader(QStringLiteral("总览"),
                                   QStringLiteral("水下机器人状态、推进器与控制概览。"),
                                   QStringLiteral("演示 · 未连接设备")));

    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(12);
    auto *overview = new CardWidget(QStringLiteral("机器人总览（俯视图）"), IconKind::Dashboard);
    auto *overviewGrid = new QGridLayout;
    overviewGrid->setContentsMargins(0, 0, 0, 0);
    overviewGrid->setSpacing(8);
    const DashboardSnapshot preview = dashboardPreview();
    overviewGrid->addWidget(thrusterTile(preview.thrusters.at(0)), 0, 0);
    overviewGrid->addWidget(thrusterTile(preview.thrusters.at(1)), 0, 2);
    overviewGrid->addWidget(thrusterTile(preview.thrusters.at(2)), 1, 0);
    overviewGrid->addWidget(new RovTopView, 0, 1, 3, 1);
    overviewGrid->addWidget(thrusterTile(preview.thrusters.at(3)), 1, 2);
    overviewGrid->addWidget(thrusterTile(preview.thrusters.at(4)), 2, 0);
    overviewGrid->addWidget(thrusterTile(preview.thrusters.at(5)), 2, 2);
    overviewGrid->setColumnStretch(1, 2);
    overview->contentLayout()->addLayout(overviewGrid);
    topRow->addWidget(overview, 6);

    auto *stateColumn = new QVBoxLayout;
    stateColumn->setSpacing(12);
    auto *stateCard = new CardWidget(QStringLiteral("机器人状态"), IconKind::Status);
    auto *stateGrid = new QGridLayout;
    stateGrid->setSpacing(8);
    stateGrid->addWidget(metricTile(QStringLiteral("深度"), m_depthValue, QStringLiteral("--")), 0,
                         0);
    stateGrid->addWidget(metricTile(QStringLiteral("横滚"), m_rollValue, QStringLiteral("--")), 0,
                         1);
    stateGrid->addWidget(metricTile(QStringLiteral("俯仰"), m_pitchValue, QStringLiteral("--")), 0,
                         2);
    stateGrid->addWidget(metricTile(QStringLiteral("航向"), m_yawValue, QStringLiteral("--")), 0,
                         3);
    stateGrid->addWidget(
        metricTile(QStringLiteral("母线电压"), m_voltageValue, QStringLiteral("--")), 1, 0);
    stateGrid->addWidget(metricTile(QStringLiteral("工作模式"), m_modeValue, QStringLiteral("--")),
                         1, 1);
    stateGrid->addWidget(metricTile(QStringLiteral("解锁状态"), m_armValue, QStringLiteral("--")),
                         1, 2);
    stateGrid->addWidget(metricTile(QStringLiteral("漏水状态"), m_leakValue, QStringLiteral("--")),
                         1, 3);
    stateGrid->addWidget(
        metricTile(QStringLiteral("内部温度"), m_temperatureValue, QStringLiteral("--")), 2, 0);
    stateGrid->setColumnStretch(0, 1);
    stateGrid->setColumnStretch(1, 1);
    stateGrid->setColumnStretch(2, 1);
    stateGrid->setColumnStretch(3, 1);
    stateCard->contentLayout()->addLayout(stateGrid);
    stateColumn->addWidget(stateCard);

    auto *controlCard = new CardWidget(QStringLiteral("六自由度手动控制"), IconKind::Action);
    auto *enableRow = new QHBoxLayout;
    enableRow->addWidget(makeLabel(QStringLiteral("启用控制"), QStringLiteral("bodyValue")));
    auto *enable = new QCheckBox;
    enable->setChecked(false);
    enableRow->addStretch();
    enableRow->addWidget(enable);
    controlCard->contentLayout()->addLayout(enableRow);
    auto *permissionRow = new QHBoxLayout;
    permissionRow->addWidget(makeLabel(QStringLiteral("控制权限"), QStringLiteral("bodyValue")));
    permissionRow->addStretch();
    m_controlPermission = makeStatusPill(QStringLiteral("仅演示"), QStringLiteral("statusWarn"));
    permissionRow->addWidget(m_controlPermission);
    controlCard->contentLayout()->addLayout(permissionRow);
    auto *axisGrid = new QGridLayout;
    axisGrid->setHorizontalSpacing(10);
    axisGrid->setVerticalSpacing(7);
    const QStringList axes = {QStringLiteral("前进"), QStringLiteral("横移"),
                              QStringLiteral("升沉"), QStringLiteral("横滚"),
                              QStringLiteral("俯仰"), QStringLiteral("航向")};
    for (int i = 0; i < axes.size(); ++i)
    {
        auto *axisBox = new QVBoxLayout;
        axisBox->setSpacing(3);
        axisBox->addWidget(makeLabel(axes.at(i), QStringLiteral("bodyValue")), 0, Qt::AlignCenter);
        auto *row = new QHBoxLayout;
        auto *minus = makeButton(QStringLiteral("−"), QStringLiteral("softButton"));
        auto *plus = makeButton(QStringLiteral("+"), QStringLiteral("softButton"));
        auto *zero = makeLabel(QStringLiteral("0"), QStringLiteral("bodyValue"));
        row->addWidget(minus);
        row->addWidget(zero, 1, Qt::AlignCenter);
        row->addWidget(plus);
        axisBox->addLayout(row);
        axisGrid->addLayout(axisBox, i / 3, i % 3);
        connect(minus, &QPushButton::clicked, this,
                [this, i]()
                {
                    Q_UNUSED(i)
                    emit manualControlRequested(SixDofControlRequest());
                    logRequest(QStringLiteral("手动控制：请求减小"));
                });
        connect(plus, &QPushButton::clicked, this,
                [this, i]()
                {
                    Q_UNUSED(i)
                    emit manualControlRequested(SixDofControlRequest());
                    logRequest(QStringLiteral("手动控制：请求增大"));
                });
    }
    controlCard->contentLayout()->addLayout(axisGrid);
    auto *limitRow = new QHBoxLayout;
    limitRow->addWidget(makeLabel(QStringLiteral("最大推力上限"), QStringLiteral("bodyValue")));
    auto *limitSlider = new QSlider(Qt::Horizontal);
    limitSlider->setRange(0, 100);
    limitSlider->setValue(preview.thrustLimitPercent);
    limitRow->addWidget(limitSlider, 1);
    auto *limitLabel = makeLabel(QStringLiteral("70%"), QStringLiteral("bodyValue"));
    limitRow->addWidget(limitLabel);
    connect(limitSlider, &QSlider::valueChanged, this,
            [this, limitLabel](const int value)
            {
                limitLabel->setText(QStringLiteral("%1%").arg(value));
                emit thrustLimitRequested(ThrustLimitRequest{value});
                logRequest(QStringLiteral("推力上限请求：%1%").arg(value));
            });
    controlCard->contentLayout()->addLayout(limitRow);
    stateColumn->addWidget(controlCard);
    topRow->addLayout(stateColumn, 6);
    root->addLayout(topRow, 1);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->setSpacing(12);
    auto *summary = new CardWidget(QStringLiteral("电机状态汇总"), IconKind::Motor);
    auto *summaryGrid = new QGridLayout;
    summaryGrid->setColumnStretch(0, 1);
    summaryGrid->setColumnStretch(1, 1);
    summaryGrid->setColumnStretch(2, 1);
    summaryGrid->setColumnStretch(3, 1);
    summaryGrid->addWidget(makeMetricLabel(QStringLiteral("总推进器")), 0, 0);
    summaryGrid->addWidget(makeMetricLabel(QStringLiteral("在线")), 0, 1);
    summaryGrid->addWidget(makeMetricLabel(QStringLiteral("离线")), 0, 2);
    summaryGrid->addWidget(makeMetricLabel(QStringLiteral("警告")), 0, 3);
    summaryGrid->addWidget(makeMetricValue(QStringLiteral("6")), 1, 0);
    summaryGrid->addWidget(makeLabel(QStringLiteral("6"), QStringLiteral("metricValue")), 1, 1);
    summaryGrid->addWidget(makeLabel(QStringLiteral("0"), QStringLiteral("metricValue")), 1, 2);
    summaryGrid->addWidget(makeLabel(QStringLiteral("0"), QStringLiteral("metricValue")), 1, 3);
    summaryGrid->addWidget(makeMetricLabel(QStringLiteral("平均转速  1,082")), 2, 0, 1, 2);
    summaryGrid->addWidget(makeMetricLabel(QStringLiteral("平均电流  1.1 A")), 2, 2);
    summaryGrid->addWidget(makeMetricLabel(QStringLiteral("平均温度  25.3 °C")), 2, 3);
    summary->contentLayout()->addLayout(summaryGrid);
    bottomRow->addWidget(summary, 4);

    auto *alarms = new CardWidget(QStringLiteral("活动报警"), IconKind::Alarm);
    m_alarmValue = makeMetricValue(QStringLiteral("无活动报警"));
    alarms->contentLayout()->addStretch();
    alarms->contentLayout()->addWidget(m_alarmValue, 0, Qt::AlignCenter);
    alarms->contentLayout()->addWidget(
        makeLabel(QStringLiteral("所有系统正常。"), QStringLiteral("mutedLabel")), 0,
        Qt::AlignCenter);
    alarms->contentLayout()->addStretch();
    bottomRow->addWidget(alarms, 4);

    auto *quick = new CardWidget(QStringLiteral("快捷操作"), IconKind::Action);
    auto *quickGrid = new QGridLayout;
    auto *arm = makeButton(QStringLiteral("解锁电机"), QStringLiteral("primaryButton"));
    auto *disarm = makeButton(QStringLiteral("停用电机"), QStringLiteral("dangerButton"));
    auto *hold = makeButton(QStringLiteral("保持位置"), QStringLiteral("softButton"));
    auto *surface = makeButton(QStringLiteral("上浮（紧急）"), QStringLiteral("softButton"));
    quickGrid->addWidget(arm, 0, 0);
    quickGrid->addWidget(disarm, 0, 1);
    quickGrid->addWidget(hold, 1, 0);
    quickGrid->addWidget(surface, 1, 1);
    quick->contentLayout()->addLayout(quickGrid);
    m_requestLog =
        makeLabel(QStringLiteral("请求已记录；未连接设备。"), QStringLiteral("mutedLabel"));
    m_requestLog->setWordWrap(true);
    quick->contentLayout()->addWidget(m_requestLog);
    connect(arm, &QPushButton::clicked, this,
            [this]()
            {
                emit armRequested();
                logRequest(QStringLiteral("请求解锁电机"));
            });
    connect(disarm, &QPushButton::clicked, this,
            [this]()
            {
                emit disarmRequested();
                logRequest(QStringLiteral("请求停用电机"));
            });
    connect(hold, &QPushButton::clicked, this,
            [this]()
            {
                emit holdPositionRequested();
                logRequest(QStringLiteral("请求保持位置"));
            });
    connect(surface, &QPushButton::clicked, this,
            [this]()
            {
                emit surfaceRequested();
                logRequest(QStringLiteral("请求紧急上浮"));
            });
    bottomRow->addWidget(quick, 4);
    root->addLayout(bottomRow);

    setSnapshot(preview);
}

void DashboardPage::setSnapshot(const DashboardSnapshot &snapshot)
{
    m_snapshot = snapshot;
    refreshView();
}

void DashboardPage::refreshView()
{
    m_depthValue->setText(QStringLiteral("%1 m").arg(m_snapshot.depthM, 0, 'f', 1));
    m_rollValue->setText(QStringLiteral("%1°").arg(m_snapshot.rollDeg, 0, 'f', 1));
    m_pitchValue->setText(QStringLiteral("%1°").arg(m_snapshot.pitchDeg, 0, 'f', 1));
    m_yawValue->setText(QStringLiteral("%1°").arg(m_snapshot.yawDeg, 0, 'f', 1));
    m_voltageValue->setText(QStringLiteral("%1 V").arg(m_snapshot.busVoltageV, 0, 'f', 1));
    m_modeValue->setText(m_snapshot.robotMode);
    m_armValue->setText(m_snapshot.armed ? QStringLiteral("已解锁") : QStringLiteral("已停用"));
    m_leakValue->setText(m_snapshot.leakDetected ? QStringLiteral("报警") : QStringLiteral("正常"));
    m_temperatureValue->setText(
        QStringLiteral("%1 °C").arg(m_snapshot.internalTemperatureC, 0, 'f', 1));
    if (m_controlPermission != nullptr)
    {
        m_controlPermission->setText(m_snapshot.canControl ? QStringLiteral("允许")
                                                           : QStringLiteral("仅演示"));
        m_controlPermission->setObjectName(m_snapshot.canControl ? QStringLiteral("statusGood")
                                                                 : QStringLiteral("statusWarn"));
        m_controlPermission->style()->unpolish(m_controlPermission);
        m_controlPermission->style()->polish(m_controlPermission);
    }
    m_alarmValue->setText(m_snapshot.alarmCount == 0
                              ? QStringLiteral("无活动报警")
                              : QStringLiteral("%1 条活动报警").arg(m_snapshot.alarmCount));
}

void DashboardPage::logRequest(const QString &message)
{
    if (m_requestLog != nullptr)
    {
        m_requestLog->setText(QStringLiteral("最近请求：%1 · 仅演示").arg(message));
    }
}

} // namespace rov
