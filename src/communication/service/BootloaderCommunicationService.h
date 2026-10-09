#pragma once

#include "communication/protocol/CanFlowControlProtocol.h"
#include "communication/protocol/CanGatewayConfigProtocol.h"
#include "communication/protocol/CanGatewayProtocol.h"
#include "communication/protocol/ObserverMotorProtocol.h"
#include "communication/protocol/SystemHeartbeatProtocol.h"
#include "communication/protocol/SensorProtocol.h"
#include "communication/transport/SerialTransport.h"
#include "communication/transport/TcpTransport.h"

#include <QObject>
#include <QSet>
#include <QTimer>

namespace rov
{

class BootloaderCommunicationService final : public QObject
{
    Q_OBJECT

  public:
    explicit BootloaderCommunicationService(QObject *parent = nullptr);

    QVector<SerialDeviceInfo> enumerateDevices() const;
    bool open(const SerialDeviceInfo &device);
    bool openTcp(const QString &host, quint16 port, bool allowWrites = false);
    bool isTcpRequested() const;
    bool writesAllowed() const;
    void close();
    bool isOpen() const;
    bool sendCanFrame(const CanGatewayFrame &frame);
    bool sendSensorFrame(const SensorFrame &frame);
    bool sendObserverMotorControl(const ObserverMotorProtocol::ControlFrame &control,
                                  QString *error = nullptr);
    bool sendObserverMotorCalibration(ObserverMotorProtocol::CalibrationAction action,
                                      quint8 nodeId, quint16 sequence,
                                      QString *error = nullptr);
    bool setCanBitrate(quint32 nominalBps, quint32 dataBps);
    /**
     * @brief 使用 AA59 Credit/ACK 状态机发送一批连续 CAN 逻辑块。
     *
     * 每个 CanGatewayFrame 对应一个 AA59 DATA_BLOCK。Classic CAN 的长块由
     * H750 从 canId 开始自动拆分；CAN FD 长块由 H750 自动补齐合法 DLC。
     */
    bool startFlowTransfer(const QVector<CanGatewayFrame> &blocks);
    void cancelFlowTransfer();
    bool isFlowTransferActive() const;

  signals:
    void rawBytesReceived(const QByteArray &bytes);
    void frameReceived(const CanGatewayFrame &frame);
    void frameSent(const CanGatewayFrame &frame);
    void heartbeatReceived(const SystemHeartbeat &heartbeat);
    void sensorFrameReceived(const rov::SensorFrame &frame);
    void opened(const QString &portName);
    void closed();
    void errorOccurred(const QString &message);
    void connectionStatusChanged(const QString &message);
    void canBitrateConfigured(quint16 sequence, quint8 status, quint32 nominalBps, quint32 dataBps);
    // Validated, matched reply including optional RX timestamp; existing UI signal is unchanged.
    void canBitrateResponseReceived(const rov::CanGatewayConfigResponse &response);
    void canBitrateError(const QString &message);
    void flowTransferProgress(int completedBlocks, int totalBlocks);
    void flowTransferFinished(bool success, const QString &message);

  private:
    enum class FlowState
    {
        Idle,
        WaitingBeginAck,
        SendingBlocks,
        WaitingFinalBlockAck,
        WaitingEndAck,
    };

    bool sendFlowFrame(CanFlowCommand command, const QByteArray &payload = QByteArray());
    void pumpFlowBlocks();
    void handleFlowFrame(const CanFlowFrame &frame);
    void handleFlowAck(const CanFlowAck &ack);
    void finishFlowTransfer(bool success, const QString &message);
    bool validateFlowBlock(const CanGatewayFrame &block, QString &error) const;
    void processReceivedBytes(const QByteArray &bytes);
    void finishCanBitrateConfig(CanGatewayConfigStatus status, quint32 nominalBitrate,
                                quint32 dataBitrate, const QString &message);
    void handleCanBitrateConfigResponse(CanGatewayConfigResponse response);

    void resetSession();
    ByteTransport *m_transport = nullptr;
    SerialTransport *m_serial = nullptr;
    TcpTransport *m_tcp = nullptr;
    bool m_writesAllowed = true;
    CanGatewayTimestampUnwrapper m_gatewayTimestamp;
    bool m_canBitratePending = false;
    quint16 m_canBitrateSequence = 0;
    CanGatewayConfigRequest m_pendingCanBitrate;
    QTimer m_canBitrateTimeout;
    CanFlowDecoder m_flowDecoder;
    SystemHeartbeatDecoder m_heartbeatDecoder;
    QByteArray m_receiveBuffer;
    QTimer m_flowTimeout;
    FlowState m_flowState = FlowState::Idle;
    QVector<CanGatewayFrame> m_flowBlocks;
    QSet<quint32> m_seenFlowAckSequences;
    quint32 m_flowFrameSequence = 0;
    quint32 m_flowSentBytes = 0;
    quint32 m_flowTotalBytes = 0;
    quint32 m_lastAcknowledgedBlock = 0xFFFFFFFFU;
    int m_nextFlowBlock = 0;
    int m_flowCredit = 0;
};

} // namespace rov
