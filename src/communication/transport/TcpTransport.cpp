#include "communication/transport/TcpTransport.h"
#include <QtGlobal>
#include <QNetworkProxy>

namespace rov
{
TcpTransport::TcpTransport(QObject *parent) : ByteTransport(parent)
{
    m_connectTimeout.setSingleShot(true);
    m_silenceTimeout.setSingleShot(true);
    m_retry.setSingleShot(true);
    m_socket.setReadBufferSize(256 * 1024);
    // Robot LAN traffic is direct; do not inherit a system HTTP/browser proxy.
    m_socket.setProxy(QNetworkProxy::NoProxy);
    connect(&m_socket, &QTcpSocket::connected, this, [this]() {
        if (!m_requested) { m_socket.abort(); return; }
        m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_socket.setSocketOption(QAbstractSocket::KeepAliveOption, 1);
        if (!m_ownerToken.isEmpty()) {
            m_handshakePending = true;
            m_handshakeBuffer.clear();
            const QByteArray handshake = "OWNER " + m_ownerToken.toLatin1() + "\n";
            if (m_socket.write(handshake) != handshake.size())
                failed(QStringLiteral("无法提交 TCP 控制权握手"));
            return;
        }
        completeConnection();
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, [this]() {
        QByteArray bytes = m_socket.readAll();
        if (m_handshakePending) {
            m_handshakeBuffer.append(bytes);
            if (m_handshakeBuffer.size() > 64) {
                failed(QStringLiteral("Nano 控制权握手回复过长"));
                return;
            }
            const int newline = m_handshakeBuffer.indexOf('\n');
            if (newline < 0)
                return;
            const QByteArray response = m_handshakeBuffer.left(newline).trimmed();
            bytes = m_handshakeBuffer.mid(newline + 1);
            m_handshakeBuffer.clear();
            m_handshakePending = false;
            if (response != QByteArrayLiteral("OK GUI")) {
                failed(QStringLiteral("Nano 拒绝上位机串口控制权握手；请先接管控制权"));
                return;
            }
            completeConnection();
        }
        if (!m_requested || !m_connected || bytes.isEmpty()) return;
        m_silenceTimeout.start(m_silenceMs);
        emit bytesReceived(bytes);
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this]() { failed(QStringLiteral("TCP 对端已断开")); });
    connect(&m_socket, &QAbstractSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) { failed(m_socket.errorString()); });
    connect(&m_connectTimeout, &QTimer::timeout, this, [this]() { failed(QStringLiteral("TCP 连接超时")); });
    connect(&m_silenceTimeout, &QTimer::timeout, this, [this]() { failed(QStringLiteral("TCP 持续未收到网关数据")); });
    connect(&m_retry, &QTimer::timeout, this, &TcpTransport::attempt);
}
TcpTransport::~TcpTransport() { close(); }
void TcpTransport::setTimeouts(int connectMs, int silenceMs, int retryMs)
{
    m_connectMs = qMax(50, connectMs);
    m_silenceMs = qMax(100, silenceMs);
    m_retryMs = qMax(50, retryMs);
    m_nextRetryMs = m_retryMs;
}
bool TcpTransport::open(const QString &host, quint16 port, bool allowWrites)
{
    close();
    if (host.trimmed().isEmpty() || port == 0)
    {
        emit errorOccurred(QStringLiteral("请填写 Nano 地址和有效端口"));
        return false;
    }
    m_host = host.trimmed(); m_port = port; m_allowWrites = allowWrites;
    m_ownerToken.clear();
    m_requested = true; m_nextRetryMs = m_retryMs; attempt();
    return true; // Asynchronous connection requested; opened() confirms completion.
}
void TcpTransport::setOwnerToken(const QString &token)
{
    if (m_ownerToken == token)
        return;
    const bool wasRequested = m_requested;
    const bool wasConnected = m_connected;
    m_requested = false;
    m_retry.stop(); m_connectTimeout.stop(); m_silenceTimeout.stop();
    m_socket.abort();
    m_connected = false; m_handshakePending = false; m_handshakeBuffer.clear();
    m_ownerToken = token;
    m_allowWrites = !token.isEmpty();
    if (wasConnected) emit closed();
    m_requested = wasRequested;
    if (m_requested) {
        m_nextRetryMs = m_retryMs;
        attempt();
    }
}
void TcpTransport::setWritesAllowed(const bool allowed)
{
    m_allowWrites = allowed && !m_ownerToken.isEmpty();
}
void TcpTransport::attempt()
{
    if (!m_requested) return;
    emit statusChanged(QStringLiteral("正在连接 %1").arg(portName()));
    m_connectTimeout.start(m_connectMs);
    m_socket.connectToHost(m_host, m_port);
}
void TcpTransport::completeConnection()
{
    if (!m_requested) { m_socket.abort(); return; }
    m_handshakePending = false;
    m_connectTimeout.stop();
    m_connected = true;
    m_nextRetryMs = m_retryMs;
    m_silenceTimeout.start(m_silenceMs);
    emit opened(portName());
    emit statusChanged(QStringLiteral("TCP 已连接 · %1").arg(m_allowWrites ? QStringLiteral("已接管") : QStringLiteral("只读")));
}
void TcpTransport::failed(const QString &reason)
{
    if (m_failing || !m_requested) return;
    m_failing = true;
    m_connectTimeout.stop(); m_silenceTimeout.stop(); m_handshakePending=false; m_handshakeBuffer.clear();
    const bool wasConnected = m_connected; m_connected = false;
    m_socket.abort(); // Discard pending TX. Never retry commands across sessions.
    if (wasConnected) emit closed();
    emit errorOccurred(reason);
    if (m_requested && !m_retry.isActive())
    {
        emit statusChanged(QStringLiteral("连接中断，%1 秒后重连").arg(m_nextRetryMs / 1000.0, 0, 'f', 1));
        m_retry.start(m_nextRetryMs);
        m_nextRetryMs = qMin(5000, m_nextRetryMs * 2);
    }
    m_failing = false;
}
void TcpTransport::close()
{
    m_requested = false;
    m_retry.stop(); m_connectTimeout.stop(); m_silenceTimeout.stop();
    const bool wasConnected = m_connected; m_connected = false;
    m_socket.abort();
    if (wasConnected) emit closed();
}
bool TcpTransport::isOpen() const { return m_connected && m_socket.state() == QAbstractSocket::ConnectedState; }
QString TcpTransport::portName() const { return QStringLiteral("tcp://%1:%2").arg(m_host).arg(m_port); }
bool TcpTransport::writeBytes(const QByteArray &bytes)
{
    if (!isOpen() || !m_allowWrites || m_ownerToken.isEmpty()) return false;
    if (bytes.size() > 256 * 1024 || m_socket.bytesToWrite() + bytes.size() > 256 * 1024)
    { failed(QStringLiteral("TCP 发送积压，已断开并丢弃待发数据")); return false; }
    if (m_socket.write(bytes) != bytes.size())
    { failed(QStringLiteral("TCP 发送失败")); return false; }
    return true;
}
}
