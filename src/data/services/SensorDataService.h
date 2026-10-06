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
    };
    void refresh();
    void applyStatus(quint8 target, quint32 status);
    void finish(quint8 target, SensorResult result, const QString &detail = QString());
    bool consumeTelemetry(const SensorFrame &frame);
    bool consumeReply(const SensorFrame &frame, SensorResult result);

    SensorSnapshot m_state;
    std::array<qint64, 2> m_lastSeen{{-1, -1}};
    // A matched reply anchors the gateway clock. Pre-handshake / old queued
    // telemetry must not become fresh merely because USB delivered it now.
    std::array<qint64, 2> m_clockAnchorSeen{{-1, -1}};
    std::array<quint32, 2> m_clockAnchorUs{{0, 0}};
    qint64 m_rawSeen = -1, m_quatSeen = -1, m_eulerSeen = -1, m_depthSeen = -1;
    QHash<quint8, Pending> m_pending;
    QHash<quint16, QPair<quint32, quint32>> m_lastStream;
    quint32 m_sequence = 0;
    QElapsedTimer m_clock;
    QTimer m_refresh;
    std::function<bool(const SensorFrame &)> m_sender;
};
} // namespace rov
