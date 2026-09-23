#include "communication/bootloader/BootloaderFirmwareReader.h"

#include "communication/bootloader/BootloaderProtocol.h"
#include "communication/bootloader/BootloaderService.h"
#include "communication/service/BootloaderCommunicationService.h"

#include <QtGlobal>

namespace rov
{

namespace
{
constexpr quint32 kConfigPageAddress = 0x0801F800U;
constexpr quint32 kAppStartAddress = 0x08005000U;
constexpr quint32 kAppEndAddress = kConfigPageAddress - 1U;
constexpr quint32 kAppMaxSize = kConfigPageAddress - kAppStartAddress;
constexpr int kConfigRecordSize = 84;
constexpr int kConfigCrcOffset = 80;
constexpr quint32 kConfigMagic = 0x31474643U; // "CFG1" in little-endian storage.
constexpr quint16 kConfigVersion = 1U;
constexpr int kBootEntryDelayMs = 1000;
constexpr int kResponseTimeoutMs = 2500;
constexpr int kMaxVersionProbeAttempts = 3;

quint16 readLe16(const QByteArray &bytes, const int offset)
{
    return static_cast<quint16>(static_cast<quint8>(bytes.at(offset)))
           | static_cast<quint16>(static_cast<quint8>(bytes.at(offset + 1))) << 8U;
}

quint32 readLe32(const QByteArray &bytes, const int offset)
{
    return static_cast<quint32>(static_cast<quint8>(bytes.at(offset)))
           | static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 1))) << 8U
           | static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 2))) << 16U
           | static_cast<quint32>(static_cast<quint8>(bytes.at(offset + 3))) << 24U;
}

void appendLe32(QByteArray &bytes, const quint32 value)
{
    bytes.append(static_cast<char>(value & 0xFFU));
    bytes.append(static_cast<char>((value >> 8U) & 0xFFU));
    bytes.append(static_cast<char>((value >> 16U) & 0xFFU));
    bytes.append(static_cast<char>((value >> 24U) & 0xFFU));
}

} // namespace

BootloaderFirmwareReader::BootloaderFirmwareReader(BootloaderService *bootloader,
                                                   BootloaderCommunicationService *communication,
                                                   QObject *parent)
    : QObject(parent), m_bootloader(bootloader), m_communication(communication),
      m_bootEntryTimer(this), m_responseTimer(this)
{
    m_bootEntryTimer.setSingleShot(true);
    m_bootEntryTimer.setInterval(kBootEntryDelayMs);
    connect(&m_bootEntryTimer, &QTimer::timeout, this,
            [this]()
            {
                if (m_running && m_phase == Phase::WaitingEnterBoot)
                    sendVersionProbe();
            });

    m_responseTimer.setSingleShot(true);
    m_responseTimer.setInterval(kResponseTimeoutMs);
    connect(&m_responseTimer, &QTimer::timeout, this, &BootloaderFirmwareReader::onReadTimeout);

    if (m_bootloader != nullptr)
        connect(m_bootloader, &BootloaderService::hostResponseReceived,
                this, &BootloaderFirmwareReader::handleResponse);
    if (m_communication != nullptr)
    {
        connect(m_communication, &BootloaderCommunicationService::closed, this,
                [this]()
                {
                    if (m_running)
                        finish(false, QStringLiteral("读取固件失败：CAN 网关连接已断开"), true);
                });
        connect(m_communication, &BootloaderCommunicationService::errorOccurred, this,
                [this](const QString &message)
                {
                    if (m_running)
                        finish(false, QStringLiteral("读取固件失败：通信错误：%1").arg(message),
                               true);
                });
    }
}

bool BootloaderFirmwareReader::parseConfigMetadata(const QByteArray &record,
                                                    const quint8 expectedNode,
                                                    quint32 &appSize, quint32 &appCrc32,
                                                    QString *error)
{
    const auto reject = [error](const QString &message)
    {
        if (error != nullptr)
            *error = message;
        return false;
    };

    if (record.size() != kConfigRecordSize)
        return reject(QStringLiteral("配置元数据长度错误：需要 %1 字节，收到 %2")
                          .arg(kConfigRecordSize)
                          .arg(record.size()));
    if (expectedNode < 1U || expectedNode > 8U)
        return reject(QStringLiteral("目标节点号无效"));
    if (readLe32(record, 0) != kConfigMagic)
        return reject(QStringLiteral("配置元数据魔数无效"));
    if (readLe16(record, 4) != kConfigVersion || readLe16(record, 6) != kConfigRecordSize)
        return reject(QStringLiteral("配置元数据版本或结构长度不受支持"));
    if (static_cast<quint8>(record.at(8)) != expectedNode)
        return reject(QStringLiteral("配置元数据节点号与目标节点不一致"));

    const quint32 storedCrc = readLe32(record, kConfigCrcOffset);
    const quint32 computedCrc = BootloaderProtocol::crc32Mpeg2(record.left(kConfigCrcOffset));
    if (storedCrc != computedCrc)
        return reject(QStringLiteral("配置元数据 CRC32 校验失败"));
    if (static_cast<quint8>(record.at(44)) != 1U)
        return reject(QStringLiteral("设备没有标记有效 APP，无法确认固件长度"));

    const quint32 parsedSize = readLe32(record, 36);
    if (parsedSize == 0U || parsedSize > kAppMaxSize)
        return reject(QStringLiteral("APP 长度超出可读区域（%1 字节）").arg(parsedSize));

    appSize = parsedSize;
    appCrc32 = readLe32(record, 40);
    if (error != nullptr)
        error->clear();
    return true;
}

bool BootloaderFirmwareReader::start(const quint8 target)
{
    if (m_running)
        return false;
    if (m_bootloader == nullptr || m_communication == nullptr || !m_communication->isOpen())
    {
        emit finished(false, QByteArray(), QStringLiteral("读取失败：USB CDC/CAN 网关未连接"));
        return false;
    }
    if (m_communication->isFlowTransferActive())
    {
        emit finished(false, QByteArray(), QStringLiteral("读取失败：网关正在执行其他数据传输"));
        return false;
    }
    if (target < 1U || target > 8U)
    {
        emit finished(false, QByteArray(), QStringLiteral("读取失败：目标节点号无效"));
        return false;
    }

    m_target = target;
    m_versionAttempts = 0;
    m_running = true;
    m_readInFlight = false;
    m_phase = Phase::WaitingEnterBoot;
    m_expectedImageSize = 0;
    m_expectedImageCrc32 = 0;
    m_requestLength = 0;
    m_requestReceived = 0;
    m_requestAddress = 0;
    m_requestData.clear();
    m_configRecord.clear();
    m_image.clear();
    emit progressChanged(m_target, 0);

    if (!m_bootloader->sendHostCommand(m_target, BootCommand::EnterBoot))
    {
        finish(false, QStringLiteral("读取失败：发送 ENTER_BOOT 失败"));
        return false;
    }
    // APP 不回 ENTER_BOOT；若目标本来就在 Bootloader，会在响应到达时提前探测。
    m_bootEntryTimer.start();
    return true;
}

void BootloaderFirmwareReader::cancel()
{
    if (!m_running)
        return;
    finish(false, QStringLiteral("读取固件已取消"), true);
}

void BootloaderFirmwareReader::handleResponse(const BootResponse &response)
{
    if (!m_running || response.nodeId != m_target)
        return;

    if (m_phase == Phase::WaitingEnterBoot && response.command == BootCommand::EnterBoot)
    {
        if (response.status != BootStatus::Ready)
        {
            finish(false, QStringLiteral("读取失败：目标节点未接受 ENTER_BOOT（%1）")
                             .arg(bootStatusName(response.status)));
            return;
        }
        m_bootEntryTimer.stop();
        sendVersionProbe();
        return;
    }

    if (m_phase == Phase::WaitingVersion && response.command == BootCommand::GetVersion)
    {
        m_responseTimer.stop();
        if (response.status != BootStatus::Ready || response.data.size() < 3)
        {
            finish(false, QStringLiteral("读取失败：Bootloader 版本探测未成功（%1）")
                             .arg(bootStatusName(response.status)));
            return;
        }
        m_phase = Phase::ReadingMetadata;
        if (!sendRead(kConfigPageAddress, static_cast<quint8>(kConfigRecordSize)))
            finish(false, QStringLiteral("读取失败：无法读取配置元数据"));
        return;
    }

    if ((m_phase == Phase::ReadingMetadata || m_phase == Phase::ReadingApp)
        && response.command == BootCommand::Read)
        handleReadResponse(response);
}

void BootloaderFirmwareReader::sendVersionProbe()
{
    if (!m_running || m_bootloader == nullptr || m_communication == nullptr
        || !m_communication->isOpen())
    {
        finish(false, QStringLiteral("读取失败：探测 Bootloader 时通信已断开"));
        return;
    }

    m_phase = Phase::WaitingVersion;
    ++m_versionAttempts;
    if (!m_bootloader->sendHostCommand(m_target, BootCommand::GetVersion))
    {
        finish(false, QStringLiteral("读取失败：发送 GET_VERSION 失败"));
        return;
    }
    m_responseTimer.start();
}

bool BootloaderFirmwareReader::sendRead(const quint32 address, const quint8 length)
{
    if (!m_running || m_bootloader == nullptr || m_communication == nullptr
        || !m_communication->isOpen() || length == 0U)
        return false;

    QByteArray params;
    params.reserve(4);
    appendLe32(params, address);
    m_requestAddress = address;
    m_requestLength = length;
    m_requestReceived = 0;
    m_requestData.clear();
    m_requestData.reserve(length);
    m_readInFlight = true;
    if (!m_bootloader->sendHostCommand(m_target, BootCommand::Read, length, params))
    {
        m_readInFlight = false;
        return false;
    }
    m_responseTimer.start();
    return true;
}

void BootloaderFirmwareReader::handleReadResponse(const BootResponse &response)
{
    if (!m_readInFlight)
        return;
    if (response.status != BootStatus::Ready)
    {
        m_responseTimer.stop();
        finish(false, QStringLiteral("Node%1 READ 失败：%2（错误码 0x%3）")
                           .arg(m_target)
                           .arg(bootStatusName(response.status))
                           .arg(response.errorCode, 2, 16, QLatin1Char('0')).toUpper(), true);
        return;
    }
    if (response.data.size() != 4)
    {
        m_responseTimer.stop();
        finish(false, QStringLiteral("Node%1 READ 响应长度错误：需要 4 字节数据")
                           .arg(m_target), true);
        return;
    }

    const int remaining = static_cast<int>(m_requestLength) - m_requestReceived;
    if (remaining <= 0)
        return;
    const int take = qMin(4, remaining);
    m_requestData.append(response.data.constData(), take);
    m_requestReceived = static_cast<quint16>(m_requestReceived + take);
    m_responseTimer.start();
    if (m_requestReceived < m_requestLength)
        return;

    m_responseTimer.stop();
    m_readInFlight = false;
    if (m_phase == Phase::ReadingMetadata)
    {
        m_configRecord = m_requestData;
        QString metadataError;
        if (!parseConfigMetadata(m_configRecord, m_target, m_expectedImageSize,
                                 m_expectedImageCrc32, &metadataError))
        {
            finish(false, QStringLiteral("读取失败：%1").arg(metadataError));
            return;
        }
        m_image.clear();
        m_image.reserve(static_cast<int>(m_expectedImageSize));
        m_phase = Phase::ReadingApp;
        requestNextAppChunk();
        return;
    }

    m_image.append(m_requestData);
    emit progressChanged(m_target,
                         qBound(0, static_cast<int>(m_image.size() * 100
                                                   / static_cast<qint64>(m_expectedImageSize)), 100));
    requestNextAppChunk();
}

void BootloaderFirmwareReader::requestNextAppChunk()
{
    if (!m_running || m_phase != Phase::ReadingApp)
        return;
    if (static_cast<quint32>(m_image.size()) == m_expectedImageSize)
    {
        const quint32 actualCrc = BootloaderProtocol::crc32Mpeg2(m_image);
        if (actualCrc != m_expectedImageCrc32)
        {
            finish(false, QStringLiteral("读取失败：APP CRC32 不匹配，期望 0x%1，实际 0x%2")
                               .arg(m_expectedImageCrc32, 8, 16, QLatin1Char('0'))
                               .arg(actualCrc, 8, 16, QLatin1Char('0')).toUpper());
            return;
        }
        emit progressChanged(m_target, 100);
        finish(true, QStringLiteral("已完整读取并通过 CRC32 校验（%1 字节）")
                             .arg(m_expectedImageSize));
        return;
    }

    const quint32 completed = static_cast<quint32>(m_image.size());
    const quint32 remaining = m_expectedImageSize - completed;
    const quint8 chunkLength = static_cast<quint8>(qMin(remaining, 255U));
    const quint32 address = kAppStartAddress + completed;
    if (address > kAppEndAddress || remaining > kAppEndAddress - address + 1U)
    {
        finish(false, QStringLiteral("读取失败：APP 地址/长度超出 APP 分区"));
        return;
    }
    if (!sendRead(address, chunkLength))
        finish(false, QStringLiteral("读取失败：发送 READ 请求失败，地址 0x%1，长度 %2")
                           .arg(address, 8, 16, QLatin1Char('0'))
                           .arg(chunkLength).toUpper());
}

void BootloaderFirmwareReader::finish(const bool success, const QString &message,
                                      const bool abortPendingRead)
{
    if (m_responseTimer.isActive())
        m_responseTimer.stop();
    if (m_bootEntryTimer.isActive())
        m_bootEntryTimer.stop();
    if (abortPendingRead && m_readInFlight && m_bootloader != nullptr
        && m_communication != nullptr && m_communication->isOpen())
        m_bootloader->sendHostCommand(m_target, BootCommand::Abort);

    const QByteArray result = success ? m_image : QByteArray();
    m_running = false;
    m_readInFlight = false;
    m_phase = Phase::Idle;
    m_requestLength = 0;
    m_requestReceived = 0;
    m_requestAddress = 0;
    m_expectedImageSize = 0;
    m_expectedImageCrc32 = 0;
    m_requestData.clear();
    m_configRecord.clear();
    m_image.clear();
    emit finished(success, result, message);
}

void BootloaderFirmwareReader::onReadTimeout()
{
    if (!m_running)
        return;
    if (m_phase == Phase::WaitingVersion)
    {
        if (m_versionAttempts < kMaxVersionProbeAttempts && m_communication != nullptr
            && m_communication->isOpen())
        {
            sendVersionProbe();
            return;
        }
        finish(false, QStringLiteral("读取失败：GET_VERSION 连续 %1 次探测超时")
                           .arg(m_versionAttempts));
        return;
    }
    if (m_readInFlight)
    {
        finish(false, QStringLiteral("Node%1 READ 响应超时：已收到 %2/%3 字节，停止读取")
                           .arg(m_target)
                           .arg(m_requestReceived)
                           .arg(m_requestLength), true);
    }
}

} // namespace rov
