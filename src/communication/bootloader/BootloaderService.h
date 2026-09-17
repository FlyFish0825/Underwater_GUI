#pragma once

#include "communication/bootloader/BootloaderProtocol.h"
#include "communication/service/BootloaderCommunicationService.h"

#include <QObject>

namespace rov
{

/**
 * @brief 面向 Firmware UI 的异步 Bootloader 命令服务。
 *
 * 服务只接收结构化参数并产生 CAN payload，再交给通信服务封装 AA55；
 * 不阻塞 GUI 等待响应，也不把命令号和 CRC 计算散落在 QWidget 中。
 */
class BootloaderService final : public QObject
{
    Q_OBJECT

  public:
    explicit BootloaderService(BootloaderCommunicationService *communication,
                                QObject *parent = nullptr);

    bool sendHostCommand(quint8 target, BootCommand command, quint8 byte2 = 0,
                         const QByteArray &params = QByteArray());
    bool sendPeerCommand(quint8 target, BootCommand command, quint8 source, quint16 session,
                         quint16 value);

  signals:
    void hostResponseReceived(const rov::BootResponse &response);
    void peerMessageReceived(const rov::PeerControlMessage &message);
    void commandSent(rov::BootCommand command, quint8 target);
    void errorOccurred(const QString &message);

  private:
    BootloaderCommunicationService *m_communication = nullptr;
};

} // namespace rov
