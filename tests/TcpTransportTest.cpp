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
static void checkHandshake(const QByteArray &reply, const QByteArray &expectedData,
                           const QString &expectedError = {}, bool fragmented = false)
{
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost, 0), "handshake fixture listen");
    TcpTransport transport;
    transport.setTimeouts(2000, 2000, 1000);
    QByteArray received;
    QString error;
    QObject::connect(&transport, &ByteTransport::bytesReceived,
                     [&](const QByteArray &bytes) { received += bytes; });
    QObject::connect(&transport, &ByteTransport::errorOccurred,
                     [&](const QString &message) { error = message; });
    check(transport.open(QStringLiteral("127.0.0.1"), server.serverPort()), "handshake fixture open");
    check(until([&] { return server.hasPendingConnections() && transport.isOpen(); }),
          "handshake fixture read-only connection");
    server.nextPendingConnection()->deleteLater();
    transport.setOwnerToken(QStringLiteral("fixture-session-token"));
    check(until([&] { return server.hasPendingConnections(); }), "handshake fixture owned connection");
    auto *peer = server.nextPendingConnection();
    check(until([&] { return peer->bytesAvailable() > 0; }), "handshake fixture token arrived");
    check(peer->readAll() == QByteArrayLiteral("OWNER fixture-session-token\n"),
          "handshake fixture token matches");
    if (fragmented) {
        peer->write(reply.left(3));
        peer->flush();
        until([] { return false; }, 40);
        check(!transport.isOpen() && received.isEmpty() && error.isEmpty(),
              "partial handshake waits without exposing reply bytes");
    }
    peer->write(fragmented ? reply.mid(3) : reply);
    peer->flush();
    if (expectedError.isEmpty()) {
        check(until([&] { return transport.isOpen() && received == expectedData; }, 1000),
              "handshake plus coalesced telemetry opens and preserves exact binary data");
        check(error.isEmpty(), "valid handshake with telemetry must not report an error");
    } else {
        check(until([&] { return !error.isEmpty(); }, 1000), "invalid handshake reports an error");
        check(error == expectedError && !transport.isOpen() && received.isEmpty(),
              "invalid handshake stays closed and exposes no telemetry");
    }
    transport.close();
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    try {
        // 合法握手后可立即跟随大量二进制遥测，包括 NUL 和换行符。
        const QByteArray telemetry = QByteArray::fromHex("00aa55ff00580d0aaa5b").repeated(128);
        checkHandshake(QByteArrayLiteral("OK GUI\n") + telemetry, telemetry);
        checkHandshake(QByteArrayLiteral("OK GUI\n") + telemetry, telemetry, {}, true);
        const QString tooLong = QStringLiteral("Nano 控制权握手回复过长");
        checkHandshake(QByteArray(65, 'X'), {}, tooLong);
        checkHandshake(QByteArray(64, 'X') + '\n' + telemetry, {}, tooLong);
        checkHandshake(QByteArrayLiteral("DENIED\n") + telemetry, {},
                       QStringLiteral("Nano 拒绝上位机串口控制权握手；请先接管控制权"));
        QTcpServer server;check(server.listen(QHostAddress::LocalHost,0),"listen");
        TcpTransport transport;transport.setTimeouts(300,600,50);
        int opened=0,closed=0;QByteArray received;
        QObject::connect(&transport,&ByteTransport::opened,[&](const QString &){++opened;});
        QObject::connect(&transport,&ByteTransport::closed,[&]{++closed;});
        QObject::connect(&transport,&ByteTransport::bytesReceived,[&](const QByteArray &b){received+=b;});
        check(transport.open(QStringLiteral("127.0.0.1"),server.serverPort(),false),"async open");
        check(until([&]{return server.hasPendingConnections()&&transport.isOpen();}),"connect");
        auto *peer=server.nextPendingConnection();
        const QByteArray payload=QByteArray::fromHex("00aa55ff00580d0aaa5b");
        for(char b:payload){peer->write(QByteArray(1,b));peer->flush();QCoreApplication::processEvents();}
        check(until([&]{return received==payload;}),"arbitrary fragmentation, exact RX");
        check(!transport.writeBytes(payload),"read-only TCP never sends without an ownership token");
        peer->abort();check(until([&]{return closed==1;}),"disconnect signalled");
        check(until([&]{return server.hasPendingConnections()&&opened==2;}),"automatic reconnect");
        peer=server.nextPendingConnection();check(peer->bytesAvailable()==0,"no command replay");
        check(until([&]{return closed>=2;},2000),"silence timeout");
        transport.close();const int previous=opened;
        until([]{return false;},250);check(opened==previous&&!transport.connectionRequested(),"manual close stops reconnect");
        while(server.hasPendingConnections()){auto *p=server.nextPendingConnection();p->abort();p->deleteLater();}
        check(transport.open(QStringLiteral("127.0.0.1"),server.serverPort(),true),"owned TCP open");
        check(until([&]{return server.hasPendingConnections();}),"pre-ownership client connected");
        peer=server.nextPendingConnection();
        transport.setOwnerToken(QStringLiteral("fixture-session-token"));
        check(until([&]{return server.hasPendingConnections();}),"owned client reconnected");
        peer=server.nextPendingConnection();
        check(until([&]{return peer->bytesAvailable()>0;}),"ownership preamble sent");
        check(peer->readAll()==QByteArrayLiteral("OWNER fixture-session-token\n"),"wire preamble carries only the control token");
        peer->write("OK GUI\n");peer->flush();
        check(until([&]{return transport.isOpen();}),"server validates ownership before opening the byte stream");
        check(transport.writeBytes(payload),"owned TX accepted");
        check(until([&]{return peer->bytesAvailable()==payload.size();}),"owned TX arrived");
        check(peer->readAll()==payload,"TX bytes remain unchanged after the handoff preamble");
        transport.close();

        BootloaderCommunicationService service;
        int frames=0,serviceCloses=0;CanGatewayFrame last;
        QObject::connect(&service,&BootloaderCommunicationService::frameReceived,[&](const CanGatewayFrame &f){++frames;last=f;});
        QObject::connect(&service,&BootloaderCommunicationService::closed,[&]{++serviceCloses;});
        QTcpServer controlServer;check(controlServer.listen(QHostAddress::LocalHost,0),"listen for service control");
        check(service.openTcp(QStringLiteral("127.0.0.1"),server.serverPort(),false,controlServer.serverPort()),"service read-only open");
        check(until([&]{return controlServer.hasPendingConnections();}),"read-only ownership monitor connected");
        auto *controlPeer=controlServer.nextPendingConnection();
        check(until([&]{return controlPeer->bytesAvailable()>0;})&&controlPeer->readAll()=="STATE\n","service queries current owner");
        controlPeer->write("STATE NANO\n");controlPeer->flush();
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
        peer->abort();
        service.close();
        controlPeer->deleteLater();QCoreApplication::processEvents();
        check(service.openTcp(QStringLiteral("127.0.0.1"),server.serverPort(),false,controlServer.serverPort()),"service control open");
        check(until([&]{return controlServer.hasPendingConnections();}),"writable ownership monitor connected");
        controlPeer=controlServer.nextPendingConnection();
        check(until([&]{return controlPeer->bytesAvailable()>0;})&&controlPeer->readAll()=="STATE\n","control owner queried before takeover");
        controlPeer->write("STATE NANO\n");controlPeer->flush();
        check(until([&]{return server.hasPendingConnections();}),"read-only data session established before takeover");
        peer=server.nextPendingConnection();
        check(service.requestTcpControl(true),"GUI takeover request accepted");
        check(until([&]{return controlPeer->bytesAvailable()>0;})&&controlPeer->readAll()=="TAKEOVER GUI\n","explicit takeover requested on the management channel");
        controlPeer->write("OK GUI fixture-session-token GUI took over robot control\n");controlPeer->flush();
        check(until([&]{return server.hasPendingConnections();}),"data socket reconnected with the lease token");
        peer=server.nextPendingConnection();
        check(until([&]{return peer->bytesAvailable()>0;}),"service authenticates the data channel");
        check(peer->readAll()=="OWNER fixture-session-token\n","controller token is not mixed into serial data");
        peer->write("OK GUI\n");peer->flush();
        check(until([&]{return service.isOpen()&&service.writesAllowed();}),"write permission waits for both lease and UART socket authentication");
        check(service.sendCanFrame(frame),"service encodes outgoing frame");
        check(until([&]{return peer->bytesAvailable()==packet.size();}),"service TX arrived");
        check(peer->readAll()==packet,"service TX wire format unchanged");
        const QByteArray rawSerial=QByteArray::fromHex("aa5b010401024316c1040600000000000100040264001e7c5baa");
        check(service.sendRawBytes(rawSerial),"raw serial write allowed only under the GUI ownership lease");
        check(until([&]{return peer->bytesAvailable()==rawSerial.size();}),"raw bytes reach the Nano byte stream");
        check(peer->readAll()==rawSerial,"raw serial bytes pass through without an added envelope");
        peer->abort();controlPeer->deleteLater();
        service.close();
        std::cout<<"PASS "<<checks<<" TCP transport/service checks\n";return 0;
    }catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
