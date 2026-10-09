#include "communication/transport/TcpControlTransport.h"

#include <QNetworkProxy>

namespace rov
{
TcpControlTransport::TcpControlTransport(QObject *parent) : QObject(parent)
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(1000);
    m_heartbeat.setInterval(1000);
    m_socket.setProxy(QNetworkProxy::NoProxy);
    m_socket.setReadBufferSize(4096);
    connect(&m_retry, &QTimer::timeout, this, &TcpControlTransport::attempt);
    connect(&m_heartbeat, &QTimer::timeout, this, [this]() {
        if (isConnected() && !m_token.isEmpty())
            sendLine("PING " + m_token.toLatin1() + "\n");
    });
    connect(&m_socket, &QTcpSocket::connected, this, [this]() {
        m_retry.stop();
        m_retryScheduled = false;
        m_receiveBuffer.clear();
        sendLine("STATE\n");
        m_heartbeat.start();
        reportState(QStringLiteral("控制权服务已连接"));
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, [this]() {
        m_receiveBuffer.append(m_socket.readAll());
        if (m_receiveBuffer.size() > 4096) {
            m_socket.abort();
            scheduleRetry(QStringLiteral("控制权服务回复过长"));
            return;
        }
        processLines();
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this]() {
        m_heartbeat.stop();
        setLeaseToken(QString());
        m_receiveBuffer.clear();
        reportState(QStringLiteral("控制权服务已断开；机器人将在 3 秒内收回控制权"));
        if (m_requested)
            scheduleRetry(QStringLiteral("控制权服务连接中断"));
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_requested)
            scheduleRetry(m_socket.errorString());
    });
}

void TcpControlTransport::open(const QString &host, const quint16 port)
{
    close();
    if (host.trimmed().isEmpty() || port == 0) {
        m_owner = QStringLiteral("UNKNOWN");
        reportState(QStringLiteral("控制权服务地址或端口无效"));
        return;
    }
    m_host = host.trimmed();
    m_socket.setProperty("tcpControlPort", port);
    m_requested = true;
    attempt();
}

void TcpControlTransport::close()
{
    m_requested = false;
    m_retry.stop();
    m_heartbeat.stop();
    m_retryScheduled = false;
    setLeaseToken(QString());
    m_receiveBuffer.clear();
    m_owner = QStringLiteral("UNKNOWN");
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
        m_socket.abort();
    reportState(QStringLiteral("控制权服务未连接"));
}

void TcpControlTransport::attempt()
{
    if (!m_requested || m_socket.state() != QAbstractSocket::UnconnectedState)
        return;
    m_retryScheduled = false;
    const auto port = m_socket.property("tcpControlPort").toUInt();
    if (port == 0)
        return;
    m_socket.connectToHost(m_host, static_cast<quint16>(port));
}

void TcpControlTransport::sendLine(const QByteArray &line)
{
    if (isConnected() && line.size() <= 256 && m_socket.write(line) == line.size())
        return;
    if (m_requested)
        scheduleRetry(QStringLiteral("无法提交控制权请求"));
}

bool TcpControlTransport::requestControl(const bool takeover)
{
    if (!isConnected()) {
        reportState(QStringLiteral("控制权服务尚未连接"));
        return false;
    }
    sendLine(takeover ? QByteArrayLiteral("TAKEOVER GUI\n") : QByteArrayLiteral("CLAIM GUI\n"));
    return true;
}

bool TcpControlTransport::releaseControl()
{
    if (!isConnected() || m_token.isEmpty())
        return false;
    sendLine("RELEASE GUI\n");
    return true;
}

bool TcpControlTransport::isConnected() const
{
    return m_socket.state() == QAbstractSocket::ConnectedState;
}

void TcpControlTransport::processLines()
{
    int newline = -1;
    while ((newline = m_receiveBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_receiveBuffer.left(newline).trimmed();
        m_receiveBuffer.remove(0, newline + 1);
        const auto fields = QString::fromUtf8(line).split(' ', Qt::SkipEmptyParts);
        if (fields.isEmpty())
            continue;
        if (fields[0] == QStringLiteral("STATE") && fields.size() >= 2) {
            m_owner = fields[1];
            if (m_owner != QStringLiteral("GUI"))
                setLeaseToken(QString());
            reportState(m_owner == QStringLiteral("GUI")
                ? QStringLiteral("机器人已有其他上位机控制会话")
                : QStringLiteral("当前控制者：%1").arg(m_owner));
        } else if (fields[0] == QStringLiteral("OK") && fields.size() >= 2) {
            m_owner = fields[1];
            if (m_owner == QStringLiteral("GUI") && fields.size() >= 3) {
                setLeaseToken(fields[2]);
                reportState(QStringLiteral("上位机已获得机器人控制权"));
            } else {
                setLeaseToken(QString());
                reportState(QStringLiteral("当前控制者：%1").arg(m_owner));
            }
        } else if (fields[0] == QStringLiteral("HEARTBEAT")) {
            if (fields.size() >= 2)
                m_owner = fields[1];
            reportState(QStringLiteral("上位机控制权有效"));
        } else if (fields[0] == QStringLiteral("BUSY") && fields.size() >= 2) {
            m_owner = fields[1];
            const QString detail = fields.mid(2).join(' ');
            setLeaseToken(QString());
            reportState(detail);
            emit requestRejected(m_owner, detail);
        } else if (fields[0] == QStringLiteral("REVOKED")) {
            m_owner = fields.size() >= 2 ? fields[1] : QStringLiteral("UNKNOWN");
            setLeaseToken(QString());
            reportState(QStringLiteral("控制权已被 %1 接管").arg(m_owner));
        } else if (fields[0] == QStringLiteral("ERROR")) {
            const QString detail = fields.mid(1).join(' ');
            reportState(detail);
            emit requestRejected(m_owner, detail);
        }
    }
}

void TcpControlTransport::reportState(const QString &message)
{
    emit stateChanged(isConnected(), m_owner, ownsControl(), message);
}

void TcpControlTransport::scheduleRetry(const QString &message)
{
    if (!m_requested)
        return;
    if (!m_retryScheduled) {
        m_retryScheduled = true;
        m_retry.start();
    }
    m_heartbeat.stop();
    setLeaseToken(QString());
    m_socket.abort();
    reportState(message);
}

void TcpControlTransport::setLeaseToken(const QString &token)
{
    if (m_token == token)
        return;
    m_token = token;
    emit leaseTokenChanged(m_token);
}
}  // namespace rov
