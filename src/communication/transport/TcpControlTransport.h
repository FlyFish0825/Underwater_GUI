#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

namespace rov
{
class TcpControlTransport final : public QObject
{
    Q_OBJECT
  public:
    explicit TcpControlTransport(QObject *parent = nullptr);
    void open(const QString &host, quint16 port);
    void close();
    bool requestControl(bool takeover);
    bool releaseControl();
    bool isConnected() const;
    bool ownsControl() const { return !m_token.isEmpty(); }
    QString owner() const { return m_owner; }
    QString leaseToken() const { return m_token; }

  signals:
    void stateChanged(bool connected, const QString &owner, bool owned, const QString &message);
    void requestRejected(const QString &owner, const QString &message);
    void leaseTokenChanged(const QString &token);

  private:
    void attempt();
    void sendLine(const QByteArray &line);
    void processLines();
    void reportState(const QString &message = QString());
    void scheduleRetry(const QString &message);
    void setLeaseToken(const QString &token);

    QTcpSocket m_socket;
    QTimer m_retry;
    QTimer m_heartbeat;
    QString m_host;
    QString m_owner = QStringLiteral("UNKNOWN");
    QString m_token;
    QByteArray m_receiveBuffer;
    bool m_requested = false;
    bool m_retryScheduled = false;
};
}  // namespace rov
