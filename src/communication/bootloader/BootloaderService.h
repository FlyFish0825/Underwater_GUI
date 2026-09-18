#pragma once

#include "communication/bootloader/BootloaderProtocol.h"
#include "communication/service/BootloaderCommunicationService.h"

#include <QObject>

namespace rov
{

/**
 * @brief 面向 Firmware UI 的异步 Bootloader 命令服务。
 *
 * 服务只接收结构化参数并产生 CAN payload。零散控制帧使用 AA55；连续
 * APP 数据窗口使用 AA59 Credit/ACK 状态机。不阻塞 GUI 等待响应，也不把
 * 命令号和 CRC 计算散落在 QWidget 中。
 */
class BootloaderService final : public QObject
{
    Q_OBJECT

  public:
    explicit BootloaderService(BootloaderCommunicationService *communication,
                                QObject *parent = nullptr);

    bool sendHostCommand(quint8 target, BootCommand command, quint8 byte2 = 0,
                         const QByteArray &params = QByteArray());
    /**
     * @brief 发送一个 64 字节逻辑 DATA 包。
     * @param canFd false 时拆成 0x100~0x107 的 Classic CAN 片段，true 时发送
     *              一帧标准 CAN FD+BRS（FLAGS=0x06）。
     */
    bool sendDataPacket(quint8 target, quint16 sequence, const QByteArray &payload,
                        quint16 session = 0, bool canFd = false);
    /**
     * @brief 发送 Classic CAN 逻辑包中的一个 8 字节物理分片。
     *
     * Bootloader 底层要求 0x100~0x107 严格按序到达。下载状态机每组连续
     * 发送八帧后暂停 1 ms，并按 WRITE 返回的窗口大小等待 Flash 提交确认。
     */
    bool sendClassicDataFragment(quint8 target, quint16 sequence,
                                 const QByteArray &payload, quint16 session,
                                 quint8 fragmentIndex);
    /**
     * @brief 把一段连续 Bootloader DATA 包交给 AA59 状态机发送。
     *
     * payloads 中每项是 Bootloader 的 56 字节有效负载；函数会编码成 64 字节
     * DATA 包。Classic CAN 与 CAN FD 都使用同一 AA59 Credit/ACK 流控。
     */
    bool startDataWindow(quint8 target, quint16 firstSequence,
                         const QVector<QByteArray> &payloads, quint16 session,
                         bool canFd);
    void cancelDataWindow();
    bool sendPeerCommand(quint8 target, BootCommand command, quint8 source, quint16 session,
                         quint16 value);

  signals:
    void hostResponseReceived(const rov::BootResponse &response);
    void peerMessageReceived(const rov::PeerControlMessage &message);
    void commandSent(rov::BootCommand command, quint8 target);
    void errorOccurred(const QString &message);
    void dataWindowProgress(int completedPackets, int totalPackets);
    void dataWindowFinished(bool success, const QString &message);

  private:
    BootloaderCommunicationService *m_communication = nullptr;
    quint16 m_gatewaySequence = 0;
};

} // namespace rov
