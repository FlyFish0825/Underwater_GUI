#pragma once

#include "communication/bootloader/BootloaderTypes.h"

#include <QByteArray>
#include <QVector>

namespace rov
{

/**
 * @brief STM32G431 Bootloader Host/Peer 协议编解码器。
 *
 * 这里只处理 CAN 层 payload：Host CONTROL/Node RESPONSE/Peer Control 固定
 * 8 字节，以及 64 字节逻辑 DATA 包。它不依赖 USB CDC、AA55、Qt Widget
 * 或升级状态机，因此未来换成 TCP/Nano Transport 时无需修改。
 */
class BootloaderProtocol final
{
  public:
    static constexpr quint32 hostControlCanId = 0x000;
    static constexpr int dataPacketSize = 64;
    static constexpr int dataPayloadSize = 56;

    static QByteArray encodeHostControl(quint8 target, BootCommand command,
                                        quint8 byte2 = 0,
                                        const QByteArray &params = QByteArray());
    static QByteArray encodePeerControl(quint8 target, BootCommand command, quint8 source,
                                         quint16 session, quint16 value);
    /**
     * @brief 编码一个逻辑 DATA 包。
     *
     * 逻辑包固定 64 字节，最后不足 56 字节的固件内容按协议补 0xFF。
     * Session=0 是参考文档规定的 Legacy 单节点升级模式。
     */
    static QByteArray encodeDataPacket(quint8 target, quint16 sequence,
                                       const QByteArray &payload, quint16 session = 0);
    /** @brief STM32H750 只支持 Classic CAN 时，将逻辑包拆成 8 个物理帧。 */
    static QVector<QByteArray> splitClassicDataPacket(const QByteArray &packet);
    /** @brief CRC-32/MPEG-2：Poly 0x04C11DB7、Init 0xFFFFFFFF、无反射。 */
    static quint32 crc32Mpeg2(const QByteArray &bytes);
    static bool decodeHostResponse(quint32 canId, const QByteArray &data, BootResponse &response);
    static bool decodePeerControl(quint32 canId, const QByteArray &data,
                                  PeerControlMessage &message);
    static quint8 crc8(const QByteArray &bytes);
};

} // namespace rov
