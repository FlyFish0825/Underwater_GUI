#include "communication/bootloader/BootloaderDownloadController.h"

#include "communication/bootloader/BootloaderProtocol.h"

#include <QFile>
#include <QFileInfo>

namespace rov
{

namespace
{
quint16 readLe16(const QByteArray &data, int offset = 0)
{
    return static_cast<quint8>(data.at(offset))
           | (static_cast<quint16>(static_cast<quint8>(data.at(offset + 1))) << 8U);
}

void appendLe32(QByteArray &bytes, quint32 value)
{
    bytes.append(static_cast<char>(value & 0xFFU));
    bytes.append(static_cast<char>((value >> 8U) & 0xFFU));
    bytes.append(static_cast<char>((value >> 16U) & 0xFFU));
    bytes.append(static_cast<char>((value >> 24U) & 0xFFU));
}

QString hex32(const quint32 value)
{
    return QStringLiteral("0x%1").arg(value, 8, 16, QLatin1Char('0')).toUpper();
}

// 与 Bootloader 的 Trial 设计配套：Bootloader 在跳 APP 前开启约 3 秒的
// IWDG。上位机先给 APP 留出 CAN 初始化时间，再连续发送 ENTER_BOOT，最后
// 轮询 Bootloader 的 GET_INFO 确认 app_valid 已由 Trial 返回路径恢复。
constexpr int kTrialAppBootDelayMs = 500;
constexpr int kTrialReturnRepeatCount = 3;
constexpr int kTrialReturnRepeatIntervalMs = 120;
constexpr int kTrialBootloaderSettleMs = 650;
constexpr int kTrialProbeIntervalMs = 500;
constexpr int kTrialMaxProbeAttempts = 6;

} // namespace

BootloaderDownloadController::BootloaderDownloadController(BootloaderService *service,
                                                           QObject *parent)
    : QObject(parent), m_service(service), m_stepTimer(this), m_responseTimer(this)
{
    Q_ASSERT(m_service != nullptr);
    m_stepTimer.setSingleShot(true);
    connect(&m_stepTimer, &QTimer::timeout, this, &BootloaderDownloadController::step);
    m_responseTimer.setSingleShot(true);
    connect(&m_responseTimer, &QTimer::timeout, this,
            [this]() {
                if (m_phase == Phase::WaitingWindow)
                    queryWindow();
                else if (m_phase == Phase::TrialChecking)
                {
                    // APP 未返回时，Bootloader 的 IWDG 会在约 3 秒后复位 MCU。
                    // 轮询期间不发送新的跳转命令，也不把短暂的复位窗口误判为失败。
                    if (m_trialProbeAttempts >= kTrialMaxProbeAttempts)
                        fail(QStringLiteral("Trial 试运行未确认返回 Bootloader；已等待看门狗复位窗口，APP 保持未验证状态"));
                    else
                        m_stepTimer.start(kTrialProbeIntervalMs);
                }
                else
                    fail(QStringLiteral("等待设备回复超时，下载已停止"));
            });
    connect(m_service, &BootloaderService::hostResponseReceived, this,
            &BootloaderDownloadController::handleResponse);
    connect(m_service, &BootloaderService::dataWindowProgress, this,
            &BootloaderDownloadController::handleDataWindowProgress);
    connect(m_service, &BootloaderService::dataWindowFinished, this,
            &BootloaderDownloadController::handleDataWindowFinished);
}

bool BootloaderDownloadController::start(const quint8 target, const QString &firmwarePath,
                                        const bool canFd)
{
    if (isRunning())
    {
        emit logMessage(QStringLiteral("下载任务正在进行，请先等待当前任务结束"));
        return false;
    }
    if (m_service == nullptr)
    {
        emit logMessage(QStringLiteral("下载失败：通信服务不可用"));
        return false;
    }
    if (firmwarePath.isEmpty())
    {
        emit logMessage(QStringLiteral("下载失败：尚未选择固件文件"));
        return false;
    }
    if (QFileInfo(firmwarePath).suffix().compare(QStringLiteral("bin"), Qt::CaseInsensitive) != 0)
    {
        emit logMessage(QStringLiteral("下载失败：正式 Bootloader 下载目前只接受 .bin 镜像，HEX/UF2 请先转换为 BIN"));
        return false;
    }

    QFile file(firmwarePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        emit logMessage(QStringLiteral("下载失败：无法打开固件：%1").arg(file.errorString()));
        return false;
    }
    const QByteArray image = file.readAll();
    file.close();
    if (image.isEmpty())
    {
        emit logMessage(QStringLiteral("下载失败：固件文件为空"));
        return false;
    }
    if (image.size() > 106 * 1024)
    {
        emit logMessage(QStringLiteral("下载失败：固件大小 %1 字节，超过 APP 区域 106 KiB 限制")
                            .arg(image.size()));
        return false;
    }
    m_target = target;
    m_canFd = canFd;
    m_firmwarePath = QFileInfo(firmwarePath).absoluteFilePath();
    m_firmware = image;
    m_crc32 = BootloaderProtocol::crc32Mpeg2(m_firmware);
    m_sequence = 0;
    m_windowPackets = m_windowStart = m_windowEnd = m_committed = m_windowAttempts = 0;
    m_windowQueried = false;
    m_pendingWindowStart = m_pendingWindowEnd = 0;
    m_hasDeferredWindowResponse = false;
    m_trialEnterBootAttempts = 0;
    m_trialProbeAttempts = 0;
    m_totalPackets = (m_firmware.size() + BootloaderProtocol::dataPayloadSize - 1)
                     / BootloaderProtocol::dataPayloadSize;
    m_phase = Phase::WaitingBoot;
    emit phaseChanged(QStringLiteral("准备进入 Bootloader（数据面：%1）")
                          .arg(m_canFd ? QStringLiteral("CAN FD+BRS") : QStringLiteral("Classic CAN")));
    emit progressChanged(m_target, 0, 0, m_totalPackets);
    emit logMessage(QStringLiteral("开始正式下载：Node %1 · %2 · %3 · %4 字节 · %5 · 数据包 %6 个")
                        .arg(m_target)
                        .arg(QFileInfo(m_firmwarePath).fileName())
                        .arg(m_canFd ? QStringLiteral("CAN FD+BRS") : QStringLiteral("Classic CAN"))
                        .arg(m_firmware.size())
                        .arg(hex32(m_crc32))
                        .arg(m_totalPackets));

    // APP 收到 ENTER_BOOT 后不会 ACK；Bootloader 已在运行时会回复，但两种情况
    // 都统一等待一小段时间再擦除，给 MCU 留出复位和重新进入 Bootloader 的时间。
    if (!sendControl(BootCommand::EnterBoot))
    {
        fail(QStringLiteral("发送 ENTER_BOOT 失败"));
        return false;
    }
    m_stepTimer.start(800);
    return true;
}

void BootloaderDownloadController::cancel()
{
    if (!isRunning())
        return;
    m_stepTimer.stop();
    m_responseTimer.stop();
    // 先切回 Idle，再取消 AA59，避免取消信号被当成本次下载失败重复处理。
    m_phase = Phase::Idle;
    if (m_service != nullptr)
    {
        m_service->cancelDataWindow();
        m_service->sendHostCommand(m_target, BootCommand::Abort);
    }
    emit logMessage(QStringLiteral("已发送 ABORT，用户取消本次下载"));
    emit phaseChanged(QStringLiteral("已取消"));
    emit finished(false, QStringLiteral("用户取消下载"));
}

void BootloaderDownloadController::step()
{
    if (!isRunning())
        return;
    if (m_phase == Phase::WaitingBoot)
        sendErase();
    else if (m_phase == Phase::Streaming)
        pumpData();
    else if (m_phase == Phase::TrialBooting || m_phase == Phase::TrialReturning)
        sendTrialEnterBoot();
    else if (m_phase == Phase::TrialChecking)
        queryTrialResult();
}

bool BootloaderDownloadController::sendControl(const BootCommand command, const quint8 byte2,
                                               const QByteArray &params)
{
    if (m_service == nullptr || !m_service->sendHostCommand(m_target, command, byte2, params))
        return false;
    const QString raw = BootloaderProtocol::encodeHostControl(m_target, command, byte2, params)
                            .toHex(' ')
                            .toUpper();
    emit logMessage(QStringLiteral("TX Node%1 · %2 · CAN=0x000 · DATA=%3")
                        .arg(m_target)
                        .arg(bootCommandName(command))
                        .arg(QString(raw)));
    return true;
}

void BootloaderDownloadController::sendErase()
{
    m_phase = Phase::Erasing;
    emit phaseChanged(QStringLiteral("正在擦除 APP 区域"));
    if (!sendControl(BootCommand::Erase))
    {
        fail(QStringLiteral("发送 ERASE 失败"));
        return;
    }
    armResponseTimeout(8000, QStringLiteral("擦除 APP"));
}

void BootloaderDownloadController::sendWrite()
{
    m_phase = Phase::Writing;
    emit phaseChanged(QStringLiteral("正在建立写入会话"));
    QByteArray params;
    appendLe32(params, static_cast<quint32>(m_firmware.size()));
    // Byte2=0 表示 APP 区域，Session=0 表示 Legacy 单节点模式。
    if (!sendControl(BootCommand::Write, 0, params))
    {
        fail(QStringLiteral("发送 WRITE 失败"));
        return;
    }
    armResponseTimeout(3000, QStringLiteral("建立写入会话"));
}

void BootloaderDownloadController::pumpData()
{
    if (m_sequence >= m_totalPackets)
    {
        sendWriteEnd();
        return;
    }

    // AA59 负责 USB→H750→CAN 的 Credit/ACK；Bootloader 自己的窗口负责
    // 节点 Flash 提交确认。两层边界必须分开，不能用网关 ACK 替代节点 ACK。
    const int batchEnd = m_windowPackets > 0 ? m_windowEnd : m_totalPackets;
    QVector<QByteArray> payloads;
    payloads.reserve(batchEnd - m_sequence);
    for (int sequence = m_sequence; sequence < batchEnd; ++sequence)
    {
        const int offset = sequence * BootloaderProtocol::dataPayloadSize;
        payloads.append(m_firmware.mid(offset, BootloaderProtocol::dataPayloadSize));
    }
    m_pendingWindowStart = m_sequence;
    m_pendingWindowEnd = static_cast<quint16>(batchEnd);
    m_hasDeferredWindowResponse = false;
    m_phase = Phase::WaitingGatewayFlow;
    emit phaseChanged(QStringLiteral("AA59 流控发送固件数据"));
    emit logMessage(QStringLiteral("AA59 BEGIN：Bootloader Seq %1～%2 · %3 个逻辑块 · %4")
                        .arg(m_pendingWindowStart)
                        .arg(m_pendingWindowEnd - 1)
                        .arg(payloads.size())
                        .arg(m_canFd ? QStringLiteral("CAN FD+BRS")
                                     : QStringLiteral("Classic CAN，由 H750 拆分 8 帧")));
    if (!m_service->startDataWindow(m_target, m_pendingWindowStart, payloads, 0, m_canFd))
    {
        fail(QStringLiteral("无法启动 AA59 数据窗口"));
        return;
    }
}

void BootloaderDownloadController::handleDataWindowProgress(const int completedPackets,
                                                            const int totalPackets)
{
    if (m_phase != Phase::WaitingGatewayFlow || totalPackets <= 0)
        return;
    if (m_windowPackets == 0)
    {
        const int completed = static_cast<int>(m_pendingWindowStart) + completedPackets;
        emit progressChanged(m_target, completed * 100 / qMax(1, m_totalPackets),
                             static_cast<quint16>(completed), m_totalPackets);
    }
}

void BootloaderDownloadController::handleDataWindowFinished(const bool success,
                                                            const QString &message)
{
    if (m_phase != Phase::WaitingGatewayFlow)
        return;
    if (!success)
    {
        fail(QStringLiteral("网关 AA59 流控失败：%1").arg(message));
        return;
    }

    m_sequence = m_pendingWindowEnd;
    emit logMessage(QStringLiteral("%1；已确认 H750 接收 Bootloader Seq %2～%3")
                        .arg(message)
                        .arg(m_pendingWindowStart)
                        .arg(m_pendingWindowEnd - 1));
    if (m_windowPackets == 0)
    {
        sendWriteEnd();
        return;
    }

    m_phase = Phase::WaitingWindow;
    m_windowQueried = false;
    emit phaseChanged(QStringLiteral("等待 Bootloader 窗口写入确认"));
    if (m_hasDeferredWindowResponse)
    {
        m_hasDeferredWindowResponse = false;
        handleWindow(m_deferredWindowResponse);
    }
    else
        m_responseTimer.start(500);
}

void BootloaderDownloadController::queryWindow()
{
    // ACK 丢失时先查询；无响应时不猜测进度、不直接重发或擦除。
    if (++m_windowAttempts > 5)
    {
        fail(QStringLiteral("窗口连续查询/重发无进展，请检查连接后重新下载"));
        return;
    }
    m_windowQueried = true;
    emit logMessage(QStringLiteral("窗口等待超时或设备忙，查询窗口状态（%1/5）").arg(m_windowAttempts));
    if (!sendControl(BootCommand::WindowStatus))
    {
        fail(QStringLiteral("查询窗口状态失败"));
        return;
    }
    m_responseTimer.start(500);
}

void BootloaderDownloadController::handleWindow(const BootResponse &response)
{
    if (response.status != BootStatus::Write || response.data.size() != 4)
        return;
    const int next = readLe16(response.data);
    const int window = static_cast<quint8>(response.data.at(2));
    const int credits = static_cast<quint8>(response.data.at(3));
    if (next < m_committed)
        return; // 忽略延迟到达的旧窗口通知。
    if (next > m_sequence || next > m_totalPackets || window != m_windowPackets)
    {
        fail(QStringLiteral("窗口响应范围或窗口大小异常，已停止下载"));
        return;
    }
    if (next > m_committed)
    {
        m_committed = next;
        m_windowAttempts = 0;
        emit progressChanged(m_target, next * 100 / m_totalPackets, next, m_totalPackets);
        emit logMessage(QStringLiteral("窗口已写入并回读：%1/%2 包").arg(next).arg(m_totalPackets));
    }
    if (m_phase != Phase::WaitingWindow && next < m_windowEnd)
        return;
    if (next == m_totalPackets)
    {
        m_stepTimer.stop();
        m_responseTimer.stop();
        sendWriteEnd();
        return;
    }
    if (credits == 0)
    {
        m_stepTimer.stop();
        m_phase = Phase::WaitingWindow;
        if (!m_responseTimer.isActive())
            m_responseTimer.start(500);
        return;
    }
    if (next < m_windowEnd && !m_windowQueried)
        return; // 未确认完成时，等待超时后的主动查询再决定重发。
    m_stepTimer.stop();
    m_responseTimer.stop();
    if (next >= m_windowEnd)
    {
        m_windowStart = next;
        m_windowEnd = qMin(next + m_windowPackets, m_totalPackets);
    }
    else
        emit logMessage(QStringLiteral("窗口尚未提交完整，重发 Seq %1~%2")
                            .arg(m_windowStart).arg(m_windowEnd - 1));
    m_sequence = static_cast<quint16>(m_windowStart);
    m_windowQueried = false;
    m_phase = Phase::Streaming;
    emit phaseChanged(QStringLiteral("正在发送固件窗口"));
    m_stepTimer.start(1);
}

void BootloaderDownloadController::sendWriteEnd()
{
    m_phase = Phase::Ending;
    emit phaseChanged(QStringLiteral("正在结束写入并检查缺包"));
    if (!sendControl(BootCommand::WriteEnd))
    {
        fail(QStringLiteral("发送 WRITE_END 失败"));
        return;
    }
    armResponseTimeout(5000, QStringLiteral("结束写入"));
}

void BootloaderDownloadController::sendVerify()
{
    m_phase = Phase::Verifying;
    emit phaseChanged(QStringLiteral("正在校验固件 CRC32"));
    QByteArray params;
    appendLe32(params, m_crc32);
    if (!sendControl(BootCommand::Verify, 0, params))
    {
        fail(QStringLiteral("发送 VERIFY 失败"));
        return;
    }
    armResponseTimeout(8000, QStringLiteral("校验固件"));
}

void BootloaderDownloadController::sendTrialJump()
{
    m_phase = Phase::TrialBooting;
    m_trialEnterBootAttempts = 0;
    m_trialProbeAttempts = 0;
    emit phaseChanged(QStringLiteral("校验通过，安全试运行 APP"));
    // Byte2=0x01 是 Bootloader 的 Trial Jump：先将 app_valid 置为 0，
    // 开启 Bootloader IWDG，再跳入 APP。禁止使用 Byte2=0x00 的直接跳转。
    if (!sendControl(BootCommand::JumpApp, 0x01U))
    {
        fail(QStringLiteral("发送 Trial JUMP_APP 失败"));
        return;
    }
    emit logMessage(QStringLiteral("已发送 Trial JUMP_APP（Byte2=0x01）；Bootloader 已开启看门狗保护，等待 APP 初始化"));
    // Trial 的成功条件不是 JUMP_APP 的 READY，而是 APP 收到 ENTER_BOOT 后
    // 自行软件复位回 Bootloader；因此不能因跳转 ACK 丢失而结束这个安全闭环。
    m_stepTimer.start(kTrialAppBootDelayMs);
}

void BootloaderDownloadController::sendTrialEnterBoot()
{
    if (!sendControl(BootCommand::EnterBoot))
    {
        fail(QStringLiteral("试运行后发送 ENTER_BOOT 失败"));
        return;
    }

    ++m_trialEnterBootAttempts;
    emit logMessage(QStringLiteral("Trial 返回请求 %1/%2：已向 APP 发送 ENTER_BOOT，等待其软件复位回 Bootloader")
                        .arg(m_trialEnterBootAttempts)
                        .arg(kTrialReturnRepeatCount));
    if (m_trialEnterBootAttempts < kTrialReturnRepeatCount)
    {
        m_phase = Phase::TrialReturning;
        m_stepTimer.start(kTrialReturnRepeatIntervalMs);
        return;
    }

    m_phase = Phase::TrialChecking;
    emit phaseChanged(QStringLiteral("等待 Trial 返回并验证 APP"));
    m_stepTimer.start(kTrialBootloaderSettleMs);
}

void BootloaderDownloadController::queryTrialResult()
{
    if (m_trialProbeAttempts >= kTrialMaxProbeAttempts)
    {
        fail(QStringLiteral("Trial 试运行未确认返回 Bootloader；APP 仍未标记为可用"));
        return;
    }
    ++m_trialProbeAttempts;
    if (!sendControl(BootCommand::GetInfo))
    {
        fail(QStringLiteral("无法读取 Trial 结果"));
        return;
    }
    emit logMessage(QStringLiteral("Trial 结果探测 %1/%2：读取 Bootloader APP 有效标记")
                        .arg(m_trialProbeAttempts)
                        .arg(kTrialMaxProbeAttempts));
    armResponseTimeout(kTrialProbeIntervalMs, QStringLiteral("读取 Trial 结果"));
}

void BootloaderDownloadController::armResponseTimeout(const int milliseconds,
                                                       const QString &phase)
{
    Q_UNUSED(phase)
    m_responseTimer.start(milliseconds);
}

void BootloaderDownloadController::handleResponse(const BootResponse &response)
{
    if (!isRunning() || response.nodeId != m_target)
        return;

    const bool expected =
        (m_phase == Phase::Erasing && response.command == BootCommand::Erase)
        || (m_phase == Phase::Writing && response.command == BootCommand::Write)
        || ((m_phase == Phase::Streaming || m_phase == Phase::WaitingGatewayFlow
             || m_phase == Phase::WaitingWindow)
            && response.command == BootCommand::Write && response.status == BootStatus::Error)
        || (m_phase == Phase::Ending
            && (response.command == BootCommand::WriteEnd
                || response.command == BootCommand::MissingCount))
        || (m_phase == Phase::Verifying && response.command == BootCommand::Verify)
        || (m_phase == Phase::TrialBooting && response.command == BootCommand::JumpApp)
        || (m_phase == Phase::TrialChecking && response.command == BootCommand::GetInfo);
    const bool windowResponse = m_windowPackets > 0
        && (m_phase == Phase::Streaming || m_phase == Phase::WaitingGatewayFlow
            || m_phase == Phase::WaitingWindow)
        && response.command == BootCommand::WindowStatus;
    if (!expected && !windowResponse)
        return;

    if (response.status == BootStatus::Error)
    {
        const QString code = QStringLiteral("0x%1").arg(response.errorCode, 2, 16, QLatin1Char('0')).toUpper();
        fail(QStringLiteral("设备拒绝 %1：设备返回错误（错误码 %2）")
                 .arg(bootCommandName(response.command), code));
        return;
    }

    if (m_phase == Phase::WaitingBoot)
        return;
    if (windowResponse)
    {
        if (m_phase == Phase::WaitingGatewayFlow)
        {
            // 节点回复可能比 AA59 END ACK 更早到达；先缓存，等网关会话完整
            // 结束后再推进 Bootloader 窗口，避免两套状态机互相越级。
            m_deferredWindowResponse = response;
            m_hasDeferredWindowResponse = true;
            return;
        }
        handleWindow(response);
        return;
    }
    if (m_phase == Phase::Erasing && response.command == BootCommand::Erase)
    {
        if (response.status == BootStatus::Erase)
            return; // 底层擦除开始时会先回一次 ERASE 状态。
        if (response.status == BootStatus::Ready)
        {
            m_responseTimer.stop();
            sendWrite();
        }
        return;
    }
    if (m_phase == Phase::Writing && response.command == BootCommand::Write)
    {
        if (response.status == BootStatus::Write || response.status == BootStatus::Ready)
        {
            if (response.data.size() != 4)
                return;
            m_windowPackets = static_cast<quint8>(response.data.at(3));
            if (m_windowPackets > 0 && (readLe16(response.data, 1) != m_totalPackets
                                       || response.data.at(0) != 0))
            {
                fail(QStringLiteral("WRITE 返回的区域或总包数与固件不一致"));
                return;
            }
            m_responseTimer.stop();
            m_windowEnd = qMin(m_windowPackets, m_totalPackets);
            emit logMessage(QStringLiteral("下载窗口：%1 个逻辑包（0 表示旧版连续模式）").arg(m_windowPackets));
            m_phase = Phase::Streaming;
            emit phaseChanged(QStringLiteral("正在发送固件数据"));
            m_stepTimer.start(0);
        }
        return;
    }
    if (m_phase == Phase::Ending)
    {
        if (response.command == BootCommand::WriteEnd && response.status == BootStatus::Write
            && m_windowPackets > 0)
        {
            m_phase = Phase::WaitingWindow;
            queryWindow();
            return;
        }
        if (response.command == BootCommand::MissingCount)
        {
            const quint16 missing = response.data.size() >= 2
                                        ? static_cast<quint16>(static_cast<quint8>(response.data.at(0)))
                                              | (static_cast<quint16>(static_cast<quint8>(response.data.at(1))) << 8U)
                                        : 0;
            if (missing != 0)
            {
                fail(QStringLiteral("设备报告缺少 %1 个数据包，当前版本未自动修复，已停止以避免误刷" ).arg(missing));
                return;
            }
        }
        if (response.command == BootCommand::WriteEnd
            && (response.status == BootStatus::Verify || response.status == BootStatus::Ready
                || response.status == BootStatus::Repair))
        {
            if (response.data.size() >= 2)
            {
                const quint16 missing = response.data.size() >= 2
                                            ? static_cast<quint16>(static_cast<quint8>(response.data.at(0)))
                                                  | (static_cast<quint16>(static_cast<quint8>(response.data.at(1))) << 8U)
                                            : 0;
                if (missing != 0)
                {
                    fail(QStringLiteral("设备报告缺少 %1 个数据包，当前版本未自动修复，已停止以避免误刷" ).arg(missing));
                    return;
                }
            }
            m_responseTimer.stop();
            sendVerify();
        }
        return;
    }
    if (m_phase == Phase::Verifying && response.command == BootCommand::Verify)
    {
        if (response.status == BootStatus::Verify)
            return;
        if (response.status == BootStatus::Ready)
        {
            m_responseTimer.stop();
            sendTrialJump();
        }
        return;
    }
    if (m_phase == Phase::TrialBooting && response.command == BootCommand::JumpApp)
    {
        // Trial Jump 的 READY 只能说明命令已被接收；后续仍必须等待 APP
        // 接收 ENTER_BOOT 并返回，不能在这里把下载判为成功。
        return;
    }
    if (m_phase == Phase::TrialChecking && response.command == BootCommand::GetInfo
        && response.status == BootStatus::Ready)
    {
        m_responseTimer.stop();
        const bool appValid = response.data.size() >= 3
                              && static_cast<quint8>(response.data.at(2)) != 0U;
        if (!appValid)
        {
            fail(QStringLiteral("Trial 已回到 Bootloader，但 APP 仍未标记为可用；看门狗保护已阻止直接启动"));
            return;
        }
        complete(QStringLiteral("下载成功：Trial 返回验证通过，Bootloader 已标记 APP 可用；当前安全停留 Bootloader，下次复位或上电将自动启动 APP"));
    }
}

void BootloaderDownloadController::fail(const QString &message)
{
    if (!isRunning())
        return;
    m_stepTimer.stop();
    m_responseTimer.stop();
    m_phase = Phase::Idle;
    if (m_service != nullptr)
        m_service->cancelDataWindow();
    emit phaseChanged(QStringLiteral("失败"));
    emit logMessage(QStringLiteral("下载失败：%1").arg(message));
    emit finished(false, message);
}

void BootloaderDownloadController::complete(const QString &message)
{
    if (!isRunning())
        return;
    m_stepTimer.stop();
    m_responseTimer.stop();
    m_phase = Phase::Idle;
    emit phaseChanged(QStringLiteral("完成"));
    emit logMessage(message);
    emit progressChanged(m_target, 100, static_cast<quint16>(m_totalPackets), m_totalPackets);
    emit finished(true, message);
}

} // namespace rov
