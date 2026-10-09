// Bounded receive-only LAN probe. It never requests control ownership or transmits serial bytes.
#include "communication/service/BootloaderCommunicationService.h"
#include <QCoreApplication>
#include <QTimer>
#include <QTextStream>
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()<3){QTextStream(stderr)<<"Usage: nano_tcp_probe HOST PORT\n";return 2;}
    bool ok=false;const auto port=args[2].toUShort(&ok);if(!ok||!port)return 2;
    rov::BootloaderCommunicationService service;
    quint64 bytes=0;int frames=0,heartbeats=0,opens=0;
    QObject::connect(&service,&rov::BootloaderCommunicationService::rawBytesReceived,[&](const QByteArray &b){bytes+=b.size();});
    QObject::connect(&service,&rov::BootloaderCommunicationService::heartbeatReceived,[&](const rov::SystemHeartbeat &){++heartbeats;});
    QObject::connect(&service,&rov::BootloaderCommunicationService::frameReceived,[&](const rov::CanGatewayFrame &f){++frames;if(f.canId==0x123 && f.data==QByteArray("LINKTEST"))echo=true;});
    QObject::connect(&service,&rov::BootloaderCommunicationService::opened,[&](const QString &endpoint){
        ++opens;QTextStream(stdout)<<"CONNECTED READ-ONLY "<<endpoint<<Qt::endl;
    });
    QObject::connect(&service,&rov::BootloaderCommunicationService::errorOccurred,[&](const QString &e){QTextStream(stdout)<<"LINK "<<e<<Qt::endl;});
    service.openTcp(args[1],port,fixture);
    QTimer::singleShot(12000,&app,[&]{
        service.close();const bool passed=bytes>0&&opens>0&&(heartbeats>0||frames>0);
        QTextStream(stdout)<<"bytes="<<bytes<<" frames="<<frames<<" heartbeats="<<heartbeats<<" opens="<<opens<<" transmitted=0 result="<<(passed?"PASS":"FAIL")<<Qt::endl;
        app.exit(passed?0:1);
    });
    return app.exec();
}
