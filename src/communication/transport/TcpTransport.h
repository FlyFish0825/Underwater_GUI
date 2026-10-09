#pragma once
#include "communication/transport/ByteTransport.h"
#include <QTcpSocket>
#include <QTimer>

namespace rov
{
class TcpTransport final : public ByteTransport
{
    Q_OBJECT
  public:
    explicit TcpTransport(QObject *parent = nullptr);
    ~TcpTransport() override;
    bool open(const QString &host, quint16 port, bool allowWrites = false);
    void setOwnerToken(const QString &token);
    void setWritesAllowed(bool allowed);
    void close() override;
    bool isOpen() const override;
    QString portName() const override;
    bool writeBytes(const QByteArray &bytes) override;
    bool connectionRequested() const { return m_requested; }
    void setTimeouts(int connectMs, int silenceMs, int retryMs);
  signals:
    void statusChanged(const QString &status);
  private:
    void attempt();
    void failed(const QString &reason);
    void completeConnection();
    QTcpSocket m_socket;
    QTimer m_connectTimeout, m_silenceTimeout, m_retry;
    QString m_host;
    QString m_ownerToken;
    QByteArray m_handshakeBuffer;
    quint16 m_port = 0;
    bool m_requested = false, m_connected = false, m_allowWrites = false, m_failing = false;
    bool m_handshakePending = false;
    int m_connectMs = 3000, m_silenceMs = 5000, m_retryMs = 1000, m_nextRetryMs = 1000;
};
}
