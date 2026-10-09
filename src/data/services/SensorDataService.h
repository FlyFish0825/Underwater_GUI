#pragma once

#include "communication/protocol/SensorProtocol.h"
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <functional>

namespace rov
{
class SensorDataService final : public QObject
{
    Q_OBJECT
  public:
    explicit SensorDataService(QObject *parent = nullptr);
    void setSender(std::function<bool(const SensorFrame &)> sender);
    void setConnected(bool connected);
    bool request(const SensorRequest &request);
    void handleFrame(const SensorFrame &frame);
    SensorSnapshot snapshot() const;

  signals:
    void snapshotChanged(const rov::SensorSnapshot &snapshot);
    void parameterReceived(const rov::SensorParameterFeedback &feedback);
    void commandFinished(quint8 target, quint32 sequence, rov::SensorResult result,
                         const QString &message);
    void protocolError(const QString &message);

  private:
    struct Pending
    {
        SensorRequest request;
        QByteArray payload;
        quint32 sequence = 0;
        qint64 deadlineMs = 0;
        bool automatic = false;
    };
    struct DepthSamplingChange
    {
        bool active = false;
        bool applying = false; // false: read current OSR/rate; true: apply and verify.
        quint16 osr = 0, rateHz = 0;
        quint16 currentOsr = 0, currentRateHz = 0;
        QVector<SensorRequest> steps; // At most 4; no retries or rollback writes.
    };
    struct ImuRateSample { qint64 receivedMs; quint32 timestampUs; };
    double imuReceivedRateHz(int group) const;
    void refreshImuRates();
    bool sendRequest(const SensorRequest &request, bool automatic);
    void refresh();
    void applyStatus(quint8 target, quint32 status);
    void clearMeasurements(quint8 target);
    void finish(quint8 target, SensorResult result, const QString &detail = QString());
    bool consumeTelemetry(const SensorFrame &frame, qint64 deliveryAgeMs = 0);
    bool consumeReply(const SensorFrame &frame, SensorResult result);

    SensorSnapshot m_state;
    QVector<SensorRequest> m_imuReads; // One-shot GET_STATUS + two cached parameter reads.
    std::array<QVector<ImuRateSample>, 2> m_imuRates; // Raw / combined attitude, <=512 each.
    std::array<qint64, 3> m_imuDeliveryAgeMs{{0, 0, 0}}; // raw / quaternion / Euler
    qint64 m_imuRateStartedMs = -1;
    quint32 m_imuRateNotBeforeUs = 0;
    std::array<qint64, 2> m_lastSeen{{-1, -1}};
    std::array<qint64, 2> m_statusSeen{{-1, -1}};
    // A matched reply anchors the gateway clock. Pre-handshake / old queued
    // telemetry must not become fresh merely because USB delivered it now.
    std::array<qint64, 2> m_clockAnchorSeen{{-1, -1}};
    std::array<quint32, 2> m_clockAnchorUs{{0, 0}};
    std::array<qint64, 2> m_streamChangedMs{{-1, -1}};
    std::array<quint32, 2> m_streamNotBeforeUs{{0, 0}};
    qint64 m_rawSeen = -1, m_quatSeen = -1, m_eulerSeen = -1, m_depthSeen = -1;
    qint64 m_depthDeliveryAgeMs = 0;
    quint32 m_depthNotBeforeUs = 0;
    bool m_depthAwaitingSample = false;
    QHash<quint8, Pending> m_pending;
    // Bounded, one-shot GETs after discovery / zero change. No polling or automatic writes.
    QVector<SensorRequest> m_depthReads;
    DepthSamplingChange m_depthSampling;
    QHash<quint16, QPair<quint32, quint32>> m_lastStream;
    quint32 m_sequence = 0;
    QElapsedTimer m_clock;
    QTimer m_refresh;
    std::function<bool(const SensorFrame &)> m_sender;
};
} // namespace rov
