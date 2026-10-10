#include "pages/dashboard/SensorPanel.h"
#include "ui/common/AppComboBox.h"
#include "ui/common/UiPrimitives.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <cmath>

namespace rov
{
namespace
{
QTableWidget *readout(const QStringList &names, const QStringList &units, QWidget *parent)
{
    auto *table = new QTableWidget(names.size(), 3, parent);
    table->setHorizontalHeaderLabels({QStringLiteral("变量"), QStringLiteral("反馈值"), QStringLiteral("单位")});
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(27);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setMinimumHeight(32 + names.size() * 27);
    for (int i = 0; i < names.size(); ++i)
    {
        table->setItem(i, 0, new QTableWidgetItem(names.at(i)));
        table->setItem(i, 1, new QTableWidgetItem(QStringLiteral("--")));
        table->setItem(i, 2, new QTableWidgetItem(units.at(i)));
        table->item(i, 1)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    return table;
}
void displayValue(QTableWidget *table, int row, bool valid, double value, int decimals = 3)
{
    table->item(row, 1)->setText(valid && std::isfinite(value) ? QString::number(value, 'f', decimals) : QStringLiteral("--"));
}
QDoubleSpinBox *decimalEditor(double minimum, double maximum, double value, int decimals)
{
    auto *spin = new QDoubleSpinBox;
    spin->setDecimals(decimals); spin->setRange(minimum, maximum); spin->setValue(value);
    spin->setMinimumWidth(110);
    return spin;
}
QSpinBox *integerEditor(int minimum, int maximum, int value)
{
    auto *spin = new QSpinBox;
    spin->setRange(minimum, maximum); spin->setValue(value); spin->setMinimumWidth(110);
    return spin;
}
QString deviceInfo(const SensorDeviceState &d)
{
    return d.infoKnown ? QStringLiteral("%1   |   固件：%2   |   能力：0x%3")
                            .arg(d.name, d.firmware).arg(d.capabilities, 8, 16, QLatin1Char('0'))
                       : QStringLiteral("设备信息尚未读取；不根据默认值假设设备能力");
}
QString ageText(qint64 age)
{
    return age < 0 ? QStringLiteral("尚无样本") : QStringLiteral("样本龄 %1 ms").arg(age);
}
QString depthLastErrorText(const quint32 status)
{
    const quint32 code = (status >> SensorStatus::LastErrorShift) & 0xFFU;
    if (code == 0) return {};
    const quint32 command = (status >> SensorStatus::LastI2cCommandShift) & 0xFFU;
    QString description;
    switch (code)
    {
    case 1: description = QStringLiteral("参数错误"); break;
    case 2: description = QStringLiteral("设备未就绪 / 初始化失败"); break;
    case 3: description = QStringLiteral("I²C 总线忙"); break;
    case 4: description = QStringLiteral("I²C 超时"); break;
    case 5: description = QStringLiteral("I²C 错误或设备未应答"); break;
    case 6: description = QStringLiteral("PROM CRC 校验失败"); break;
    case 7: description = QStringLiteral("尚无有效水面基准"); break;
    case 8: description = QStringLiteral("尚无完成的采样"); break;
    case 9: description = QStringLiteral("操作不支持"); break;
    default: description = QStringLiteral("未知驱动错误"); break;
    }
    const QString errorText = QStringLiteral("最近错误 %1：%2").arg(code).arg(description);
    if (code < 3 || code > 6) return errorText;
    const QString commandText = QString::number(command, 16).rightJustified(2, QLatin1Char('0')).toUpper();
    return errorText + QStringLiteral(" · 关联 I²C 命令 0x") + commandText;
}
QString depthDriverPhaseText(const quint32 status)
{
    if (!(status & SensorStatus::DepthDiagnosticsValid)) return {};
    const quint32 state = (status & SensorStatus::DepthDriverStateMask)
        >> SensorStatus::DepthDriverStateShift;
    const quint32 phase = (status & SensorStatus::DepthI2cPhaseMask)
        >> SensorStatus::DepthI2cPhaseShift;
    const QStringList states = {QStringLiteral("离线"), QStringLiteral("等待复位"),
        QStringLiteral("读取 PROM"), QStringLiteral("空闲"), QStringLiteral("压力转换"),
        QStringLiteral("温度转换"), QStringLiteral("等待旧转换结束"), QStringLiteral("等待 I²C 回调")};
    const QStringList phases = {QStringLiteral("I²C 空闲"), QStringLiteral("I²C 事务进行中"),
        QStringLiteral("I²C 结果待取"), QStringLiteral("I²C 阶段未知")};
    return QStringLiteral("驱动阶段：%1 · %2")
        .arg(states.at(static_cast<int>(state)), phases.at(static_cast<int>(phase)));
}
QString streamText(const SensorStreamState state)
{
    switch (state)
    {
    case SensorStreamState::Running: return QStringLiteral("上传已开启（设备确认）");
    case SensorStreamState::Stopped: return QStringLiteral("测量上传已停止，底层仍采样");
    case SensorStreamState::Unknown: return QStringLiteral("上传开关尚未确认");
    }
    return {};
}
}

SensorPanel::SensorPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("sensorPanel"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0); root->setSpacing(8);
    auto *heading = makeLabel(QStringLiteral("IMU / 深度计 · 详情与配置"), QStringLiteral("bodyValue"));
    heading->setObjectName(QStringLiteral("sensorDetailsHeading"));
    auto *headingRow = new QHBoxLayout;
    headingRow->addWidget(heading); headingRow->addStretch();
    auto *collapse = makeButton(QStringLiteral("收起，返回总览"), QStringLiteral("softButton"));
    collapse->setObjectName(QStringLiteral("sensorPanelCollapse"));
    connect(collapse, &QPushButton::clicked, this, &SensorPanel::collapseRequested);
    headingRow->addWidget(collapse); root->addLayout(headingRow);
    m_connection = makeLabel(QStringLiteral("网关未连接，请在设置页连接 USB CDC"), QStringLiteral("mutedLabel"));
    m_connection->setObjectName(QStringLiteral("sensorConnectionStatus"));
    root->addWidget(m_connection);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("sensorTabs"));
    root->addWidget(tabs);

    auto *imu = new QWidget;
    auto *il = new QVBoxLayout(imu); il->setSpacing(10);
    auto *imuDevice = new CardWidget(QStringLiteral("主 IMU · USART1"), IconKind::Status);
    m_imuInfo = makeLabel(QString(), QStringLiteral("mutedLabel")); m_imuInfo->setTextFormat(Qt::PlainText);
    m_imuInfo->setObjectName(QStringLiteral("imuDeviceInfo")); m_imuInfo->setWordWrap(true);
    m_imuStatus = makeLabel(QString()); m_imuStatus->setWordWrap(true);
    m_imuStatus->setObjectName(QStringLiteral("imuDeviceStatus"));
    m_imuCommand = makeLabel(QString()); m_imuCommand->setWordWrap(true);
    m_imuCommand->setObjectName(QStringLiteral("imuCommandResult"));
    auto *imuActions = new QHBoxLayout;
    imuActions->addWidget(actionButton(QStringLiteral("读取信息"), kImuSensor, SensorOperation::GetInfo));
    imuActions->addWidget(actionButton(QStringLiteral("查询状态"), kImuSensor, SensorOperation::GetStatus));
    imuActions->addWidget(actionButton(QStringLiteral("开始上传"), kImuSensor, SensorOperation::StartStream));
    imuActions->addWidget(actionButton(QStringLiteral("停止上传"), kImuSensor, SensorOperation::StopStream));
    imuActions->addStretch();
    imuDevice->contentLayout()->addWidget(m_imuInfo);
    imuDevice->contentLayout()->addWidget(m_imuStatus);
    imuDevice->contentLayout()->addLayout(imuActions);
    imuDevice->contentLayout()->addWidget(m_imuCommand);
    il->addWidget(imuDevice);
    auto *measurements = new QHBoxLayout;
    auto *rawCard = new CardWidget(QStringLiteral("加速度 / 角速度 / 磁场"), IconKind::Waveform);
    m_rawTable = readout({"Ax", "Ay", "Az", "Gx", "Gy", "Gz", "Mx", "My", "Mz"},
        {"g", "g", "g", "rad/s", "rad/s", "rad/s", QStringLiteral("协议单位"), QStringLiteral("协议单位"), QStringLiteral("协议单位")}, rawCard);
    m_rawTable->setObjectName(QStringLiteral("imuRawReadout"));
    rawCard->contentLayout()->addWidget(m_rawTable);
    auto *attCard = new CardWidget(QStringLiteral("IMU 自身姿态 · 非整机融合"), IconKind::Dashboard);
    m_attitudeTable = readout({"Roll", "Pitch", "Yaw", "Qw", "Qx", "Qy", "Qz"},
        {QStringLiteral("°"), QStringLiteral("°"), QStringLiteral("°"), "", "", "", ""}, attCard);
    m_attitudeTable->setObjectName(QStringLiteral("imuAttitudeReadout"));
    attCard->contentLayout()->addWidget(m_attitudeTable);
    auto *timeNote = makeLabel(QStringLiteral("时间戳来自 H750 收齐数据帧的时刻；不是 IMU 原生采样时间。磁场物理单位待厂家确认。"), QStringLiteral("mutedLabel"));
    timeNote->setWordWrap(true); attCard->contentLayout()->addWidget(timeNote);
    measurements->addWidget(rawCard, 1); measurements->addWidget(attCard, 1); il->addLayout(measurements);

    auto *imuConfig = new CardWidget(QStringLiteral("IMU 参数 · 请求值与设备反馈分开"), IconKind::Settings);
    auto *ig = new QGridLayout;
    addParameter(ig, 0, kImuSensor, 0x0001, QStringLiteral("输出频率 / Hz"), integerEditor(10, 100, 25), 5);
    auto *algorithm = new AppComboBox; m_imuAlgorithm = algorithm;
    algorithm->addItem(QStringLiteral("未选择（不下发）"), QVariant());
    algorithm->addItem(QStringLiteral("六轴算法"), 6); algorithm->addItem(QStringLiteral("九轴算法"), 9);
    addParameter(ig, 1, kImuSensor, 0x0003, QStringLiteral("算法模式"), algorithm, 6);
    connect(algorithm, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]() { setSnapshot(m_snapshot); }); // Enable only a real selection; never send here.
    imuConfig->contentLayout()->addLayout(ig);
    auto *imuNote = makeLabel(QStringLiteral("读取仅返回下发缓存，不代表设备确认。设置频率后自动观察约 2 s 原始流；合并姿态流可能约为其两倍。此设备不使用硬件校准，不开放清除、保存和恢复操作。"), QStringLiteral("mutedLabel"));
    imuNote->setWordWrap(true); imuConfig->contentLayout()->addWidget(imuNote); il->addWidget(imuConfig);
    // Reuse the existing IMU configuration area. Hardware calibration/reset controls are absent,
    // regardless of firmware capability bits 7/8; software calibration is a separate host workflow.
    m_imuRateStatus = makeLabel(QString(), QStringLiteral("mutedLabel"));
    m_imuRateStatus->setObjectName(QStringLiteral("imuRateObservation"));
    m_imuRateStatus->setWordWrap(true); m_imuRateStatus->setTextFormat(Qt::PlainText);
    imuConfig->contentLayout()->addWidget(m_imuRateStatus);
    il->addStretch();
    tabs->addTab(imu, QStringLiteral("IMU"));

    auto *depth = new QWidget;
    auto *dl = new QVBoxLayout(depth); dl->setSpacing(10);
    auto *depthDevice = new CardWidget(QStringLiteral("深度计 · I2C3 / PA8 + PC9"), IconKind::Status);
    m_depthInfo = makeLabel(QString(), QStringLiteral("mutedLabel")); m_depthInfo->setTextFormat(Qt::PlainText);
    m_depthInfo->setObjectName(QStringLiteral("depthDeviceInfo")); m_depthInfo->setWordWrap(true);
    m_depthStatus = makeLabel(QString()); m_depthStatus->setWordWrap(true);
    m_depthStatus->setObjectName(QStringLiteral("depthDeviceStatus"));
    m_depthCommand = makeLabel(QString()); m_depthCommand->setWordWrap(true);
    m_depthCommand->setObjectName(QStringLiteral("depthCommandResult"));
    auto *da = new QHBoxLayout;
    auto *depthInfoRead = actionButton(QStringLiteral("读取信息"), kDepthSensor, SensorOperation::GetInfo);
    depthInfoRead->setToolTip(QStringLiteral("读取描述后自动逐项读取状态与五个参数；只读，不写入编辑框默认值。"));
    da->addWidget(depthInfoRead);
    da->addWidget(actionButton(QStringLiteral("查询状态"), kDepthSensor, SensorOperation::GetStatus));
    da->addWidget(actionButton(QStringLiteral("开始上传"), kDepthSensor, SensorOperation::StartStream));
    da->addWidget(actionButton(QStringLiteral("停止上传"), kDepthSensor, SensorOperation::StopStream));
    da->addWidget(actionButton(QStringLiteral("采集水面零点"), kDepthSensor, SensorOperation::ZeroDepth, 10, true));
    da->addStretch();
    depthDevice->contentLayout()->addWidget(m_depthInfo); depthDevice->contentLayout()->addWidget(m_depthStatus);
    depthDevice->contentLayout()->addLayout(da); depthDevice->contentLayout()->addWidget(m_depthCommand); dl->addWidget(depthDevice);
    auto *depthData = new CardWidget(QStringLiteral("压力、温度与水深"), IconKind::Waveform);
    m_depthTable = readout({QStringLiteral("绝对压力"), QStringLiteral("传感器温度"), QStringLiteral("原始水深（正向下）"),
        QStringLiteral("滤波水深"), QStringLiteral("水面参考压力 P0"), QStringLiteral("压力 ADC D1"), QStringLiteral("温度 ADC D2")},
        {"Pa", QStringLiteral("°C"), "m", "m", "Pa", "count", "count"}, depthData);
    m_depthTable->setObjectName(QStringLiteral("depthReadout"));
    depthData->contentLayout()->addWidget(m_depthTable); dl->addWidget(depthData);
    auto *dc = new CardWidget(QStringLiteral("深度计参数 · 当前仅保存在 H750 RAM"), IconKind::Settings);
    auto *dg = new QGridLayout;
    m_depthRate = integerEditor(1, depth02baMaxRateHz(4096), 25);
    addParameter(dg, 0, kDepthSensor, 0x0001, QStringLiteral("输出频率 / Hz"), m_depthRate, 11);
    auto *osr = new AppComboBox; m_depthOsr = osr;
    for (int n : {256, 512, 1024, 2048, 4096, 8192})
        osr->addItem(QStringLiteral("%1（1–%2 Hz）").arg(n).arg(depth02baMaxRateHz(n)), n);
    osr->setCurrentIndex(osr->findData(4096));
    addParameter(dg, 1, kDepthSensor, 0x0101, QStringLiteral("过采样 OSR"), osr, 11);
    addParameter(dg, 2, kDepthSensor, 0x0102, QStringLiteral("水密度 / kg/m³"), decimalEditor(900, 1300, 1029, 2), 11);
    addParameter(dg, 3, kDepthSensor, 0x0104, QStringLiteral("滤波 K（越大越平滑）"), decimalEditor(0, 0.99, 0, 2), 11);
    dc->contentLayout()->addLayout(dg);
    m_depthSamplingNote = makeLabel(QString(), QStringLiteral("mutedLabel"));
    m_depthSamplingNote->setObjectName(QStringLiteral("depthSamplingConstraint"));
    m_depthSamplingNote->setWordWrap(true); dc->contentLayout()->addWidget(m_depthSamplingNote);
    connect(osr, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updateDepthRateLimit(); });
    updateDepthRateLimit();
    dl->addWidget(dc); dl->addStretch();
    tabs->addTab(depth, QStringLiteral("深度计"));
    setSnapshot(SensorSnapshot{});
}

void SensorPanel::updateDepthRateLimit()
{
    const int osr = m_depthOsr->currentData().toInt();
    const int maximum = depth02baMaxRateHz(osr);
    const int previous = m_depthRate->value();
    m_depthRate->setRange(1, qMax(1, maximum)); // Also constrains keyboard, paste, wheel and arrows.
    const QString limit = QStringLiteral("02BA · OSR %1：只允许 1–%2 Hz（整数）").arg(osr).arg(maximum);
    m_depthRate->setToolTip(limit);
    m_depthSamplingNote->setText(limit
        + (previous > maximum ? QStringLiteral("；待应用频率已自动降到 %1 Hz").arg(maximum) : QString())
        + QStringLiteral("。OSR/频率为一组，任一“应用组合”均提交两项；自动排序并逐步确认。选择不发命令，反馈列才是设备值。保留设备已有水面参考；仅在确有需要、探头位于水面时手动采集零点。"));
}

bool SensorPanel::confirmAction(const QString &message)
{
    // Independent modal window: this page may be hosted in QGraphicsProxyWidget.
    QMessageBox box;
    box.setWindowTitle(QStringLiteral("确认传感器操作")); box.setIcon(QMessageBox::Warning);
    box.setText(message); box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No); box.setWindowModality(Qt::ApplicationModal);
    box.setWindowFlag(Qt::Window, true);
    return box.exec() == QMessageBox::Yes;
}
QPushButton *SensorPanel::actionButton(const QString &name, quint8 target, SensorOperation operation,
    int capability, bool confirmation, bool deviceAction, quint8 calType, quint8 calAction)
{
    auto *b = makeButton(name, confirmation ? QStringLiteral("softButton") : QStringLiteral("primaryButton"));
    b->setObjectName(QStringLiteral("sensor%1Command%2").arg(target).arg(quint8(operation), 2, 16, QLatin1Char('0')));
    b->setProperty("needsPressure", operation == SensorOperation::ZeroDepth);
    b->setProperty("needsImuOnline", operation == SensorOperation::Calibrate);
    m_controls.append({b, target, capability, deviceAction});
    connect(b, &QPushButton::clicked, this, [this, name, target, operation, confirmation, calType, calAction]() {
        QString warning = QStringLiteral("执行“%1”？此操作只发送一次，不会自动重试。").arg(name);
        if (operation == SensorOperation::ZeroDepth)
            warning += QStringLiteral("\n必须把探头置于水面参考位置；在水下归零会改变深度基准。请不要在定深控制中执行。");
        if (confirmation && !confirmAction(warning)) return;
        SensorRequest r; r.target = target; r.operation = operation;
        r.calibrationType = calType; r.calibrationAction = calAction;
        emit requestIssued(r);
    });
    return b;
}
void SensorPanel::addParameter(QGridLayout *grid, int row, quint8 target, quint16 id,
                              const QString &name, QWidget *editor, int capability)
{
    editor->setObjectName(QStringLiteral("sensor%1Param%2Editor").arg(target).arg(id, 4, 16, QLatin1Char('0')));
    auto *feedback = makeLabel(QStringLiteral("未读取"), QStringLiteral("mutedLabel"));
    feedback->setTextFormat(Qt::PlainText); feedback->setWordWrap(true);
    feedback->setObjectName(QStringLiteral("sensor%1Param%2Feedback").arg(target).arg(id, 4, 16, QLatin1Char('0')));
    auto *read = makeButton(QStringLiteral("读取"), QStringLiteral("softButton"));
    const bool samplingPair = target == kDepthSensor && (id == 0x0001 || id == 0x0101);
    auto *write = makeButton(samplingPair ? QStringLiteral("应用组合") : QStringLiteral("应用"), QStringLiteral("primaryButton"));
    write->setProperty("needsAlgorithm", target == kImuSensor && id == 0x0003);
    if (samplingPair) write->setToolTip(QStringLiteral("同时应用左侧选定的 OSR 和频率，自动安排下发顺序并回读确认。"));
    read->setObjectName(QStringLiteral("sensor%1Param%2Read").arg(target).arg(id, 4, 16, QLatin1Char('0')));
    write->setObjectName(QStringLiteral("sensor%1Param%2Write").arg(target).arg(id, 4, 16, QLatin1Char('0')));
    grid->addWidget(makeLabel(name), row, 0); grid->addWidget(editor, row, 1);
    grid->addWidget(feedback, row, 2); grid->addWidget(read, row, 3); grid->addWidget(write, row, 4);
    grid->setColumnStretch(2, 1); grid->setHorizontalSpacing(10); grid->setVerticalSpacing(8);
    m_controls.append({read, target, capability, false});
    m_controls.append({write, target, capability, target == kImuSensor});
    m_parameters.append({target, id, feedback, editor});
    connect(read, &QPushButton::clicked, this, [this, target, id]() {
        SensorRequest r; r.target = target; r.operation = SensorOperation::GetParameter; r.parameterId = id;
        emit requestIssued(r);
    });
    connect(write, &QPushButton::clicked, this, [this, target, id, editor, name, samplingPair]() {
        if (id == 0x0103 && !confirmAction(QStringLiteral("应用“%1”？\n请核对水面基准；配置仅保存在 RAM。").arg(name))) return;
        SensorRequest r; r.target = target; r.operation = SensorOperation::SetParameter; r.parameterId = id;
        if (samplingPair)
        {
            m_depthRate->interpretText();
            r.samplingOsr = quint16(m_depthOsr->currentData().toUInt());
            r.samplingRateHz = quint16(m_depthRate->value());
            r.value = id == 0x0001 ? r.samplingRateHz : r.samplingOsr;
        }
        else if (auto *combo = qobject_cast<QComboBox *>(editor)) r.value = combo->currentData();
        else if (auto *spin = qobject_cast<QSpinBox *>(editor)) { spin->interpretText(); r.value = spin->value(); }
        else if (auto *decimal = qobject_cast<QDoubleSpinBox *>(editor)) r.value = decimal->value();
        if (!r.value.isValid()) return; // The algorithm placeholder is not a six-axis default.
        emit requestIssued(r);
    });
}

void SensorPanel::setSnapshot(const SensorSnapshot &s)
{
    const bool reset = m_snapshot.connected && !s.connected;
    m_snapshot = s;
    m_connection->setText(s.connected ? QStringLiteral("网关已连接 · 传感器使用同一 USB CDC 链路；上传开关不停止底层采样")
                                     : QStringLiteral("网关未连接，请在设置页连接 USB CDC"));
    const auto &i = s.devices[0]; const auto &d = s.devices[1];
    m_imuInfo->setText(deviceInfo(i) + QStringLiteral("   |   型号不可读，不推断六/九轴"));
    m_depthInfo->setText(deviceInfo(d) + QStringLiteral("   |   固定型号：MS5837-02BA"));
    m_imuStatus->setText((i.status & SensorStatus::PinBlocked)
        ? QStringLiteral("%1 · PA9 配置发送受保护/占用（PIN_BLOCKED），只读与接收可用 · %2 · 接收 %3 / 错误 %4")
            .arg(i.online ? QStringLiteral("IMU 接收在线") : QStringLiteral("IMU 尚无有效接收"), ageText(s.rawAgeMs))
            .arg(i.goodFrames).arg(i.errors)
        : QStringLiteral("%1 · %2 · 姿态%3 · 接收 %4 / 错误 %5")
            .arg(i.online ? QStringLiteral("IMU 在线") : QStringLiteral("IMU 离线 / 尚无数据"), ageText(s.rawAgeMs), ageText(s.attitudeAgeMs))
            .arg(i.goodFrames).arg(i.errors));
    m_imuStatus->setText(m_imuStatus->text()
        + QStringLiteral("\n原始%1 · 四元数%2 · 欧拉角%3").arg(ageText(s.rawAgeMs), ageText(s.quaternionAgeMs), ageText(s.attitudeAgeMs))
        + (i.statusKnown ? QStringLiteral(" · 原始采样 %1 · 设备%2").arg(i.sampleSequence).arg(ageText(i.sampleAgeMs)) : QString()));
    m_imuStatus->setToolTip(QStringLiteral("状态 0x%1；CONFIG_UNKNOWN 仅表示配置缓存未知，不抹掉有效测量。\n原始帧时刻 %2 µs；合并姿态帧时刻 %3 µs。三组有效位独立，合并帧不提供各组独立原生时间戳。")
        .arg(i.status,8,16,QLatin1Char('0')).arg(s.rawTimestampUs).arg(s.attitudeTimestampUs));
    const auto rateText = [](double hz) { return hz >= 0 ? QString::number(hz,'f',1) + QStringLiteral(" Hz") : QStringLiteral("--"); };
    m_imuRateStatus->setText(QStringLiteral("近 2 s 实收：原始 %1 · 合并姿态 %2\n最近频率设置：%3")
        .arg(rateText(s.imuRawRateHz), rateText(s.imuAttitudeRateHz), s.imuRateMessage));
    m_imuCommand->setToolTip(i.lastRequestHex.isEmpty() ? QStringLiteral("尚无命令报文")
        : QStringLiteral("最近请求编码（提交帧，不代表发送成功）：\n%1\n匹配回复 RX：\n%2").arg(i.lastRequestHex,
            i.lastReplyHex.isEmpty() ? QStringLiteral("尚无匹配回复") : i.lastReplyHex));
    m_depthStatus->setText(QStringLiteral("%1 · PROM %2 · MS5837-02BA · 水面参考 %3 · %4 · 错误 %5")
        .arg(d.online ? QStringLiteral("I2C 设备在线") : QStringLiteral("设备离线 / 尚无数据"),
             (d.status & SensorStatus::PromValid) ? QStringLiteral("已校验") : QStringLiteral("未通过"),
             s.zeroValid ? QStringLiteral("已设定") : QStringLiteral("未设定"),
             ageText(s.depthAgeMs)).arg(d.errors));
    QString diagnostics;
    if (d.statusKnown)
        diagnostics = QStringLiteral("\n完成采样 %1 / 良好 %2 · 设备%3")
                          .arg(d.sampleSequence).arg(d.goodFrames).arg(ageText(d.sampleAgeMs));
    const QString depthError = depthLastErrorText(d.status);
    if (!depthError.isEmpty()) diagnostics += QStringLiteral("\n") + depthError;
    const QString depthPhase = depthDriverPhaseText(d.status);
    if (!depthPhase.isEmpty()) diagnostics += QStringLiteral("\n") + depthPhase;
    if (s.depthAgeMs >= 0)
        diagnostics += QStringLiteral(" · 数据产生时刻 %1 µs · 流序号 %2")
                           .arg(s.depthTimestampUs).arg(d.sequence);
    m_depthStatus->setText(m_depthStatus->text() + diagnostics);
    m_imuStatus->setText(m_imuStatus->text() + QStringLiteral("\n") + streamText(i.streamState));
    m_depthStatus->setText(m_depthStatus->text() + QStringLiteral("\n") + streamText(d.streamState));
    m_depthStatus->setToolTip(QStringLiteral("状态字 0x%1\n诊断标志有效时：status[12:10] 是驱动阶段，bit13 表示诊断字段有效，status[15:14] 是 I²C 事务阶段；status[23:16] 是总线相关错误的 I²C 命令上下文，status[31:24] 是 MS5837 驱动错误码。\n数据时刻是设备 D2 读完时刻，不是 USB 接收时间；32 位微秒时钟约 71.6 分钟回绕。\n完成采样数与流序号不同；0x82/0x83 共用本 TARGET 的流序号，背压下允许跳号。")
        .arg(d.status, 8, 16, QLatin1Char('0')));
    m_imuCommand->setText(i.lastCommand); m_depthCommand->setText(d.lastCommand);
    // Protocol bytes are prepared by the service; the page only presents the snapshot strings.
    m_depthCommand->setToolTip(d.lastRequestHex.isEmpty()
        ? QStringLiteral("尚无命令报文")
        : QStringLiteral("最近请求编码（提交帧，不代表发送成功）：\n%1\n匹配回复 RX：\n%2")
              .arg(d.lastRequestHex, d.lastReplyHex.isEmpty() ? QStringLiteral("尚无匹配回复") : d.lastReplyHex));
    for (int n = 0; n < 3; ++n)
    {
        displayValue(m_rawTable, n, s.rawValid, s.accelG.at(n), 5);
        displayValue(m_rawTable, n + 3, s.rawValid, s.gyroRadS.at(n), 5);
        displayValue(m_rawTable, n + 6, s.rawValid, s.magProtocolUnits.at(n), 3);
        displayValue(m_attitudeTable, n, s.eulerValid, s.eulerDeg.at(n), 3);
    }
    for (int n = 0; n < 4; ++n) displayValue(m_attitudeTable, n + 3, s.quaternionValid, s.quaternionWxyz.at(n), 6);
    displayValue(m_depthTable, 0, s.pressureValid, s.pressurePa, 1);
    displayValue(m_depthTable, 1, s.temperatureValid, s.temperatureC, 2);
    displayValue(m_depthTable, 2, s.depthValid, s.depthRawM, 4);
    displayValue(m_depthTable, 3, s.depthValid, s.depthFilteredM, 4);
    const bool depthFresh = s.connected && d.online && s.depthAgeMs >= 0 && s.depthAgeMs <= 2500;
    displayValue(m_depthTable, 4, depthFresh && s.surfacePressureValid, s.surfacePressurePa, 1);
    const bool adcValid = depthFresh && s.depthRawValid;
    displayValue(m_depthTable, 5, adcValid, s.rawAdcD1, 0); displayValue(m_depthTable, 6, adcValid, s.rawAdcD2, 0);
    for (const auto &c : m_controls)
    {
        if (c.button->isHidden()) {
            c.button->setEnabled(false);
            continue;
        }
        const auto &device = s.devices.at(c.target - 1);
        bool enabled = s.connected && !device.pending;
        if (c.capability >= 0) enabled = enabled && device.infoKnown && (device.capabilities & (1U << c.capability));
        if (c.deviceAction && c.target == kImuSensor) enabled = enabled && !(device.status & SensorStatus::PinBlocked);
        if (c.button->property("needsAlgorithm").toBool())
            enabled = enabled && m_imuAlgorithm && m_imuAlgorithm->currentData().isValid();
        if (c.button->property("needsPressure").toBool()) enabled = enabled && s.pressureValid;
        if (c.button->property("needsImuOnline").toBool()) enabled = enabled && device.online;
        c.button->setEnabled(enabled);
    }
    for (const auto &p : m_parameters)
    {
        p.editor->setEnabled(!s.devices.at(p.target - 1).pending);
        if (reset) p.feedback->setText(QStringLiteral("未读取"));
    }
}
void SensorPanel::setParameterFeedback(const SensorParameterFeedback &f)
{
    for (const auto &p : m_parameters)
        if (p.target == f.target && p.id == f.parameterId)
            p.feedback->setText(!f.value.isValid()
                ? (f.message.isEmpty() ? QStringLiteral("未读取") : f.message)
                : QStringLiteral("%1（%2）").arg(f.value.toString(),
                    f.target == kImuSensor ? (f.message.isEmpty() ? QStringLiteral("缓存 / 未确认") : f.message)
                    : f.confirmed ? QStringLiteral("设备确认") : QStringLiteral("缓存 / 未确认")));
}
} // namespace rov
