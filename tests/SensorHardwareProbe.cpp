// Explicit, opt-in COM11 hardware smoke test. No CAN/motor commands, no calibration,
// no model selection, no zero capture and no flash writes. RAM tests restore originals.
#include "communication/service/BootloaderCommunicationService.h"
#include "communication/protocol/SensorProtocol.h"
#include "data/services/SensorDataService.h"
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
#include <cmath>

using namespace rov;
static void waitMs(int ms) {
    QElapsedTimer t; t.start();
    while(t.elapsed()<ms) { QCoreApplication::processEvents(); QThread::msleep(2); }
}
struct Probe {
    BootloaderCommunicationService communication;
    QVector<SensorFrame> replies;
    QJsonArray trace, checks;
    QJsonObject depthReadOnly, imuReport;
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
            if(trace.size()<500 || (f.flags&2U)) trace.append(QJsonObject{{"direction","rx"},{"packet",QString(encodeSensorFrame(f).toHex(' '))}});
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
    // New MS5837 acceptance mode: uses the production service, sends GETs only,
    // and does not assume that an unrelated IMU or a physical water zero is available.
    void runDepthReadOnly() {
        SensorDataService service;
        QObject::connect(&communication, &BootloaderCommunicationService::sensorFrameReceived,
                         &service, &SensorDataService::handleFrame);
        service.setSender([this](const SensorFrame &f) {
            trace.append(QJsonObject{{"direction","tx"},{"packet",QString(encodeSensorFrame(f).toHex(' '))}});
            return communication.sendSensorFrame(f);
        });
        bool complete=false;
        SensorResult outcome=SensorResult::Timeout;
        QObject::connect(&service,&SensorDataService::commandFinished,&service,
                         [&](quint8,quint32,SensorResult r,const QString &){complete=true;outcome=r;});
        service.setConnected(true);
        const auto query=[&](SensorOperation operation,quint16 id=0) {
            complete=false; outcome=SensorResult::Timeout;
            SensorRequest r; r.target=kDepthSensor; r.operation=operation; r.parameterId=id;
            if(!service.request(r))return SensorResult::IoError;
            QElapsedTimer timer;timer.start();
            while(!complete&&timer.elapsed()<4500)waitMs(5);
            return outcome;
        };
        const int initialFrames=depthFrames;
        check(query(SensorOperation::GetInfo)==SensorResult::Ok,"depth GET_INFO through production SensorDataService");
        check(service.snapshot().devices[1].infoKnown,"depth descriptor accepted");
        check(query(SensorOperation::GetStatus)==SensorResult::Ok,"depth GET_STATUS through production service");
        for(quint16 id:{quint16(0x0001),quint16(0x0101),quint16(0x0102),quint16(0x0103),quint16(0x0104),quint16(0x0105)}) {
            const auto result=query(SensorOperation::GetParameter,id);
            check(result==SensorResult::Ok||(id==0x0103&&result==SensorResult::NotReady),
                  QString("depth GET_PARAMETER %1 (unset zero may be NOT_READY)").arg(id,4,16,QChar('0')));
        }
        waitMs(2200);
        const auto s=service.snapshot();const auto &d=s.devices[1];
        check(d.online&&d.statusKnown,"depth online with parsed acquisition statistics");
        check(depthFrames>initialFrames&&s.depthAgeMs>=0,"live 0x82 enters the production snapshot");
        check(s.depthRawValid&&s.rawAdcD1>0&&s.rawAdcD1<0xFFFFFF&&s.rawAdcD2>0&&s.rawAdcD2<0xFFFFFF,
              "fresh, valid depth D1/D2 ADC measurements");
        check(heartbeats>0,"AA58 heartbeat and depth AA5B coexist");
        const bool confirmed=(d.status&SensorStatus::ModelConfirmed)&&!(d.status&SensorStatus::ConfigUnknown);
        if(confirmed)check(s.pressureValid&&s.temperatureValid,"confirmed model produces valid pressure and temperature");
        else check(!s.pressureValid&&!s.temperatureValid&&!s.depthValid,"unknown model is never shown as physical zero");
        if(!(d.status&SensorStatus::ZeroValid))check(!s.depthValid,"unset water zero remains unavailable, not 0 m");
        check(s.depthAgeMs>=0&&s.depthAgeMs<=2500,"sample freshness uses its device production timestamp");
        depthReadOnly=QJsonObject{{"mode","depth-read-only"},{"model",int(d.model)},
            {"status",double(d.status)},{"sample_sequence",double(d.sampleSequence)},
            {"stream_sequence",double(d.sequence)},{"device_sample_age_ms",double(d.sampleAgeMs)},
            {"good_frames",double(d.goodFrames)},{"errors",double(d.errors)},
            {"pressure_valid",s.pressureValid},{"temperature_valid",s.temperatureValid},
            {"depth_valid",s.depthValid},{"zero_valid",s.zeroValid},{"raw_valid",s.depthRawValid},
            {"pressure_pa",s.pressurePa},{"temperature_c",s.temperatureC},
            {"depth_raw_m",s.depthRawM},{"depth_filtered_m",s.depthFilteredM},
            {"surface_pressure_pa",s.surfacePressurePa},{"d1",double(s.rawAdcD1)},{"d2",double(s.rawAdcD2)},
            {"sample_timestamp_us",double(s.depthTimestampUs)},{"sample_age_ms",double(s.depthAgeMs)},
            {"limitations","GET_INFO/GET_STATUS/GET_PARAMETER only. No model change, zero, stream switch, IMU command, CAN command or firmware write. Physical water-depth accuracy is not validated."}};
    }

    // Explicit opt-in: exercise the production paired request and restore the saved RAM pair.
    // Never change model, zero, density, filter, streaming, IMU or CAN configuration.
    void runDepthSampling() {
        depthReadOnly = QJsonObject{{"mode","depth-sampling-test"},
            {"limitations","Explicit OSR/rate RAM changes only, originals restored and checked. No model/zero/IMU/CAN/flash changes. Not an accuracy calibration."}};
        const auto modelFrame = parameter(2,0x0105);
        const auto osrFrame = parameter(2,0x0101), rateFrame = parameter(2,0x0001), zeroBefore = parameter(2,0x0103);
        quint16 id=0; QVariant model, originalOsr, originalRate;
        const bool saved = result(modelFrame)==0 && decodeSensorParameter(modelFrame.payload.mid(1),id,model) && id==0x0105
            && result(osrFrame)==0 && decodeSensorParameter(osrFrame.payload.mid(1),id,originalOsr) && id==0x0101
            && result(rateFrame)==0 && decodeSensorParameter(rateFrame.payload.mid(1),id,originalRate) && id==0x0001;
        check(saved && model.toUInt()==2, "save original 02BA OSR/rate before any writes");
        if(!saved || model.toUInt()!=2)return;
        depthReadOnly.insert("original_osr",originalOsr.toInt());
        depthReadOnly.insert("original_rate_hz",originalRate.toInt());
        SensorDataService service;
        QObject::connect(&communication,&BootloaderCommunicationService::sensorFrameReceived,
                         &service,&SensorDataService::handleFrame);
        service.setConnected(true);
        service.setSender([this](const SensorFrame &f) {
            trace.append(QJsonObject{{"direction","tx"},{"packet",QString(encodeSensorFrame(f).toHex(' '))}});
            return communication.sendSensorFrame(f);
        });
        const auto apply = [&](int osr,int rate,const QString &label) {
            SensorRequest r; r.target=kDepthSensor; r.operation=SensorOperation::SetParameter;
            r.parameterId=0x0001; r.value=rate; r.samplingOsr=quint16(osr); r.samplingRateHz=quint16(rate);
            const bool accepted=service.request(r);
            QElapsedTimer timer;timer.start();
            while(accepted && service.snapshot().devices[1].pending && timer.elapsed()<12000)waitMs(5);
            const auto state=service.snapshot();
            const bool ok=accepted && !state.devices[1].pending
                && state.devices[1].lastCommand.contains(QStringLiteral("组合已回读确认"));
            check(ok,label);
            depthReadOnly.insert(label,QJsonObject{{"pass",ok},{"osr",osr},{"rate_hz",rate},
                {"message",state.devices[1].lastCommand},{"last_tx",state.devices[1].lastRequestHex},
                {"last_rx",state.devices[1].lastReplyHex}});
            return ok;
        };
        const bool high=apply(512,100,"paired speed-up: OSR 512 / 100 Hz");
        if(high) { waitMs(250); apply(8192,25,"paired high-OSR: OSR 8192 / 25 Hz"); }
        // Restoration is attempted even if an exercise step failed. Never hide restoration failure.
        apply(originalOsr.toInt(),originalRate.toInt(),"restore original OSR/rate pair");
        const auto osrAfter=parameter(2,0x0101),rateAfter=parameter(2,0x0001),zeroAfter=parameter(2,0x0103);
        check(result(osrAfter)==0 && result(rateAfter)==0 && osrAfter.payload==osrFrame.payload && rateAfter.payload==rateFrame.payload,
              "independent GETs confirm the original pair was restored");
        check((result(zeroBefore)==0 || result(zeroBefore)==8) && zeroAfter.payload==zeroBefore.payload,
              "water zero configuration unchanged");
        depthReadOnly.insert("restored",result(osrAfter)==0 && result(rateAfter)==0 && osrAfter.payload==osrFrame.payload && rateAfter.payload==rateFrame.payload);
    }


    // Only explicit --imu-rate-test may send SET 0001, and it restores a verified baseline.
    // Default --imu-read-only does not change IMU settings, depth zero, or forwarding.
    void runImu(bool exercise) {
        SensorDataService service; // Depth startup policy is OFF in this tool.
        QHash<quint16,SensorParameterFeedback> parameters;
        SensorResult lastResult=SensorResult::Timeout;
        QObject::connect(&communication,&BootloaderCommunicationService::sensorFrameReceived,
                         &service,&SensorDataService::handleFrame);
        QObject::connect(&service,&SensorDataService::parameterReceived,&service,
                         [&](const SensorParameterFeedback &f){if(f.target==1)parameters.insert(f.parameterId,f);});
        QObject::connect(&service,&SensorDataService::commandFinished,&service,
                         [&](quint8 t,quint32,SensorResult r,const QString &){if(t==1)lastResult=r;});
        service.setSender([&](const SensorFrame &f) {
            const bool safeRead=f.command>=1 && f.command<=3;
            const bool rateWrite=exercise && f.target==1 && f.command==4 && f.payload.size()==6
                && SensorWire::read16(f.payload,0)==1;
            if(!safeRead && !rateWrite)return false;
            trace.append(QJsonObject{{"direction","tx"},{"packet",QString(encodeSensorFrame(f).toHex(' '))}});
            return communication.sendSensorFrame(f);
        });
        service.setConnected(true);
        SensorRequest info;info.target=1;info.operation=SensorOperation::GetInfo;
        const bool submitted=service.request(info);
        QElapsedTimer timer;timer.start();
        while(submitted && timer.elapsed()<5000 && parameters.size()<2)waitMs(5);
        auto snapshot=service.snapshot();
        check(submitted && snapshot.devices[0].infoKnown,"IMU GET_INFO accepted by production service");
        if(!snapshot.devices[0].infoKnown)return;
        check(snapshot.devices[0].model==0,"IMU identity remains unknown, never inferred from axes");
        check(snapshot.devices[0].statusKnown,"IMU automatic GET_STATUS decoded");
        check(parameters.size()==2,"IMU two cached parameters automatically queried");
        for(quint16 id:{quint16(1),quint16(3)}) {
            const auto f=parameters.value(id);
            check(parameters.contains(id) && !f.confirmed
                  && (f.value.isValid() || f.message.contains("NOT_READY")),
                  QString("IMU parameter %1 is cache/unset, not verified device readback").arg(id));
        }
        SensorRequest depth;depth.target=2;depth.operation=SensorOperation::GetStatus;service.request(depth);
        waitMs(2400);snapshot=service.snapshot();
        check(snapshot.rawValid,"fresh IMU raw acceleration/gyro/magnetic group");
        check(snapshot.quaternionValid && snapshot.eulerValid,"fresh independent quaternion and Euler fields");
        check(snapshot.imuRawRateHz>0 && snapshot.imuAttitudeRateHz>0,"separate observed raw and combined attitude rates");
        check(snapshot.rawAgeMs>=0 && snapshot.rawAgeMs<=500,"IMU source timestamp plus delivery freshness");
        check(snapshot.devices[1].online && depthFrames>0,"depth TARGET=2 coexists without any IMU-derived depth");
        check(heartbeats>0,"AA58 heartbeat coexists with IMU AA5B");
        const auto numbers=[](const auto &values){QJsonArray a;for(double v:values)a.append(v);return a;};
        imuReport=QJsonObject{{"mode",exercise?"imu-rate-test":"imu-read-only"},
            {"firmware",snapshot.devices[0].firmware},{"model",int(snapshot.devices[0].model)},
            {"status",double(snapshot.devices[0].status)},{"raw_valid",snapshot.rawValid},
            {"quaternion_valid",snapshot.quaternionValid},{"euler_valid",snapshot.eulerValid},
            {"raw_rate_hz",snapshot.imuRawRateHz},{"combined_attitude_rate_hz",snapshot.imuAttitudeRateHz},
            {"raw_age_ms",double(snapshot.rawAgeMs)},{"raw_timestamp_us",double(snapshot.rawTimestampUs)},
            {"attitude_timestamp_us",double(snapshot.attitudeTimestampUs)},
            {"sample_sequence",double(snapshot.devices[0].sampleSequence)},
            {"errors",double(snapshot.devices[0].errors)},
            {"accel_g",numbers(snapshot.accelG)},{"gyro_rad_s",numbers(snapshot.gyroRadS)},
            {"mag_protocol_units",numbers(snapshot.magProtocolUnits)},
            {"quaternion_wxyz",numbers(snapshot.quaternionWxyz)},{"euler_degrees",numbers(snapshot.eulerDeg)},
            {"limitations","No hardware calibration/clear/reset/save/IMU mode or depth configuration writes. No sensor accuracy calibration. Parameters have no native ACK."}};
        for(quint16 id:{quint16(1),quint16(3)})
            imuReport.insert(QString("parameter_%1").arg(id),QJsonObject{{"value",QJsonValue::fromVariant(parameters[id].value)},
                {"confirmed",parameters[id].confirmed},{"message",parameters[id].message}});
        if(!exercise)return;
        const int original=parameters[1].value.toInt();
        const bool canRestore=parameters[1].value.isValid() && original>=10 && original<=100
            && snapshot.rawValid && std::abs(snapshot.imuRawRateHz-original)<=qMax(1.0,original*0.1)
            && !(snapshot.devices[0].status&SensorStatus::PinBlocked);
        if(!canRestore) {
            imuReport.insert("rate_test_skipped","PIN_BLOCKED or no cached rate matching observation; no write was attempted because baseline cannot be safely restored.");
            return;
        }
        const auto setRate=[&](int hz,const QString &key) {
            SensorRequest request;request.target=1;request.operation=SensorOperation::SetParameter;request.parameterId=1;request.value=hz;
            const bool sent=service.request(request);QElapsedTimer deadline;deadline.start();
            while(sent && deadline.elapsed()<5000) {
                waitMs(5);const auto s=service.snapshot();
                if(!s.devices[0].pending && s.imuRateCheck!=ImuRateCheck::Observing && s.imuRateCheck!=ImuRateCheck::NotRequested)break;
            }
            const auto s=service.snapshot();
            const bool ok=sent && lastResult==SensorResult::Unconfirmed && s.imuRateCheck==ImuRateCheck::Matches;
            check(ok,key);
            imuReport.insert(key,QJsonObject{{"requested_hz",hz},{"observed_hz",s.imuRateObservedHz},
                {"passed",ok},{"message",s.imuRateMessage},{"tx",s.devices[0].lastRequestHex},{"rx",s.devices[0].lastReplyHex}});
            return ok;
        };
        setRate(original==50?25:50,"set_and_observe");
        const bool restored=setRate(original,"restore_and_observe");
        imuReport.insert("restored",restored);imuReport.insert("original_hz",original);
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
        if(!depthReadOnly.isEmpty()) {
            data.insert("depth_read_only",depthReadOnly);
            data.insert("hardware_limitations",depthReadOnly.value("limitations"));
        }
        if(!imuReport.isEmpty()) {
            data.insert("imu",imuReport);data.insert("hardware_limitations",imuReport.value("limitations"));
        }
        return data;
    }
};
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);const QStringList args=app.arguments();
    const int portIndex=args.indexOf("--port"),outIndex=args.indexOf("--output");
    if(portIndex<0 || portIndex+1>=args.size() || outIndex<0 || outIndex+1>=args.size()) {
        std::cerr<<"Usage: rov_sensor_hardware_probe --port COM11 --output report.json [--exercise-ram] [--capture-only] [--depth-read-only] [--depth-sampling-test] [--imu-read-only] [--imu-rate-test]\n";return 2;
    }
    const QString port=args[portIndex+1];Probe p;SerialDeviceInfo device;bool found=false;
    for(const auto &d:p.communication.enumerateDevices())if(d.portName.compare(port,Qt::CaseInsensitive)==0){device=d;found=true;break;}
    p.check(found,"explicit STM32 USB CDC port found");
    if(found && p.communication.open(device)) {
        if(args.contains("--imu-read-only") || args.contains("--imu-rate-test"))p.runImu(args.contains("--imu-rate-test"));
        else if(args.contains("--depth-sampling-test"))p.runDepthSampling();
        else if(args.contains("--depth-read-only"))p.runDepthReadOnly();
        else if(args.contains("--capture-only"))waitMs(2500);else p.run(args.contains("--exercise-ram"));
        p.communication.close();
    }else p.check(false,"open port exclusively");
    QFile out(args[outIndex+1]);if(!out.open(QIODevice::WriteOnly)){std::cerr<<"Cannot write report\n";return 3;}
    out.write(QJsonDocument(p.report()).toJson());out.close();
    std::cout<<"Hardware checks: "<<p.checks.size()<<", failures: "<<p.failures<<"\n";
    return p.failures?1:0;
}
