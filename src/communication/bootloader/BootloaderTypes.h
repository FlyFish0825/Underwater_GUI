#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>

namespace rov
{

/**
 * @brief STM32G431 Bootloader V1.3 的命令编号。
 *
 * Host 命令和自治 Peer 命令共用协议文档中的编号，但帧类型、CAN ID 和
 * 语义不同，调用方必须根据命令类别选择对应的编码函数，不能只看数值。
 */
enum class BootCommand : quint8
{
    GetVersion = 0x01,
    GetDeviceId = 0x02,
    GetInfo = 0x03,
    EnterBoot = 0x04,
    SetGuard = 0x05,
    ReleaseGuard = 0x06,
    SessionBegin = 0x07,
    SessionCrc32 = 0x08,
    Erase = 0x10,
    Write = 0x11,
    Read = 0x12,
    Verify = 0x13,
    WriteEnd = 0x14,
    MissingCount = 0x15,
    MissingItem = 0x16,
    ProviderGrant = 0x17,
    Abort = 0x18,
    CoordinatorClaim = 0x19,
    ProviderAssign = 0x1A,
    ProviderDone = 0x1B,
    RepairRoundEnd = 0x1C,
    RecoveryReady = 0x1D,
    RecoveryFailed = 0x1E,
    JumpApp = 0x20,
    Reset = 0x21,
    VerifyRequest = 0x22,
    VerifyResult = 0x23,
    GuardUpdateBegin = 0x24,
    GuardUpdateReady = 0x25,
    RollbackRequest = 0x26,
    RollbackSizeLo = 0x27,
    RollbackSizeHi = 0x28,
    RollbackCrcLo = 0x29,
    RollbackCrcHi = 0x2A,
    RollbackBegin = 0x2B,
    RollbackPrepared = 0x2C,
    FullStream = 0x2D,
    CommitPrepare = 0x2E,
    CommitAck = 0x2F,
    GetStatus = 0x30,
    CommitExecute = 0x31,
    WindowStatus = 0x32,
};

enum class BootStatus : quint8
{
    Idle = 0x00,
    Erase = 0x01,
    Write = 0x02,
    Verify = 0x03,
    Ready = 0x04,
    Error = 0x05,
    Repair = 0x06,
    Guard = 0x07,
};

enum class BootMessageKind
{
    HostResponse,
    PeerControl,
};

struct BootResponse
{
    quint8 nodeId = 0;
    BootCommand command = BootCommand::GetVersion;
    BootStatus status = BootStatus::Error;
    QByteArray data;
    quint8 errorCode = 0;
    quint32 canId = 0;
    QByteArray rawData;
};

struct PeerControlMessage
{
    quint8 target = 0;
    BootCommand command = BootCommand::MissingCount;
    quint8 source = 0;
    quint16 session = 0;
    quint16 value = 0;
    quint32 canId = 0;
    QByteArray rawData;
};

QString bootCommandName(BootCommand command);
QString bootStatusName(BootStatus status);
bool isPeerCommand(BootCommand command);

} // namespace rov

Q_DECLARE_METATYPE(rov::BootResponse)
Q_DECLARE_METATYPE(rov::PeerControlMessage)
