#include "communication/transport/TcpTransport.h"
#include "communication/service/BootloaderCommunicationService.h"
#include <QCoreApplication>
#include <QTcpServer>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <functional>
using namespace rov;
static int checks=0;
static void check(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
static bool until(const std::function<bool()> &condition,int ms=3000)
{
    QElapsedTimer time;time.start();
    while(time.elapsed()<ms){QCoreApplication::processEvents();if(condition())return true;QThread::msleep(2);}return false;
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    try {
        QTcpServer server;check(server.listen(QHostAddress::LocalHost,0),"listen");
        TcpTransport transport;transport.setTimeouts(300,600,50);
        int opened=0,closed=0;QByteArray received;
        QObject::connect(&transport,&ByteTransport::opened,[&](const QString &){++opened;});
        QObject::connect(&transport,&ByteTransport::closed,[&]{++closed;});
        QObject::connect(&transport,&ByteTransport::bytesReceived,[&](const QByteArray &b){received+=b;});
        check(transport.open(QStringLiteral("127.0.0.1"),server.serverPort(),true),"async open");
        check(until([&]{return server.hasPendingConnections()&&transport.isOpen();}),"connect");
        auto *peer=server.nextPendingConnection();
        const QByteArray payload=QByteArray::fromHex("00aa55ff00580d0aaa5b");
        for(char b:payload){peer->write(QByteArray(1,b));peer->flush();QCoreApplication::processEvents();}
        check(until([&]{return received==payload;}),"arbitrary fragmentation, exact RX");
        check(transport.writeBytes(payload),"TX accepted");
        check(until([&]{return peer->bytesAvailable()==payload.size();}),"TX arrived");
        check(peer->readAll()==payload,"exact TX");
        peer->abort();check(until([&]{return closed==1;}),"disconnect signalled");
        check(until([&]{return server.hasPendingConnections()&&opened==2;}),"automatic reconnect");
        peer=server.nextPendingConnection();check(peer->bytesAvailable()==0,"no command replay");
        check(until([&]{return closed>=2;},2000),"silence timeout");
        transport.close();const int previous=opened;
        until([]{return false;},250);check(opened==previous&&!transport.connectionRequested(),"manual close stops reconnect");
        while(server.hasPendingConnections()){auto *p=server.nextPendingConnection();p->abort();p->deleteLater();}

        BootloaderCommunicationService service;
        int frames=0,serviceCloses=0;CanGatewayFrame last;
        QObject::connect(&service,&BootloaderCommunicationService::frameReceived,[&](const CanGatewayFrame &f){++frames;last=f;});
        QObject::connect(&service,&BootloaderCommunicationService::closed,[&]{++serviceCloses;});
        check(service.openTcp(QStringLiteral("127.0.0.1"),server.serverPort(),false),"service read-only open");
        check(until([&]{return service.isOpen()&&server.hasPendingConnections();}),"service connected");
        peer=server.nextPendingConnection();CanGatewayFrame frame;frame.canId=0x201;frame.flags=2;frame.data=QByteArray(12,'\0');
        const auto packet=encodeCanGatewayFrame(frame);
        peer->write(packet.left(5));peer->flush();QCoreApplication::processEvents();
        peer->write(packet.mid(5)+packet);peer->flush();
        check(until([&]{return frames==2;}),"service reuses incremental protocol parser");
        check(last.canId==0x201&&last.data==frame.data,"decoded payload intact");
        check(!service.sendCanFrame(frame)&&!service.writesAllowed(),"read-only commands rejected");
        peer->write(packet.left(5));peer->flush();until([]{return false;},40);
        peer->abort();check(until([&]{return serviceCloses>0;}),"service disconnected");
        check(until([&]{return service.isOpen()&&server.hasPendingConnections();},4000),"service reconnect");
        peer=server.nextPendingConnection();peer->write(packet);peer->flush();
        check(until([&]{return frames==3;}),"old partial frame cleared on reconnect");
        service.close();
        check(service.openTcp(QStringLiteral("127.0.0.1"),server.serverPort(),true),"service writable open");
        check(until([&]{return service.isOpen()&&server.hasPendingConnections();}),"writable connected");peer=server.nextPendingConnection();
        check(service.sendCanFrame(frame),"service encodes outgoing frame");
        check(until([&]{return peer->bytesAvailable()==packet.size();}),"service TX arrived");
        check(peer->readAll()==packet,"service TX wire format unchanged");
        service.close();
        std::cout<<"PASS "<<checks<<" TCP transport/service checks\n";return 0;
    }catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
