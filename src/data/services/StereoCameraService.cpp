#include "data/services/StereoCameraService.h"
#include "communication/protocol/StereoCameraProtocol.h"

#include <QBuffer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <optional>

namespace rov
{
namespace
{
struct Mailbox
{
    QMutex mutex;
    VisionSnapshot snapshot;
    StereoCameraFrame frame;
    QString error;
    bool dirty = false;
};

struct DecodeResult
{
    StereoCameraFrame frame;
    QString error;
};

DecodeResult decode(StereoCameraPacket packet)
{
    DecodeResult result;
    // Qt/libjpeg may tolerate a truncated JPEG. Require the complete envelope too.
    if (packet.jpeg.size() < 4 || !packet.jpeg.startsWith(QByteArray::fromHex("ffd8")) ||
        !packet.jpeg.endsWith(QByteArray::fromHex("ffd9")))
    {
        result.error = QStringLiteral("双目 JPEG 不完整或标记无效");
        return result;
    }
    QBuffer buffer(&packet.jpeg);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "JPEG");
    const QSize expected(static_cast<int>(packet.width), static_cast<int>(packet.height));
    if (reader.size() != expected)
        result.error = QStringLiteral("JPEG 内部尺寸与双目帧头不一致");
    else
    {
        result.frame.stitched = QImage::fromData(packet.jpeg, "JPEG");
        if (result.frame.stitched.isNull() || result.frame.stitched.size() != expected)
            result.error = QStringLiteral("双目 JPEG 解码失败或尺寸不一致");
        else
        {
            // Nano has already rotated and ordered both eyes. No further transforms.
            result.frame.left =
                result.frame.stitched.copy(0, 0, expected.width() / 2, expected.height());
            result.frame.right = result.frame.stitched.copy(
                expected.width() / 2, 0, expected.width() / 2, expected.height());
            result.frame.sequence = packet.sequence;
            result.frame.receiptNs = packet.receiptNs;
        }
    }
    return result;
}

class CameraWorker final : public QObject
{
  public:
    explicit CameraWorker(std::shared_ptr<Mailbox> mailbox) : m_mailbox(std::move(mailbox))
    {
        m_snapshot.stereo = true;
        m_snapshot.cameraDevice = QStringLiteral("机器人双目相机");
        m_snapshot.streamState = QStringLiteral("未启动");
        m_snapshot.resolution = QStringLiteral("--");
        m_snapshot.frameRate = QStringLiteral("--");
        m_snapshot.nominalFrameRate = QStringLiteral("30 FPS（输出目标）");
        m_snapshot.pixelFormat = QStringLiteral("JPEG / 彩色");
        m_snapshot.lastFrame = QStringLiteral("--");
        m_snapshot.latency = QStringLiteral("网络接收");
        m_snapshot.nodeState = QStringLiteral("Nano 双目转发");
        m_snapshot.processingMode = QStringLiteral("原始双目 / 未标定");
        m_snapshot.modelsLoaded = QStringLiteral("0");
        m_snapshot.cameraStamp.freshness = DataFreshness::Offline;
        m_pool.setMaxThreadCount(1);
        publish();
    }

    ~CameraWorker() override
    {
        m_pool.waitForDone();
    }

    void initialize()
    {
        if (m_socket)
            return;
        m_clock.start();
        m_socket = new QTcpSocket(this);
        m_socket->setProxy(QNetworkProxy::NoProxy);
        m_socket->setReadBufferSize(StereoCameraParser::MaxBufferSize);
        m_retry = new QTimer(this);
        m_retry->setSingleShot(true);
        m_connectTimeout = new QTimer(this);
        m_connectTimeout->setSingleShot(true);
        m_watchdog = new QTimer(this);
        m_watchdog->setInterval(100);
        m_watcher = new QFutureWatcher<DecodeResult>(this);
        connect(m_retry, &QTimer::timeout, this, [this] { connectCamera(); });
        connect(m_connectTimeout, &QTimer::timeout, this,
                [this] { fail(QStringLiteral("相机连接超时")); });
        connect(m_watchdog, &QTimer::timeout, this,
                [this]
                {
                    if (!m_snapshot.connected)
                        return;
                    const qint64 reference = m_lastValidMs >= 0 ? m_lastValidMs : m_connectedMs;
                    if (m_clock.elapsed() - reference >= 2000 &&
                        m_snapshot.streamState != QStringLiteral("画面超时"))
                    {
                        invalidate(DataFreshness::Stale);
                        m_snapshot.streamState = QStringLiteral("画面超时");
                        publish();
                    }
                });
        connect(m_socket, &QTcpSocket::connected, this,
                [this]
                {
                    m_connectTimeout->stop();
                    m_snapshot.connected = true;
                    m_connectedMs = m_clock.elapsed();
                    m_snapshot.streamState = QStringLiteral("等待相机画面");
                    m_watchdog->start();
                    publish();
                });
        connect(m_socket, &QTcpSocket::disconnected, this,
                [this] { fail(QStringLiteral("相机连接已断开")); });
        connect(m_socket, &QTcpSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError) { fail(m_socket->errorString()); });
        connect(m_socket, &QTcpSocket::readyRead, this, [this] { readFrames(); });
        connect(m_watcher, &QFutureWatcher<DecodeResult>::finished, this,
                [this]
                {
                    m_decoding = false;
                    const DecodeResult result = m_watcher->result();
                    if (m_decodeEpoch == m_epoch && m_snapshot.connected)
                    {
                        if (!result.error.isEmpty())
                        {
                            fail(result.error);
                            return;
                        }
                        // Freshness uses the local monotonic receive time, never Nano time.
                        if (m_clock.elapsed() - m_decodeReceiveMs < 2000)
                        {
                            m_lastValidMs = m_decodeReceiveMs;
                            m_retryStep = 0;
                            ++m_fpsFrames;
                            const qint64 elapsed = m_clock.elapsed() - m_fpsStartMs;
                            if (elapsed >= 1000)
                            {
                                m_snapshot.frameRate = QStringLiteral("%1 FPS").arg(
                                    1000.0 * m_fpsFrames / elapsed, 0, 'f', 1);
                                m_fpsStartMs = m_clock.elapsed();
                                m_fpsFrames = 0;
                            }
                            m_snapshot.frameAvailable = true;
                            m_snapshot.cameraStamp.validity = DataValidity::Valid;
                            m_snapshot.cameraStamp.freshness = DataFreshness::Fresh;
                            m_snapshot.cameraStamp.reason.clear();
                            m_snapshot.streamState = QStringLiteral("双目画面接收中");
                            m_snapshot.resolution = QStringLiteral("%1 × %2（每目 %3 × %2）")
                                                        .arg(result.frame.stitched.width())
                                                        .arg(result.frame.stitched.height())
                                                        .arg(result.frame.left.width());
                            m_snapshot.lastFrame = QStringLiteral("%1 / 帧 %2")
                                                       .arg(QDateTime::currentDateTime().toString(
                                                           QStringLiteral("HH:mm:ss.zzz")))
                                                       .arg(result.frame.sequence);
                            QMutexLocker lock(&m_mailbox->mutex);
                            m_mailbox->frame = result.frame;
                            m_mailbox->snapshot = m_snapshot;
                            m_mailbox->dirty = true;
                        }
                    }
                    decodePending();
                });
    }

    void startCamera(const QString &host, quint16 port)
    {
        initialize();
        stopCamera();
        if (host.trimmed().isEmpty() || port == 0)
        {
            m_snapshot.streamState = QStringLiteral("相机地址或端口无效");
            publish(m_snapshot.streamState);
            return;
        }
        m_host = host.trimmed();
        m_port = port;
        m_snapshot.cameraDevice = QStringLiteral("机器人双目 %1:%2").arg(m_host).arg(port);
        m_snapshot.connectionRequested = true;
        m_retryStep = 0;
        connectCamera();
    }

    void stopCamera()
    {
        initialize();
        m_snapshot.connectionRequested = false;
        m_retry->stop();
        m_connectTimeout->stop();
        m_watchdog->stop();
        resetConnection();
        m_socket->abort();
        m_snapshot.streamState = QStringLiteral("已停止");
        publish();
    }

  private:
    void invalidate(DataFreshness freshness)
    {
        m_snapshot.frameAvailable = false;
        m_snapshot.frameRate = QStringLiteral("--");
        m_snapshot.lastFrame = QStringLiteral("--");
        m_snapshot.cameraStamp.validity = DataValidity::Invalid;
        m_snapshot.cameraStamp.freshness = freshness;
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->frame = {};
    }

    void resetConnection()
    {
        ++m_epoch; // Ignore any decode still completing from the previous session.
        m_parser.clear();
        m_pending.reset();
        m_lastValidMs = -1;
        m_fpsFrames = 0;
        m_fpsStartMs = m_clock.elapsed();
        m_snapshot.connected = false;
        m_snapshot.resolution = QStringLiteral("--");
        invalidate(DataFreshness::Offline);
    }

    void connectCamera()
    {
        if (!m_snapshot.connectionRequested)
            return;
        resetConnection();
        m_snapshot.streamState = QStringLiteral("正在连接相机");
        publish();
        m_socket->connectToHost(m_host, m_port);
        m_connectTimeout->start(3000);
    }

    void fail(const QString &reason)
    {
        if (!m_snapshot.connectionRequested || m_failing || m_retry->isActive())
            return;
        m_failing = true;
        m_connectTimeout->stop();
        m_watchdog->stop();
        resetConnection();
        m_socket->abort();
        static const int delays[] = {1000, 2000, 5000};
        const int delay = delays[qMin(m_retryStep++, 2)];
        m_snapshot.streamState = QStringLiteral("相机断开，%1 秒后重连").arg(delay / 1000);
        m_snapshot.cameraStamp.reason = reason;
        publish(reason);
        m_retry->start(delay);
        m_failing = false;
    }

    void readFrames()
    {
        // Finite read size and total work per event keep watchdog/stop responsive.
        int budget = 1024 * 1024;
        while (m_snapshot.connected && m_socket->bytesAvailable() > 0 && budget > 0)
        {
            const QByteArray bytes = m_socket->read(qMin<qint64>(64 * 1024, budget));
            budget -= bytes.size();
            QString error;
            if (!m_parser.feed(
                    bytes,
                    [this](StereoCameraPacket packet)
                    {
                        m_pending = std::move(packet); // One waiting JPEG, overwrite old backlog.
                        m_pendingReceiveMs = m_clock.elapsed();
                    },
                    &error))
            {
                fail(error);
                return;
            }
        }
        decodePending();
        if (m_snapshot.connected && m_socket->bytesAvailable() > 0)
            QTimer::singleShot(0, this, [this] { readFrames(); });
    }

    void decodePending()
    {
        if (m_decoding || !m_pending || !m_snapshot.connected)
            return;
        m_decoding = true;
        m_decodeEpoch = m_epoch;
        m_decodeReceiveMs = m_pendingReceiveMs;
        StereoCameraPacket packet = std::move(*m_pending);
        m_pending.reset();
        m_watcher->setFuture(QtConcurrent::run(&m_pool, [packet = std::move(packet)]() mutable
                                               { return decode(std::move(packet)); }));
    }

    void publish(const QString &error = {})
    {
        QMutexLocker lock(&m_mailbox->mutex);
        m_mailbox->snapshot = m_snapshot;
        m_mailbox->error = error;
        m_mailbox->dirty = true;
    }

    std::shared_ptr<Mailbox> m_mailbox;
    VisionSnapshot m_snapshot;
    QTcpSocket *m_socket = nullptr;
    QTimer *m_retry = nullptr;
    QTimer *m_connectTimeout = nullptr;
    QTimer *m_watchdog = nullptr;
    QFutureWatcher<DecodeResult> *m_watcher = nullptr;
    QThreadPool m_pool;
    QElapsedTimer m_clock;
    StereoCameraParser m_parser;
    std::optional<StereoCameraPacket> m_pending;
    QString m_host;
    quint16 m_port = 9001;
    quint64 m_epoch = 0;
    quint64 m_decodeEpoch = 0;
    qint64 m_pendingReceiveMs = 0;
    qint64 m_decodeReceiveMs = 0;
    qint64 m_connectedMs = 0;
    qint64 m_lastValidMs = -1;
    qint64 m_fpsStartMs = 0;
    int m_fpsFrames = 0;
    int m_retryStep = 0;
    bool m_decoding = false;
    bool m_failing = false;
};
} // namespace

class StereoCameraService::Impl
{
  public:
    std::shared_ptr<Mailbox> mailbox = std::make_shared<Mailbox>();
    QThread thread;
    CameraWorker *worker = new CameraWorker(mailbox);
    explicit Impl()
    {
        worker->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        thread.start();
    }
    ~Impl()
    {
        QMetaObject::invokeMethod(
            worker, [this] { worker->stopCamera(); }, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
};

StereoCameraService::StereoCameraService(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
    auto *timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(33);
    connect(timer, &QTimer::timeout, this, &StereoCameraService::publishLatest);
    timer->start();
}

StereoCameraService::~StereoCameraService() = default;

void StereoCameraService::startCamera(const QString &host, quint16 port)
{
    QMetaObject::invokeMethod(
        m_impl->worker, [worker = m_impl->worker, host, port] { worker->startCamera(host, port); },
        Qt::QueuedConnection);
}

void StereoCameraService::stopCamera()
{
    QMetaObject::invokeMethod(
        m_impl->worker, [this] { m_impl->worker->stopCamera(); }, Qt::BlockingQueuedConnection);
    publishLatest();
}

void StereoCameraService::publishLatest()
{
    VisionSnapshot snapshot;
    StereoCameraFrame frame;
    QString error;
    {
        QMutexLocker lock(&m_impl->mailbox->mutex);
        if (!m_impl->mailbox->dirty)
            return;
        snapshot = m_impl->mailbox->snapshot;
        frame = std::move(m_impl->mailbox->frame);
        error = std::move(m_impl->mailbox->error);
        m_impl->mailbox->dirty = false;
    }
    emit snapshotChanged(snapshot);
    if (!frame.stitched.isNull() && snapshot.frameAvailable)
        emit frameReady(frame);
    if (!error.isEmpty())
        emit errorOccurred(error);
}
} // namespace rov
