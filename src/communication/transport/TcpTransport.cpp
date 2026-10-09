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
        m_connectTimeout.stop();
        m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
        m_socket.setSocketOption(QAbstractSocket::KeepAliveOption, 1);
        m_connected = true;
        m_nextRetryMs = m_retryMs;
        m_silenceTimeout.start(m_silenceMs);
        emit opened(portName());
        emit statusChanged(QStringLiteral("TCP 已连接 · %1").arg(m_allowWrites ? QStringLiteral("允许发送") : QStringLiteral("只读")));
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, [this]() {
        const QByteArray bytes = m_socket.readAll();
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
    m_requested = true; m_nextRetryMs = m_retryMs; attempt();
    return true; // Asynchronous connection requested; opened() confirms completion.
}
void TcpTransport::attempt()
{
    if (!m_requested) return;
    emit statusChanged(QStringLiteral("正在连接 %1").arg(portName()));
    m_connectTimeout.start(m_connectMs);
    m_socket.connectToHost(m_host, m_port);
}
void TcpTransport::failed(const QString &reason)
{
    if (m_failing || !m_requested) return;
    m_failing = true;
    m_connectTimeout.stop(); m_silenceTimeout.stop();
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
    if (!isOpen() || !m_allowWrites) return false;
    if (bytes.size() > 256 * 1024 || m_socket.bytesToWrite() + bytes.size() > 256 * 1024)
    { failed(QStringLiteral("TCP 发送积压，已断开并丢弃待发数据")); return false; }
    if (m_socket.write(bytes) != bytes.size())
    { failed(QStringLiteral("TCP 发送失败")); return false; }
    return true;
}
}
