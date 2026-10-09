#include "pages/firmware/FirmwarePage.h"
#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QTcpServer>
#include <QSettings>
#include <QElapsedTimer>
#include <QThread>
#include <QVBoxLayout>
#include <QFile>
#include <QFont>
#include <QDebug>
#include <functional>
#include <iostream>
static bool until(const std::function<bool()> &condition,int ms=3000){QElapsedTimer t;t.start();while(t.elapsed()<ms){qApp->processEvents();if(condition())return true;QThread::msleep(2);}return false;}
int main(int argc,char **argv)
{
    fluent::prepareHighDpiApplication();QApplication app(argc,argv);fluent::initializeResources();
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"),9));
    QCoreApplication::setOrganizationName(QStringLiteral("ROVTests"));QCoreApplication::setApplicationName(QStringLiteral("NanoConnectionTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,QCoreApplication::applicationDirPath()+QStringLiteral("/settings-test"));
    QSettings().clear();
    QFile theme(QStringLiteral(":/theme/theme.qss"));if(theme.open(QIODevice::ReadOnly))app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    rov::FirmwarePage page(nullptr,false);QWidget panel;panel.setAttribute(Qt::WA_DontShowOnScreen);auto *layout=new QVBoxLayout(&panel);layout->addWidget(page.connectionBar());
    auto *bar=page.connectionBar();
    auto *mode=bar->findChild<QComboBox *>(QStringLiteral("connectionModeCombo"));
    auto *host=bar->findChild<QLineEdit *>(QStringLiteral("nanoHostEdit"));
    auto *port=bar->findChild<QSpinBox *>(QStringLiteral("nanoPortSpin"));
    auto *writes=bar->findChild<QCheckBox *>(QStringLiteral("nanoAllowWrites"));
    auto *button=bar->findChild<QPushButton *>(QStringLiteral("nanoConnectButton"));
    if(!mode||!host||!port||!writes||!button||writes->isChecked())return 1;
    const auto args=app.arguments();const int liveIndex=args.indexOf(QStringLiteral("--live-host"));
    const QString liveHost=liveIndex>=0&&liveIndex+1<args.size()?args.at(liveIndex+1):QString();
    const bool live=!liveHost.isEmpty();int heartbeats=0;
    QObject::connect(page.communicationService(),&rov::BootloaderCommunicationService::heartbeatReceived,
        [&](const rov::SystemHeartbeat &){++heartbeats;});
    QTcpServer server;if(!live&&!server.listen(QHostAddress::LocalHost,0))return 1;
    mode->setCurrentIndex(1);host->setText(live?liveHost:QStringLiteral("127.0.0.1"));port->setValue(live?9000:server.serverPort());
    panel.resize(1280,140);panel.show();button->click();
    if(!until([&]{return page.communicationService()->isOpen()&&(live||server.hasPendingConnections());}))return 1;
    if(live){if(!until([&]{return heartbeats>=2;},6000))return 1;}
    else{auto *peer=server.nextPendingConnection();peer->write(QByteArray::fromHex("aa"));peer->flush();}
    if(page.communicationService()->writesAllowed()||host->isEnabled()||button->text()!=QStringLiteral("断开"))return 1;
    const auto prefix=live?QStringLiteral("/nano-tcp-live"):QStringLiteral("/nano-tcp-connection");
    app.processEvents();panel.grab().save(QCoreApplication::applicationDirPath()+prefix+QStringLiteral(".png"));
    panel.resize(1000,140);app.processEvents();
    if(bar->width()>panel.width()||button->mapTo(&panel,QPoint(button->width(),0)).x()>panel.width())return 1;
    panel.grab().save(QCoreApplication::applicationDirPath()+prefix+QStringLiteral("-1000.png"));
    button->click();if(page.communicationService()->isTcpRequested()||!host->isEnabled())return 1;
    mode->setCurrentIndex(0);app.processEvents();if(host->isVisible())return 1;
    page.closeAuxiliaryWindows();std::cout<<"PASS Nano connection UI: mode, readonly, connect/disconnect, narrow layout; live="<<live<<" heartbeats="<<heartbeats<<'\n';return 0;
}
