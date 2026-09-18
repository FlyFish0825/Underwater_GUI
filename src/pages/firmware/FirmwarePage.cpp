#include "pages/firmware/FirmwarePage.h"

#include "preview/PreviewData.h"
#include "pages/firmware/BootloaderCommandDialog.h"
#include "pages/firmware/FirmwareHistoryDialog.h"
#include "pages/firmware/FirmwareLogFormatter.h"
#include "communication/bootloader/BootloaderDownloadController.h"
#include "communication/bootloader/BootloaderProtocol.h"
#include "ui/common/AppProgressBar.h"
#include "ui/common/UiPrimitives.h"

#include <QDateTime>
#include <QApplication>
#include <QScrollArea>
#include <QCryptographicHash>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QComboBox>
#include <QHash>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>

namespace
{

bool isSupportedFirmwarePath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("bin") || suffix == QStringLiteral("hex")
           || suffix == QStringLiteral("uf2");
}

QString firstFirmwareUrl(const QMimeData *mimeData)
{
    if (mimeData == nullptr || !mimeData->hasUrls())
        return {};

    for (const QUrl &url : mimeData->urls())
    {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (isSupportedFirmwarePath(path))
            return path;
    }
    return {};
}

class FirmwareDropZone final : public QFrame
{
  public:
    using FileHandler = std::function<void(const QString &)>;

    explicit FirmwareDropZone(QWidget *parent = nullptr) : QFrame(parent)
    {
        setAcceptDrops(true);
        setObjectName(QStringLiteral("firmwareDropZone"));
    }

    void setFileHandler(FileHandler handler) { m_fileHandler = std::move(handler); }

  protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (!firstFirmwareUrl(event->mimeData()).isEmpty())
        {
            setDropActive(true);
            event->acceptProposedAction();
            return;
        }
        event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (!firstFirmwareUrl(event->mimeData()).isEmpty())
        {
            event->acceptProposedAction();
            return;
        }
        event->ignore();
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        setDropActive(false);
        event->accept();
    }

    void dropEvent(QDropEvent *event) override
    {
        const QString path = firstFirmwareUrl(event->mimeData());
        setDropActive(false);
        if (path.isEmpty())
        {
            event->ignore();
            return;
        }
        event->acceptProposedAction();
        if (m_fileHandler)
            m_fileHandler(path);
    }

  private:
    void setDropActive(const bool active)
    {
        if (property("dropActive").toBool() == active)
            return;
        setProperty("dropActive", active);
        if (style() != nullptr)
        {
            style()->unpolish(this);
            style()->polish(this);
        }
        update();
    }

    FileHandler m_fileHandler;
};

QString humanFileSize(const qint64 bytes)
{
    const QString exact = QLocale().toString(bytes);
    if (bytes < 1024)
        return QStringLiteral("%1 B (%2 字节)").arg(bytes).arg(exact);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB (%2 字节)").arg(bytes / 1024.0, 0, 'f', 1).arg(exact);
    return QStringLiteral("%1 MB (%2 字节)").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2).arg(exact);
}

QString firmwareVersionFromName(const QString &baseName)
{
    static const QRegularExpression versionPattern(
        QStringLiteral("(?:^|[^0-9])v?(\\d+)\\.(\\d+)\\.(\\d+)(?:[^0-9]|$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = versionPattern.match(baseName);
    if (!match.hasMatch())
        return QStringLiteral("未识别");
    return QStringLiteral("v%1.%2.%3").arg(match.captured(1), match.captured(2), match.captured(3));
}

QString shortSha256(const QByteArray &digest)
{
    const QString hex = QString::fromLatin1(digest.toHex()).toLower();
    if (hex.size() <= 24)
        return hex;
    return QStringLiteral("%1…%2").arg(hex.left(12), hex.right(12));
}

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
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(8);
    root->addWidget(makePageHeader(QStringLiteral("固件升级"),
                                   QStringLiteral("按底层 Bootloader 协议下载固件并校验后启动 APP。"),
                                   QStringLiteral("手动选择 Classic CAN / CAN FD+BRS")));

    auto *mainRow = new QHBoxLayout;
    m_mainRowLayout = mainRow;
    mainRow->setSpacing(8);
    auto *fileCard = new CardWidget(QStringLiteral("固件文件"), IconKind::File);
    fileCard->contentLayout()->setContentsMargins(10, 8, 10, 10);
    fileCard->contentLayout()->setSpacing(6);
    auto *dropZone = new FirmwareDropZone;
    m_dropZone = dropZone;
    dropZone->setMinimumHeight(62);
    auto *dropLayout = new QHBoxLayout(dropZone);
    dropLayout->setContentsMargins(10, 6, 10, 6);
    dropLayout->setSpacing(8);
    auto *fileIcon = new IconWidget(IconKind::File);
    fileIcon->setFixedSize(22, 22);
    dropLayout->addWidget(fileIcon, 0, Qt::AlignVCenter);
    auto *dropText = new QVBoxLayout;
    dropText->setSpacing(1);
    m_dropTitle = makeLabel(QStringLiteral("将固件文件拖放到此处"), QStringLiteral("bodyValue"));
    m_dropHint = makeLabel(QStringLiteral("正式下载支持 .bin；.hex、.uf2 可载入查看"),
                           QStringLiteral("mutedLabel"));
    dropText->addWidget(m_dropTitle);
    dropText->addWidget(m_dropHint);
    dropLayout->addLayout(dropText, 1);
    auto *browse = makeButton(QStringLiteral("浏览…"), QStringLiteral("softButton"));
    browse->setToolTip(QStringLiteral("选择本地 .bin、.hex 或 .uf2 固件文件"));
    dropLayout->addWidget(browse, 0, Qt::AlignVCenter);
    fileCard->contentLayout()->addWidget(dropZone);
    auto *info = new QGridLayout;
    info->setVerticalSpacing(4);
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
    auto *fileDetails = new QWidget;
    fileDetails->setObjectName(QStringLiteral("firmwareFileDetails"));
    fileDetails->setStyleSheet(QStringLiteral("QWidget#firmwareFileDetails { background: white; }"));
    auto *fileDetailsLayout = new QVBoxLayout(fileDetails);
    fileDetailsLayout->setContentsMargins(0, 0, 4, 0);
    fileDetailsLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    fileDetailsLayout->addLayout(info);
    fileDetailsLayout->addStretch();
    m_fileName->setWordWrap(true);
    m_description->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto *fileDetailsScroll = new QScrollArea;
    fileDetailsScroll->setObjectName(QStringLiteral("firmwareFileDetailsScroll"));
    fileDetailsScroll->setFrameShape(QFrame::NoFrame);
    fileDetailsScroll->setWidgetResizable(true);
    fileDetailsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    fileDetailsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    fileDetailsScroll->setFixedHeight(160);
    fileDetailsScroll->setWidget(fileDetails);
    fileCard->contentLayout()->addWidget(fileDetailsScroll);
    fileCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto *left = new QVBoxLayout;
    left->setSpacing(8);
    left->addWidget(fileCard);
    auto *nodeControl = new CardWidget(QStringLiteral("节点控制"), IconKind::Action);
    nodeControl->contentLayout()->setContentsMargins(10, 8, 10, 10);
    nodeControl->contentLayout()->setSpacing(6);
    auto *targetForm = new QGridLayout;
    targetForm->addWidget(makeLabel(QStringLiteral("当前目标"), QStringLiteral("mutedLabel")), 0, 0);
    // 使用 Qt 原生下拉框，确保弹出列表始终跟随控件定位；Fluent 自定义
    // ComboBox 在当前无边框主窗口/DPI 组合下会出现弹层坐标偏移。
    m_targetNodeCombo = new QComboBox;
    targetForm->addWidget(m_targetNodeCombo, 0, 1);
    nodeControl->contentLayout()->addLayout(targetForm);
    auto *nodeBody = new QHBoxLayout;
    nodeBody->setSpacing(10);
    auto *commandColumn = new QVBoxLayout;
    commandColumn->setSpacing(5);
    commandColumn->addWidget(
        makeLabel(QStringLiteral("常用命令"), QStringLiteral("mutedLabel")));
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
    commonGrid->setHorizontalSpacing(5);
    commonGrid->setVerticalSpacing(5);
    commandColumn->addLayout(commonGrid);
    auto *advanced = makeButton(QStringLiteral("高级命令…"), QStringLiteral("softButton"));
    commandColumn->addWidget(advanced);
    commandColumn->addStretch();
    nodeBody->addLayout(commandColumn, 1);
    auto *stateColumn = new QVBoxLayout;
    stateColumn->setSpacing(5);
    stateColumn->addWidget(
        makeLabel(QStringLiteral("当前节点状态"), QStringLiteral("mutedLabel")));
    auto *state = new QGridLayout;
    state->setHorizontalSpacing(6);
    state->setVerticalSpacing(2);
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
    stateColumn->addLayout(state);
    stateColumn->addStretch();
    nodeBody->addLayout(stateColumn, 1);
    nodeControl->contentLayout()->addLayout(nodeBody);
    left->addWidget(nodeControl);
    left->addStretch();
    mainRow->addLayout(left, 3);

    auto *right = new QVBoxLayout;
    right->setSpacing(8);
    // USB CDC 只负责连接状态检查，控件由 MainWindow 放到全局连接栏。
    m_connectionBar = new QFrame;
    m_connectionBar->setObjectName(QStringLiteral("connectionBar"));
    m_connectionBar->setFixedHeight(40);
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
    connectionRow->addWidget(makeLabel(QStringLiteral("升级总线"), QStringLiteral("mutedLabel")));
    m_transferModeCombo = new QComboBox;
    m_transferModeCombo->setToolTip(QStringLiteral("手动选择固件 DATA 使用 Classic CAN 分片或 CAN FD+BRS"));
    m_transferModeCombo->addItem(QStringLiteral("Classic CAN（8 分片）"), false);
    m_transferModeCombo->addItem(QStringLiteral("CAN FD+BRS（64 字节）"), true);
    m_transferModeCombo->setCurrentIndex(0);
    m_transferModeCombo->setMinimumWidth(150);
    connectionRow->addWidget(m_transferModeCombo);
    m_serialStatus = makeLabel(connectionStatusText(false, QStringLiteral("未连接 · VID_0483 PID_5740")),
                               QStringLiteral("mutedLabel"));
    m_serialStatus->setTextFormat(Qt::RichText);
    connectionRow->addWidget(m_serialStatus, 1);

    auto *targetCard =
        new CardWidget(QStringLiteral("目标节点与升级进度（8）"), IconKind::Firmware);
    targetCard->contentLayout()->setContentsMargins(8, 6, 8, 8);
    targetCard->contentLayout()->setSpacing(5);
    m_nodeTable = new QTableWidget(0, 6);
    m_nodeTable->setHorizontalHeaderLabels(
        {QStringLiteral("节点 ID"), QStringLiteral("设备名称"), QStringLiteral("当前版本"),
         QStringLiteral("目标版本"), QStringLiteral("在线状态"), QStringLiteral("升级状态")});
    m_nodeTable->horizontalHeader()->setStretchLastSection(true);
    m_nodeTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_nodeTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nodeTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nodeTable->verticalHeader()->setDefaultSectionSize(25);
    m_nodeTable->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    // 行号栏需要同时容纳主题边距和两位数字，避免首列数字被裁切。
    m_nodeTable->verticalHeader()->setFixedWidth(46);
    // 表格高度只容纳表头和 8 行数据，避免空白视口挤占日志区域。
    m_nodeTable->setFixedHeight(8 * 25 + 36);
    m_nodeTable->setColumnWidth(0, 76);
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
    auto *progressLayout = new QGridLayout;
    progressLayout->setHorizontalSpacing(10);
    progressLayout->setVerticalSpacing(3);
    m_progressBars.clear();
    m_progressStates.clear();
    for (int i = 0; i < 8; ++i)
    {
        auto *row = new QHBoxLayout;
        row->addWidget(
            makeLabel(QStringLiteral("0x%1").arg(i + 1, 2, 16, QLatin1Char('0')).toUpper(),
                      QStringLiteral("bodyValue")));
        row->addWidget(
            makeLabel(thrusterDisplayName(i), QStringLiteral("bodyValue")), 1);
        auto *bar = new AppProgressBar;
        bar->setRange(0, 100);
        bar->setValue(i == 0 ? 100 : (i == 1 ? 65 : 0));
        row->addWidget(bar, 2);
        auto *barState = makeLabel(i == 0 ? QStringLiteral("成功") : QStringLiteral("空闲"),
                                   i == 0 ? QStringLiteral("statusGood") : QStringLiteral("statusIdle"));
        row->addWidget(barState);
        m_progressBars.append(bar);
        m_progressStates.append(barState);
        progressLayout->addLayout(row, i / 2, i % 2);
    }
    auto *actions = new QHBoxLayout;
    auto *enterBootButton = makeButton(QStringLiteral("进入 Boot"), QStringLiteral("softButton"));
    auto *eraseButton = makeButton(QStringLiteral("擦除"), QStringLiteral("softButton"));
    actions->addWidget(enterBootButton);
    actions->addWidget(eraseButton);
    m_programButton = makeButton(QStringLiteral("编程"), QStringLiteral("softButton"));
    m_programButton->setToolTip(QStringLiteral("按 Legacy 协议完整下载 BIN：擦除、写入、校验并启动 APP"));
    actions->addWidget(m_programButton);
    auto *verify = makeButton(QStringLiteral("校验"), QStringLiteral("softButton"));
    actions->addWidget(verify);
    auto *resetButton = makeButton(QStringLiteral("重启"), QStringLiteral("softButton"));
    actions->addWidget(resetButton);
    m_updateButton = makeButton(QStringLiteral("下载到选中节点"), QStringLiteral("primaryButton"));
    m_updateButton->setToolTip(QStringLiteral("对当前目标节点执行完整 Bootloader 下载"));
    actions->addWidget(m_updateButton, 1);
    progressLayout->addLayout(actions, 4, 0, 1, 2);
    targetCard->contentLayout()->addLayout(progressLayout);
    targetCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    right->addWidget(targetCard, 0);

    auto *logCard = new CardWidget(QStringLiteral("升级日志"), IconKind::List);
    logCard->contentLayout()->setContentsMargins(8, 6, 8, 8);
    logCard->contentLayout()->setSpacing(5);
    auto *clearLog = makeButton(QStringLiteral("清屏"), QStringLiteral("softButton"));
    auto *history = makeButton(QStringLiteral("历史记录"), QStringLiteral("softButton"));
    // 标题栏原有的 stretch 会把这两个按钮推到右侧，与“升级日志”保持同一行。
    auto *logHeaderLayout =
        qobject_cast<QHBoxLayout *>(logCard->titleLabel()->parentWidget()->layout());
    if (logHeaderLayout != nullptr)
    {
        logHeaderLayout->addWidget(clearLog);
        logHeaderLayout->addWidget(history);
    }
    m_requestLog = new QTextBrowser;
    m_requestLog->setReadOnly(true);
    m_requestLog->setLineWrapMode(QTextEdit::NoWrap);
    m_requestLog->setMinimumHeight(100);
    m_requestLog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_requestLog->setOpenLinks(false);
    m_requestLog->setOpenExternalLinks(false);
    logCard->contentLayout()->addWidget(m_requestLog);
    logCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    right->addWidget(logCard, 1);
    mainRow->addLayout(right, 7);
    root->addLayout(mainRow, 1);

    connect(verify, &QPushButton::clicked, this,
            [this]()
            {
                emit verifyRequested();
                if (m_firmwarePath.isEmpty())
                {
                    logRequest(QStringLiteral("校验失败：请先选择 .bin 固件文件"));
                    return;
                }
                QFile file(m_firmwarePath);
                if (!file.open(QIODevice::ReadOnly))
                {
                    logRequest(QStringLiteral("校验失败：无法读取固件：%1").arg(file.errorString()));
                    return;
                }
                const quint32 crc = BootloaderProtocol::crc32Mpeg2(file.readAll());
                QByteArray params;
                params.append(static_cast<char>(crc & 0xFFU));
                params.append(static_cast<char>((crc >> 8U) & 0xFFU));
                params.append(static_cast<char>((crc >> 16U) & 0xFFU));
                params.append(static_cast<char>((crc >> 24U) & 0xFFU));
                sendCommonCommand(BootCommand::Verify, QStringLiteral("校验固件"), 0, params);
            });
    connect(enterBootButton, &QPushButton::clicked, this,
            [this]() { sendCommonCommand(BootCommand::EnterBoot, QStringLiteral("进入 Boot")); });
    connect(eraseButton, &QPushButton::clicked, this,
            [this]() { sendCommonCommand(BootCommand::Erase, QStringLiteral("擦除 APP")); });
    connect(resetButton, &QPushButton::clicked, this,
            [this]() { sendCommonCommand(BootCommand::Reset, QStringLiteral("重启节点")); });
    dropZone->setFileHandler([this](const QString &path) { loadFirmwareFile(path); });
    connect(browse, &QPushButton::clicked, this, &FirmwarePage::browseFirmwareFile);
    connect(m_programButton, &QPushButton::clicked, this, &FirmwarePage::startFirmwareDownload);
    connect(m_updateButton, &QPushButton::clicked, this, &FirmwarePage::startFirmwareDownload);
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
    m_downloadController = new BootloaderDownloadController(m_bootloader, this);
    connect(m_downloadController, &BootloaderDownloadController::phaseChanged, this,
            [this](const QString &phase)
            {
                if (m_stateProgress != nullptr)
                    m_stateProgress->setText(phase);
            });
    connect(m_downloadController, &BootloaderDownloadController::progressChanged, this,
            &FirmwarePage::updateDownloadProgress);
    connect(m_downloadController, &BootloaderDownloadController::logMessage, this,
            &FirmwarePage::logRequest);
    connect(m_downloadController, &BootloaderDownloadController::finished, this,
            &FirmwarePage::finishFirmwareDownload);
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

    // APP 收到 ENTER_BOOT 后不会回复，而是复位进入 Bootloader；进入后自动
    // 补发一次 GET_VERSION，避免用户必须手动猜测复位是否完成。
    m_bootProbeTimer = new QTimer(this);
    m_bootProbeTimer->setSingleShot(true);
    m_bootProbeTimer->setInterval(1000);
    connect(m_bootProbeTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_bootloader == nullptr || m_communication == nullptr
                    || !m_communication->isOpen() || m_bootProbeTarget == 0)
                    return;
                if (!m_bootloader->sendHostCommand(m_bootProbeTarget, BootCommand::GetVersion))
                {
                    logRequest(QStringLiteral("自动读取版本失败：串口未连接或发送失败"));
                    return;
                }
                const QByteArray raw = BootloaderProtocol::encodeHostControl(
                                            m_bootProbeTarget, BootCommand::GetVersion)
                                            .toHex(' ')
                                            .toUpper();
                logRequest(QStringLiteral("自动探测 Node%1 · 读取版本 · CAN=0x000 · DATA=%2")
                               .arg(m_bootProbeTarget)
                               .arg(QString(raw)));
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

void FirmwarePage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_mainRowLayout == nullptr)
    {
        return;
    }

    // QScrollArea 会在布局切换前暂时按页面的旧 minimumSizeHint 扩大子页，
    // 因此这里以实际窗口宽度判断断点，避免窄屏仍被旧的横向最小宽度锁住。
    const int availableWidth = window() != nullptr ? window()->width() : event->size().width();
    const bool stacked = availableWidth < 1200;
    const QBoxLayout::Direction direction =
        stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    if (m_mainRowLayout->direction() == direction)
    {
        return;
    }

    m_mainRowLayout->setDirection(direction);
    m_mainRowLayout->setStretch(0, stacked ? 0 : 3);
    m_mainRowLayout->setStretch(1, stacked ? 0 : 7);
    m_mainRowLayout->invalidate();
    if (layout() != nullptr)
    {
        layout()->activate();
    }
    updateGeometry();
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
    // 演示快照没有真实文件路径；真正拖入/选择文件后由 m_firmwarePath 覆盖。
    if (m_snapshot.fileName.isEmpty())
        m_firmwarePath.clear();
    refreshView();
}

void FirmwarePage::browseFirmwareFile()
{
    const QString path = QFileDialog::getOpenFileName(
        QApplication::activeWindow(), QStringLiteral("选择固件文件"), QString(),
        QStringLiteral("固件文件 (*.bin *.hex *.uf2);;BIN 文件 (*.bin);;HEX 文件 (*.hex);;UF2 文件 (*.uf2)"));
    if (!path.isEmpty())
        loadFirmwareFile(path);
}

bool FirmwarePage::loadFirmwareFile(const QString &path)
{
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile() || !fileInfo.isReadable())
    {
        logRequest(QStringLiteral("错误：无法读取固件文件：%1").arg(path));
        return false;
    }
    if (!isSupportedFirmwarePath(path))
    {
        logRequest(QStringLiteral("错误：不支持的固件格式：%1（仅支持 .bin、.hex、.uf2）")
                       .arg(fileInfo.fileName()));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        logRequest(QStringLiteral("错误：打开固件失败：%1").arg(file.errorString()));
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
    {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && !file.atEnd())
        {
            logRequest(QStringLiteral("错误：读取固件失败：%1").arg(file.errorString()));
            return false;
        }
        hash.addData(chunk);
    }
    file.close();

    m_firmwarePath = fileInfo.absoluteFilePath();
    m_snapshot.fileName = fileInfo.fileName();
    m_snapshot.fileVersion = firmwareVersionFromName(fileInfo.completeBaseName());
    m_snapshot.fileSize = humanFileSize(fileInfo.size());
    m_snapshot.checksum = shortSha256(hash.result());
    m_snapshot.fileDescription = QStringLiteral("已载入本地固件，可用于正式 Bootloader 下载。\n"
                                                 "Legacy 流程支持 .bin；DATA 可手动选择 Classic CAN 或 CAN FD+BRS。\n"
                                                 "路径：%1")
                                     .arg(m_firmwarePath);
    refreshView();
    if (m_dropTitle != nullptr)
        m_dropTitle->setText(QStringLiteral("已加载：%1").arg(m_snapshot.fileName));
    if (m_dropHint != nullptr)
        m_dropHint->setText(QStringLiteral("拖入其他文件可替换 · %1").arg(m_snapshot.fileVersion));
    logRequest(QStringLiteral("已载入固件：%1 · %2 · SHA-256 %3")
                   .arg(m_snapshot.fileName, m_snapshot.fileSize, m_snapshot.checksum));
    emit firmwareFileSelected(FirmwareFileRequest{m_firmwarePath});
    return true;
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
    if (command == BootCommand::Abort && m_downloadController != nullptr
        && m_downloadController->isRunning())
    {
        cancelFirmwareDownload();
        return;
    }
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
        {
            m_bootProbeTarget = target;
            if (m_bootProbeTimer != nullptr)
                m_bootProbeTimer->start();
            logRequest(QStringLiteral("TX Node%1 · ENTER_BOOT · CAN=0x000 · DATA=%2 · APP 不回复 ACK，等待复位；1 秒后自动读取版本")
                           .arg(target)
                           .arg(raw));
        }
        else
            logRequest(QStringLiteral("TX Node%1 · %2 · CAN=0x000 · DATA=%3")
                           .arg(target)
                           .arg(bootCommandName(command))
                           .arg(raw));
    }
    else
        logRequest(QStringLiteral("%1：发送失败").arg(label));
}

void FirmwarePage::startFirmwareDownload()
{
    if (m_downloadController == nullptr || m_communication == nullptr
        || !m_communication->isOpen())
    {
        logRequest(QStringLiteral("下载失败：USB CDC 串口尚未连接，无法发送 Bootloader 命令"));
        return;
    }
    const int index = m_targetNodeCombo == nullptr ? -1 : m_targetNodeCombo->currentIndex();
    if (index < 0 || index >= m_snapshot.nodes.size())
    {
        logRequest(QStringLiteral("下载失败：请先选择目标节点"));
        return;
    }
    const QString path = m_firmwarePath;
    if (path.isEmpty())
    {
        logRequest(QStringLiteral("下载失败：请先拖入或浏览选择 .bin 固件文件"));
        return;
    }

    const quint8 target = nodeIdFromText(m_snapshot.nodes.at(index).nodeId);
    const bool canFd = m_transferModeCombo != nullptr
                       && m_transferModeCombo->currentData().toBool();
    if (!m_downloadController->start(target, path, canFd))
        return;

    m_snapshot.nodes[index].state = FirmwareState::Programming;
    m_snapshot.nodes[index].progressPercent = 0;
    if (m_nodeTable != nullptr && m_nodeTable->item(index, 5) != nullptr)
        m_nodeTable->item(index, 5)->setText(QStringLiteral("准备下载"));
    if (m_programButton != nullptr)
        m_programButton->setEnabled(false);
    if (m_updateButton != nullptr)
        m_updateButton->setEnabled(false);
    logRequest(QStringLiteral("已启动 Node %1 的正式 Bootloader 下载 · 数据面：%2")
                   .arg(target)
                   .arg(canFd ? QStringLiteral("CAN FD+BRS") : QStringLiteral("Classic CAN")));
}

void FirmwarePage::cancelFirmwareDownload()
{
    if (m_downloadController != nullptr)
        m_downloadController->cancel();
}

void FirmwarePage::updateDownloadProgress(const quint8 target, const int percent,
                                          const quint16 sequence, const int totalPackets)
{
    for (int row = 0; row < m_snapshot.nodes.size(); ++row)
    {
        if (nodeIdFromText(m_snapshot.nodes.at(row).nodeId) != target)
            continue;
        m_snapshot.nodes[row].progressPercent = percent;
        if (m_nodeTable != nullptr && m_nodeTable->item(row, 5) != nullptr)
            m_nodeTable->item(row, 5)->setText(percent >= 100 ? QStringLiteral("成功")
                                                               : QStringLiteral("下载中 %1%").arg(percent));
        if (row < m_progressBars.size() && m_progressBars.at(row) != nullptr)
            m_progressBars.at(row)->setValue(percent);
        if (row < m_progressStates.size() && m_progressStates.at(row) != nullptr)
        {
            m_progressStates.at(row)->setText(percent >= 100 ? QStringLiteral("成功")
                                                               : QStringLiteral("%1%").arg(percent));
            m_progressStates.at(row)->setProperty("class",
                                                  percent >= 100 ? QStringLiteral("statusGood")
                                                                 : QStringLiteral("statusBusy"));
        }
        if (m_targetNodeCombo != nullptr && m_targetNodeCombo->currentIndex() == row
            && m_stateProgress != nullptr)
        {
            m_stateProgress->setText(QStringLiteral("%1%（%2/%3 包）")
                                         .arg(percent)
                                         .arg(sequence)
                                         .arg(totalPackets));
        }
        break;
    }
}

void FirmwarePage::finishFirmwareDownload(const bool success, const QString &message)
{
    const int index = m_targetNodeCombo == nullptr ? -1 : m_targetNodeCombo->currentIndex();
    if (index >= 0 && index < m_snapshot.nodes.size())
    {
        m_snapshot.nodes[index].state = success ? FirmwareState::Completed : FirmwareState::Failed;
        if (success)
            m_snapshot.nodes[index].progressPercent = 100;
        if (m_nodeTable != nullptr && m_nodeTable->item(index, 5) != nullptr)
            m_nodeTable->item(index, 5)->setText(success ? QStringLiteral("成功")
                                                         : QStringLiteral("失败"));
        if (!success && index < m_progressStates.size() && m_progressStates.at(index) != nullptr)
            m_progressStates.at(index)->setText(QStringLiteral("失败"));
    }
    if (m_programButton != nullptr)
        m_programButton->setEnabled(true);
    if (m_updateButton != nullptr)
        m_updateButton->setEnabled(true);
    if (m_stateProgress != nullptr)
        m_stateProgress->setText(message);
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
