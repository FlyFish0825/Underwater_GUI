// Explicit, opt-in COM11 hardware smoke test. No CAN/motor commands, no calibration,
// no model selection, no zero capture and no flash writes. RAM tests restore originals.
#include "communication/service/BootloaderCommunicationService.h"
#include "communication/protocol/SensorProtocol.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDateTime>
#include <iostream>
#include <functional>

using namespace rov;
static void waitMs(int ms) {
    QElapsedTimer t; t.start();
    while(t.elapsed()<ms) { QCoreApplication::processEvents(); QThread::msleep(2); }
}
struct Probe {
    BootloaderCommunicationService communication;
    QVector<SensorFrame> replies;
    QJsonArray trace, checks;
    quint32 next=0x61060000U;
    int heartbeats=0, depthFrames=0, imuFrames=0, failures=0;
    SensorFrame lastDepth;
    QByteArray prefix;
    Probe() {
        QObject::connect(&communication,&BootloaderCommunicationService::sensorFrameReceived,
                         [this](const SensorFrame &f){
            if(f.flags&2U) replies.append(f);
            if(f.flags==8U && f.command==0x82) { ++depthFrames; lastDepth=f; }
            if(f.flags==8U && (f.command==0x80 || f.command==0x81)) ++imuFrames;
            if(trace.size()<500) trace.append(QJsonObject{{"direction","rx"},{"packet",QString(encodeSensorFrame(f).toHex(' '))}});
        });
        QObject::connect(&communication,&BootloaderCommunicationService::heartbeatReceived,
                         [this](const SystemHeartbeat &){++heartbeats;});
        QObject::connect(&communication,&BootloaderCommunicationService::rawBytesReceived,
                         [this](const QByteArray &b){if(prefix.size()<1024)prefix.append(b.left(1024-prefix.size()));});
        QObject::connect(&communication,&BootloaderCommunicationService::errorOccurred,
                         [this](const QString &s){ trace.append(QJsonObject{{"transport_error",s}}); });
    }
    void check(bool ok,const QString &name) {
        checks.append(QJsonObject{{"check",name},{"pass",ok}});
        std::cout<<(ok?"PASS ":"FAIL ")<<name.toStdString()<<"\n";
        if(!ok)++failures;
    }
    SensorFrame request(quint8 target,quint8 command,const QByteArray &payload={}) {
        SensorFrame f; f.target=target; f.command=command; f.sequence=++next; f.payload=payload;
        trace.append(QJsonObject{{"direction","tx"},{"packet",QString(encodeSensorFrame(f).toHex(' '))}});
        if(!communication.sendSensorFrame(f))return {};
        QElapsedTimer t; t.start();
        while(t.elapsed()<1800) {
            QCoreApplication::processEvents();
            for(int i=0;i<replies.size();++i)if(replies[i].sequence==f.sequence && replies[i].target==target && replies[i].command==command+0x40U)return replies.takeAt(i);
            QThread::msleep(2);
        }
        return {};
    }
    int result(const SensorFrame &f)const {return f.payload.isEmpty()?-1:quint8(f.payload[0]);}
    SensorFrame parameter(quint8 target,quint16 id) {
        QByteArray p;SensorWire::append16(p,id);return request(target,3,p);
    }
    bool roundTrip(quint16 id,const QVariant &v) {
        const SensorFrame original=parameter(2,id);
        if(result(original)!=0 || original.payload.size()<6) {check(false,QString("read original %1").arg(id,4,16,QChar('0')));return false;}
        const QByteArray saved=original.payload.mid(1);
        SensorRequest r; r.target=2; r.operation=SensorOperation::SetParameter; r.parameterId=id; r.value=v;
        QByteArray payload;QString error;
        if(!makeSensorRequestPayload(r,payload,error)){check(false,error);return false;}
        const SensorFrame changed=request(2,4,payload);
        const SensorFrame readBack=parameter(2,id);
        const bool ok=result(changed)==0 && changed.payload.mid(1)==payload && result(readBack)==0 && readBack.payload.mid(1)==payload;
        check(ok,QString("RAM write/readback %1").arg(id,4,16,QChar('0')));
        const SensorFrame restored=request(2,4,saved);
        const SensorFrame verify=parameter(2,id);
        const bool restoredOk=result(restored)==0 && result(verify)==0 && verify.payload.mid(1)==saved;
        check(restoredOk,QString("restore original %1").arg(id,4,16,QChar('0')));
        return ok&&restoredOk;
    }
    void run(bool ram) {
        const auto imuInfo=request(1,1), depthInfo=request(2,1);
        check(result(imuInfo)==0 && imuInfo.payload.size()==31,"IMU GET_INFO via AA5B");
        check(result(depthInfo)==0 && depthInfo.payload.size()==31,"depth GET_INFO via AA5B");
        if(result(imuInfo)<0 || result(depthInfo)<0)return;
        const auto imuStatus=request(1,2),depthStatus=request(2,2);
        check(result(imuStatus)==0 && imuStatus.payload.size()==21,"IMU GET_STATUS schema");
        check(result(depthStatus)==0 && depthStatus.payload.size()==21,"depth GET_STATUS schema");
        if(imuStatus.payload.size()==21) {
            const quint32 flags=SensorWire::read32(imuStatus.payload,1);
            check((flags&(1U<<9))!=0,"IMU configuration TX guard reported");
            if(flags&(1U<<9)) {
                QByteArray p;SensorWire::append16(p,1);p.append(char(4));p.append(char(2));SensorWire::append16(p,25);
                check(result(request(1,4,p))==9,"blocked IMU SET_PARAMETER rejected without UART TX");
            }
        }
        const auto model=parameter(2,0x0105);
        check(result(model)==0 && model.payload.size()==6,"model parameter readable");
        const bool unknown=model.payload.size()==6 && quint8(model.payload[5])==0;
        check(unknown,"unconfirmed physical model stays UNKNOWN");
        check(result(request(2,5))==1,"SAVE_CONFIG explicitly unsupported");
        if(ram) {
            roundTrip(0x0001,10); roundTrip(0x0101,1024);
            roundTrip(0x0102,1000.0); roundTrip(0x0104,0.35);
        }
        waitMs(2200);
        check(heartbeats>0,"AA58 heartbeat coexists with AA5B");
        check(imuFrames>0,"real IMU telemetry received through H750 RX DMA and AA5B");
        check(depthFrames>0,"real I2C depth raw ADC telemetry received");
        if(lastDepth.payload.size()==32) {
            const quint32 flags=SensorWire::read32(lastDepth.payload,0);
            const quint32 d1=SensorWire::read32(lastDepth.payload,24),d2=SensorWire::read32(lastDepth.payload,28);
            check((flags&(1U<<8))!=0,"physical PROM CRC valid");
            check(d1>0 && d1<0xFFFFFF && d2>0 && d2<0xFFFFFF,"physical D1/D2 plausible 24-bit values");
            if(unknown)check((flags&((1U<<4)|(1U<<5)|(1U<<6)))==0,"unknown model never advertises pressure/temperature/depth valid");
        }
        check(result(request(2,8))==0,"STOP_STREAM reply");
        waitMs(100); const int paused=depthFrames;
        const auto before=request(2,2);waitMs(600);const auto after=request(2,2);
        check(depthFrames==paused,"STOP_STREAM stops forwarding");
        if(before.payload.size()==21 && after.payload.size()==21)
            check(SensorWire::read32(after.payload,5)>SensorWire::read32(before.payload,5),"depth acquisition continues while forwarding stopped");
        check(result(request(2,7))==0,"START_STREAM reply");waitMs(300);
        check(depthFrames>paused,"forwarding resumes with actual samples");
        SensorFrame bad; bad.target=2;bad.command=1;bad.sequence=++next;
        QByteArray corrupt=encodeSensorFrame(bad);corrupt[16]=char(quint8(corrupt[16])^1U);
        auto *transport=communication.findChild<SerialTransport *>();
        check(transport && transport->writeBytes(corrupt),"send corrupted read-only query for CRC rejection test");waitMs(200);
        bool gotBad=false;for(const auto &f:replies)if(f.sequence==bad.sequence)gotBad=true;
        check(!gotBad,"bad CRC causes no command execution/reply");
        check(result(request(2,1))==0,"valid query recovers after bad CRC");
    }
    QJsonObject report()const {
        QJsonObject data{{"utc",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {"checks",checks},{"failures",failures},{"heartbeats",heartbeats},{"depth_frames",depthFrames},
            {"imu_frames",imuFrames},{"rx_prefix_hex",QString(prefix.toHex(' '))},{"trace",trace},
            {"hardware_limitations","IMU pins blocked; depth model/zero not selected. This does not prove IMU measurements or calibrated depth accuracy."}};
        if(lastDepth.payload.size()==32)data.insert("last_depth",QJsonObject{{"status",double(SensorWire::read32(lastDepth.payload,0))},
            {"d1",double(SensorWire::read32(lastDepth.payload,24))},{"d2",double(SensorWire::read32(lastDepth.payload,28))}});
        return data;
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);const QStringList args=app.arguments();
    const int portIndex=args.indexOf("--port"),outIndex=args.indexOf("--output");
    if(portIndex<0 || portIndex+1>=args.size() || outIndex<0 || outIndex+1>=args.size()) {
        std::cerr<<"Usage: rov_sensor_hardware_probe --port COM11 --output report.json [--exercise-ram] [--capture-only]\n";return 2;
    }
    const QString port=args[portIndex+1];Probe p;SerialDeviceInfo device;bool found=false;
    for(const auto &d:p.communication.enumerateDevices())if(d.portName.compare(port,Qt::CaseInsensitive)==0){device=d;found=true;break;}
    p.check(found,"explicit STM32 USB CDC port found");
    if(found && p.communication.open(device)) {
        if(args.contains("--capture-only"))waitMs(2500);else p.run(args.contains("--exercise-ram"));
        p.communication.close();
    }else p.check(false,"open port exclusively");
    QFile out(args[outIndex+1]);if(!out.open(QIODevice::WriteOnly)){std::cerr<<"Cannot write report\n";return 3;}
    out.write(QJsonDocument(p.report()).toJson());out.close();
    std::cout<<"Hardware checks: "<<p.checks.size()<<", failures: "<<p.failures<<"\n";
    return p.failures?1:0;
}
