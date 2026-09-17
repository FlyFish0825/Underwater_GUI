#include "pages/firmware/FirmwarePage.h"

#include "preview/PreviewData.h"
#include "pages/firmware/BootloaderCommandDialog.h"
#include "pages/firmware/FirmwareHistoryDialog.h"
#include "pages/firmware/FirmwareLogFormatter.h"
#include "communication/bootloader/BootloaderProtocol.h"
#include "ui/common/UiPrimitives.h"

#include <QDateTime>
#include <QFileDialog>
#include <QComboBox>
#include <QHash>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

namespace
{

QString firmwareStateText(const rov::FirmwareState state)
{
    switch (state)
    {
    case rov::FirmwareState::Completed:
        return QStringLiteral("成功");
    case rov::FirmwareState::Programming:
        return QStringLiteral("编程中");
    case rov::FirmwareState::Verifying:
        return QStringLiteral("校验中");
    case rov::FirmwareState::Failed:
        return QStringLiteral("失败");
    default:
        return QStringLiteral("空闲");
    }
}

QString connectionStatusText(const bool connected, const QString &message)
{
    const QString color = connected ? QStringLiteral("#078d4a") : QStringLiteral("#d64545");
    return QStringLiteral("<span style=\"color:%1;\">● %2</span>").arg(color, message);
}

quint8 nodeIdFromText(const QString &text)
{
    QString value = text.trimmed();
    if (value.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        value = value.mid(2);
    bool ok = false;
    const uint parsed = value.toUInt(&ok, 16);
    return ok && parsed <= 0xFFU ? static_cast<quint8>(parsed) : 0;
}

QString thrusterDisplayName(const int index)
{
    static const QStringList names = {
        QStringLiteral("推进器 1（左前）"),   QStringLiteral("推进器 2（右前）"),
        QStringLiteral("推进器 3（左后）"),   QStringLiteral("推进器 4（右后）"),
        QStringLiteral("推进器 5（内左前）"), QStringLiteral("推进器 6（内右前）"),
        QStringLiteral("推进器 7（内左后）"), QStringLiteral("推进器 8（内右后）"),
    };
    return names.value(index, QStringLiteral("推进器 %1").arg(index + 1));
}

QString commandNameZh(const QString &name)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("GET_VERSION"), QStringLiteral("读取版本")},
        {QStringLiteral("GET_DEVICE_ID"), QStringLiteral("读取设备 ID")},
        {QStringLiteral("GET_INFO"), QStringLiteral("读取设备信息")},
        {QStringLiteral("ENTER_BOOT"), QStringLiteral("进入 Bootloader")},
        {QStringLiteral("SET_GUARD"), QStringLiteral("设置升级保护")},
        {QStringLiteral("RELEASE_GUARD"), QStringLiteral("解除升级保护")},
        {QStringLiteral("SESSION_BEGIN"), QStringLiteral("开始升级会话")},
        {QStringLiteral("SESSION_CRC32"), QStringLiteral("校验会话 CRC32")},
        {QStringLiteral("ERASE"), QStringLiteral("擦除 Flash")},
        {QStringLiteral("WRITE"), QStringLiteral("写入固件")},
        {QStringLiteral("READ"), QStringLiteral("读取固件")},
        {QStringLiteral("VERIFY"), QStringLiteral("校验固件")},
        {QStringLiteral("WRITE_END"), QStringLiteral("结束写入")},
        {QStringLiteral("ABORT"), QStringLiteral("中止操作")},
        {QStringLiteral("JUMP_APP"), QStringLiteral("启动 APP")},
        {QStringLiteral("RESET"), QStringLiteral("复位节点")},
        {QStringLiteral("GET_STATUS"), QStringLiteral("读取运行状态")},
        {QStringLiteral("VERIFY_REQUEST"), QStringLiteral("请求校验")},
        {QStringLiteral("VERIFY_RESULT"), QStringLiteral("返回校验结果")},
        {QStringLiteral("MISSING_COUNT"), QStringLiteral("查询缺失块数量")},
        {QStringLiteral("MISSING_ITEM"), QStringLiteral("查询缺失块")},
        {QStringLiteral("PROVIDER_GRANT"), QStringLiteral("授权数据提供者")},
        {QStringLiteral("COORDINATOR_CLAIM"), QStringLiteral("协调器声明")},
        {QStringLiteral("PROVIDER_ASSIGN"), QStringLiteral("分配数据提供者")},
        {QStringLiteral("PROVIDER_DONE"), QStringLiteral("数据提供者完成")},
        {QStringLiteral("REPAIR_ROUND_END"), QStringLiteral("结束修复轮次")},
        {QStringLiteral("RECOVERY_READY"), QStringLiteral("恢复就绪")},
        {QStringLiteral("RECOVERY_FAILED"), QStringLiteral("恢复失败")},
    };
    return names.value(name, QStringLiteral("命令 %1").arg(name));
}

QString statusNameZh(const QString &name)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("IDLE"), QStringLiteral("空闲")},
        {QStringLiteral("ERASE"), QStringLiteral("擦除中")},
        {QStringLiteral("WRITE"), QStringLiteral("写入中")},
        {QStringLiteral("VERIFY"), QStringLiteral("校验中")},
        {QStringLiteral("READY"), QStringLiteral("就绪")},
        {QStringLiteral("REPAIR"), QStringLiteral("修复中")},
        {QStringLiteral("GUARD"), QStringLiteral("保护中")},
        {QStringLiteral("ERROR"), QStringLiteral("错误")},
    };
    return names.value(name, name);
}

QString logHtml(const QString &timestampedMessage)
{
    QString body = timestampedMessage;
    QString time;
    if (body.startsWith(QLatin1Char('[')))
    {
        const int end = body.indexOf(QLatin1Char(']'));
        if (end > 0)
        {
            time = body.left(end + 1);
            body = body.mid(end + 1).trimmed();
        }
    }

    QString summary = body;
    QString raw;
    QString color = QStringLiteral("#5b7390");
    const QStringList parts = body.split(QStringLiteral(" · "));
    if (body.startsWith(QStringLiteral("TX Node")) && parts.size() >= 2)
    {
        const QString node = parts.at(0).mid(QStringLiteral("TX Node").size());
        summary = QStringLiteral("发送 · 节点 %1 · %2").arg(node, commandNameZh(parts.at(1)));
        color = QStringLiteral("#2369c8");
        raw = body;
    }
    else if (body.startsWith(QStringLiteral("TX Peer")) && parts.size() >= 2)
    {
        summary = QStringLiteral("发送 · Peer · %1").arg(commandNameZh(parts.at(1)));
        color = QStringLiteral("#2369c8");
        raw = body;
    }
    else if (body.startsWith(QStringLiteral("RX ")) && parts.size() >= 3)
    {
        const QString status = parts.at(2).trimmed();
        summary = QStringLiteral("接收 · 节点 %1 · %2 · 状态：%3")
                      .arg(parts.at(0).mid(3), commandNameZh(parts.at(1)), statusNameZh(status));
        color = status == QStringLiteral("ERROR") ? QStringLiteral("#d64545")
                                                    : QStringLiteral("#078d4a");
        raw = body;
    }
    else if (body.startsWith(QStringLiteral("Peer · ")) && parts.size() >= 2)
    {
        summary = QStringLiteral("接收 · Peer · %1").arg(commandNameZh(parts.at(1)));
        color = QStringLiteral("#078d4a");
        raw = body;
    }
    else if (body.startsWith(QStringLiteral("解析到 CAN")))
    {
        summary = QStringLiteral("接收 · CAN 网关帧");
        color = QStringLiteral("#078d4a");
        raw = body;
    }
    else if (body.contains(QStringLiteral("错误")) || body.contains(QStringLiteral("失败"))
             || body.contains(QStringLiteral("超时")) || body.contains(QStringLiteral("未连接"))
             || body.contains(QStringLiteral("断开")))
    {
        color = QStringLiteral("#d64545");
    }

    QString html = QStringLiteral("<div style=\"color:%1; margin:1px 0;\"><b>%2</b> %3")
                       .arg(color, time.toHtmlEscaped(), summary.toHtmlEscaped());
    if (!raw.isEmpty())
    {
        html += QStringLiteral("<br/><span style=\"color:#8798aa;\">原始：%1</span>")
                    .arg(raw.toHtmlEscaped());
    }
    return html + QStringLiteral("</div>");
}

QLabel *fileField(QLabel *&target, const QString &label)
{
    auto *holder = new QWidget;
    auto *layout = new QHBoxLayout(holder);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(rov::makeLabel(label, QStringLiteral("mutedLabel")));
    target = rov::makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    layout->addWidget(target);
    layout->addStretch();
    return target;
}

} // namespace

namespace rov
{

FirmwarePage::FirmwarePage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("firmwarePage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(10);
    root->addWidget(makePageHeader(QStringLiteral("固件升级"),
                                   QStringLiteral("管理固件信息并发出未来升级请求。"),
                                   QStringLiteral("演示 · 仅界面与请求")));

    auto *mainRow = new QHBoxLayout;
    mainRow->setSpacing(12);
    auto *fileCard = new CardWidget(QStringLiteral("固件文件"), IconKind::File);
    auto *dropZone = new QFrame;
    dropZone->setObjectName(QStringLiteral("card"));
    dropZone->setMinimumHeight(78);
    auto *dropLayout = new QHBoxLayout(dropZone);
    dropLayout->setContentsMargins(10, 6, 10, 6);
    dropLayout->setSpacing(8);
    auto *fileIcon = new IconWidget(IconKind::File);
    fileIcon->setFixedSize(22, 22);
    dropLayout->addWidget(fileIcon, 0, Qt::AlignVCenter);
    auto *dropText = new QVBoxLayout;
    dropText->setSpacing(1);
    dropText->addWidget(
        makeLabel(QStringLiteral("将固件文件拖放到此处 · 或点击浏览"), QStringLiteral("bodyValue")));
    dropText->addWidget(
        makeLabel(QStringLiteral("支持格式：.bin、.hex、.uf2"), QStringLiteral("mutedLabel")));
    dropLayout->addLayout(dropText, 1);
    fileCard->contentLayout()->addWidget(dropZone);
    auto *info = new QGridLayout;
    info->setVerticalSpacing(8);
    info->addWidget(makeLabel(QStringLiteral("文件名"), QStringLiteral("mutedLabel")), 0, 0);
    m_fileName = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    info->addWidget(m_fileName, 0, 1);
    info->addWidget(makeLabel(QStringLiteral("版本"), QStringLiteral("mutedLabel")), 1, 0);
    m_fileVersion = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    info->addWidget(m_fileVersion, 1, 1);
    info->addWidget(makeLabel(QStringLiteral("文件大小"), QStringLiteral("mutedLabel")), 2, 0);
    m_fileSize = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    info->addWidget(m_fileSize, 2, 1);
    info->addWidget(makeLabel(QStringLiteral("校验摘要"), QStringLiteral("mutedLabel")), 3, 0);
    m_checksum = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    info->addWidget(m_checksum, 3, 1);
    info->addWidget(makeLabel(QStringLiteral("说明"), QStringLiteral("mutedLabel")), 4, 0);
    m_description = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    m_description->setWordWrap(true);
    info->addWidget(m_description, 4, 1);
    fileCard->contentLayout()->addLayout(info);
    fileCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto *left = new QVBoxLayout;
    left->setSpacing(12);
    left->addWidget(fileCard);
    auto *nodeControl = new CardWidget(QStringLiteral("节点控制"), IconKind::Action);
    auto *targetForm = new QGridLayout;
    targetForm->addWidget(makeLabel(QStringLiteral("当前目标"), QStringLiteral("mutedLabel")), 0, 0);
    m_targetNodeCombo = new QComboBox;
    targetForm->addWidget(m_targetNodeCombo, 0, 1);
    nodeControl->contentLayout()->addLayout(targetForm);
    nodeControl->contentLayout()->addWidget(makeLabel(QStringLiteral("常用命令"), QStringLiteral("mutedLabel")));
    auto *commonGrid = new QGridLayout;
    const auto addCommon = [this, commonGrid](const QString &text, BootCommand command, int row, int col)
    {
        auto *button = makeButton(text, QStringLiteral("softButton"));
        commonGrid->addWidget(button, row, col);
        connect(button, &QPushButton::clicked, this,
                [this, command, text]() { sendCommonCommand(command, text); });
    };
    addCommon(QStringLiteral("读取版本"), BootCommand::GetVersion, 0, 0);
    addCommon(QStringLiteral("设备 ID"), BootCommand::GetDeviceId, 0, 1);
    addCommon(QStringLiteral("设备信息"), BootCommand::GetInfo, 1, 0);
    addCommon(QStringLiteral("运行状态"), BootCommand::GetStatus, 1, 1);
    addCommon(QStringLiteral("进入 Boot"), BootCommand::EnterBoot, 2, 0);
    addCommon(QStringLiteral("复位节点"), BootCommand::Reset, 2, 1);
    addCommon(QStringLiteral("中止操作"), BootCommand::Abort, 3, 0);
    auto *jump = makeButton(QStringLiteral("启动 APP"), QStringLiteral("softButton"));
    commonGrid->addWidget(jump, 3, 1);
    connect(jump, &QPushButton::clicked, this,
            [this]() { sendCommonCommand(BootCommand::JumpApp, QStringLiteral("启动 APP")); });
    nodeControl->contentLayout()->addLayout(commonGrid);
    auto *advanced = makeButton(QStringLiteral("高级命令…"), QStringLiteral("softButton"));
    nodeControl->contentLayout()->addWidget(advanced);
    nodeControl->contentLayout()->addWidget(makeLabel(QStringLiteral("当前节点状态"), QStringLiteral("mutedLabel")));
    auto *state = new QGridLayout;
    const auto addState = [this, state](const QString &name, QLabel *&value, int row)
    {
        state->addWidget(makeLabel(name, QStringLiteral("mutedLabel")), row, 0);
        value = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
        state->addWidget(value, row, 1);
    };
    addState(QStringLiteral("节点"), m_stateNode, 0);
    addState(QStringLiteral("设备"), m_stateDevice, 1);
    addState(QStringLiteral("Bootloader"), m_stateBootloader, 2);
    addState(QStringLiteral("APP"), m_stateApp, 3);
    addState(QStringLiteral("Config"), m_stateConfig, 4);
    addState(QStringLiteral("Status"), m_stateStatus, 5);
    addState(QStringLiteral("Last Error"), m_stateError, 6);
    addState(QStringLiteral("Progress"), m_stateProgress, 7);
    nodeControl->contentLayout()->addLayout(state);
    left->addWidget(nodeControl);
    left->addStretch();
    mainRow->addLayout(left, 3);

    auto *right = new QVBoxLayout;
    right->setSpacing(12);
    // USB CDC 只负责连接状态检查，控件由 MainWindow 放到全局连接栏。
    m_connectionBar = new QFrame;
    m_connectionBar->setObjectName(QStringLiteral("connectionBar"));
    m_connectionBar->setFixedHeight(46);
    auto *connectionRow = new QHBoxLayout(m_connectionBar);
    connectionRow->setContentsMargins(12, 4, 12, 4);
    connectionRow->setSpacing(8);
    connectionRow->addWidget(makeLabel(QStringLiteral("USB CDC 通信"), QStringLiteral("sectionTitle")));
    m_serialDeviceCombo = new QComboBox;
    m_serialDeviceCombo->setMinimumWidth(250);
    connectionRow->addWidget(m_serialDeviceCombo, 1);
    auto *refreshSerial = makeButton(QStringLiteral("刷新设备"), QStringLiteral("softButton"));
    connectionRow->addWidget(refreshSerial);
    m_serialConnectButton = makeButton(QStringLiteral("接管串口"), QStringLiteral("primaryButton"));
    connectionRow->addWidget(m_serialConnectButton);
    m_serialStatus = makeLabel(connectionStatusText(false, QStringLiteral("未连接 · VID_0483 PID_5740")),
                               QStringLiteral("mutedLabel"));
    m_serialStatus->setTextFormat(Qt::RichText);
    connectionRow->addWidget(m_serialStatus, 1);

    auto *targetCard =
        new CardWidget(QStringLiteral("目标节点与升级进度（8）"), IconKind::Firmware);
    m_nodeTable = new QTableWidget(0, 6);
    m_nodeTable->setHorizontalHeaderLabels(
        {QStringLiteral("节点 ID"), QStringLiteral("设备名称"), QStringLiteral("当前版本"),
         QStringLiteral("目标版本"), QStringLiteral("在线状态"), QStringLiteral("升级状态")});
    m_nodeTable->horizontalHeader()->setStretchLastSection(true);
    m_nodeTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_nodeTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nodeTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nodeTable->verticalHeader()->setDefaultSectionSize(24);
    m_nodeTable->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_nodeTable->verticalHeader()->setFixedWidth(36);
    // 预留表头、边框和 8 行完整行高，避免第 8 个节点被卡片裁掉。
    m_nodeTable->setMinimumHeight(8 * 26 + 42);
    m_nodeTable->setColumnWidth(0, 66);
    m_nodeTable->setColumnWidth(2, 100);
    m_nodeTable->setColumnWidth(3, 100);
    m_nodeTable->setColumnWidth(4, 100);
    m_nodeTable->setColumnWidth(5, 100);
    m_nodeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nodeTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_nodeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    targetCard->contentLayout()->addWidget(m_nodeTable);
    auto *progressTitle = makeLabel(QStringLiteral("升级进度"), QStringLiteral("sectionTitle"));
    targetCard->contentLayout()->addWidget(progressTitle);
    auto *progressLayout = new QVBoxLayout;
    progressLayout->setSpacing(4);
    for (int i = 0; i < 8; ++i)
    {
        auto *row = new QHBoxLayout;
        row->addWidget(
            makeLabel(QStringLiteral("0x%1").arg(i + 1, 2, 16, QLatin1Char('0')).toUpper(),
                      QStringLiteral("bodyValue")));
        row->addWidget(
            makeLabel(thrusterDisplayName(i), QStringLiteral("bodyValue")), 1);
        auto *bar = new QProgressBar;
        bar->setRange(0, 100);
        bar->setValue(i == 0 ? 100 : (i == 1 ? 65 : 0));
        bar->setTextVisible(false);
        row->addWidget(bar, 2);
        row->addWidget(
            makeLabel(i == 0 ? QStringLiteral("成功") : QStringLiteral("空闲"),
                      i == 0 ? QStringLiteral("statusGood") : QStringLiteral("statusIdle")));
        progressLayout->addLayout(row);
    }
    auto *actions = new QHBoxLayout;
    actions->addWidget(makeButton(QStringLiteral("进入 Boot"), QStringLiteral("softButton")));
    actions->addWidget(makeButton(QStringLiteral("擦除"), QStringLiteral("softButton")));
    actions->addWidget(makeButton(QStringLiteral("编程"), QStringLiteral("softButton")));
    auto *verify = makeButton(QStringLiteral("校验"), QStringLiteral("softButton"));
    actions->addWidget(verify);
    actions->addWidget(makeButton(QStringLiteral("重启"), QStringLiteral("softButton")));
    auto *update = makeButton(QStringLiteral("升级选中节点"), QStringLiteral("primaryButton"));
    actions->addWidget(update, 1);
    progressLayout->addLayout(actions);
    targetCard->contentLayout()->addLayout(progressLayout);
    right->addWidget(targetCard, 6);

    auto *logCard = new CardWidget(QStringLiteral("升级日志"), IconKind::List);
    auto *logToolbar = new QHBoxLayout;
    logToolbar->addStretch();
    auto *clearLog = makeButton(QStringLiteral("清屏"), QStringLiteral("softButton"));
    auto *history = makeButton(QStringLiteral("历史记录"), QStringLiteral("softButton"));
    logToolbar->addWidget(clearLog);
    logToolbar->addWidget(history);
    logCard->contentLayout()->addLayout(logToolbar);
    m_requestLog = new QTextBrowser;
    m_requestLog->setReadOnly(true);
    m_requestLog->setLineWrapMode(QTextEdit::NoWrap);
    m_requestLog->setMinimumHeight(90);
    m_requestLog->setOpenLinks(false);
    m_requestLog->setOpenExternalLinks(false);
    logCard->contentLayout()->addWidget(m_requestLog);
    logCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    right->addWidget(logCard);
    right->addStretch(1);
    mainRow->addLayout(right, 7);
    root->addLayout(mainRow, 1);

    connect(verify, &QPushButton::clicked, this,
            [this]()
            {
                emit verifyRequested();
                logRequest(QStringLiteral("请求校验；不执行设备操作"));
            });
    connect(update, &QPushButton::clicked, this,
            [this]()
            {
                FirmwareUpgradeRequest request;
                for (const auto &node : m_snapshot.nodes)
                {
                    if (node.online)
                    {
                        request.nodeIds.append(node.nodeId);
                    }
                }
                request.firmwarePath = m_snapshot.fileName;
                emit upgradeRequested(request);
                logRequest(
                    QStringLiteral("请求升级 %1 个在线演示节点").arg(request.nodeIds.size()));
            });
    connect(clearLog, &QPushButton::clicked, this,
            [this]()
            {
                m_runtimeLog.clear();
                if (m_requestLog != nullptr)
                    m_requestLog->clear();
            });
    connect(history, &QPushButton::clicked, this, &FirmwarePage::showHistory);

    m_communication = new BootloaderCommunicationService(this);
    m_bootloader = new BootloaderService(m_communication, this);
    connect(m_targetNodeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &FirmwarePage::selectNode);
    connect(m_nodeTable, &QTableWidget::cellClicked, this, &FirmwarePage::selectTableRow);
    connect(advanced, &QPushButton::clicked, this, &FirmwarePage::showCommandCenter);
    connect(refreshSerial, &QPushButton::clicked, this, &FirmwarePage::refreshSerialDevices);
    connect(m_serialConnectButton, &QPushButton::clicked, this,
            &FirmwarePage::toggleSerialConnection);
    connect(m_communication, &BootloaderCommunicationService::opened, this,
            [this](const QString &port)
            {
                m_heartbeatCount = 0;
                m_heartbeatWatchdog->start();
                m_serialConnectButton->setText(QStringLiteral("断开串口"));
                m_serialStatus->setText(
                    connectionStatusText(true, QStringLiteral("已接管 %1 · 等待下位机心跳").arg(port)));
                logRequest(QStringLiteral("已打开 %1").arg(port));
            });
    connect(m_communication, &BootloaderCommunicationService::closed, this,
            [this]()
            {
                if (m_heartbeatWatchdog != nullptr)
                    m_heartbeatWatchdog->stop();
                m_serialConnectButton->setText(QStringLiteral("接管串口"));
                m_serialStatus->setText(connectionStatusText(false, QStringLiteral("未连接 · VID_0483 PID_5740")));
                logRequest(QStringLiteral("串口已断开"));
            });
    connect(m_communication, &BootloaderCommunicationService::errorOccurred, this,
            [this](const QString &message)
            {
                m_serialStatus->setText(connectionStatusText(false, QStringLiteral("通信错误：%1").arg(message)));
                logRequest(QStringLiteral("错误：%1").arg(message));
            });
    connect(m_communication, &BootloaderCommunicationService::frameReceived, this,
            [this](const CanGatewayFrame &frame)
            { logRequest(QStringLiteral("解析到 %1").arg(describeCanGatewayFrame(frame))); });
    connect(m_bootloader, &BootloaderService::hostResponseReceived, this,
            &FirmwarePage::handleBootResponse);
    connect(m_bootloader, &BootloaderService::peerMessageReceived, this,
            &FirmwarePage::handlePeerMessage);
    m_heartbeatWatchdog = new QTimer(this);
    m_heartbeatWatchdog->setSingleShot(true);
    m_heartbeatWatchdog->setInterval(2500);
    connect(m_heartbeatWatchdog, &QTimer::timeout, this,
            [this]()
            {
                m_serialStatus->setText(
                    connectionStatusText(false, QStringLiteral("串口已连接 · 心跳超时，疑似下位机离线")));
                logRequest(QStringLiteral("心跳超时：超过 2.5 秒未收到 AA58 PING"));
                if (m_communication != nullptr && m_communication->isOpen())
                    m_communication->close();
            });
    connect(m_communication, &BootloaderCommunicationService::heartbeatReceived, this,
            [this](const SystemHeartbeat &heartbeat)
            {
                m_heartbeatWatchdog->start();
                Q_UNUSED(heartbeat)
                ++m_heartbeatCount;
                m_serialStatus->setText(
                    connectionStatusText(true, QStringLiteral("下位机在线 · 心跳 %1").arg(m_heartbeatCount)));
            });

    refreshSerialDevices();
    m_deviceScanTimer = new QTimer(this);
    m_deviceScanTimer->setInterval(1000);
    connect(m_deviceScanTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_communication != nullptr && !m_communication->isOpen())
                    refreshSerialDevices();
            });
    m_deviceScanTimer->start();
    setSnapshot(firmwarePreview());
}

void FirmwarePage::closeAuxiliaryWindows()
{
    if (m_commandDialog != nullptr)
    {
        auto *dialog = m_commandDialog.data();
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        dialog->close();
        delete dialog;
        m_commandDialog = nullptr;
    }
    if (m_historyDialog != nullptr)
    {
        auto *dialog = m_historyDialog.data();
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        dialog->close();
        delete dialog;
        m_historyDialog = nullptr;
    }
}

QWidget *FirmwarePage::connectionBar() const
{
    return m_connectionBar;
}

void FirmwarePage::setSnapshot(const FirmwareSnapshot &snapshot)
{
    m_snapshot = snapshot;
    refreshView();
}

void FirmwarePage::refreshView()
{
    if (m_nodeTable == nullptr)
    {
        return;
    }
    m_fileName->setText(m_snapshot.fileName);
    m_fileVersion->setText(m_snapshot.fileVersion);
    m_fileSize->setText(m_snapshot.fileSize);
    m_checksum->setText(m_snapshot.checksum);
    m_description->setText(m_snapshot.fileDescription);
    m_nodeTable->setRowCount(m_snapshot.nodes.size());
    for (int row = 0; row < m_snapshot.nodes.size(); ++row)
    {
        const auto &node = m_snapshot.nodes.at(row);
        m_nodeTable->setItem(row, 0, new QTableWidgetItem(node.nodeId));
        m_nodeTable->setItem(row, 1, new QTableWidgetItem(node.deviceName));
        m_nodeTable->setItem(row, 2, new QTableWidgetItem(node.currentVersion));
        m_nodeTable->setItem(row, 3, new QTableWidgetItem(node.targetVersion));
        m_nodeTable->setItem(
            row, 4,
            new QTableWidgetItem(node.online ? QStringLiteral("在线") : QStringLiteral("离线")));
        m_nodeTable->setItem(row, 5, new QTableWidgetItem(firmwareStateText(node.state)));
    }
    if (m_targetNodeCombo != nullptr)
    {
        const QVariant previous = m_targetNodeCombo->currentData();
        const QSignalBlocker blocker(m_targetNodeCombo);
        m_targetNodeCombo->clear();
        for (const auto &node : m_snapshot.nodes)
        {
            const quint8 id = nodeIdFromText(node.nodeId);
            m_targetNodeCombo->addItem(QStringLiteral("%1 · %2").arg(node.nodeId, node.deviceName), id);
        }
        int selected = previous.isValid() ? m_targetNodeCombo->findData(previous) : 0;
        if (selected < 0)
            selected = 0;
        if (m_targetNodeCombo->count() > 0)
            m_targetNodeCombo->setCurrentIndex(selected);
        selectNode(selected);
    }
    if (!m_snapshot.log.isEmpty())
    {
        m_runtimeLog = m_snapshot.log;
        renderRuntimeLog();
    }
}

void FirmwarePage::selectNode(int index)
{
    if (index < 0 || index >= m_snapshot.nodes.size())
        return;
    const auto &node = m_snapshot.nodes.at(index);
    if (m_nodeTable != nullptr)
    {
        const QSignalBlocker blocker(m_nodeTable);
        m_nodeTable->selectRow(index);
    }
    m_stateNode->setText(node.nodeId);
    m_stateDevice->setText(node.deviceName);
    m_stateBootloader->setText(QStringLiteral("--"));
    m_stateApp->setText(QStringLiteral("--"));
    m_stateConfig->setText(QStringLiteral("--"));
    m_stateStatus->setText(QStringLiteral("--"));
    m_stateError->setText(QStringLiteral("--"));
    m_stateProgress->setText(QStringLiteral("--"));
}

void FirmwarePage::selectTableRow(int row, int column)
{
    Q_UNUSED(column)
    if (m_targetNodeCombo == nullptr || row < 0 || row >= m_targetNodeCombo->count())
        return;
    const QSignalBlocker blocker(m_targetNodeCombo);
    m_targetNodeCombo->setCurrentIndex(row);
    selectNode(row);
}

void FirmwarePage::sendCommonCommand(BootCommand command, const QString &label, quint8 byte2,
                                     const QByteArray &params)
{
    if (m_bootloader == nullptr || m_communication == nullptr || !m_communication->isOpen())
    {
        logRequest(QStringLiteral("%1：串口未连接").arg(label));
        return;
    }
    const int index = m_targetNodeCombo == nullptr ? -1 : m_targetNodeCombo->currentIndex();
    if (index < 0 || index >= m_snapshot.nodes.size())
    {
        logRequest(QStringLiteral("%1：没有选择目标节点").arg(label));
        return;
    }
    const quint8 target = nodeIdFromText(m_snapshot.nodes.at(index).nodeId);
    if (m_bootloader->sendHostCommand(target, command, byte2, params))
    {
        const QString raw = BootloaderProtocol::encodeHostControl(target, command, byte2, params)
                                 .toHex(' ')
                                 .toUpper();
        if (command == BootCommand::EnterBoot)
            logRequest(QStringLiteral("TX Node%1 · ENTER_BOOT · CAN=0x000 · DATA=%2 · APP 不回复 ACK，等待复位")
                           .arg(target)
                           .arg(raw));
        else
            logRequest(QStringLiteral("TX Node%1 · %2 · CAN=0x000 · DATA=%3")
                           .arg(target)
                           .arg(bootCommandName(command))
                           .arg(raw));
    }
    else
        logRequest(QStringLiteral("%1：发送失败").arg(label));
}

void FirmwarePage::showCommandCenter()
{
    if (m_commandDialog != nullptr)
    {
        if (m_commandDialog->isVisible())
        {
            m_commandDialog->raise();
            m_commandDialog->activateWindow();
            return;
        }
        m_commandDialog->deleteLater();
        m_commandDialog = nullptr;
    }

    // 脱离 QGraphicsProxyWidget 作为顶层窗口显示，避免关闭后再次打开失败。
    QWidget *owner = window();
    // 保持独立顶层窗口，避免被 QGraphicsProxyWidget 或页面容器缩放；
    // 主窗口退出时由 closeAuxiliaryWindows() 显式销毁。
    m_commandDialog = new BootloaderCommandDialog(nullptr);
    m_commandDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_commandDialog->setWindowFlag(Qt::Window, true);
    m_commandDialog->setWindowModality(Qt::ApplicationModal);
    if (owner != nullptr)
    {
        const QSize ownerSize = owner->size();
        m_commandDialog->resize(qMax(900, ownerSize.width() * 3 / 5),
                                qMax(620, ownerSize.height() * 3 / 5));
        m_commandDialog->move(owner->frameGeometry().center() - m_commandDialog->rect().center());
    }
    if (m_targetNodeCombo != nullptr)
        m_commandDialog->setTarget(static_cast<quint8>(m_targetNodeCombo->currentData().toInt()));
    // 打开时先灌入当前会话日志，随后通过信号持续接收新日志。
    m_commandDialog->setDebugHistory(m_runtimeLog);
    connect(this, &FirmwarePage::debugLogAppended, m_commandDialog,
            &BootloaderCommandDialog::appendDebugMessage);
    connect(m_commandDialog, &BootloaderCommandDialog::commandRequested, this,
            [this](quint8 target, BootCommand command, quint8 byte2, const QByteArray &params)
            {
                if (m_bootloader != nullptr && m_communication != nullptr && m_communication->isOpen()
                    && m_bootloader->sendHostCommand(target, command, byte2, params))
                {
                    const QString raw = BootloaderProtocol::encodeHostControl(target, command, byte2, params)
                                             .toHex(' ')
                                             .toUpper();
                    if (command == BootCommand::EnterBoot)
                        logRequest(QStringLiteral("TX Node%1 · ENTER_BOOT · CAN=0x000 · DATA=%2 · APP 不回复 ACK，等待复位")
                                       .arg(target)
                                       .arg(raw));
                    else if (command == BootCommand::JumpApp && byte2 == 1)
                        logRequest(QStringLiteral("TX Node%1 · JUMP_APP · CAN=0x000 · DATA=%2 · Trial Jump，等待 APP 回传 ENTER_BOOT")
                                       .arg(target)
                                       .arg(raw));
                    else
                        logRequest(QStringLiteral("TX Node%1 · %2 · CAN=0x000 · DATA=%3")
                                       .arg(target)
                                       .arg(bootCommandName(command))
                                       .arg(raw));
                }
                else
                    logRequest(QStringLiteral("%1：串口未连接或参数非法").arg(bootCommandName(command)));
            });
    connect(m_commandDialog, &BootloaderCommandDialog::peerCommandRequested, this,
            [this](quint8 target, BootCommand command, quint8 source, quint16 session, quint16 value)
            {
                if (m_bootloader != nullptr && m_communication != nullptr && m_communication->isOpen()
                    && m_bootloader->sendPeerCommand(target, command, source, session, value))
                {
                    const QString raw = BootloaderProtocol::encodePeerControl(
                                             target, command, source, session, value)
                                             .toHex(' ')
                                             .toUpper();
                    logRequest(QStringLiteral("TX Peer · %1 · CAN=0x%2 · DATA=%3 · Source=%4 · Target=%5")
                                   .arg(bootCommandName(command))
                                   .arg(QStringLiteral("%1").arg(0x600 + target, 3, 16, QLatin1Char('0')).toUpper())
                                   .arg(raw)
                                   .arg(source)
                                   .arg(target));
                }
                else
                    logRequest(QStringLiteral("%1：串口未连接或参数非法").arg(bootCommandName(command)));
            });
    m_commandDialog->open();
    m_commandDialog->raise();
    m_commandDialog->activateWindow();
}

void FirmwarePage::handleBootResponse(const BootResponse &response)
{
    const QString nodeText = QStringLiteral("0x%1").arg(response.nodeId, 2, 16, QLatin1Char('0')).toUpper();
    for (int row = 0; row < m_snapshot.nodes.size(); ++row)
    {
        if (nodeIdFromText(m_snapshot.nodes.at(row).nodeId) == response.nodeId)
        {
            m_snapshot.nodes[row].online = true;
            m_nodeTable->item(row, 4)->setText(QStringLiteral("在线"));
            if (m_targetNodeCombo != nullptr && m_targetNodeCombo->currentIndex() == row)
            {
                m_stateStatus->setText(bootStatusName(response.status));
                if (response.status == BootStatus::Error)
                {
                    const QString code =
                        QStringLiteral("0x%1").arg(response.errorCode, 2, 16, QLatin1Char('0')).toUpper();
                    const QString meaning = response.errorCode == 0
                                                 ? QStringLiteral("设备未提供具体错误原因")
                                                 : bootErrorNameZh(response.errorCode);
                    m_stateError->setText(QStringLiteral("%1（%2）").arg(meaning, code));
                }
                else
                    m_stateError->setText(QStringLiteral("无"));
                if (response.command == BootCommand::GetVersion && response.data.size() >= 3)
                    m_stateBootloader->setText(QStringLiteral("v%1.%2.%3")
                                                   .arg(static_cast<quint8>(response.data.at(0)))
                                                   .arg(static_cast<quint8>(response.data.at(1)))
                                                   .arg(static_cast<quint8>(response.data.at(2))));
                else if (response.command == BootCommand::GetInfo && response.data.size() >= 4)
                {
                    m_stateApp->setText(static_cast<quint8>(response.data.at(2)) ? QStringLiteral("Valid")
                                                                                   : QStringLiteral("Invalid"));
                    m_stateConfig->setText(static_cast<quint8>(response.data.at(3)) ? QStringLiteral("Valid")
                                                                                      : QStringLiteral("Invalid"));
                }
                else if (response.command == BootCommand::GetStatus && response.data.size() >= 3)
                {
                    m_stateStatus->setText(bootStatusName(static_cast<BootStatus>(static_cast<quint8>(response.data.at(0)))));
                    const quint8 errorCode = static_cast<quint8>(response.data.at(1));
                    const QString code =
                        QStringLiteral("0x%1").arg(errorCode, 2, 16, QLatin1Char('0')).toUpper();
                    m_stateError->setText(QStringLiteral("%1（%2）")
                                              .arg(errorCode == 0 ? QStringLiteral("无")
                                                                   : bootErrorNameZh(errorCode),
                                                   code));
                    m_stateProgress->setText(QStringLiteral("%1 %").arg(static_cast<quint8>(response.data.at(2))));
                }
            }
            break;
        }
    }
    logRequest(QStringLiteral("RX %1 · %2 · %3 · DATA=%4")
                   .arg(nodeText, bootCommandName(response.command), bootStatusName(response.status))
                   .arg(QString(response.rawData.toHex(' ').toUpper())));
}

void FirmwarePage::handlePeerMessage(const PeerControlMessage &message)
{
    logRequest(QStringLiteral("Peer · %1 · Source=%2 · Target=%3 · DATA=%4")
                   .arg(bootCommandName(message.command))
                   .arg(message.source)
                   .arg(message.target)
                   .arg(QString(message.rawData.toHex(' ').toUpper())));
}

void FirmwarePage::showHistory()
{
    if (m_historyDialog != nullptr)
    {
        if (m_historyDialog->isVisible())
        {
            m_historyDialog->raise();
            m_historyDialog->activateWindow();
            return;
        }

        // 某些 Windows 样式关闭窗口时只先隐藏，延迟销毁可能尚未执行；
        // 这里主动清理隐藏实例，确保下一次点击一定创建新窗口。
        m_historyDialog->deleteLater();
        m_historyDialog = nullptr;
    }
    // 不把历史窗口挂到 QGraphicsProxyWidget 页面上，否则页面缩放会连带缩放
    // 历史窗口。使用真正的顶层窗口，确保尺寸和位置按屏幕像素正常显示。
    QWidget *owner = window();
    // 历史窗口保持独立顶层，退出主窗口时由 closeAuxiliaryWindows() 回收。
    m_historyDialog = new FirmwareHistoryDialog(&m_historyStore, nullptr);
    m_historyDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_historyDialog->setWindowFlag(Qt::Window, true);
    m_historyDialog->setWindowModality(Qt::ApplicationModal);
    if (owner != nullptr)
    {
        const QSize ownerSize = owner->size();
        const int dialogWidth = qMax(900, ownerSize.width() * 3 / 5);
        const int dialogHeight = qMax(600, ownerSize.height() * 3 / 5);
        m_historyDialog->resize(dialogWidth, dialogHeight);
        m_historyDialog->move(owner->frameGeometry().center() - m_historyDialog->rect().center());
    }
    m_historyDialog->open();
    m_historyDialog->raise();
    m_historyDialog->activateWindow();
}

void FirmwarePage::refreshSerialDevices()
{
    if (m_serialDeviceCombo == nullptr)
        return;
    if (m_communication != nullptr && m_communication->isOpen())
        return;
    m_serialDevices = m_communication->enumerateDevices();
    QStringList ports;
    for (const auto &device : m_serialDevices)
        ports.append(device.portName);
    const QString signature = ports.join(QLatin1Char('|'));
    const bool devicesChanged = signature != m_deviceSignature;
    m_deviceSignature = signature;
    m_serialDeviceCombo->clear();
    for (const auto &device : m_serialDevices)
    {
        const QString name = device.displayName.isEmpty() ? device.portName : device.displayName;
        m_serialDeviceCombo->addItem(QStringLiteral("%1 · %2").arg(device.portName, name));
    }
    if (m_serialDevices.isEmpty())
    {
        m_serialDeviceCombo->addItem(QStringLiteral("未发现 VID_0483 PID_5740 设备"));
        m_serialStatus->setText(connectionStatusText(false, QStringLiteral("未发现 Lamost USB CDC 虚拟串口")));
        if (devicesChanged)
            logRequest(QStringLiteral("刷新设备：未发现 VID_0483 PID_5740"));
    }
    else
    {
        m_serialStatus->setText(
            connectionStatusText(false, QStringLiteral("发现 %1 个匹配设备，正在自动连接").arg(m_serialDevices.size())));
        if (devicesChanged)
            logRequest(QStringLiteral("刷新设备：发现 %1 个匹配设备").arg(m_serialDevices.size()));
        if (m_communication != nullptr && !m_communication->isOpen())
        {
            m_serialStatus->setText(connectionStatusText(
                false, QStringLiteral("正在自动连接 %1 …").arg(m_serialDevices.first().portName)));
            m_communication->open(m_serialDevices.first());
        }
    }
}

void FirmwarePage::toggleSerialConnection()
{
    if (m_communication == nullptr)
        return;
    if (m_communication->isOpen())
    {
        m_communication->close();
        return;
    }
    const int index = m_serialDeviceCombo == nullptr ? -1 : m_serialDeviceCombo->currentIndex();
    if (index < 0 || index >= m_serialDevices.size())
    {
        logRequest(QStringLiteral("请先刷新并选择 VID_0483 PID_5740 设备"));
        return;
    }
    m_communication->open(m_serialDevices.at(index));
}

void FirmwarePage::renderRuntimeLog()
{
    if (m_requestLog == nullptr)
        return;
    m_requestLog->clear();
    for (const QString &line : m_runtimeLog)
        m_requestLog->append(formatFirmwareLogHtml(line));
    m_requestLog->moveCursor(QTextCursor::End);
}

void FirmwarePage::logRequest(const QString &message)
{
    const QString timestamped =
        QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), message);
    m_runtimeLog.append(timestamped);
    while (m_runtimeLog.size() > 100)
        m_runtimeLog.removeFirst();
    renderRuntimeLog();
    m_historyStore.append(message);
    emit debugLogAppended(timestamped);
}

} // namespace rov
