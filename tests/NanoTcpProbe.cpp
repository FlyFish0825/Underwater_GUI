// Bounded LAN probe. Default is receive-only; --echo-fixture is only for a synthetic PTY peer.
#include "communication/service/BootloaderCommunicationService.h"
#include <QCoreApplication>
#include <QTimer>
#include <QTextStream>
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()<3){QTextStream(stderr)<<"Usage: nano_tcp_probe HOST PORT [--echo-fixture]\n";return 2;}
    bool ok=false;const auto port=args[2].toUShort(&ok);if(!ok||!port)return 2;
    const bool fixture=args.contains(QStringLiteral("--echo-fixture"));
    rov::BootloaderCommunicationService service;
    quint64 bytes=0;int frames=0,heartbeats=0,opens=0;bool echo=false,sent=false;
    QObject::connect(&service,&rov::BootloaderCommunicationService::rawBytesReceived,[&](const QByteArray &b){bytes+=b.size();});
    QObject::connect(&service,&rov::BootloaderCommunicationService::heartbeatReceived,[&](const rov::SystemHeartbeat &){++heartbeats;});
    QObject::connect(&service,&rov::BootloaderCommunicationService::frameReceived,[&](const rov::CanGatewayFrame &f){++frames;if(f.canId==0x123 && f.data==QByteArray("LINKTEST"))echo=true;});
    QObject::connect(&service,&rov::BootloaderCommunicationService::opened,[&](const QString &endpoint){
        ++opens;QTextStream(stdout)<<"CONNECTED "<<endpoint<<Qt::endl;
        if(fixture){rov::CanGatewayFrame f;f.canId=0x123;f.flags=0;f.data="LINKTEST";sent=service.sendCanFrame(f);}
    });
    QObject::connect(&service,&rov::BootloaderCommunicationService::errorOccurred,[&](const QString &e){QTextStream(stdout)<<"LINK "<<e<<Qt::endl;});
    service.openTcp(args[1],port,fixture);
    QTimer::singleShot(12000,&app,[&]{
        service.close();const bool passed=bytes>0&&opens>0&&(fixture?(sent&&echo):(heartbeats>0||frames>0));
        QTextStream(stdout)<<"bytes="<<bytes<<" frames="<<frames<<" heartbeats="<<heartbeats<<" opens="<<opens<<" synthetic_echo="<<echo<<" result="<<(passed?"PASS":"FAIL")<<Qt::endl;
        app.exit(passed?0:1);
    });
    return app.exec();
}
