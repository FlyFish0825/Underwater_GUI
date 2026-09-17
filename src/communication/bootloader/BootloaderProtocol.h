#pragma once

#include "communication/bootloader/BootloaderTypes.h"

#include <QByteArray>

namespace rov
{

/**
 * @brief STM32G431 Bootloader Host/Peer 协议编解码器。
 *
 * 这里只处理 CAN 层 payload：Host CONTROL 固定 8 字节、Node RESPONSE
 * 固定 8 字节以及 Peer Control 固定 8 字节。它不依赖 USB CDC、AA55、
 * Qt Widget 或升级状态机，因此未来换成 TCP/Nano Transport 时无需修改。
 */
class BootloaderProtocol final
{
  public:
    static constexpr quint32 hostControlCanId = 0x000;

    static QByteArray encodeHostControl(quint8 target, BootCommand command,
                                        quint8 byte2 = 0,
                                        const QByteArray &params = QByteArray());
    static QByteArray encodePeerControl(quint8 target, BootCommand command, quint8 source,
                                         quint16 session, quint16 value);
    static bool decodeHostResponse(quint32 canId, const QByteArray &data, BootResponse &response);
    static bool decodePeerControl(quint32 canId, const QByteArray &data,
                                  PeerControlMessage &message);
    static quint8 crc8(const QByteArray &bytes);
};

} // namespace rov
