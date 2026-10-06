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
    // Application policy only; off by default so read-only probes remain read-only.
    // On a new connection, configure the fixed 02BA and capture a missing water zero once.
    void setDepthStartupEnabled(bool enabled);
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
        bool applying = false; // false: read current model/OSR/rate; true: apply and verify.
        quint16 osr = 0, rateHz = 0;
        quint16 currentOsr = 0, currentRateHz = 0;
        QVector<SensorRequest> steps; // At most 4; no retries or rollback writes.
    };
    bool sendRequest(const SensorRequest &request, bool automatic);
    void refresh();
    void processDepthStartup();
    void applyStatus(quint8 target, quint32 status);
    void finish(quint8 target, SensorResult result, const QString &detail = QString());
    bool consumeTelemetry(const SensorFrame &frame, qint64 deliveryAgeMs = 0);
    bool consumeReply(const SensorFrame &frame, SensorResult result);

    enum class DepthStartup { Idle, WaitInfo, WaitPressure };
    bool m_depthStartupEnabled = false;
    DepthStartup m_depthStartup = DepthStartup::Idle;
    qint64 m_depthStartupDeadlineMs = 0;
    SensorSnapshot m_state;
    std::array<qint64, 2> m_lastSeen{{-1, -1}};
    std::array<qint64, 2> m_statusSeen{{-1, -1}};
    // A matched reply anchors the gateway clock. Pre-handshake / old queued
    // telemetry must not become fresh merely because USB delivered it now.
    std::array<qint64, 2> m_clockAnchorSeen{{-1, -1}};
    std::array<quint32, 2> m_clockAnchorUs{{0, 0}};
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
