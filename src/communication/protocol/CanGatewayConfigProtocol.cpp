#include "communication/protocol/CanGatewayConfigProtocol.h"

#include <QtGlobal>

namespace rov
{

namespace
{

constexpr quint8 kFrameStart0 = 0xAAU;
constexpr quint8 kFrameStart1 = 0x55U;
constexpr quint8 kFrameEnd0 = 0x55U;
constexpr quint8 kFrameEnd1 = 0xAAU;
constexpr quint8 kControlFlags = 0x80U;
constexpr quint8 kSetBitrateCommand = 0x01U;
constexpr quint8 kSetBitrateResponse = 0x81U;
constexpr quint8 kRequestBodyLength = 16U;
constexpr quint8 kResponseBodyLength = 17U;

void setError(QString *error, const QString &message)
{
    if (error != nullptr)
        *error = message;
}

void appendLe16(QByteArray &bytes, const quint16 value)
{
    bytes.append(static_cast<char>(value & 0xFFU));
    bytes.append(static_cast<char>((value >> 8U) & 0xFFU));
}

void appendLe32(QByteArray &bytes, const quint32 value)
{
    bytes.append(static_cast<char>(value & 0xFFU));
    bytes.append(static_cast<char>((value >> 8U) & 0xFFU));
    bytes.append(static_cast<char>((value >> 16U) & 0xFFU));
    bytes.append(static_cast<char>((value >> 24U) & 0xFFU));
}

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

bool isValidStatus(const CanGatewayConfigStatus status)
{
    return status == CanGatewayConfigStatus::Ok
           || status == CanGatewayConfigStatus::UnsupportedRate
           || status == CanGatewayConfigStatus::ApplyFailed;
}

bool validateEnvelope(const QByteArray &packet,
                      const quint8 expectedBodyLength,
                      QString *error)
{
    const int expectedPacketLength = static_cast<int>(expectedBodyLength) + 6;
    if (packet.size() != expectedPacketLength)
    {
        setError(error, QStringLiteral("配置包长度错误：期望 %1，收到 %2")
                             .arg(expectedPacketLength).arg(packet.size()));
        return false;
    }
    if (static_cast<quint8>(packet.at(0)) != kFrameStart0
        || static_cast<quint8>(packet.at(1)) != kFrameStart1)
    {
        setError(error, QStringLiteral("配置包帧头错误"));
        return false;
    }
    if (static_cast<quint8>(packet.at(2)) != expectedBodyLength)
    {
        setError(error, QStringLiteral("配置包 BODY_LEN 错误"));
        return false;
    }
    if (static_cast<quint8>(packet.at(expectedPacketLength - 2)) != kFrameEnd0
        || static_cast<quint8>(packet.at(expectedPacketLength - 1)) != kFrameEnd1)
    {
        setError(error, QStringLiteral("配置包帧尾错误"));
        return false;
    }
    const quint8 expectedCrc = canGatewayConfigCrc8(packet.mid(2, expectedBodyLength + 1));
    const quint8 actualCrc = static_cast<quint8>(packet.at(expectedPacketLength - 3));
    if (expectedCrc != actualCrc)
    {
        setError(error, QStringLiteral("配置包 CRC8 错误"));
        return false;
    }
    return true;
}

QByteArray makePacket(const QByteArray &body)
{
    QByteArray packet = QByteArray::fromHex("AA55");
    packet.append(static_cast<char>(body.size()));
    packet.append(body);
    packet.append(static_cast<char>(canGatewayConfigCrc8(packet.mid(2))));
    packet.append(QByteArray::fromHex("55AA"));
    return packet;
}

} // namespace

bool isSupportedCanNominalBitrate(const quint32 bitrate)
{
    switch (bitrate)
    {
    case 50000U:
    case 100000U:
    case 125000U:
    case 250000U:
    case 500000U:
    case 800000U:
    case 1000000U:
        return true;
    default:
        return false;
    }
}

bool isSupportedCanDataBitrate(const quint32 bitrate)
{
    switch (bitrate)
    {
    case 500000U:
    case 1000000U:
    case 2000000U:
    case 4000000U:
    case 5000000U:
    case 8000000U:
        return true;
    default:
        return false;
    }
}

quint8 canGatewayConfigCrc8(const QByteArray &bytes)
{
    quint8 crc = 0;
    for (const auto value : bytes)
    {
        crc ^= static_cast<quint8>(value);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80U) ? static_cast<quint8>((crc << 1U) ^ 0x07U)
                                : static_cast<quint8>(crc << 1U);
    }
    return crc;
}

QByteArray encodeCanGatewayConfigRequest(const CanGatewayConfigRequest &request)
{
    if (!isSupportedCanNominalBitrate(request.nominalBitrate)
        || !isSupportedCanDataBitrate(request.dataBitrate))
        return {};

    QByteArray body;
    body.reserve(kRequestBodyLength);
    appendLe16(body, request.sequence);
    body.append(4, '\0');
    body.append(static_cast<char>(kControlFlags));
    body.append(static_cast<char>(kSetBitrateCommand));
    appendLe32(body, request.nominalBitrate);
    appendLe32(body, request.dataBitrate);
    return makePacket(body);
}

bool decodeCanGatewayConfigRequest(const QByteArray &packet,
                                   CanGatewayConfigRequest &request,
                                   QString *error)
{
    if (!validateEnvelope(packet, kRequestBodyLength, error))
        return false;
    if (static_cast<quint8>(packet.at(9)) != kControlFlags
        || static_cast<quint8>(packet.at(10)) != kSetBitrateCommand)
    {
        setError(error, QStringLiteral("不是 SET_BITRATE 配置请求"));
        return false;
    }
    request.sequence = readLe16(packet, 3);
    request.nominalBitrate = readLe32(packet, 11);
    request.dataBitrate = readLe32(packet, 15);
    if (!isSupportedCanNominalBitrate(request.nominalBitrate)
        || !isSupportedCanDataBitrate(request.dataBitrate))
    {
        setError(error, QStringLiteral("配置请求包含不支持的 CAN 速率"));
        return false;
    }
    return true;
}

QByteArray encodeCanGatewayConfigResponse(const CanGatewayConfigResponse &response)
{
    if (!isValidStatus(response.status)
        || !isSupportedCanNominalBitrate(response.nominalBitrate)
        || !isSupportedCanDataBitrate(response.dataBitrate))
        return {};

    QByteArray body;
    body.reserve(kResponseBodyLength);
    appendLe16(body, response.sequence);
    body.append(4, '\0');
    body.append(static_cast<char>(kControlFlags));
    body.append(static_cast<char>(kSetBitrateResponse));
    body.append(static_cast<char>(response.status));
    appendLe32(body, response.nominalBitrate);
    appendLe32(body, response.dataBitrate);
    return makePacket(body);
}

bool decodeCanGatewayConfigResponse(const QByteArray &packet,
                                    CanGatewayConfigResponse &response,
                                    QString *error)
{
    if (!validateEnvelope(packet, kResponseBodyLength, error))
        return false;
    if (static_cast<quint8>(packet.at(9)) != kControlFlags
        || static_cast<quint8>(packet.at(10)) != kSetBitrateResponse)
    {
        setError(error, QStringLiteral("不是 SET_BITRATE 配置回复"));
        return false;
    }
    response.sequence = readLe16(packet, 3);
    response.status = static_cast<CanGatewayConfigStatus>(static_cast<quint8>(packet.at(11)));
    response.nominalBitrate = readLe32(packet, 12);
    response.dataBitrate = readLe32(packet, 16);
    if (!isValidStatus(response.status))
    {
        setError(error, QStringLiteral("配置回复状态码不受支持"));
        return false;
    }
    if (!isSupportedCanNominalBitrate(response.nominalBitrate)
        || !isSupportedCanDataBitrate(response.dataBitrate))
    {
        setError(error, QStringLiteral("配置回复包含非法的当前 CAN 速率"));
        return false;
    }
    return true;
}

bool isCanGatewayConfigResponsePacket(const QByteArray &packet)
{
    return packet.size() >= 11
           && static_cast<quint8>(packet.at(2)) == kResponseBodyLength
           && static_cast<quint8>(packet.at(9)) == kControlFlags;
}

QString canGatewayConfigStatusText(const CanGatewayConfigStatus status)
{
    switch (status)
    {
    case CanGatewayConfigStatus::Ok:
        return QStringLiteral("配置成功");
    case CanGatewayConfigStatus::UnsupportedRate:
        return QStringLiteral("网关不支持请求的 CAN 速率");
    case CanGatewayConfigStatus::ApplyFailed:
        return QStringLiteral("网关应用 CAN 速率失败");
    case CanGatewayConfigStatus::Timeout:
        return QStringLiteral("等待网关速率配置回复超时");
    case CanGatewayConfigStatus::TransportError:
        return QStringLiteral("发送 CAN 速率配置包失败");
    }
    return QStringLiteral("未知配置状态");
}

} // namespace rov
