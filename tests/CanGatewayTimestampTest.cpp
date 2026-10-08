// Supplied 2026-10-07 AA55 uplink specification. All injected packets are synthetic.
// Never opens a serial port, writes CAN, or changes firmware/device configuration.
#include "communication/protocol/CanGatewayProtocol.h"
#include "communication/protocol/CanGatewayConfigProtocol.h"
#include "communication/protocol/CanFlowControlProtocol.h"
#include "communication/protocol/SystemHeartbeatProtocol.h"
#include "communication/protocol/SensorProtocol.h"
#include "communication/protocol/ObserverMotorProtocol.h"
#include "communication/transport/SerialTransport.h"
#include <QCoreApplication>
#include <QSet>
#include <QTimer>
#include <QHash>
#include <QObject>
// Test-only access to arm a pending configuration without sending a real command.
#define private public
#include "communication/service/BootloaderCommunicationService.h"
#undef private
#include <array>
#include <iostream>
#include <random>

using namespace rov;
static int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::cerr << __LINE__ << ": " #x "\n"; } } while (0)
static quint8 referenceCrc(const QByteArray &b)
{
    // Independent table-driven implementation; production uses per-bit updates.
    static const auto table = [] {
        std::array<quint8,256> t{};
        for (int i=0; i<256; ++i) { unsigned c=i; for(int k=0;k<8;++k)c=(c&128)?(c<<1)^7:c<<1; t[i]=quint8(c); }
        return t;
    }();
    quint8 crc=0; for (char v:b) crc=table[crc^quint8(v)]; return crc;
}
static void le16(QByteArray &b,quint16 v) { b.append(char(v)); b.append(char(v>>8)); }
static void le32(QByteArray &b,quint32 v) { le16(b,quint16(v)); le16(b,quint16(v>>16)); }
static QByteArray seal(const QByteArray &body)
{
    QByteArray b=QByteArray::fromHex("aa55"); b.append(char(body.size())); b+=body;
    b.append(char(referenceCrc(b.mid(2)))); b+=QByteArray::fromHex("55aa"); return b;
}
static QByteArray canPacket(int n,bool timestamped,quint32 time=0,quint16 seq=1,int fixed=0)
{
    QByteArray b; le16(b,seq); le32(b,0x123); b.append(char(n>8?2:0)); b.append(char(n));
    for(int i=0;i<n;++i)b.append(char((17*i+9)&255));
    if(timestamped)le32(b,time); else if(fixed) b=b.leftJustified(fixed,char(0));
    return seal(b);
}
static QByteArray configPacket(bool timestamped,quint32 time=0,quint16 seq=2,int status=0)
{
    QByteArray b; le16(b,seq); le32(b,0); b.append(char(0x80)); b.append(char(0x81));
    b.append(char(status)); le32(b,500000); le32(b,5000000);
    if(timestamped)le32(b,time); return seal(b);
}
static QByteArray heartbeat()
{
    QByteArray b=QByteArray::fromHex("aa5801010000070000000500000000000a141e2832");
    le16(b,SensorWire::crc16(b.mid(1))); return b+QByteArray::fromHex("58aa");
}
static void inject(BootloaderCommunicationService &service,const QByteArray &b)
{
    auto *transport=service.findChild<SerialTransport *>(); CHECK(transport);
    if(transport)CHECK(QMetaObject::invokeMethod(transport,"bytesReceived",Qt::DirectConnection,Q_ARG(QByteArray,b)));
}
static void testDocumentVectors()
{
    const std::array<QByteArray,3> packets{{
        QByteArray::fromHex("AA551401002301000000081122334455667788000000006955AA"),
        QByteArray::fromHex("AA55140100FE0700000008554152545F52582100000000B255AA"),
        QByteArray::fromHex("AA5518010023010000020CAABBCCDDEEFF010203040506E80300009E55AA")}};
    const std::array<QByteArray,3> expected{{QByteArray::fromHex("1122334455667788"),QByteArray("UART_RX!"),QByteArray::fromHex("aabbccddeeff010203040506")}};
    for(int i=0;i<3;++i) {
        CanGatewayFrame frame; CHECK(decodeCanGatewayFrame(packets[i],frame));
        CHECK(frame.sequence==1 && frame.canId==quint32(i==1?0x7fe:0x123));
        CHECK(frame.data==expected[i] && frame.hasTimestamp && frame.timestampUs==quint32(i==2?1000:0));
        CHECK(frame.timestampExtendedUs==-1); // Pure decoder does not invent a session.
        CHECK(referenceCrc(packets[i].mid(2,packets[i].size()-5))==quint8(packets[i].at(packets[i].size()-3)));
    }
    CanGatewayConfigResponse config;
    const auto packet=QByteArray::fromHex("AA551502000000000080810020A10700404B4C0000000000C955AA");
    CHECK(decodeCanGatewayConfigResponse(packet,config));
    CHECK(config.sequence==2 && config.status==CanGatewayConfigStatus::Ok);
    CHECK(config.nominalBitrate==500000 && config.dataBitrate==5000000);
    CHECK(config.hasTimestamp && config.timestampUs==0 && packet.size()==27);
    CHECK(encodeCanGatewayConfigResponse(config)==packet);
    CanGatewayTimestampUnwrapper clock;
    CHECK(clock.extend(0)==0); // TS=0 is valid, not absent.
}
static void testLengthsAndSplits()
{
    for(int n=0;n<=64;++n)for(bool modern:{false,true}) {
        const auto wire=canPacket(n,modern,0xFEDCBA98U);
        CanGatewayFrame frame;
        CHECK(decodeCanGatewayFrame(wire,frame));
        CHECK(frame.data.size()==n && frame.hasTimestamp==modern);
        CHECK(frame.timestampUs==(modern?0xFEDCBA98U:0U));
        CHECK(wire.size()==(modern?18:14)+n);
        // Downlink always remains legacy even if RX metadata is present.
        CHECK(encodeCanGatewayFrame(frame)==canPacket(n,false));
        for(int cut=0;cut<=wire.size();++cut) {
            CanGatewayDecoder d; auto got=d.feed(wire.left(cut));got+=d.feed(wire.mid(cut));
            CHECK(got.size()==1); if(got.size()==1)CHECK(got[0].data==frame.data && got[0].hasTimestamp==modern);
            CHECK(d.bufferedBytes()==0);
        }
        CanGatewayDecoder d; QVector<CanGatewayFrame> all;
        for(char b:wire)all+=d.feed(QByteArray(1,b));
        CHECK(all.size()==1 && all[0].data==frame.data);
    }
    CHECK(canPacket(64,true).size()==82);
    CanGatewayFrame a,b;
    CHECK(decodeCanGatewayFrame(canPacket(8,true),a) && decodeCanGatewayFrame(canPacket(12,false),b));
    CHECK(a.hasTimestamp && !b.hasTimestamp); // Both BODY_LEN=20, but LEN disambiguates.
    CHECK(decodeCanGatewayFrame(canPacket(12,false,0,1,72),a));
    CHECK(a.data.size()==12 && !a.hasTimestamp);
    CHECK(decodeCanGatewayFrame(canPacket(60,true,1234),a));
    CHECK(a.hasTimestamp && a.timestampUs==1234); // 72/60 must not silently discard new timestamp.
    CHECK(!decodeCanGatewayFrame(canPacket(65,true),a));
    CHECK(!decodeCanGatewayFrame(canPacket(65,false),a));
    const auto valid=canPacket(8,true,1000);
    for(int n=0;n<valid.size();++n)CHECK(!decodeCanGatewayFrame(valid.left(n),a));
    CHECK(!decodeCanGatewayFrame(valid+char(0),a));
    for(int body=0;body<=255;++body) {
        if(body==quint8(valid.at(2)))continue;
        auto malformed=valid;malformed[2]=char(body);CHECK(!decodeCanGatewayFrame(malformed,a));
    }
}
static void testCorruptionAndUnwrap()
{
    const auto valid=canPacket(64,true,0xFFFFFF00U);
    CanGatewayFrame sentinel;sentinel.canId=0x778;sentinel.hasTimestamp=true;sentinel.timestampUs=42;
    for(int i=0;i<valid.size();++i)for(int bit=0;bit<8;++bit){
        auto bad=valid;bad[i]=char(quint8(bad[i])^(1U<<bit));auto decoded=sentinel;
        CHECK(!decodeCanGatewayFrame(bad,decoded));
        CHECK(decoded.canId==sentinel.canId && decoded.timestampUs==42);
        CanGatewayDecoder d;auto frames=d.feed(bad+valid);
        // Corrupted length may need more input to finish a false candidate, so append another frame.
        if(frames.isEmpty())frames+=d.feed(valid);
        CHECK(!frames.isEmpty());
        if(!frames.isEmpty())CHECK(frames.last().hasTimestamp && frames.last().data.size()==64);
    }
    CanGatewayTimestampUnwrapper t;
    CHECK(t.extend(0xFFFFFF00U)==4294967040LL);
    CHECK(t.extend(1000)==4294968296LL);
    CHECK(t.extend(1000)==4294968296LL);
    CHECK(t.extend(900)==4294968196LL); // Priority-reordered sample, no extra epoch.
    CHECK(t.extend(0xFFFFFF80U)==4294967168LL); // Late previous-epoch sample.
    CHECK(t.extend(2000)==4294969296LL);
    CHECK(t.extend(2000U+0x80000000U)==-1); // Half-cycle ambiguity does not mutate clock.
    CHECK(t.extend(2100)==4294969396LL);
    t.reset();CHECK(t.extend(0)==0);CHECK(t.extend(0xFFFFFFFEU)==-1);CHECK(t.extend(1000)==1000);
    t.reset();CHECK(t.extend(0)==0);CHECK(t.extend(0x70000000)==1879048192LL);
    CHECK(t.extend(0xE0000000)==3758096384LL);CHECK(t.extend(0x50000000)==5637144576LL);
    CHECK(t.extend(0xC0000000)==7516192768LL);CHECK(t.extend(0x30000000)==9395240960LL);
}
static void testConfiguration()
{
    for(bool modern:{false,true})for(int status=0;status<3;++status){
        const auto packet=configPacket(modern,0xFFFFFFFFU,42,status);CanGatewayConfigResponse f;
        CHECK(isCanGatewayConfigResponsePacket(packet));CHECK(decodeCanGatewayConfigResponse(packet,f));
        CHECK(f.sequence==42 && int(f.status)==status && f.hasTimestamp==modern);
        CHECK(f.timestampUs==(modern?0xFFFFFFFFU:0U));CHECK(encodeCanGatewayConfigResponse(f)==packet);
        CanGatewayFrame notCan; CHECK(!decodeCanGatewayFrame(packet,notCan));
        for(int i=0;i<packet.size();++i){auto bad=packet;bad[i]=char(quint8(bad[i])^1);
            auto sentinel=f; CHECK(!decodeCanGatewayConfigResponse(bad,sentinel));CHECK(sentinel.sequence==42);}
    }
    CanGatewayConfigResponse f;CHECK(decodeCanGatewayConfigResponse(configPacket(true,42),f));
    CHECK(decodeCanGatewayConfigResponse(configPacket(false),f));CHECK(!f.hasTimestamp && f.timestampExtendedUs==-1 && f.timestampUs==0);
    auto bad=configPacket(true);bad[11]=char(99);bad[24]=char(referenceCrc(bad.mid(2,22)));
    CHECK(!decodeCanGatewayConfigResponse(bad,f));
    CanGatewayConfigRequest r;r.sequence=2;r.nominalBitrate=500000;r.dataBitrate=5000000;
    CHECK(encodeCanGatewayConfigRequest(r).toHex()=="aa5510020000000000800120a10700404b4c000255aa");
    // Metadata output does not break existing four-argument UI completion signal.
    for(bool modern:{false,true})for(int cut=0;cut<=27;++cut){
        BootloaderCommunicationService service;int typed=0,legacy=0,can=0;
        CanGatewayConfigResponse seen;
        service.m_canBitratePending=true;service.m_pendingCanBitrate.sequence=2;
        QObject::connect(&service,&BootloaderCommunicationService::canBitrateResponseReceived,[&](const CanGatewayConfigResponse &v){seen=v;++typed;});
        QObject::connect(&service,&BootloaderCommunicationService::canBitrateConfigured,[&](quint16,quint8,quint32 n,quint32 d){++legacy;CHECK(n==500000 && d==5000000);});
        QObject::connect(&service,&BootloaderCommunicationService::frameReceived,[&](const CanGatewayFrame &){++can;});
        const auto wire=configPacket(modern,1000);inject(service,wire.left(cut));inject(service,wire.mid(cut));
        CHECK(typed==1 && legacy==1 && can==0 && !service.m_canBitratePending);
        CHECK(seen.hasTimestamp==modern && seen.timestampExtendedUs==(modern?1000:-1));
    }
    BootloaderCommunicationService service;int done=0;service.m_canBitratePending=true;service.m_pendingCanBitrate.sequence=2;
    QObject::connect(&service,&BootloaderCommunicationService::canBitrateResponseReceived,[&](const CanGatewayConfigResponse &){++done;});
    auto badCrc=configPacket(true,0xFFFFFF00U);badCrc[24]=char(quint8(badCrc[24])^1);
    inject(service,badCrc);CHECK(service.m_canBitratePending && done==0);
    inject(service,configPacket(true,1000));CHECK(!service.m_canBitratePending && done==1);
    CHECK(service.m_gatewayTimestamp.extend(2000)==2000); // Corrupt response never moved the epoch.
}
static void testSharedRouter()
{
    BootloaderCommunicationService service;QVector<CanGatewayFrame> frames;int imu=0,depth=0,hb=0;
    QObject::connect(&service,&BootloaderCommunicationService::frameReceived,[&](const CanGatewayFrame &f){frames.append(f);});
    QObject::connect(&service,&BootloaderCommunicationService::sensorFrameReceived,[&](const SensorFrame &f){if(f.target==1)++imu;else ++depth;});
    QObject::connect(&service,&BootloaderCommunicationService::heartbeatReceived,[&](const SystemHeartbeat &){++hb;});
    SensorFrame sf;sf.target=1;sf.flags=8;sf.command=0x80;sf.payload=QByteArray(40,char(0));
    const auto iw=encodeSensorFrame(sf);sf.target=2;sf.command=0x82;sf.payload=QByteArray(32,char(0));const auto dw=encodeSensorFrame(sf);
    CanFlowFrame flow;flow.command=CanFlowCommand::DataBlock;flow.payload=canPacket(8,true);const auto fw=encodeCanFlowFrame(flow);
    const auto block=canPacket(64,true,1000)+canPacket(12,false)+iw+dw+heartbeat()+fw;
    std::mt19937 random(20261007);QByteArray stream;
    for(int i=0;i<2000;++i)stream+=block;
    for(int at=0;at<stream.size();){const int n=1+int(random()%113);inject(service,stream.mid(at,n));at+=n;}
    CHECK(frames.size()==4000 && imu==2000 && depth==2000 && hb==2000);
    for(int i=0;i<frames.size();i+=2){CHECK(frames[i].data.size()==64 && frames[i].timestampExtendedUs==1000);CHECK(!frames[i+1].hasTimestamp);}
    // Valid nested sensor bytes are owned by DATA and must never escape.
    QByteArray body;le16(body,2);le32(body,0x123);body.append(char(2));body.append(char(iw.size()));body+=iw;le32(body,2000);
    inject(service,seal(body));CHECK(frames.size()==4001 && imu==2000);
    auto rejected=seal(body);rejected[rejected.size()-3]=char(quint8(rejected[rejected.size()-3])^1);
    inject(service,rejected+canPacket(8,true,3000));
    CHECK(imu==2001 && frames.last().timestampUs==3000); // Bad outer candidate is rescanned, not consumed wholesale.
    service.close(); // close on an unopened fake transport is not an actual connection event.
    auto *transport=service.findChild<SerialTransport *>();CHECK(transport);
    if(transport)CHECK(QMetaObject::invokeMethod(transport,"closed",Qt::DirectConnection));
    inject(service,canPacket(8,true,0));CHECK(frames.last().timestampExtendedUs==0);
    // Shared epoch for data and matched bitrate reply, including reordering across wrap.
    inject(service,canPacket(8,true,0x70000000U));inject(service,canPacket(8,true,0xE0000000U));
    inject(service,canPacket(8,true,0xFFFFFF00U));
    service.m_canBitratePending=true;service.m_pendingCanBitrate.sequence=2;
    qint64 cfg=-1;QObject::connect(&service,&BootloaderCommunicationService::canBitrateResponseReceived,[&](const CanGatewayConfigResponse &f){cfg=f.timestampExtendedUs;});
    inject(service,configPacket(true,1000));CHECK(cfg==4294968296LL);
    inject(service,canPacket(8,true,0xFFFFFF80U));CHECK(frames.last().timestampExtendedUs==4294967168LL);
    inject(service,canPacket(8,true,2000));CHECK(frames.last().timestampExtendedUs==4294969296LL);
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    testDocumentVectors();testLengthsAndSplits();testCorruptionAndUnwrap();testConfiguration();testSharedRouter();
    std::cout<<"AA55 timestamp protocol/service: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
