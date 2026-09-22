#include "pages/manipulator/ManipulatorPage.h"

#include "ui/common/AppSlider.h"
#include "ui/common/UiPrimitives.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace
{

class ArmView final : public QWidget
{
  public:
    explicit ArmView(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(350, 300);
    }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), Qt::white);
        const QPointF base(width() * 0.24, height() * 0.56);
        const QPointF shoulder(width() * 0.43, height() * 0.41);
        const QPointF elbow(width() * 0.66, height() * 0.52);
        const QPointF wrist(width() * 0.82, height() * 0.70);
        painter.setPen(QPen(QColor(QStringLiteral("#293b4d")), 14, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(base, shoulder);
        painter.drawLine(shoulder, elbow);
        painter.drawLine(elbow, wrist);
        painter.setPen(QPen(QColor(QStringLiteral("#1687ee")), 2, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(base, shoulder);
        painter.drawLine(shoulder, elbow);
        painter.drawLine(elbow, wrist);
        painter.setBrush(QColor(QStringLiteral("#dce5ee")));
        painter.setPen(QPen(QColor(QStringLiteral("#516b87")), 2));
        painter.drawEllipse(base, 25, 25);
        painter.drawEllipse(shoulder, 19, 19);
        painter.drawEllipse(elbow, 17, 17);
        painter.drawEllipse(wrist, 14, 14);
        painter.setBrush(QColor(QStringLiteral("#218fe7")));
        painter.drawEllipse(shoulder, 5, 5);
        painter.drawEllipse(elbow, 5, 5);
        painter.drawEllipse(wrist, 4, 4);
        painter.setPen(QPen(QColor(QStringLiteral("#1f5d99")), 1.3));
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 10, QFont::DemiBold));
        painter.drawText(shoulder + QPointF(14, -18), QStringLiteral("肩部  12.4°"));
        painter.drawText(elbow + QPointF(12, -12), QStringLiteral("肘部  -35.2°"));
        painter.drawText(wrist + QPointF(-18, 38), QStringLiteral("腕部  18.6°"));
        painter.drawText(QRectF(25, 38, 130, 20), Qt::AlignLeft, QStringLiteral("夹爪：打开"));
        painter.setPen(QPen(QColor(QStringLiteral("#7890aa")), 1.2));
        painter.drawText(QRectF(25, height() - 48, 150, 20), Qt::AlignLeft,
                         QStringLiteral("→ 前方（X）"));
        painter.drawText(QRectF(25, height() - 25, 150, 20), Qt::AlignLeft,
                         QStringLiteral("↓ 下方（Z）"));
    }
};

} // namespace

namespace rov
{

ManipulatorPage::ManipulatorPage(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(10);
    root->addWidget(makePageHeader(QStringLiteral("机械臂"),
                                   QStringLiteral("平面机械臂控制与状态。"),
                                   QStringLiteral("设备离线")));

    auto *mainRow = new QHBoxLayout;
    mainRow->setSpacing(12);
    auto *viewCard = new CardWidget(QStringLiteral("机械臂总览（侧视图）"), IconKind::Manipulator);
    viewCard->contentLayout()->addWidget(new ArmView);
    mainRow->addWidget(viewCard, 4);

    auto *right = new QVBoxLayout;
    right->setSpacing(12);
    auto *cartesian =
        new CardWidget(QStringLiteral("末端执行器控制（笛卡尔坐标）"), IconKind::Action);
    auto *cartesianGrid = new QGridLayout;
    cartesianGrid->setSpacing(10);
    const QStringList axes = {QStringLiteral("X - 前伸"), QStringLiteral("Z - 垂直（上/下）")};
    for (int i = 0; i < axes.size(); ++i)
    {
        auto *axis = new QFrame;
        axis->setObjectName(QStringLiteral("card"));
        auto *axisLayout = new QVBoxLayout(axis);
        axisLayout->addWidget(makeLabel(axes.at(i), QStringLiteral("bodyValue")));
        auto *values = new QHBoxLayout;
        values->addWidget(makeLabel(QStringLiteral("目标\n--"), QStringLiteral("bodyValue")));
        values->addWidget(makeLabel(QStringLiteral("实际\n--"), QStringLiteral("bodyValue")));
        values->addWidget(makeLabel(QStringLiteral("误差\n--"), QStringLiteral("bodyValue")));
        axisLayout->addLayout(values);
        auto *slider = new AppSlider(Qt::Horizontal);
        slider->setRange(0, 100);
        slider->setValue(0);
        slider->setEnabled(false);
        axisLayout->addWidget(slider);
        cartesianGrid->addWidget(axis, 0, i);
    }
    cartesian->contentLayout()->addLayout(cartesianGrid);
    right->addWidget(cartesian);

    auto *jointCard = new CardWidget(QStringLiteral("关节控制"), IconKind::Manipulator);
    m_jointTable = new QTableWidget(0, 6);
    m_jointTable->setHorizontalHeaderLabels({QStringLiteral("关节"), QStringLiteral("启用"),
                                             QStringLiteral("目标"), QStringLiteral("实际"),
                                             QStringLiteral("限位"), QStringLiteral("温度")});
    m_jointTable->horizontalHeader()->setStretchLastSection(true);
    m_jointTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_jointTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_jointTable->setColumnWidth(1, 78);
    m_jointTable->setColumnWidth(2, 92);
    m_jointTable->setColumnWidth(3, 92);
    m_jointTable->setColumnWidth(4, 140);
    m_jointTable->setColumnWidth(5, 82);
    m_jointTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_jointTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    jointCard->contentLayout()->addWidget(m_jointTable);
    right->addWidget(jointCard, 2);
    mainRow->addLayout(right, 6);
    root->addLayout(mainRow, 1);

    auto *bottom = new QHBoxLayout;
    auto *status = new CardWidget(QStringLiteral("机械臂状态"), IconKind::Status);
    auto *statusGrid = new QGridLayout;
    m_systemState = makeStatusPill(QStringLiteral("离线"), QStringLiteral("statusWarn"));
    m_controlMode = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    m_faultStatus = makeStatusPill(QStringLiteral("--"), QStringLiteral("statusWarn"));
    statusGrid->addWidget(makeMetricLabel(QStringLiteral("系统状态")), 0, 0);
    statusGrid->addWidget(m_systemState, 0, 1);
    statusGrid->addWidget(makeMetricLabel(QStringLiteral("控制模式")), 1, 0);
    statusGrid->addWidget(m_controlMode, 1, 1);
    statusGrid->addWidget(makeMetricLabel(QStringLiteral("归零状态")), 2, 0);
    statusGrid->addWidget(makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue")), 2, 1);
    statusGrid->addWidget(makeMetricLabel(QStringLiteral("故障状态")), 3, 0);
    statusGrid->addWidget(m_faultStatus, 3, 1);
    status->contentLayout()->addLayout(statusGrid);
    bottom->addWidget(status, 3);

    auto *actions = new CardWidget(QStringLiteral("快捷操作"), IconKind::Action);
    auto *actionGrid = new QGridLayout;
    auto *home = makeButton(QStringLiteral("机械臂归零"), QStringLiteral("primaryButton"));
    auto *stop = makeButton(QStringLiteral("停止运动"), QStringLiteral("dangerButton"));
    auto *open = makeButton(QStringLiteral("打开夹爪"), QStringLiteral("softButton"));
    auto *close = makeButton(QStringLiteral("关闭夹爪"), QStringLiteral("softButton"));
    actionGrid->addWidget(home, 0, 0);
    actionGrid->addWidget(stop, 0, 1);
    actionGrid->addWidget(open, 1, 0);
    actionGrid->addWidget(close, 1, 1);
    actions->contentLayout()->addLayout(actionGrid);
    bottom->addWidget(actions, 3);

    auto *settings = new CardWidget(QStringLiteral("运动设置"), IconKind::Settings);
    auto *settingsLayout = new QVBoxLayout;
    const QStringList settingsNames = {QStringLiteral("运动速度"), QStringLiteral("位置容差"),
                                       QStringLiteral("笛卡尔速度")};
    for (const QString &name : settingsNames)
    {
        auto *row = new QHBoxLayout;
        row->addWidget(makeMetricLabel(name));
        auto *slider = new AppSlider(Qt::Horizontal);
        slider->setValue(50);
        row->addWidget(slider);
        settingsLayout->addLayout(row);
    }
    settings->contentLayout()->addLayout(settingsLayout);
    bottom->addWidget(settings, 3);
    root->addLayout(bottom);

    connect(home, &QPushButton::clicked, this,
            [this]()
            {
                emit homeRequested();
                logRequest(QStringLiteral("请求机械臂归零"));
            });
    connect(stop, &QPushButton::clicked, this,
            [this]()
            {
                emit stopRequested();
                logRequest(QStringLiteral("请求停止运动"));
            });
    connect(open, &QPushButton::clicked, this,
            [this]()
            {
                emit gripperRequested(GripperRequest{true});
                logRequest(QStringLiteral("请求打开夹爪"));
            });
    connect(close, &QPushButton::clicked, this,
            [this]()
            {
                emit gripperRequested(GripperRequest{false});
                logRequest(QStringLiteral("请求关闭夹爪"));
            });
    m_requestLog =
        makeLabel(QStringLiteral("请求已记录；未连接设备。"), QStringLiteral("mutedLabel"));
    status->contentLayout()->addWidget(m_requestLog);

    ManipulatorSnapshot initialSnapshot;
    initialSnapshot.armStamp.freshness = DataFreshness::Offline;
    initialSnapshot.systemState = QStringLiteral("离线");
    initialSnapshot.controlUnavailableReason = QStringLiteral("机械臂设备未连接");
    setSnapshot(initialSnapshot);
}

void ManipulatorPage::setSnapshot(const ManipulatorSnapshot &snapshot)
{
    m_snapshot = snapshot;
    refreshView();
}

void ManipulatorPage::refreshView()
{
    if (m_jointTable == nullptr)
    {
        return;
    }
    const bool available = m_snapshot.armStamp.validity == DataValidity::Valid &&
                           m_snapshot.armStamp.freshness != DataFreshness::Offline;
    m_systemState->setText(available && !m_snapshot.systemState.isEmpty()
                               ? m_snapshot.systemState
                               : QStringLiteral("离线"));
    m_controlMode->setText(available && !m_snapshot.controlMode.isEmpty()
                               ? m_snapshot.controlMode
                               : QStringLiteral("--"));
    m_faultStatus->setText(available && !m_snapshot.faultStatus.isEmpty()
                               ? m_snapshot.faultStatus
                               : QStringLiteral("--"));
    m_jointTable->setRowCount(m_snapshot.joints.size());
    for (int row = 0; row < m_snapshot.joints.size(); ++row)
    {
        const auto &joint = m_snapshot.joints.at(row);
        m_jointTable->setItem(row, 0, new QTableWidgetItem(joint.label));
        m_jointTable->setItem(row, 1,
                              new QTableWidgetItem(joint.enabled ? QStringLiteral("已启用")
                                                                 : QStringLiteral("已停用")));
        m_jointTable->setItem(
            row, 2, new QTableWidgetItem(QStringLiteral("%1°").arg(joint.targetDeg, 0, 'f', 1)));
        m_jointTable->setItem(
            row, 3, new QTableWidgetItem(QStringLiteral("%1°").arg(joint.actualDeg, 0, 'f', 1)));
        m_jointTable->setItem(row, 4,
                              new QTableWidgetItem(QStringLiteral("%1° ~ %2°")
                                                       .arg(joint.minDeg, 0, 'f', 0)
                                                       .arg(joint.maxDeg, 0, 'f', 0)));
        m_jointTable->setItem(
            row, 5,
            new QTableWidgetItem(QStringLiteral("%1 °C").arg(joint.temperatureC, 0, 'f', 0)));
    }
}

void ManipulatorPage::logRequest(const QString &message)
{
    if (m_requestLog != nullptr)
    {
        m_requestLog->setText(QStringLiteral("最近请求：%1 · 设备离线，未下发").arg(message));
    }
}

} // namespace rov
