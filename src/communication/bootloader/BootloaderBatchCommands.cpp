#include "communication/bootloader/BootloaderBatchCommands.h"
#include <utility>

namespace rov
{
BootloaderBatchCommands::BootloaderBatchCommands(Sender sender, QObject *parent)
    : QObject(parent), m_sender(std::move(sender))
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (!m_running)
            return;
        const auto pending = m_pending;
        m_pending.clear();
        for (quint8 target : pending)
        {
            ++m_unconfirmed;
            emit nodeResult(target, QStringLiteral("%1：已发送，未收到确认")
                                        .arg(bootCommandName(m_commands.at(m_round))));
        }
        advance();
    });
}

bool BootloaderBatchCommands::start(const QVector<quint8> &targets,
                                    const QVector<BootCommand> &commands)
{
    if (m_running || targets.isEmpty() || commands.isEmpty() || !m_sender)
        return false;
    QSet<quint8> unique;
    for (quint8 target : targets)
    {
        if (target < 1 || target > 8 || unique.contains(target))
            return false;
        unique.insert(target);
    }
    for (BootCommand command : commands)
    {
        switch (command)
        {
        case BootCommand::EnterBoot:
        case BootCommand::Reset:
        case BootCommand::JumpApp:
        case BootCommand::GetVersion:
        case BootCommand::GetDeviceId:
        case BootCommand::GetInfo:
        case BootCommand::GetStatus:
            break;
        default:
            return false;
        }
    }
    m_targets = targets;
    m_commands = commands;
    // APP 的 ENTER_BOOT 不回 ACK；等待复位后用版本回包确认 Boot 已就绪。
    if (m_commands.last() == BootCommand::EnterBoot)
        m_commands.append(BootCommand::GetVersion);
    m_round = m_confirmed = m_failed = m_unconfirmed = 0;
    m_running = true;
    sendRound();
    return true;
}

void BootloaderBatchCommands::sendRound()
{
    const BootCommand command = m_commands.at(m_round);
    m_pending.clear();
    m_sending = true;
    for (quint8 target : m_targets)
    {
        if (!m_running)
            break;
        if (command != BootCommand::EnterBoot)
            m_pending.insert(target);
        emit nodeResult(target, QStringLiteral("%1：发送中").arg(bootCommandName(command)));
        // 与单节点入口一致，跳转只能使用 Trial 看门狗保护。
        if (!m_sender(target, command, command == BootCommand::JumpApp ? 0x01U : 0x00U))
        {
            if (!m_running)
                break;
            m_pending.remove(target);
            ++m_failed;
            emit nodeResult(target, QStringLiteral("%1：发送失败").arg(bootCommandName(command)));
        }
        else if (m_running && (command == BootCommand::EnterBoot || m_pending.contains(target)))
            emit nodeResult(target, QStringLiteral("%1：已发送，等待%2")
                                        .arg(bootCommandName(command),
                                             command == BootCommand::EnterBoot
                                                 ? QStringLiteral("Boot 版本确认")
                                                 : QStringLiteral("响应")));
    }
    m_sending = false;
    if (!m_running)
        return;
    if (command == BootCommand::EnterBoot)
        m_timer.start(1000);
    else if (m_pending.isEmpty())
        advance();
    else
        m_timer.start(2000);
}

void BootloaderBatchCommands::handleResponse(const BootResponse &response)
{
    if (!m_running || response.command != m_commands.at(m_round)
        || !m_pending.contains(response.nodeId))
        return;
    // 短回包不能被计为有效版本/信息/状态确认。
    const int minimum = response.command == BootCommand::GetVersion ? 3
                        : response.command == BootCommand::GetInfo ? 4
                        : response.command == BootCommand::GetStatus ? 3 : 0;
    if (response.status != BootStatus::Error && response.data.size() < minimum)
        return;
    m_pending.remove(response.nodeId);
    const bool failed = response.status == BootStatus::Error || response.errorCode != 0;
    failed ? ++m_failed : ++m_confirmed;
    QString detail = QStringLiteral("已响应");
    if (failed)
        detail = QStringLiteral("设备报错 0x%1").arg(response.errorCode, 2, 16, QLatin1Char('0'));
    else if (response.command == BootCommand::GetVersion)
        detail = QStringLiteral("v%1.%2.%3")
                     .arg(static_cast<quint8>(response.data.at(0)))
                     .arg(static_cast<quint8>(response.data.at(1)))
                     .arg(static_cast<quint8>(response.data.at(2)));
    else if (response.command == BootCommand::GetDeviceId)
        detail = QStringLiteral("ID %1").arg(QString::fromLatin1(response.data.toHex(' ').toUpper()));
    else if (response.command == BootCommand::GetInfo)
        detail = QStringLiteral("APP %1 / 配置 %2")
                     .arg(response.data.at(2) ? QStringLiteral("有效") : QStringLiteral("无效"),
                          response.data.at(3) ? QStringLiteral("有效") : QStringLiteral("无效"));
    else if (response.command == BootCommand::GetStatus)
        detail = QStringLiteral("%1 / 错误 0x%2 / %3%")
                     .arg(bootStatusName(static_cast<BootStatus>(static_cast<quint8>(response.data.at(0)))))
                     .arg(static_cast<quint8>(response.data.at(1)), 2, 16, QLatin1Char('0'))
                     .arg(static_cast<quint8>(response.data.at(2)));
    emit nodeResult(response.nodeId,
                    QStringLiteral("%1：%2").arg(bootCommandName(response.command), detail));
    if (m_pending.isEmpty() && !m_sending)
        advance();
}

void BootloaderBatchCommands::advance()
{
    m_timer.stop();
    if (++m_round == m_commands.size())
        finish();
    else
        sendRound();
}

void BootloaderBatchCommands::finish()
{
    m_running = false;
    emit finished(QStringLiteral("批量操作结束：响应确认 %1 项，失败 %2 项，未确认 %3 项")
                      .arg(m_confirmed).arg(m_failed).arg(m_unconfirmed));
}

void BootloaderBatchCommands::cancel()
{
    if (!m_running)
        return;
    m_timer.stop();
    m_running = false;
    m_pending.clear();
    for (quint8 target : m_targets)
        emit nodeResult(target, QStringLiteral("批量操作已停止；已发送的指令无法撤回"));
    emit finished(QStringLiteral("批量操作已停止；已发送的指令无法撤回"));
}
}
