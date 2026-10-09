#include "StereoCameraTestSupport.h"
#include "app/MainWindow.h"
#include "communication/service/BootloaderCommunicationService.h"
#include "data/services/StereoCameraService.h"
#include "pages/vision/VisionPage.h"
#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <iostream>

int main(int argc, char **argv)
{
    fluent::prepareHighDpiApplication();
    QApplication app(argc, argv);
    fluent::initializeResources();
    QCoreApplication::setOrganizationName(QStringLiteral("ROVTests"));
    QCoreApplication::setApplicationName(QStringLiteral("StereoCameraProbe"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       app.applicationDirPath() + QStringLiteral("/probe-settings"));
    const auto args = app.arguments();
    const auto option = [&](const QString &name, const QString &fallback)
    {
        const int i = args.indexOf(name);
        return i >= 0 && i + 1 < args.size() ? args.at(i + 1) : fallback;
    };
    const bool mock = args.contains(QStringLiteral("--mock"));
    QTcpServer mockServer;
    QTimer mockTimer;
    QByteArray mockPacket;
    if (mock)
    {
        if (args.contains(QStringLiteral("--with-gateway")) ||
            !mockServer.listen(QHostAddress::LocalHost, 0))
            return 1;
        mockPacket = stereoPacket(1, 2560, 720);
        mockTimer.setTimerType(Qt::PreciseTimer);
        mockTimer.setInterval(33);
        QObject::connect(&mockServer, &QTcpServer::newConnection,
                         [&]
                         {
                             auto *peer = mockServer.nextPendingConnection();
                             QObject::connect(
                                 &mockTimer, &QTimer::timeout, peer,
                                 [&, peer, sequence = quint64(0)]() mutable
                                 {
                                     if (peer->state() != QAbstractSocket::ConnectedState ||
                                         peer->bytesToWrite() > mockPacket.size() * 3)
                                         return;
                                     qToBigEndian<quint64>(
                                         ++sequence,
                                         reinterpret_cast<uchar *>(mockPacket.data()) + 8);
                                     peer->write(mockPacket);
                                 });
                             mockTimer.start();
                         });
    }
    const QString host = mock ? QStringLiteral("127.0.0.1")
                              : option(QStringLiteral("--host"), QStringLiteral("192.168.20.70"));
    const int port = mock ? mockServer.serverPort()
                          : option(QStringLiteral("--port"), QStringLiteral("9001")).toInt();
    const int target = option(QStringLiteral("--frames"), QStringLiteral("300")).toInt();
    QFile theme(QStringLiteral(":/theme/theme.qss"));
    if (theme.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    rov::MainWindow window(nullptr, false);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("pageView"));
    auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(view->scene()->items().first());
    auto *page = proxy->widget()->findChild<rov::VisionPage *>();
    auto *camera = window.findChild<rov::StereoCameraService *>();
    page->findChild<QComboBox *>(QStringLiteral("cameraSource"))->setCurrentIndex(0);
    page->findChild<QLineEdit *>(QStringLiteral("cameraHost"))->setText(host);
    page->findChild<QSpinBox *>(QStringLiteral("cameraPort"))->setValue(port);
    window.resize(1600, 1000);
    window.setPageIndex(4);
    window.show();
    rov::BootloaderCommunicationService gateway;
    int heartbeats = 0, errors = 0, count = 0;
    quint64 firstSequence = 0, lastSequence = 0;
    QElapsedTimer elapsed;
    bool saved = false;
    QObject::connect(&gateway, &rov::BootloaderCommunicationService::heartbeatReceived,
                     [&](const rov::SystemHeartbeat &) { ++heartbeats; });
    if (args.contains(QStringLiteral("--with-gateway")))
        gateway.openTcp(host, 9000, false);
    QObject::connect(camera, &rov::StereoCameraService::errorOccurred,
                     [&](const QString &e)
                     {
                         ++errors;
                         std::cerr << "camera error: " << e.toUtf8().constData() << '\n';
                     });
    QObject::connect(
        camera, &rov::StereoCameraService::frameReady,
        [&](const rov::StereoCameraFrame &frame)
        {
            if (count++ == 0)
            {
                elapsed.start();
                firstSequence = frame.sequence;
            }
            lastSequence = frame.sequence;
            if (count >= target)
            {
                // Let MainWindow deliver/render the same frame before taking the screenshot.
                QTimer::singleShot(
                    0, &window,
                    [&, frame]
                    {
                        const QString base = app.applicationDirPath() +
                                             (mock ? QStringLiteral("/stereo-camera-mock")
                                                   : QStringLiteral("/stereo-camera-live"));
                        saved = frame.stitched.save(base + QStringLiteral("-frame.jpg")) &&
                                window.grab().save(base + QStringLiteral("-page.png"));
                        std::cout << "source=" << (mock ? "mock" : "live") << " frames=" << count
                                  << " sequence_first=" << firstSequence
                                  << " sequence_last=" << lastSequence
                                  << " elapsed_ms=" << elapsed.elapsed() << " display_fps="
                                  << 1000.0 * (count - 1) / qMax<qint64>(1, elapsed.elapsed())
                                  << " size=" << frame.stitched.width() << 'x'
                                  << frame.stitched.height() << " heartbeats=" << heartbeats
                                  << " camera_errors=" << errors << " saved=" << saved << '\n';
                        app.quit();
                    });
            }
        });
    page->findChild<QPushButton *>(QStringLiteral("cameraStart"))->click();
    QTimer::singleShot(20000, &app, &QCoreApplication::quit);
    app.exec();
    page->findChild<QPushButton *>(QStringLiteral("cameraStop"))->click();
    const bool gatewayOk = !args.contains(QStringLiteral("--with-gateway")) ||
                           (gateway.isOpen() && heartbeats >= 2 && !gateway.writesAllowed());
    gateway.close();
    window.close();
    return count >= target && saved && gatewayOk && errors == 0 ? 0 : 1;
}
