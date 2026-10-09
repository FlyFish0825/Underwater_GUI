#include "StereoCameraTestSupport.h"
#include "app/MainWindow.h"
#include "data/services/StereoCameraService.h"
#include "pages/vision/VisionPage.h"
#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTcpServer>
#include <QTcpSocket>
#include <iostream>

int main(int argc, char **argv)
{
    fluent::prepareHighDpiApplication();
    QApplication app(argc, argv);
    fluent::initializeResources();
    QCoreApplication::setOrganizationName(QStringLiteral("ROVTests"));
    QCoreApplication::setApplicationName(QStringLiteral("StereoCameraPageTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       app.applicationDirPath() + QStringLiteral("/settings-test"));
    QSettings().clear();
    QFile theme(QStringLiteral(":/theme/theme.qss"));
    if (theme.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    try
    {
        rov::MainWindow window(nullptr, false); // Never discover/open real MCU ports during tests.
        window.setAttribute(Qt::WA_DontShowOnScreen);
        auto *view = window.findChild<QGraphicsView *>(QStringLiteral("pageView"));
        require(view && view->scene() && !view->scene()->items().isEmpty(),
                "main page graphics scene");
        auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(view->scene()->items().first());
        require(proxy && proxy->widget(), "main window embedded page stack");
        auto *vision = proxy->widget()->findChild<rov::VisionPage *>();
        require(vision, "original VisionPage exists in graphics proxy");
        auto &page = *vision;
        window.setPageIndex(4);
        auto *source = page.findChild<QComboBox *>(QStringLiteral("cameraSource"));
        auto *host = page.findChild<QLineEdit *>(QStringLiteral("cameraHost"));
        auto *port = page.findChild<QSpinBox *>(QStringLiteral("cameraPort"));
        auto *start = page.findChild<QPushButton *>(QStringLiteral("cameraStart"));
        auto *stop = page.findChild<QPushButton *>(QStringLiteral("cameraStop"));
        auto *save = page.findChild<QPushButton *>(QStringLiteral("cameraSaveFrame"));
        auto *left = page.findChild<QLabel *>(QStringLiteral("cameraLeftPreview"));
        auto *right = page.findChild<QLabel *>(QStringLiteral("cameraRightPreview"));
        require(source && host && port && start && stop && save && left && right,
                "original page camera controls");
        require(source->currentIndex() == 0 && host->text() == QStringLiteral("192.168.20.70") &&
                    port->value() == 9001,
                "default robot endpoint");
        require(start->isEnabled() && !stop->isEnabled() && !save->isEnabled(), "initial UI state");
        QTcpServer server;
        require(server.listen(QHostAddress::LocalHost, 0), "listen");
        host->setText(QStringLiteral("127.0.0.1"));
        port->setValue(server.serverPort());
        window.resize(1600, 1000);
        window.show();
        start->click();
        require(until([&] { return server.hasPendingConnections() && stop->isEnabled(); }),
                "page starts image service");
        require(!host->isEnabled() && !start->isEnabled(), "active endpoint locked");
        auto *peer = server.nextPendingConnection();
        peer->write(stereoPacket(1, 2560, 720));
        peer->flush();
        require(until(
                    [&]
                    {
                        return save->isEnabled() && !left->pixmap(Qt::ReturnByValue).isNull() &&
                               !right->pixmap(Qt::ReturnByValue).isNull();
                    }),
                "two visible eye images");
        const auto image = left->pixmap(Qt::ReturnByValue).size();
        require(qAbs(image.width() * 720 - image.height() * 1280) < 1280, "aspect ratio preserved");
        require(window.grab().save(app.applicationDirPath() +
                                   QStringLiteral("/stereo-camera-page.png")),
                "page screenshot");
        window.resize(1000, 720);
        QCoreApplication::processEvents();
        require(window.grab().save(app.applicationDirPath() +
                                   QStringLiteral("/stereo-camera-page-1000.png")),
                "narrow page screenshot");
        window.setPageIndex(0);
        peer->write(stereoPacket(2));
        peer->flush();
        until([] { return false; }, 100);
        window.setPageIndex(4);
        require(save->isEnabled(), "page switch retains camera session");
        require(until([&] { return !save->isEnabled(); }), "stale frame cannot be saved");
        require(left->pixmap(Qt::ReturnByValue).isNull() &&
                    right->pixmap(Qt::ReturnByValue).isNull(),
                "stale images cleared");
        peer->abort();
        require(until([&] { return left->text().contains(QStringLiteral("相机断开")); }),
                "disconnection state reaches original page");
        require(stop->isEnabled() && !start->isEnabled(), "retry can be cancelled from page");
        stop->click();
        require(!stop->isEnabled() && start->isEnabled() && host->isEnabled(),
                "stop while connected or retrying");
        int sourceChanges = 0;
        QObject::connect(&page, &rov::VisionPage::cameraSourceRequested,
                         [&](rov::CameraSource s)
                         {
                             ++sourceChanges;
                             require(s == rov::CameraSource::Local, "local source");
                         });
        source->setCurrentIndex(1);
        page.setAvailableCameras({QStringLiteral("Test local camera")});
        require(sourceChanges == 1 && !host->isVisible() && !right->isVisible() &&
                    start->isEnabled(),
                "local camera option retained");
        window.close();
        std::cout << "PASS original VisionPage: robot endpoint, side-by-side ratio, status/save, "
                     "switch, stop, local option\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
