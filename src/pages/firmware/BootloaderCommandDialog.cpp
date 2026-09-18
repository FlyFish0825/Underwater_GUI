#include "pages/firmware/BootloaderCommandDialog.h"
#include "pages/firmware/FirmwareLogFormatter.h"
#include "ui/common/AppCheckBox.h"
#include "ui/common/AppComboBox.h"
#include "ui/common/AppFluentButton.h"
#include "ui/common/AppLineEdit.h"
#include "ui/common/UiPrimitives.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

namespace rov
{

namespace
{
struct CommandItem
{
    BootCommand command;
    const char *category;
};

const CommandItem kCommands[] = {
    {BootCommand::GetVersion, "基础"},       {BootCommand::GetDeviceId, "基础"},
    {BootCommand::GetInfo, "基础"},          {BootCommand::GetStatus, "基础"},
    {BootCommand::EnterBoot, "基础"},        {BootCommand::JumpApp, "基础"},
    {BootCommand::Reset, "基础"},            {BootCommand::Abort, "基础"},
    {BootCommand::Erase, "升级"},            {BootCommand::Write, "升级"},
    {BootCommand::Read, "升级"},             {BootCommand::Verify, "升级"},
    {BootCommand::WriteEnd, "升级"},         {BootCommand::SessionBegin, "Session"},
    {BootCommand::SessionCrc32, "会话"},       {BootCommand::SetGuard, "保护"},
    {BootCommand::ReleaseGuard, "保护"},       {BootCommand::ProviderGrant, "自治"},
    {BootCommand::MissingCount, "自治"},      {BootCommand::MissingItem, "自治"},
    {BootCommand::CoordinatorClaim, "协同"},    {BootCommand::ProviderAssign, "协同"},
    {BootCommand::ProviderDone, "协同"},        {BootCommand::RepairRoundEnd, "协同"},
    {BootCommand::RecoveryReady, "协同"},       {BootCommand::RecoveryFailed, "协同"},
    {BootCommand::VerifyRequest, "协同"},       {BootCommand::VerifyResult, "协同"},
    {BootCommand::GuardUpdateBegin, "协同"},    {BootCommand::GuardUpdateReady, "协同"},
    {BootCommand::RollbackRequest, "协同"},     {BootCommand::RollbackSizeLo, "协同"},
    {BootCommand::RollbackSizeHi, "协同"},       {BootCommand::RollbackCrcLo, "协同"},
    {BootCommand::RollbackCrcHi, "协同"},        {BootCommand::RollbackBegin, "协同"},
    {BootCommand::RollbackPrepared, "协同"},     {BootCommand::FullStream, "协同"},
    {BootCommand::CommitPrepare, "协同"},       {BootCommand::CommitAck, "协同"},
    {BootCommand::CommitExecute, "协同"},
    {BootCommand::WindowStatus, "升级"},
};

QByteArray parseHexBytes(const QString &text)
{
    QString value = text;
    value.remove(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")));
    if (value.size() % 2 != 0)
        return {};
    return QByteArray::fromHex(value.toLatin1());
}

QString commandDescription(BootCommand command)
{
    switch (command)
    {
    case BootCommand::GetVersion:
        return QStringLiteral("读取目标节点的 Bootloader 版本。返回 Data0~Data2，界面解析为 v主版本.次版本.修订号。");
    case BootCommand::GetDeviceId:
        return QStringLiteral("读取目标节点唯一设备 ID。返回 4 字节小端数据，界面会显示为 0xXXXXXXXX，并保留原始字节供核对。");
    case BootCommand::GetInfo:
        return QStringLiteral("读取 APP、配置区和镜像有效性信息。返回标志会解析为“APP：有效/无效、配置：有效/无效”。");
    case BootCommand::GetStatus:
        return QStringLiteral("读取当前升级状态、错误码和进度百分比。返回内容会解析为“状态、错误码、进度”，适合升级过程中轮询。");
    case BootCommand::EnterBoot:
        return QStringLiteral("请求 APP 进入 Bootloader。APP 收到后写入启动标志并复位，不需要先回复 ACK。");
    case BootCommand::JumpApp:
        return QStringLiteral("请求 Bootloader 跳转到 APP。Byte2=0x00 为正常启动，0x01 为 Trial Jump 试运行。");
    case BootCommand::Reset:
        return QStringLiteral("复位目标节点，使其重新执行 Bootloader 启动流程。");
    case BootCommand::Abort:
        return QStringLiteral("终止当前升级 Session，释放升级状态并返回可接受新任务的状态。");
    case BootCommand::Erase:
        return QStringLiteral("擦除目标 APP 区域。执行前请确认目标节点和升级文件，擦除后旧固件不可运行。");
    case BootCommand::Write:
        return QStringLiteral("建立写入会话：Byte2=0 为 APP，参数为固件字节数（32 位小端）。回复包含总包数和窗口包数；窗口非零时每窗口必须等待 0x32 确认。");
    case BootCommand::WindowStatus:
        return QStringLiteral("查询窗口写入状态（0x32），Byte2 和参数均为 0。回复 Data0~1 为下一待写序号，Data2 为窗口包数，Data3 为可用窗口数；全部包提交后才能结束写入。");
    case BootCommand::Read:
        return QStringLiteral("读取目标存储区数据，用于调试和校验，不建议在正常升级流程中手动使用。");
    case BootCommand::Verify:
        return QStringLiteral("校验目标 APP 镜像完整性，设备会比较 CRC/摘要并返回校验结果。");
    case BootCommand::WriteEnd:
        return QStringLiteral("通知设备数据写入结束，触发最终长度、边界和完整性检查。");
    case BootCommand::SessionBegin:
        return QStringLiteral("开始一次升级 Session。参数用于声明 Session ID 或镜像信息，需与后续写入保持一致。");
    case BootCommand::SessionCrc32:
        return QStringLiteral("提交当前 Session 的 CRC32，设备据此确认整包数据是否一致。");
    case BootCommand::SetGuard:
        return QStringLiteral("设置升级保护/看门状态，防止多个节点同时修改同一升级资源。");
    case BootCommand::ReleaseGuard:
        return QStringLiteral("释放升级保护，允许其他节点或协调器接管升级资源。");
    case BootCommand::ProviderGrant:
        return QStringLiteral("向指定数据提供者授予本次升级的数据发送权限，用于多节点协同升级。");
    case BootCommand::MissingCount:
        return QStringLiteral("查询节点缺失数据块数量，常用于断点续传和恢复升级。");
    case BootCommand::MissingItem:
        return QStringLiteral("查询指定序号的缺失数据块，配合缺失数量完成定向补发。");
    case BootCommand::CoordinatorClaim:
    case BootCommand::ProviderAssign:
    case BootCommand::ProviderDone:
    case BootCommand::RepairRoundEnd:
    case BootCommand::RecoveryReady:
    case BootCommand::RecoveryFailed:
    case BootCommand::VerifyRequest:
    case BootCommand::VerifyResult:
    case BootCommand::GuardUpdateBegin:
    case BootCommand::GuardUpdateReady:
    case BootCommand::RollbackRequest:
    case BootCommand::RollbackSizeLo:
    case BootCommand::RollbackSizeHi:
    case BootCommand::RollbackCrcLo:
    case BootCommand::RollbackCrcHi:
    case BootCommand::RollbackBegin:
    case BootCommand::RollbackPrepared:
    case BootCommand::FullStream:
    case BootCommand::CommitPrepare:
    case BootCommand::CommitAck:
    case BootCommand::CommitExecute:
        return QStringLiteral("Peer/自治协同命令，用于多节点升级协调、修复、回滚或提交。默认禁止手工发送，开启协议开发模式后才可编辑参数。");
    }
    return QStringLiteral("协议命令 0x%1。请参考 Bootloader 协议定义确认参数和回复格式。")
        .arg(static_cast<quint8>(command), 2, 16, QLatin1Char('0')).toUpper();
}

} // namespace

BootloaderCommandDialog::BootloaderCommandDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("commandCenter"));
    setWindowTitle(QStringLiteral("Bootloader 命令中心"));
    // 与主界面、历史记录窗口统一使用水下机器人品牌图标。
    setWindowIcon(makeIcon(IconKind::Brand));
    resize(620, 580);
    auto *root = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(QStringLiteral("目标节点")));
    m_target = new AppComboBox;
    m_target->addItem(QStringLiteral("广播 0xFF"), 0xFF);
    for (int node = 1; node <= 8; ++node)
        m_target->addItem(QStringLiteral("节点 %1").arg(node, 2, 10, QLatin1Char('0')), node);
    top->addWidget(m_target, 1);
    top->addWidget(new QLabel(QStringLiteral("来源")));
    m_source = new AppComboBox;
    for (int node = 1; node <= 8; ++node)
        m_source->addItem(QStringLiteral("节点 %1").arg(node, 2, 10, QLatin1Char('0')), node);
    m_source->setEnabled(false);
    top->addWidget(m_source);
    top->addWidget(new QLabel(QStringLiteral("会话")));
    m_session = new AppLineEdit(QStringLiteral("0x0000"));
    m_session->setToolTip(QStringLiteral("16 位 Session ID，小端编码"));
    top->addWidget(m_session);
    root->addLayout(top);

    auto *body = new QHBoxLayout;
    m_commandList = new QListWidget;
    for (const auto &item : kCommands)
    {
        auto *listItem = new QListWidgetItem(
            QStringLiteral("[%1] %2 · 0x%3")
                .arg(QString::fromUtf8(item.category), bootCommandName(item.command))
                .arg(static_cast<quint8>(item.command), 2, 16, QLatin1Char('0')).toUpper());
        listItem->setData(Qt::UserRole, static_cast<int>(item.command));
        m_commandList->addItem(listItem);
    }
    m_commandList->setCurrentRow(0);
    body->addWidget(m_commandList, 2);

    auto *right = new QVBoxLayout;
    auto *form = new QFormLayout;
    m_jumpMode = new AppComboBox;
    m_jumpMode->addItem(QStringLiteral("正常启动 · Byte2=0x00"), 0);
    m_jumpMode->addItem(QStringLiteral("试运行 Trial · Byte2=0x01"), 1);
    form->addRow(QStringLiteral("JUMP_APP 模式"), m_jumpMode);
    m_commandCode = new AppLineEdit;
    m_commandCode->setReadOnly(true);
    m_commandCode->setToolTip(QStringLiteral("当前选中命令在 Bootloader 协议中的命令码"));
    form->addRow(QStringLiteral("命令码"), m_commandCode);
    m_byte2 = new AppLineEdit(QStringLiteral("0x00"));
    form->addRow(QStringLiteral("Byte2 / 语义参数"), m_byte2);
    m_params = new AppLineEdit(QStringLiteral("00 00 00 00"));
    m_params->setToolTip(QStringLiteral("仅高级命令使用；最多 4 字节，小端序"));
    form->addRow(QStringLiteral("参数"), m_params);
    m_value = new AppLineEdit(QStringLiteral("0x0000"));
    m_value->setEnabled(false);
    form->addRow(QStringLiteral("协同值"), m_value);
    right->addLayout(form);
    right->addWidget(new QLabel(QStringLiteral("命令说明")));
    m_commandDescription = new QLabel;
    m_commandDescription->setWordWrap(true);
    m_commandDescription->setMinimumHeight(64);
    m_commandDescription->setStyleSheet(QStringLiteral("color:#385b83; background:#f5f8fc; border:1px solid #d8e3ef; padding:6px;"));
    right->addWidget(m_commandDescription);
    m_developerMode = new AppCheckBox(QStringLiteral("协议开发模式（允许手工参数）"));
    right->addWidget(m_developerMode);
    auto *warning = new QLabel(QStringLiteral("开发调试功能：可能改变当前升级 Session 状态。"));
    warning->setWordWrap(true);
    warning->setStyleSheet(QStringLiteral("color:#b06a00;"));
    right->addWidget(warning);
    auto *send = new AppFluentButton(QStringLiteral("发送主机命令"));
    send->setFluentStyle(fluent::basicinput::Button::Accent);
    right->addWidget(send);
    right->addStretch();
    body->addLayout(right, 3);
    root->addLayout(body, 2);

    root->addWidget(new QLabel(QStringLiteral("调试监视（实时同步，只读）")));
    m_peerMonitor = new QTextBrowser;
    m_peerMonitor->setReadOnly(true);
    m_peerMonitor->setObjectName(QStringLiteral("commandMonitor"));
    m_peerMonitor->setOpenLinks(false);
    m_peerMonitor->setOpenExternalLinks(false);
    m_peerMonitor->setLineWrapMode(QTextEdit::NoWrap);
    m_peerMonitor->setMinimumHeight(190);
    root->addWidget(m_peerMonitor, 1);

    connect(send, &QPushButton::clicked, this, &BootloaderCommandDialog::sendSelected);
    connect(m_commandList, &QListWidget::currentRowChanged, this,
            [this](int) { updateParameterHints(); });
    connect(m_developerMode, &QCheckBox::toggled, this,
            [this](bool enabled)
            {
                m_source->setEnabled(enabled);
                m_value->setEnabled(enabled);
            });
    connect(m_jumpMode, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index)
            {
                if (m_commandList->currentItem() != nullptr
                    && m_commandList->currentItem()->data(Qt::UserRole).toInt()
                           == static_cast<int>(BootCommand::JumpApp))
                    m_byte2->setText(QStringLiteral("0x%1").arg(index));
            });
    updateParameterHints();
}

void BootloaderCommandDialog::setTarget(quint8 nodeId)
{
    const int index = m_target->findData(nodeId);
    if (index >= 0)
        m_target->setCurrentIndex(index);
}

void BootloaderCommandDialog::appendPeerMessage(const PeerControlMessage &message)
{
    appendDebugMessage(QStringLiteral("Peer · %1 · Source=%2 · Target=%3 · Session=0x%4 · Value=0x%5 · CAN=0x%6")
                           .arg(bootCommandName(message.command))
                           .arg(message.source)
                           .arg(message.target)
                           .arg(message.session, 4, 16, QLatin1Char('0')).toUpper()
                           .arg(message.value, 4, 16, QLatin1Char('0')).toUpper()
                           .arg(message.canId, 3, 16, QLatin1Char('0')).toUpper());
}

void BootloaderCommandDialog::appendDebugMessage(const QString &message)
{
    if (m_peerMonitor == nullptr)
        return;
    m_peerMonitor->append(formatFirmwareLogHtml(message));
    m_peerMonitor->moveCursor(QTextCursor::End);
}

void BootloaderCommandDialog::setDebugHistory(const QStringList &messages)
{
    if (m_peerMonitor == nullptr)
        return;
    m_peerMonitor->clear();
    for (const QString &message : messages)
        appendDebugMessage(message);
}

void BootloaderCommandDialog::sendSelected()
{
    if (m_commandList->currentItem() == nullptr)
        return;
    const auto command = static_cast<BootCommand>(m_commandList->currentItem()->data(Qt::UserRole).toInt());
    bool ok = false;
    QString byteText = m_byte2->text().trimmed();
    if (byteText.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        byteText = byteText.mid(2);
    const quint8 byte2 = static_cast<quint8>(byteText.toUInt(&ok, 16));
    if (!ok)
        return;
    QByteArray params = parseHexBytes(m_params->text());
    if (params.size() > 4)
        return;
    if (isPeerCommand(command) && !m_developerMode->isChecked())
        return;
    if (m_developerMode->isChecked() && isPeerCommand(command))
    {
        QString sessionText = m_session->text().trimmed();
        if (sessionText.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            sessionText = sessionText.mid(2);
        bool sessionOk = false;
        const quint16 session = static_cast<quint16>(sessionText.toUInt(&sessionOk, 16));
        QString valueText = m_value->text().trimmed();
        if (valueText.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            valueText = valueText.mid(2);
        bool valueOk = false;
        const quint16 value = static_cast<quint16>(valueText.toUInt(&valueOk, 16));
        if (sessionOk && valueOk)
            emit peerCommandRequested(static_cast<quint8>(m_target->currentData().toInt()), command,
                                       static_cast<quint8>(m_source->currentData().toInt()), session, value);
        return;
    }
    emit commandRequested(static_cast<quint8>(m_target->currentData().toInt()), command, byte2, params);
}

void BootloaderCommandDialog::updateParameterHints()
{
    if (m_commandList->currentItem() == nullptr)
        return;
    const auto command = static_cast<BootCommand>(m_commandList->currentItem()->data(Qt::UserRole).toInt());
    m_commandCode->setText(QStringLiteral("0x%1")
                               .arg(static_cast<quint8>(command), 2, 16, QLatin1Char('0'))
                               .toUpper());
    m_jumpMode->setEnabled(command == BootCommand::JumpApp);
    if (command == BootCommand::SessionBegin)
    {
        m_byte2->setText(QStringLiteral("0x07"));
        m_params->setText(QStringLiteral("34 12 00 00"));
    }
    else if (command == BootCommand::SetGuard)
    {
        m_byte2->setText(QStringLiteral("0x08"));
        m_params->setText(QStringLiteral("00 00 00 00"));
    }
    else if (command == BootCommand::JumpApp)
    {
        m_byte2->setText(QStringLiteral("0x00"));
        m_params->setText(QStringLiteral("00 00 00 00"));
    }
    else
    {
        m_byte2->setText(QStringLiteral("0x00"));
        m_params->setText(QStringLiteral("00 00 00 00"));
    }
    m_commandDescription->setText(commandDescription(command));
}

} // namespace rov
