#include "communication/bootloader/BootloaderProtocol.h"

#include <QHash>

namespace rov
{

namespace
{
QString commandName(const quint8 value)
{
    static const QHash<quint8, QString> names = {
        {0x01, QStringLiteral("GET_VERSION")},       {0x02, QStringLiteral("GET_DEVICE_ID")},
        {0x03, QStringLiteral("GET_INFO")},          {0x04, QStringLiteral("ENTER_BOOT")},
        {0x05, QStringLiteral("SET_GUARD")},         {0x06, QStringLiteral("RELEASE_GUARD")},
        {0x07, QStringLiteral("SESSION_BEGIN")},     {0x08, QStringLiteral("SESSION_CRC32")},
        {0x10, QStringLiteral("ERASE")},             {0x11, QStringLiteral("WRITE")},
        {0x12, QStringLiteral("READ")},              {0x13, QStringLiteral("VERIFY")},
        {0x14, QStringLiteral("WRITE_END")},        {0x15, QStringLiteral("MISSING_COUNT")},
        {0x16, QStringLiteral("MISSING_ITEM")},      {0x17, QStringLiteral("PROVIDER_GRANT")},
        {0x18, QStringLiteral("ABORT")},             {0x19, QStringLiteral("COORDINATOR_CLAIM")},
        {0x1A, QStringLiteral("PROVIDER_ASSIGN")},   {0x1B, QStringLiteral("PROVIDER_DONE")},
        {0x1C, QStringLiteral("REPAIR_ROUND_END")}, {0x1D, QStringLiteral("RECOVERY_READY")},
        {0x1E, QStringLiteral("RECOVERY_FAILED")},  {0x20, QStringLiteral("JUMP_APP")},
        {0x21, QStringLiteral("RESET")},             {0x22, QStringLiteral("VERIFY_REQUEST")},
        {0x23, QStringLiteral("VERIFY_RESULT")},    {0x24, QStringLiteral("GUARD_UPDATE_BEGIN")},
        {0x25, QStringLiteral("GUARD_UPDATE_READY")},{0x26, QStringLiteral("ROLLBACK_REQUEST")},
        {0x27, QStringLiteral("ROLLBACK_SIZE_LO")}, {0x28, QStringLiteral("ROLLBACK_SIZE_HI")},
        {0x29, QStringLiteral("ROLLBACK_CRC_LO")},  {0x2A, QStringLiteral("ROLLBACK_CRC_HI")},
        {0x2B, QStringLiteral("ROLLBACK_BEGIN")},   {0x2C, QStringLiteral("ROLLBACK_PREPARED")},
        {0x2D, QStringLiteral("FULL_STREAM")},      {0x2E, QStringLiteral("COMMIT_PREPARE")},
        {0x2F, QStringLiteral("COMMIT_ACK")},       {0x30, QStringLiteral("GET_STATUS")},
        {0x31, QStringLiteral("COMMIT_EXECUTE")},
    };
    return names.value(value, QStringLiteral("UNKNOWN_0x%1").arg(value, 2, 16, QLatin1Char('0')).toUpper());
}

void putLe16(QByteArray &out, quint16 value)
{
    out.append(static_cast<char>(value & 0xFFU));
    out.append(static_cast<char>((value >> 8U) & 0xFFU));
}

} // namespace

QString bootCommandName(BootCommand command)
{
    return commandName(static_cast<quint8>(command));
}

QString bootStatusName(BootStatus status)
{
    switch (status)
    {
    case BootStatus::Erase: return QStringLiteral("ERASE");
    case BootStatus::Write: return QStringLiteral("WRITE");
    case BootStatus::Verify: return QStringLiteral("VERIFY");
    case BootStatus::Ready: return QStringLiteral("READY");
    case BootStatus::Repair: return QStringLiteral("REPAIR");
    case BootStatus::Guard: return QStringLiteral("GUARD");
    case BootStatus::Error: return QStringLiteral("ERROR");
    default: return QStringLiteral("IDLE");
    }
}

bool isPeerCommand(BootCommand command)
{
    const quint8 value = static_cast<quint8>(command);
    return (value >= 0x19U && value <= 0x1EU) || (value >= 0x22U && value <= 0x2FU)
           || value == 0x31U;
}

quint8 BootloaderProtocol::crc8(const QByteArray &bytes)
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

QByteArray BootloaderProtocol::encodeHostControl(quint8 target, BootCommand command, quint8 byte2,
                                                 const QByteArray &params)
{
    if (params.size() > 4)
        return {};
    QByteArray frame(7, '\0');
    frame[0] = static_cast<char>(target);
    frame[1] = static_cast<char>(command);
    frame[2] = static_cast<char>(byte2);
    for (int i = 0; i < params.size(); ++i)
        frame[3 + i] = params.at(i);
    frame.append(static_cast<char>(crc8(frame)));
    return frame;
}

QByteArray BootloaderProtocol::encodePeerControl(quint8 target, BootCommand command, quint8 source,
                                                 quint16 session, quint16 value)
{
    QByteArray frame;
    frame.append(static_cast<char>(target));
    frame.append(static_cast<char>(command));
    frame.append(static_cast<char>(source));
    putLe16(frame, session);
    putLe16(frame, value);
    frame.append(static_cast<char>(crc8(frame)));
    return frame;
}

bool BootloaderProtocol::decodeHostResponse(quint32 canId, const QByteArray &data,
                                            BootResponse &response)
{
    if (canId < 0x501U || canId > 0x508U || data.size() != 8
        || crc8(data.left(7)) != static_cast<quint8>(data.at(7)))
        return false;
    response.nodeId = static_cast<quint8>(data.at(0));
    response.command = static_cast<BootCommand>(static_cast<quint8>(data.at(1)));
    response.status = static_cast<BootStatus>(static_cast<quint8>(data.at(2)));
    response.data = data.mid(3, 4);
    response.errorCode = response.status == BootStatus::Error ? static_cast<quint8>(data.at(3)) : 0;
    response.canId = canId;
    response.rawData = data;
    return true;
}

bool BootloaderProtocol::decodePeerControl(quint32 canId, const QByteArray &data,
                                           PeerControlMessage &message)
{
    if (canId < 0x601U || canId > 0x608U || data.size() != 8
        || crc8(data.left(7)) != static_cast<quint8>(data.at(7)))
        return false;
    message.target = static_cast<quint8>(data.at(0));
    message.command = static_cast<BootCommand>(static_cast<quint8>(data.at(1)));
    message.source = static_cast<quint8>(data.at(2));
    message.session = static_cast<quint16>(static_cast<quint8>(data.at(3)))
                      | static_cast<quint16>(static_cast<quint8>(data.at(4))) << 8U;
    message.value = static_cast<quint16>(static_cast<quint8>(data.at(5)))
                    | static_cast<quint16>(static_cast<quint8>(data.at(6))) << 8U;
    message.canId = canId;
    message.rawData = data;
    return true;
}

} // namespace rov
