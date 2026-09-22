#include "pages/firmware/FirmwarePage.h"

#include "preview/PreviewData.h"
#include "pages/firmware/BootloaderCommandDialog.h"
#include "pages/firmware/FirmwareHistoryDialog.h"
#include "pages/firmware/FirmwareLogRecordingDialog.h"
#include "pages/firmware/FirmwareLogFormatter.h"
#include "communication/bootloader/BootloaderDownloadController.h"
#include "communication/bootloader/BootloaderProtocol.h"
#include "ui/common/AppProgressBar.h"
#include "ui/common/UiPrimitives.h"

#include <QDateTime>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
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
#include <QInputDialog>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>

namespace
{

constexpr qint64 kAppPartitionSize = 106 * 1024;

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
        {QStringLiteral("JUMP_APP"), QStringLiteral("安全试运行 APP")},
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
        {QStringLiteral("WINDOW_STATUS"), QStringLiteral("窗口写入状态")},
        {QStringLiteral("GUARD_UPDATE_BEGIN"), QStringLiteral("开始更新保护")},
        {QStringLiteral("GUARD_UPDATE_READY"), QStringLiteral("保护更新就绪")},
        {QStringLiteral("ROLLBACK_REQUEST"), QStringLiteral("请求回滚")},
        {QStringLiteral("ROLLBACK_SIZE_LO"), QStringLiteral("回滚大小低字")},
        {QStringLiteral("ROLLBACK_SIZE_HI"), QStringLiteral("回滚大小高字")},
        {QStringLiteral("ROLLBACK_CRC_LO"), QStringLiteral("回滚 CRC 低字")},
        {QStringLiteral("ROLLBACK_CRC_HI"), QStringLiteral("回滚 CRC 高字")},
        {QStringLiteral("ROLLBACK_BEGIN"), QStringLiteral("开始回滚")},
        {QStringLiteral("ROLLBACK_PREPARED"), QStringLiteral("回滚准备完成")},
        {QStringLiteral("FULL_STREAM"), QStringLiteral("完整流传输")},
        {QStringLiteral("COMMIT_PREPARE"), QStringLiteral("准备提交")},
        {QStringLiteral("COMMIT_ACK"), QStringLiteral("提交确认")},
        {QStringLiteral("COMMIT_EXECUTE"), QStringLiteral("执行提交")},
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
    root->addWidget(makePageHeader(QStringLiteral("安全固件升级控制台"),
                                   QStringLiteral("选择固件与目标节点后，由升级控制器自动执行安全下载和验证。"),
                                   QStringLiteral("正常升级与协议调试相互隔离")));

    auto *modeBar = new QFrame;
    modeBar->setObjectName(QStringLiteral("connectionBar"));
    auto *modeLayout = new QHBoxLayout(modeBar);
    modeLayout->setContentsMargins(12, 5, 12, 5);
    modeLayout->setSpacing(6);
    modeLayout->addWidget(makeLabel(QStringLiteral("升级模式："), QStringLiteral("sectionTitle")));
    auto *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    const QStringList modeNames = {QStringLiteral("单节点安全升级"),
                                   QStringLiteral("多节点自治升级"),
                                   QStringLiteral("协议调试模式")};
    for (int mode = 0; mode < modeNames.size(); ++mode)
    {
        auto *button = makeButton(modeNames.at(mode), QStringLiteral("softButton"));
        button->setCheckable(true);
        button->setChecked(mode == 0);
        modeGroup->addButton(button, mode);
        modeLayout->addWidget(button);
    }
    modeLayout->addStretch();
    m_demoBanner = makeLabel(QStringLiteral("🧪 演示/离线模式 · 不会发送任何 CAN 控制命令"),
                             QStringLiteral("statusIdle"));
    modeLayout->addWidget(m_demoBanner);
    root->addWidget(modeBar);

    auto *mainRow = new QHBoxLayout;
    m_mainRowLayout = mainRow;
    mainRow->setSpacing(8);
    auto *fileCard = new CardWidget(QStringLiteral("固件文件与安全检查"), IconKind::File);
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
    info->addWidget(makeLabel(QStringLiteral("目标区域"), QStringLiteral("mutedLabel")), 3, 0);
    info->addWidget(makeLabel(QStringLiteral("APP"), QStringLiteral("bodyValue")), 3, 1);
    info->addWidget(makeLabel(QStringLiteral("CRC32"), QStringLiteral("mutedLabel")), 4, 0);
    m_checksum = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    info->addWidget(m_checksum, 4, 1);
    info->addWidget(makeLabel(QStringLiteral("说明"), QStringLiteral("mutedLabel")), 5, 0);
    m_description = makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    m_description->setWordWrap(true);
    info->addWidget(m_description, 5, 1);
    m_fileValidation = makeLabel(QStringLiteral("请选择 BIN 固件"), QStringLiteral("statusIdle"));
    m_fileValidation->setWordWrap(true);
    info->addWidget(m_fileValidation, 6, 0, 1, 2);
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
    auto *nodeControl = new CardWidget(QStringLiteral("节点信息卡"), IconKind::Action);
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
    commandColumn->addWidget(makeLabel(QStringLiteral("节点操作"), QStringLiteral("mutedLabel")));
    auto *refreshNode = makeButton(QStringLiteral("刷新状态"), QStringLiteral("softButton"));
    commandColumn->addWidget(refreshNode);
    auto *more = makeButton(QStringLiteral("更多…"), QStringLiteral("softButton"));
    auto *moreMenu = new QMenu(more);
    QAction *enterBootAction = moreMenu->addAction(QStringLiteral("进入 Bootloader"));
    QAction *trialAction = moreMenu->addAction(QStringLiteral("试运行验证"));
    QAction *resetAction = moreMenu->addAction(QStringLiteral("复位节点"));
    moreMenu->addSeparator();
    QAction *readFlashAction = moreMenu->addAction(QStringLiteral("读取 Flash"));
    QAction *protocolAction = moreMenu->addAction(QStringLiteral("协议调试"));
    more->setMenu(moreMenu);
    commandColumn->addWidget(more);
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
    addState(QStringLiteral("APP Valid"), m_stateConfig, 4);
    addState(QStringLiteral("Status"), m_stateStatus, 5);
    addState(QStringLiteral("Last Error"), m_stateError, 6);
    addState(QStringLiteral("响应/进度"), m_stateProgress, 7);
    // 完成消息可能包含较长的验证结果，必须让值列吸收可用宽度并在卡片内换行，
    // 避免 QLabel 的单行 sizeHint 把“节点控制”卡片和整页横向撑宽。
    state->setColumnStretch(1, 1);
    m_stateProgress->setWordWrap(true);
    m_stateProgress->setMinimumWidth(0);
    m_stateProgress->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
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
    m_serialConnectButton = makeButton(QStringLiteral("进入实机模式"), QStringLiteral("primaryButton"));
    connectionRow->addWidget(m_serialConnectButton);
    connectionRow->addWidget(makeLabel(QStringLiteral("升级总线"), QStringLiteral("mutedLabel")));
    m_transferModeCombo = new QComboBox;
    m_transferModeCombo->setToolTip(
        QStringLiteral("正式下载固定使用 Classic CAN；仲裁速率 1 Mbit/s，每个逻辑块由 H750 拆成 8 帧"));
    m_transferModeCombo->addItem(QStringLiteral("Classic CAN（1M，8 分片）"), false);
    m_transferModeCombo->setCurrentIndex(0);
    m_transferModeCombo->setMinimumWidth(150);
    connectionRow->addWidget(m_transferModeCombo);
    m_serialStatus = makeLabel(connectionStatusText(false, QStringLiteral("未连接 · VID_0483 PID_5740")),
                               QStringLiteral("mutedLabel"));
    m_serialStatus->setTextFormat(Qt::RichText);
    connectionRow->addWidget(m_serialStatus, 1);

    auto *multiCard = new CardWidget(QStringLiteral("多节点自治升级配置"), IconKind::Firmware);
    m_multiModePanel = multiCard;
    multiCard->contentLayout()->setContentsMargins(10, 6, 10, 8);
    auto *multiNodes = new QGridLayout;
    multiNodes->addWidget(makeLabel(QStringLiteral("目标节点"), QStringLiteral("mutedLabel")), 0, 0);
    for (int node = 1; node <= 8; ++node)
    {
        auto *check = new QCheckBox(QStringLiteral("Node%1 %2").arg(node).arg(thrusterDisplayName(node - 1)));
        check->setChecked(node == 1);
        m_multiNodeChecks.append(check);
        multiNodes->addWidget(check, 1 + (node - 1) / 4, (node - 1) % 4);
    }
    multiCard->contentLayout()->addLayout(multiNodes);
    auto *roles = new QHBoxLayout;
    roles->addWidget(makeLabel(QStringLiteral("Canary"), QStringLiteral("mutedLabel")));
    m_canaryNodeCombo = new QComboBox;
    roles->addWidget(m_canaryNodeCombo);
    roles->addWidget(makeLabel(QStringLiteral("Guard"), QStringLiteral("mutedLabel")));
    m_guardNodeCombo = new QComboBox;
    roles->addWidget(m_guardNodeCombo);
    for (int node = 1; node <= 8; ++node)
    {
        const QString nodeName = QStringLiteral("Node%1 %2").arg(node).arg(thrusterDisplayName(node - 1));
        m_canaryNodeCombo->addItem(nodeName, node);
        m_guardNodeCombo->addItem(nodeName, node);
    }
    m_guardNodeCombo->setCurrentIndex(7);
    roles->addSpacing(14);
    for (const QString &policy : {QStringLiteral("Peer Recovery"),
                                  QStringLiteral("Guard Rollback"),
                                  QStringLiteral("Distributed Verify")})
    {
        auto *option = new QCheckBox(policy);
        option->setChecked(true);
        roles->addWidget(option);
    }
    roles->addStretch();
    multiCard->contentLayout()->addLayout(roles);
    right->addWidget(m_multiModePanel);

    auto *protocolCard = new CardWidget(QStringLiteral("协议调试模式"), IconKind::Action);
    m_protocolModePanel = protocolCard;
    protocolCard->contentLayout()->setContentsMargins(10, 6, 10, 8);
    auto *protocolRow = new QHBoxLayout;
    protocolRow->addWidget(makeLabel(QStringLiteral("高级命令与原始参数仅用于协议联调，正常升级无需手动发送。"),
                                     QStringLiteral("mutedLabel")), 1);
    auto *openProtocol = makeButton(QStringLiteral("打开协议调试控制台"), QStringLiteral("softButton"));
    protocolRow->addWidget(openProtocol);
    protocolCard->contentLayout()->addLayout(protocolRow);
    right->addWidget(m_protocolModePanel);

    auto *targetCard =
        new CardWidget(QStringLiteral("节点升级状态（8）"), IconKind::Firmware);
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
    actions->addStretch();
    m_updateButton = makeButton(QStringLiteral("下载到选中节点"), QStringLiteral("primaryButton"));
    m_updateButton->setToolTip(QStringLiteral("对当前目标节点执行完整 Bootloader 下载"));
    actions->addWidget(m_updateButton);
    progressLayout->addLayout(actions, 4, 0, 1, 2);
    targetCard->contentLayout()->addLayout(progressLayout);
    targetCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    right->addWidget(targetCard, 0);

    auto *logCard = new CardWidget(QStringLiteral("CAN 升级日志"), IconKind::List);
    // 日志本身会持续增长，局部压缩卡片边距，给有效日志内容更多可见高度。
    logCard->contentLayout()->setContentsMargins(6, 3, 6, 4);
    logCard->contentLayout()->setSpacing(2);
    auto *clearLog = makeButton(QStringLiteral("清屏"), QStringLiteral("softButton"));
    auto *history = makeButton(QStringLiteral("历史记录"), QStringLiteral("softButton"));
    m_logRecordButton = makeButton(QStringLiteral("开始记录"), QStringLiteral("softButton"));
    m_viewRecordedButton = makeButton(QStringLiteral("查看记录"), QStringLiteral("softButton"));
    m_exportRecordedButton = makeButton(QStringLiteral("导出记录"), QStringLiteral("softButton"));
    m_viewRecordedButton->setEnabled(false);
    m_exportRecordedButton->setEnabled(false);
    // 标题栏原有的 stretch 会把这两个按钮推到右侧，与“升级日志”保持同一行。
    auto *logHeaderLayout =
        qobject_cast<QHBoxLayout *>(logCard->titleLabel()->parentWidget()->layout());
    if (logHeaderLayout != nullptr)
    {
        // 只压缩这张日志卡片的标题栏，不改变其他页面的 CardWidget 外观。
        logHeaderLayout->setContentsMargins(8, 4, 8, 4);
        logHeaderLayout->setSpacing(4);
        logHeaderLayout->addWidget(m_logRecordButton);
        logHeaderLayout->addWidget(m_viewRecordedButton);
        logHeaderLayout->addWidget(m_exportRecordedButton);
        logHeaderLayout->addWidget(clearLog);
        logHeaderLayout->addWidget(history);
    }
    for (QPushButton *button : {m_logRecordButton, m_viewRecordedButton, m_exportRecordedButton,
                                clearLog, history})
        button->setMaximumHeight(26);

    // 日志筛选只影响当前卡片的显示，不会删除、修改或过滤底层通信数据。
    auto *logFilterLayout = new QHBoxLayout;
    logFilterLayout->setContentsMargins(0, 0, 0, 0);
    logFilterLayout->setSpacing(3);
    m_logFilter = new QComboBox;
    m_logFilter->addItems({QStringLiteral("全部"), QStringLiteral("升级"), QStringLiteral("接收 RX"),
                           QStringLiteral("发送 TX"), QStringLiteral("警告"), QStringLiteral("错误"),
                           QStringLiteral("Classic CAN"), QStringLiteral("CAN FD")});
    m_logFilter->setToolTip(QStringLiteral("仅筛选日志卡片中显示的内容"));
    m_logFilter->setFixedHeight(26);
    logFilterLayout->addWidget(m_logFilter);
    m_logNodeFilter = new QComboBox;
    m_logNodeFilter->addItem(QStringLiteral("全部节点"), 0);
    for (int node = 1; node <= 8; ++node)
        m_logNodeFilter->addItem(QStringLiteral("节点 %1").arg(node), node);
    m_logNodeFilter->setToolTip(QStringLiteral("按节点筛选显示日志"));
    m_logNodeFilter->setFixedHeight(26);
    logFilterLayout->addWidget(m_logNodeFilter);
    m_logCanIdFilter = new QLineEdit;
    m_logCanIdFilter->setPlaceholderText(QStringLiteral("CAN ID"));
    m_logCanIdFilter->setMaximumWidth(100);
    m_logCanIdFilter->setToolTip(QStringLiteral("例如 0x501 或 501"));
    m_logCanIdFilter->setFixedHeight(26);
    logFilterLayout->addWidget(m_logCanIdFilter);
    m_logSearchFilter = new QLineEdit;
    m_logSearchFilter->setPlaceholderText(QStringLiteral("搜索日志"));
    m_logSearchFilter->setMinimumWidth(120);
    m_logSearchFilter->setFixedHeight(26);
    logFilterLayout->addWidget(m_logSearchFilter, 1);
    m_logFollowButton = makeButton(QStringLiteral("跟随最新"), QStringLiteral("softButton"));
    m_logFollowButton->setCheckable(true);
    m_logFollowButton->setChecked(true);
    m_logFollowButton->setToolTip(QStringLiteral("滚动到日志底部后自动显示最新记录"));
    m_logFollowButton->setFixedHeight(26);
    logFilterLayout->addWidget(m_logFollowButton);
    m_logPauseButton = makeButton(QStringLiteral("暂停显示"), QStringLiteral("softButton"));
    m_logPauseButton->setCheckable(true);
    m_logPauseButton->setToolTip(QStringLiteral("暂停界面刷新，不影响后台接收和历史记录保存"));
    m_logPauseButton->setFixedHeight(26);
    logFilterLayout->addWidget(m_logPauseButton);
    logCard->contentLayout()->addLayout(logFilterLayout);

    m_requestLog = new QTextBrowser;
    m_requestLog->setReadOnly(true);
    m_requestLog->setLineWrapMode(QTextEdit::WidgetWidth);
    m_requestLog->document()->setDocumentMargin(3);
    m_requestLog->setMinimumHeight(100);
    m_requestLog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_requestLog->setOpenLinks(false);
    m_requestLog->setOpenExternalLinks(false);
    logCard->contentLayout()->addWidget(m_requestLog);
    logCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    right->addWidget(logCard, 1);
    mainRow->addLayout(right, 7);
    root->addLayout(mainRow, 1);

    connect(refreshNode, &QPushButton::clicked, this,
            [this]()
            {
                sendCommonCommand(BootCommand::GetVersion, QStringLiteral("刷新 Bootloader 版本"));
                sendCommonCommand(BootCommand::GetInfo, QStringLiteral("刷新设备信息"));
                sendCommonCommand(BootCommand::GetStatus, QStringLiteral("刷新运行状态"));
            });
    connect(enterBootAction, &QAction::triggered, this,
            [this]() { sendCommonCommand(BootCommand::EnterBoot, QStringLiteral("进入 Bootloader")); });
    connect(trialAction, &QAction::triggered, this,
            [this]() { sendCommonCommand(BootCommand::JumpApp, QStringLiteral("试运行验证"), 0x01U); });
    connect(resetAction, &QAction::triggered, this,
            [this]() { sendCommonCommand(BootCommand::Reset, QStringLiteral("复位节点")); });
    connect(readFlashAction, &QAction::triggered, this, &FirmwarePage::showCommandCenter);
    connect(protocolAction, &QAction::triggered, this, &FirmwarePage::showCommandCenter);
    dropZone->setFileHandler([this](const QString &path) { loadFirmwareFile(path); });
    connect(browse, &QPushButton::clicked, this, &FirmwarePage::browseFirmwareFile);
    connect(m_updateButton, &QPushButton::clicked, this, &FirmwarePage::startFirmwareDownload);
    connect(clearLog, &QPushButton::clicked, this,
            [this]()
            {
                m_runtimeLog.clear();
                if (m_requestLog != nullptr)
                    m_requestLog->clear();
            });
    connect(history, &QPushButton::clicked, this, &FirmwarePage::showHistory);
    connect(m_logRecordButton, &QPushButton::clicked, this, &FirmwarePage::toggleLogRecording);
    connect(m_viewRecordedButton, &QPushButton::clicked, this, &FirmwarePage::showRecordedLogs);
    connect(m_exportRecordedButton, &QPushButton::clicked, this, &FirmwarePage::exportRecordedLogs);
    const auto rerenderLog = [this]()
    {
        if (!m_logPaused)
            renderRuntimeLog();
    };
    connect(m_logFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, rerenderLog);
    connect(m_logNodeFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, rerenderLog);
    connect(m_logCanIdFilter, &QLineEdit::textChanged, this, rerenderLog);
    connect(m_logSearchFilter, &QLineEdit::textChanged, this, rerenderLog);
    connect(m_logFollowButton, &QPushButton::toggled, this,
            [this](const bool follow)
            {
                m_logFollowing = follow;
                if (follow && m_requestLog != nullptr)
                    m_requestLog->moveCursor(QTextCursor::End);
            });
    connect(m_logPauseButton, &QPushButton::toggled, this,
            [this](const bool paused)
            {
                m_logPaused = paused;
                m_logPauseButton->setText(paused ? QStringLiteral("继续显示") : QStringLiteral("暂停显示"));
                if (!paused)
                    renderRuntimeLog();
            });
    connect(m_requestLog->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](const int value)
            {
                if (m_requestLog == nullptr || m_logFollowButton == nullptr)
                    return;
                const QScrollBar *bar = m_requestLog->verticalScrollBar();
                const bool atBottom = value >= bar->maximum() - 2;
                if (atBottom == m_logFollowing)
                    return;
                m_logFollowing = atBottom;
                QSignalBlocker blocker(m_logFollowButton);
                m_logFollowButton->setChecked(atBottom);
            });

    m_communication = new BootloaderCommunicationService(this);
    m_bootloader = new BootloaderService(m_communication, this);
    m_downloadController = new BootloaderDownloadController(m_bootloader, this);
    connect(m_downloadController, &BootloaderDownloadController::phaseChanged, this,
            [this](const QString &phase)
            {
                if (m_stateProgress != nullptr)
                    m_stateProgress->setText(phase);
                updateNodePhase(phase);
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
    connect(openProtocol, &QPushButton::clicked, this, &FirmwarePage::showCommandCenter);
    connect(modeGroup, qOverload<int>(&QButtonGroup::buttonClicked), this,
            &FirmwarePage::setUpgradeMode);
    setUpgradeMode(0);
    connect(refreshSerial, &QPushButton::clicked, this, &FirmwarePage::refreshSerialDevices);
    connect(m_serialConnectButton, &QPushButton::clicked, this,
            &FirmwarePage::toggleSerialConnection);
    connect(m_communication, &BootloaderCommunicationService::opened, this,
            [this](const QString &port)
            {
                m_heartbeatCount = 0;
                m_heartbeatWatchdog->start();
                m_serialConnectButton->setText(QStringLiteral("退出实机模式"));
                m_serialStatus->setText(
                    connectionStatusText(true, QStringLiteral("已接管 %1 · 等待下位机心跳").arg(port)));
                if (m_demoBanner != nullptr)
                    m_demoBanner->setText(QStringLiteral("实机模式 · CAN 控制命令已启用"));
                updateSafetyLock();
                logRequest(QStringLiteral("已打开 %1").arg(port));
            });
    connect(m_communication, &BootloaderCommunicationService::closed, this,
            [this]()
            {
                if (m_heartbeatWatchdog != nullptr)
                    m_heartbeatWatchdog->stop();
                m_serialConnectButton->setText(QStringLiteral("进入实机模式"));
                m_serialStatus->setText(connectionStatusText(false, QStringLiteral("未连接 · VID_0483 PID_5740")));
                logRequest(QStringLiteral("串口已断开"));
                updateSafetyLock();
            });
    connect(m_communication, &BootloaderCommunicationService::errorOccurred, this,
            [this](const QString &message)
            {
                m_serialStatus->setText(connectionStatusText(false, QStringLiteral("通信错误：%1").arg(message)));
                logRequest(QStringLiteral("错误：%1").arg(message));
            });
    // 不把所有 CAN 帧写入日志：电机普通反馈可达 100 Hz/更高频率，
    // 全量格式化、渲染和保存会拖慢下载页面。Bootloader 响应和下载阶段
    // 仍由 BootloaderService/下载控制器通过 logRequest() 记录。
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
    if (m_recordingDialog != nullptr)
    {
        auto *dialog = m_recordingDialog.data();
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        dialog->close();
        delete dialog;
        m_recordingDialog = nullptr;
    }
}

void FirmwarePage::setUpgradeMode(const int mode)
{
    m_upgradeMode = qBound(0, mode, 2);
    if (m_multiModePanel != nullptr)
        m_multiModePanel->setVisible(m_upgradeMode == 1);
    if (m_protocolModePanel != nullptr)
        m_protocolModePanel->setVisible(m_upgradeMode == 2);
    if (m_updateButton != nullptr)
    {
        if (m_upgradeMode == 0)
        {
            m_updateButton->setText(QStringLiteral("开始安全升级"));
            m_updateButton->setToolTip(QStringLiteral("对当前目标节点执行完整 Bootloader 下载"));
        }
        else if (m_upgradeMode == 1)
        {
            m_updateButton->setText(QStringLiteral("多节点升级待接入"));
            m_updateButton->setToolTip(QStringLiteral("当前仅配置角色与策略，不发送协议命令"));
        }
        else
        {
            m_updateButton->setText(QStringLiteral("开始安全升级"));
        }
        m_updateButton->setVisible(m_upgradeMode != 2);
    }
    updateSafetyLock();
}

void FirmwarePage::updateSafetyLock()
{
    if (m_updateButton == nullptr)
        return;
    const int index = m_targetNodeCombo == nullptr ? -1 : m_targetNodeCombo->currentIndex();
    const bool connected = m_communication != nullptr && m_communication->isOpen();
    const bool nodeOnline = index >= 0 && index < m_snapshot.nodes.size()
                            && m_snapshot.nodes.at(index).online;
    const bool versionValid = m_stateBootloader != nullptr
                              && m_stateBootloader->text().startsWith(QLatin1Char('v'));
    const bool idle = m_downloadController == nullptr || !m_downloadController->isRunning();
    const bool enabled = m_upgradeMode == 0 && connected && m_firmwareValid && nodeOnline
                         && versionValid && idle;
    m_updateButton->setEnabled(enabled);

    QStringList missing;
    if (!connected)
        missing.append(QStringLiteral("CAN 未连接"));
    if (!m_firmwareValid)
        missing.append(QStringLiteral("固件未通过安全检查"));
    if (!nodeOnline)
        missing.append(QStringLiteral("节点离线"));
    if (!versionValid)
        missing.append(QStringLiteral("版本未读取"));
    if (!idle)
        missing.append(QStringLiteral("升级任务进行中"));
    if (m_upgradeMode == 1)
    {
        missing.clear();
        missing.append(QStringLiteral("自治协调器尚未接入，仅允许配置角色与策略"));
    }
    m_updateButton->setToolTip(enabled ? QStringLiteral("安全条件已满足，可开始升级")
                                       : missing.join(QStringLiteral("；")));
}

void FirmwarePage::updateNodePhase(const QString &phase)
{
    const int row = m_targetNodeCombo == nullptr ? -1 : m_targetNodeCombo->currentIndex();
    if (row < 0 || row >= m_snapshot.nodes.size())
        return;
    QString state = QStringLiteral("检测中");
    if (phase.contains(QStringLiteral("擦除")))
        state = QStringLiteral("擦除中");
    else if (phase.contains(QStringLiteral("发送")) || phase.contains(QStringLiteral("写入")))
        state = QStringLiteral("下载中");
    else if (phase.contains(QStringLiteral("校验")))
        state = QStringLiteral("校验中");
    else if (phase.contains(QStringLiteral("Trial"), Qt::CaseInsensitive)
             || phase.contains(QStringLiteral("试运行")))
        state = QStringLiteral("试运行");
    else if (phase.contains(QStringLiteral("完成")))
        state = QStringLiteral("升级完成");
    else if (phase.contains(QStringLiteral("失败")))
        state = QStringLiteral("失败");
    if (m_nodeTable != nullptr && m_nodeTable->item(row, 5) != nullptr)
        m_nodeTable->item(row, 5)->setText(state);
    if (row < m_progressStates.size() && m_progressStates.at(row) != nullptr)
        m_progressStates.at(row)->setText(state);
}

bool FirmwarePage::confirmDangerousOperation(const BootCommand command, const quint8 target)
{
    QString expected;
    if (command == BootCommand::Erase)
        expected = QStringLiteral("ERASE");
    else if (command == BootCommand::Reset)
        expected = QStringLiteral("RESET NODE%1").arg(target);
    else if (command == BootCommand::ReleaseGuard)
        expected = QStringLiteral("RELEASE GUARD");
    else
        return true;

    bool accepted = false;
    const QString input = QInputDialog::getText(window(), QStringLiteral("确认危险操作"),
                                                QStringLiteral("请输入 %1 以继续：").arg(expected),
                                                QLineEdit::Normal, QString(), &accepted);
    return accepted && input.trimmed().compare(expected, Qt::CaseInsensitive) == 0;
}

QWidget *FirmwarePage::connectionBar() const
{
    return m_connectionBar;
}

BootloaderCommunicationService *FirmwarePage::communicationService() const
{
    return m_communication;
}

void FirmwarePage::setSnapshot(const FirmwareSnapshot &snapshot)
{
    m_snapshot = snapshot;
    // 演示快照没有真实文件路径；真正拖入/选择文件后由 m_firmwarePath 覆盖。
    if (m_snapshot.fileName.isEmpty())
    {
        m_firmwarePath.clear();
        m_firmwareValid = false;
    }
    refreshView();
    updateSafetyLock();
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
    m_firmwareValid = false;
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

    const QByteArray image = file.readAll();
    if (file.error() != QFile::NoError)
    {
        logRequest(QStringLiteral("错误：读取固件失败：%1").arg(file.errorString()));
        return false;
    }
    file.close();

    m_firmwarePath = fileInfo.absoluteFilePath();
    m_snapshot.fileName = fileInfo.fileName();
    m_snapshot.fileVersion = firmwareVersionFromName(fileInfo.completeBaseName());
    m_snapshot.fileSize = humanFileSize(fileInfo.size());
    m_snapshot.checksum = QStringLiteral("%1")
                              .arg(BootloaderProtocol::crc32Mpeg2(image), 8, 16, QLatin1Char('0'))
                              .toUpper();
    const bool isBin = fileInfo.suffix().compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0;
    m_firmwareValid = isBin && !image.isEmpty() && image.size() <= kAppPartitionSize;
    QString validationText;
    if (!isBin)
        validationText = QStringLiteral("错误：正式升级只接受 BIN 固件");
    else if (image.isEmpty())
        validationText = QStringLiteral("错误：固件文件为空");
    else if (image.size() > kAppPartitionSize)
        validationText = QStringLiteral("错误：固件超过 APP 分区（%1 / 106 KiB）")
                             .arg(humanFileSize(image.size()));
    else
        validationText = QStringLiteral("固件合法 · APP 分区 · 可开始安全升级");
    m_snapshot.fileDescription = QStringLiteral("路径：%1").arg(m_firmwarePath);
    refreshView();
    if (m_fileValidation != nullptr)
    {
        m_fileValidation->setText(validationText);
        m_fileValidation->setProperty("class", m_firmwareValid ? QStringLiteral("statusGood")
                                                               : QStringLiteral("statusDanger"));
        m_fileValidation->style()->unpolish(m_fileValidation);
        m_fileValidation->style()->polish(m_fileValidation);
    }
    if (m_dropTitle != nullptr)
        m_dropTitle->setText(QStringLiteral("已加载：%1").arg(m_snapshot.fileName));
    if (m_dropHint != nullptr)
        m_dropHint->setText(QStringLiteral("拖入其他文件可替换 · %1").arg(m_snapshot.fileVersion));
    logRequest(QStringLiteral("已载入固件：%1 · %2 · CRC32 %3 · %4")
                   .arg(m_snapshot.fileName, m_snapshot.fileSize, m_snapshot.checksum,
                        validationText));
    if (!m_firmwareValid)
        logRequest(validationText);
    emit firmwareFileSelected(FirmwareFileRequest{m_firmwarePath});
    updateSafetyLock();
    return m_firmwareValid;
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
    updateSafetyLock();
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
    // 统一安全边界：页面上任何 JUMP_APP 都只能走 Trial 模式。不能让普通
    // Byte2=0x00 跳转绕过 Bootloader 看门狗和 APP 返回 Boot 的验证。
    if (command == BootCommand::JumpApp)
        byte2 = 0x01U;
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
    if (!confirmDangerousOperation(command, target))
    {
        logRequest(QStringLiteral("已取消危险操作：%1").arg(label));
        return;
    }
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
        else if (command == BootCommand::JumpApp)
            logRequest(QStringLiteral("TX Node%1 · JUMP_APP · CAN=0x000 · DATA=%2 · 安全 Trial 跳转：Bootloader 看门狗已接管，请随后发送 ENTER_BOOT 验证返回")
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

void FirmwarePage::startFirmwareDownload()
{
    updateSafetyLock();
    if (m_updateButton == nullptr || !m_updateButton->isEnabled())
    {
        logRequest(QStringLiteral("安全锁未解除：%1")
                       .arg(m_updateButton == nullptr ? QStringLiteral("升级入口不可用")
                                                      : m_updateButton->toolTip()));
        return;
    }
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
    /*
     * 正式固件下载固定走 Classic CAN。后端仍保留 CAN FD 能力，便于以后
     * 硬件时钟和总线验证完成后恢复，但当前页面不允许误选 FD 数据面。
     */
    constexpr bool canFd = false;
    if (!m_downloadController->start(target, path, canFd))
        return;

    m_snapshot.nodes[index].state = FirmwareState::Programming;
    m_snapshot.nodes[index].progressPercent = 0;
    if (m_nodeTable != nullptr && m_nodeTable->item(index, 5) != nullptr)
        m_nodeTable->item(index, 5)->setText(QStringLiteral("准备下载"));
    if (m_updateButton != nullptr)
        m_updateButton->setEnabled(false);
    updateSafetyLock();
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
    if (m_updateButton != nullptr)
        m_updateButton->setEnabled(true);
    if (m_stateProgress != nullptr)
        m_stateProgress->setText(message);
    updateNodePhase(success ? QStringLiteral("升级完成") : QStringLiteral("失败"));
    updateSafetyLock();
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
                // 命令中心也不能绕过页面的 Trial 保护，即使在协议开发模式下
                // 手动输入了 Byte2=0x00，也强制改为 Trial Jump。
                if (command == BootCommand::JumpApp)
                    byte2 = 0x01U;
                if (!confirmDangerousOperation(command, target))
                {
                    logRequest(QStringLiteral("已取消危险操作：%1").arg(bootCommandName(command)));
                    return;
                }
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
                    else if (command == BootCommand::JumpApp)
                        logRequest(QStringLiteral("TX Node%1 · JUMP_APP · CAN=0x000 · DATA=%2 · 安全 Trial Jump：Bootloader 看门狗保护，需由上位机发送 ENTER_BOOT 验证返回")
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
                if (!confirmDangerousOperation(command, target))
                {
                    logRequest(QStringLiteral("已取消危险操作：%1").arg(bootCommandName(command)));
                    return;
                }
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
                {
                    m_stateBootloader->setText(QStringLiteral("v%1.%2.%3")
                                                   .arg(static_cast<quint8>(response.data.at(0)))
                                                   .arg(static_cast<quint8>(response.data.at(1)))
                                                   .arg(static_cast<quint8>(response.data.at(2))));
                    m_snapshot.nodes[row].currentVersion = m_stateBootloader->text();
                    if (m_nodeTable != nullptr && m_nodeTable->item(row, 2) != nullptr)
                        m_nodeTable->item(row, 2)->setText(m_stateBootloader->text());
                }
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
            updateSafetyLock();
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

void FirmwarePage::toggleLogRecording()
{
    if (m_logRecording)
    {
        m_logRecording = false;
        m_logRecordButton->setText(QStringLiteral("开始记录"));
        m_logRecordButton->setObjectName(QStringLiteral("softButton"));
        m_logRecordButton->style()->unpolish(m_logRecordButton);
        m_logRecordButton->style()->polish(m_logRecordButton);
        m_logRecordButton->update();
        m_viewRecordedButton->setEnabled(!m_recordedLogs.isEmpty());
        m_exportRecordedButton->setEnabled(!m_recordedLogs.isEmpty());
        logRequest(QStringLiteral("日志记录结束：共 %1 条").arg(m_recordedLogs.size()));
        return;
    }

    m_recordedLogs.clear();
    m_logRecording = true;
    m_logRecordButton->setText(QStringLiteral("结束记录"));
    m_logRecordButton->setObjectName(QStringLiteral("dangerButton"));
    m_logRecordButton->style()->unpolish(m_logRecordButton);
    m_logRecordButton->style()->polish(m_logRecordButton);
    m_logRecordButton->update();
    m_viewRecordedButton->setEnabled(false);
    m_exportRecordedButton->setEnabled(false);
}

void FirmwarePage::showRecordedLogs()
{
    if (m_recordedLogs.isEmpty())
        return;
    if (m_recordingDialog != nullptr)
    {
        m_recordingDialog->raise();
        m_recordingDialog->activateWindow();
        return;
    }
    m_recordingDialog = new FirmwareLogRecordingDialog(m_recordedLogs, nullptr);
    m_recordingDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_recordingDialog->setWindowModality(Qt::ApplicationModal);
    m_recordingDialog->open();
    m_recordingDialog->raise();
    m_recordingDialog->activateWindow();
}

void FirmwarePage::exportRecordedLogs()
{
    if (m_recordedLogs.isEmpty())
        return;
    FirmwareLogRecordingDialog::exportEntries(m_recordedLogs, this);
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
    const QMessageBox::StandardButton answer = QMessageBox::question(
        window(), QStringLiteral("进入实机模式"),
        QStringLiteral("进入实机模式后将允许发送 CAN 控制命令。\n请确认设备、总线和急停条件均已检查。"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;
    m_communication->open(m_serialDevices.at(index));
}

bool FirmwarePage::matchesRuntimeLogFilter(const QString &message) const
{
    const QString filter = m_logFilter == nullptr ? QStringLiteral("全部") : m_logFilter->currentText();
    const QString upper = message.toUpper();
    const bool isReceive = message.startsWith(QStringLiteral("RX "))
                           || message.startsWith(QStringLiteral("Peer "))
                           || message.contains(QStringLiteral("解析到 CAN"));
    const bool isSend = message.startsWith(QStringLiteral("TX "));
    const bool isError = message.contains(QStringLiteral("错误")) || message.contains(QStringLiteral("失败"))
                         || message.contains(QStringLiteral("超时")) || message.contains(QStringLiteral("未连接"))
                         || message.contains(QStringLiteral("断开")) || message.contains(QStringLiteral("非法"));
    const bool isWarning = message.contains(QStringLiteral("等待")) || message.contains(QStringLiteral("重试"))
                           || message.contains(QStringLiteral("未响应")) || message.contains(QStringLiteral("自动探测"))
                           || upper.contains(QStringLiteral("ABORT")) || upper.contains(QStringLiteral("ENTER_BOOT"))
                           || upper.contains(QStringLiteral("JUMP_APP"));
    const bool isUpgrade = upper.contains(QStringLiteral("SESSION_")) || upper.contains(QStringLiteral("ERASE"))
                           || upper.contains(QStringLiteral("WRITE")) || upper.contains(QStringLiteral("VERIFY"))
                           || upper.contains(QStringLiteral("COMMIT")) || upper.contains(QStringLiteral("MISSING_"))
                           || upper.contains(QStringLiteral("FULL_STREAM")) || upper.contains(QStringLiteral("ROLLBACK"));
    bool isFd = upper.contains(QStringLiteral("CAN FD"));
    const QRegularExpression flagsPattern(QStringLiteral("FLAGS=0x([0-9A-F]+)"));
    const QRegularExpressionMatch flagsMatch = flagsPattern.match(upper);
    if (flagsMatch.hasMatch())
        isFd = (flagsMatch.captured(1).toUInt(nullptr, 16) & 0x02U) != 0U;

    if (filter == QStringLiteral("升级") && !isUpgrade)
        return false;
    if (filter == QStringLiteral("接收 RX") && !isReceive)
        return false;
    if (filter == QStringLiteral("发送 TX") && !isSend)
        return false;
    if (filter == QStringLiteral("警告") && !isWarning)
        return false;
    if (filter == QStringLiteral("错误") && !isError)
        return false;
    if (filter == QStringLiteral("Classic CAN") && isFd)
        return false;
    if (filter == QStringLiteral("CAN FD") && !isFd)
        return false;

    const int node = m_logNodeFilter == nullptr ? 0 : m_logNodeFilter->currentData().toInt();
    if (node > 0)
    {
        const QRegularExpression nodePattern(
            QStringLiteral("(?:Node\\s*%1\\b|节点\\s*0x?%1\\b|RX\\s+0x0*%1\\b|"
                           "(?:Source|Target)=0x?0*%1\\b)")
                .arg(node));
        if (!nodePattern.match(message).hasMatch())
            return false;
    }

    const QString canId = m_logCanIdFilter == nullptr ? QString() : m_logCanIdFilter->text().trimmed();
    if (!canId.isEmpty())
    {
        QString normalized = canId;
        if (normalized.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            normalized = normalized.mid(2);
        const QRegularExpression idPattern(
            QStringLiteral("(?:CAN[ =]|ID\\s+)0x?%1\\b").arg(QRegularExpression::escape(normalized)),
            QRegularExpression::CaseInsensitiveOption);
        if (!idPattern.match(message).hasMatch())
            return false;
    }

    const QString search = m_logSearchFilter == nullptr ? QString() : m_logSearchFilter->text().trimmed();
    return search.isEmpty() || message.contains(search, Qt::CaseInsensitive);
}

void FirmwarePage::renderRuntimeLog()
{
    if (m_requestLog == nullptr || m_logPaused)
        return;
    const int oldScrollValue = m_requestLog->verticalScrollBar()->value();
    QStringList lines;
    for (const QString &line : m_runtimeLog)
    {
        if (matchesRuntimeLogFilter(line))
            lines.append(formatFirmwareLogHtml(line));
    }
    if (lines.isEmpty())
        lines.append(QStringLiteral("<span style=\"color:#8798aa;\">暂无匹配的日志</span>"));
    // formatFirmwareLogHtml() 本身已返回块级 div；额外 br 会制造一整行空白。
    m_requestLog->setHtml(lines.join(QString()));
    if (m_logFollowing)
        m_requestLog->moveCursor(QTextCursor::End);
    else
        m_requestLog->verticalScrollBar()->setValue(oldScrollValue);
}

void FirmwarePage::logRequest(const QString &message)
{
    // 高频电机反馈/网关逐帧诊断不能进入固件页日志链路：
    // 一旦写入这里，就会同时触发实时 HTML 重绘、可选记录、历史持久化，
    // 并广播给高级命令窗口。在 100/1000 Hz 下会反过来拖慢整个 UI。
    // Bootloader 的 TX/RX、下载进度、错误和状态日志均不匹配该格式，仍正常保留。
    if (message.contains(QStringLiteral("解析到 CAN"), Qt::CaseInsensitive))
        return;

    const QString timestamped =
        QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), message);
    m_runtimeLog.append(timestamped);
    while (m_runtimeLog.size() > 100)
        m_runtimeLog.removeFirst();
    renderRuntimeLog();
    if (m_logRecording)
    {
        m_recordedLogs.append(timestamped);
        constexpr int maxRecordedLogs = 20000;
        while (m_recordedLogs.size() > maxRecordedLogs)
            m_recordedLogs.removeFirst();
    }
    m_historyStore.append(message);
    emit debugLogAppended(timestamped);
}

} // namespace rov
