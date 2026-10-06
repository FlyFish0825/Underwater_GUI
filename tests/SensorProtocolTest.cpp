#include "communication/protocol/SensorProtocol.h"
#include "communication/service/BootloaderCommunicationService.h"
#include "data/services/SensorDataService.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <cmath>
#include <iostream>
#include <limits>

using namespace rov;
static int failures = 0;
static int checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; std::cerr << __LINE__ << ": " #expr "\n"; } } while (0)
static void waitMs(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
static SensorFrame replyFor(const SensorFrame &r, SensorResult result, const QByteArray &body = {})
{
    SensorFrame f = r;
    f.command = quint8(r.command + 0x40);
    f.flags = result == SensorResult::Ok || result == SensorResult::Unconfirmed ? 2 : 6;
    f.payload = QByteArray(1, char(result)) + body;
    return f;
}
static SensorFrame rawFrame(quint32 sequence = 1)
{
    SensorFrame f; f.command = 0x80; f.target = 1; f.flags = 8; f.sequence = sequence; f.timestampUs = 5000;
    SensorWire::append32(f.payload, SensorStatus::Online | SensorStatus::RawValid | SensorStatus::QuaternionValid | SensorStatus::EulerValid);
    for (int i = 0; i < 9; ++i) SensorWire::appendFloat(f.payload, float(i - 4) / 10.0f);
    return f;
}
static SensorFrame depthFrame(quint32 flags)
{
    SensorFrame f; f.command = 0x82; f.target = 2; f.flags = 8; f.sequence = 1; f.timestampUs = 0xFFFFFF00U;
    SensorWire::append32(f.payload, flags);
    for (float v : {121325.0f, 23.45f, 2.0394f, 2.0f, 101325.0f}) SensorWire::appendFloat(f.payload, v);
    SensorWire::append32(f.payload, 0xF01234); SensorWire::append32(f.payload, 0xABCDEF);
    return f;
}
static void testWire()
{
    CHECK(SensorWire::crc16("123456789") == 0x29B1U);
    SensorFrame request; request.command = 1; request.sequence = 0x12345678U;
    const QByteArray golden = QByteArray::fromHex("aa5b010101017856341200000000000052d65baa");
    CHECK(encodeSensorFrame(request) == golden);
    SensorFrame parsed;
    CHECK(decodeSensorFrame(golden, parsed));
    CHECK(parsed.sequence == 0x12345678U && parsed.target == 1 && parsed.payload.isEmpty());
    const QByteArray wire = encodeSensorFrame(rawFrame());
    CHECK(wire.size() == 60);
    for (int split = 0; split <= wire.size(); ++split)
    {
        SensorDecoder d; auto frames = d.feed(wire.left(split)); frames += d.feed(wire.mid(split));
        CHECK(frames.size() == 1);
        if (frames.size() == 1) CHECK(frames[0].payload == rawFrame().payload);
        CHECK(d.bufferedBytes() == 0);
    }
    SensorDecoder bytes;
    int count = 0;
    for (char b : wire) count += bytes.feed(QByteArray(1, b)).size();
    CHECK(count == 1);
    SensorDecoder mixed;
    CHECK(mixed.feed(QByteArray::fromHex("00677e23aaaa") + wire + golden + wire).size() == 3);
    for (int offset : {0, 1, 2, 4, 5, 10, 15, 20, 56, 58, 59})
    {
        QByteArray bad = wire; bad[offset] = char(quint8(bad.at(offset)) ^ 0x40);
        CHECK(!decodeSensorFrame(bad, parsed));
    }
    QByteArray crcBad = wire; crcBad[20] = char(crcBad.at(20) ^ 1);
    SensorDecoder recovery;
    CHECK(recovery.feed(crcBad + wire).size() == 1);
    CHECK(recovery.errorCount() > 0);
    CHECK(recovery.feed(QByteArray(100000, '\x55')).isEmpty());
    CHECK(recovery.bufferedBytes() <= 78);
    request.payload = QByteArray(58, '\xAA'); CHECK(encodeSensorFrame(request).size() == 78);
    request.payload.append('\0'); CHECK(encodeSensorFrame(request).isEmpty());
    request.payload.clear(); request.target = 0xFF; CHECK(encodeSensorFrame(request).isEmpty());
    request.target = 1; request.flags = 0x80; CHECK(encodeSensorFrame(request).isEmpty());
    CHECK(!decodeSensorFrame(wire + '\0', parsed));
    QByteArray oversize = golden; oversize[10] = char(255); oversize[11] = char(255);
    SensorDecoder big; CHECK(big.feed(oversize + wire).size() == 1); CHECK(big.bufferedBytes() == 0);
}
static void testRequests()
{
    SensorRequest r; r.operation = SensorOperation::SetParameter; r.parameterId = 1; r.value = 100;
    QByteArray p; QString error;
    CHECK(makeSensorRequestPayload(r, p, error)); CHECK(p.toHex() == "010004026400");
    r.value = 101; CHECK(!makeSensorRequestPayload(r, p, error));
    r.value = 10.5; CHECK(!makeSensorRequestPayload(r, p, error));
    r.value = std::numeric_limits<double>::quiet_NaN(); CHECK(!makeSensorRequestPayload(r, p, error));
    r.parameterId = 3; r.value = 9; CHECK(makeSensorRequestPayload(r, p, error));
    r.value = 10; CHECK(!makeSensorRequestPayload(r, p, error));
    r.target = 2; r.parameterId = 0x0105; r.value = 30; CHECK(makeSensorRequestPayload(r, p, error));
    r.value = 4; CHECK(!makeSensorRequestPayload(r, p, error));
    r.parameterId = 0x0101;
    for (int osr : {256,512,1024,2048,4096,8192}) { r.value = osr; CHECK(makeSensorRequestPayload(r, p, error)); }
    r.value = 4095; CHECK(!makeSensorRequestPayload(r, p, error));
    r.parameterId = 0x0104; r.value = 0.99; CHECK(makeSensorRequestPayload(r, p, error));
    quint16 id; QVariant v;
    CHECK(decodeSensorParameter(p, id, v)); CHECK(id == 0x0104);
    r.value = v; CHECK(makeSensorRequestPayload(r, p, error));
    r.value = 1.0; CHECK(!makeSensorRequestPayload(r, p, error));
    r.target = 1; r.operation = SensorOperation::ZeroDepth; CHECK(!makeSensorRequestPayload(r, p, error));
    r.operation = SensorOperation::Calibrate; r.calibrationType = 2; r.calibrationAction = 0;
    CHECK(makeSensorRequestPayload(r, p, error)); CHECK(p.size() == 4);
    r.calibrationType = 4; CHECK(!makeSensorRequestPayload(r, p, error));
}
static void testService()
{
    SensorDataService service;
    QVector<SensorFrame> sent;
    int completed = 0, feedbackCount = 0;
    SensorResult lastResult = SensorResult::Ok;
    SensorParameterFeedback feedback;
    service.setSender([&](const SensorFrame &f) { sent.append(f); return true; });
    QObject::connect(&service, &SensorDataService::commandFinished, [&](quint8, quint32, SensorResult result, const QString &) { ++completed; lastResult = result; });
    QObject::connect(&service, &SensorDataService::parameterReceived, [&](const SensorParameterFeedback &f) { ++feedbackCount; feedback = f; });
    SensorRequest r; r.operation = SensorOperation::SetParameter; r.parameterId = 1; r.value = 25;
    CHECK(!service.request(r)); CHECK(sent.isEmpty());
    service.setConnected(true);
    service.handleFrame(rawFrame());
    CHECK(!service.snapshot().rawValid); // stale buffered frames before a matched reply are ignored
    CHECK(service.request(r)); CHECK(service.snapshot().devices[0].pending);
    CHECK(!service.request(r)); CHECK(sent.size() == 1);
    SensorFrame response = replyFor(sent.last(), SensorResult::Unconfirmed, sent.last().payload);
    auto wrong = response; ++wrong.sequence; service.handleFrame(wrong);
    wrong = response; wrong.target = 2; service.handleFrame(wrong);
    wrong = response; ++wrong.command; service.handleFrame(wrong);
    CHECK(completed == 0 && service.snapshot().devices[0].pending);
    service.handleFrame(response);
    CHECK(completed == 1 && lastResult == SensorResult::Unconfirmed);
    CHECK(feedbackCount == 1 && !feedback.confirmed && feedback.value.toUInt() == 25);
    service.handleFrame(response); CHECK(completed == 1);
    CHECK(service.request(r));
    response = replyFor(sent.last(), SensorResult::Ok, QByteArray::fromHex("030004021900"));
    service.handleFrame(response); CHECK(lastResult == SensorResult::IoError && feedbackCount == 1);
    r.target = 2; r.parameterId = 0x0104; r.value = 0.99;
    CHECK(service.request(r)); service.handleFrame(replyFor(sent.last(), SensorResult::Ok, sent.last().payload));
    CHECK(lastResult == SensorResult::Ok && feedbackCount == 2 && feedback.confirmed);
    service.handleFrame(rawFrame());
    CHECK(service.snapshot().rawValid);
    CHECK(std::abs(service.snapshot().accelG[0] + 0.4) < 1e-6);
    auto staleRaw=rawFrame(50); staleRaw.timestampUs=quint32(0U-3000000U);
    staleRaw.payload.replace(4,4,QByteArray::fromHex("0000803f"));
    service.handleFrame(staleRaw);
    CHECK(std::abs(service.snapshot().accelG[0]+0.4)<1e-6);
    SensorFrame attitude; attitude.command=0x81; attitude.target=1; attitude.flags=8;
    SensorWire::append32(attitude.payload, SensorStatus::Online|SensorStatus::RawValid|SensorStatus::QuaternionValid|SensorStatus::EulerValid);
    for (float value : {1.0f,0.0f,0.0f,0.0f,0.5f,-0.5f,3.14159265f}) SensorWire::appendFloat(attitude.payload,value);
    service.handleFrame(attitude);
    CHECK(service.snapshot().quaternionValid && service.snapshot().eulerValid);
    CHECK(std::abs(service.snapshot().eulerDeg[2] - 180.0) < 0.001);
    auto bad = rawFrame(2); bad.payload.replace(4,4,QByteArray::fromHex("0000c07f"));
    service.handleFrame(bad); CHECK(std::abs(service.snapshot().accelG[0]+0.4) < 1e-6);
    const quint32 depthFlags = SensorStatus::Online|SensorStatus::PromValid|SensorStatus::ModelConfirmed|SensorStatus::PressureValid|SensorStatus::TemperatureValid|SensorStatus::DepthValid|SensorStatus::ZeroValid;
    auto dep = depthFrame(depthFlags); service.handleFrame(dep);
    CHECK(service.snapshot().depthValid && service.snapshot().rawAdcD1 == 0xF01234);
    dep=depthFrame(depthFlags & ~SensorStatus::ZeroValid); dep.sequence=2; service.handleFrame(dep);
    CHECK(!service.snapshot().depthValid && service.snapshot().pressureValid);
    dep=depthFrame(depthFlags & ~SensorStatus::ModelConfirmed); dep.sequence=3; service.handleFrame(dep);
    CHECK(!service.snapshot().depthValid && !service.snapshot().pressureValid);
    waitMs(2600);
    auto freshRaw=rawFrame(3); freshRaw.timestampUs=2600000U;
    service.handleFrame(freshRaw);
    CHECK(service.snapshot().rawValid && !service.snapshot().eulerValid && !service.snapshot().quaternionValid);
    CHECK(!service.snapshot().pressureValid);
    r.target=1; r.parameterId=1; r.value=25; CHECK(service.request(r));
    waitMs(4200); CHECK(lastResult==SensorResult::Timeout && !service.snapshot().devices[0].pending);
    CHECK(service.request(r)); service.setConnected(false);
    CHECK(lastResult==SensorResult::IoError && !service.snapshot().connected && !service.snapshot().rawValid);
    service.setConnected(true); CHECK(service.request(r));
    service.handleFrame(replyFor(sent.last(),SensorResult::PinBlocked));
    CHECK(lastResult==SensorResult::PinBlocked && (service.snapshot().devices[0].status&SensorStatus::PinBlocked));
    // TX guard must not suppress genuine, independently validated passive RX.
    auto guardedRaw=rawFrame(10);
    guardedRaw.payload.replace(0,4,QByteArray::fromHex("03020000")); // ONLINE|RAW_VALID|PIN_BLOCKED
    service.handleFrame(guardedRaw);
    CHECK(service.snapshot().rawValid && service.snapshot().devices[0].online);
    CHECK(service.snapshot().devices[0].status&SensorStatus::PinBlocked);
    attitude.payload.replace(0,4,QByteArray::fromHex("0f020000"));
    service.handleFrame(attitude);
    CHECK(service.snapshot().eulerValid && service.snapshot().quaternionValid);
    CHECK(service.snapshot().rawValid);
    auto offlineRaw=rawFrame(11);
    offlineRaw.payload.replace(0,4,QByteArray::fromHex("00020000"));
    service.handleFrame(offlineRaw);
    CHECK(!service.snapshot().rawValid && !service.snapshot().eulerValid);
}
static void testRouter()
{
    BootloaderCommunicationService communication;
    auto *transport=communication.findChild<SerialTransport *>();
    CHECK(transport!=nullptr); if(!transport) return;
    int sensor=0, can=0, heartbeat=0;
    QObject::connect(&communication,&BootloaderCommunicationService::sensorFrameReceived,[&](const SensorFrame &) { ++sensor; });
    QObject::connect(&communication,&BootloaderCommunicationService::frameReceived,[&](const CanGatewayFrame &) { ++can; });
    QObject::connect(&communication,&BootloaderCommunicationService::heartbeatReceived,[&](const SystemHeartbeat &) { ++heartbeat; });
    const QByteArray canWire=QByteArray::fromHex("aa551001002301000000081122334455667788bc55aa");
    SensorFrame frame=rawFrame(); frame.payload=canWire; // an embedded, otherwise-valid CAN frame
    const QByteArray sensorWire=encodeSensorFrame(frame);
    auto inject=[&](const QByteArray &data) { CHECK(QMetaObject::invokeMethod(transport,"bytesReceived",Qt::DirectConnection,Q_ARG(QByteArray,data))); };
    inject(sensorWire.left(7)); inject(sensorWire.mid(7)+canWire);
    CHECK(sensor==1 && can==1); // embedded AA55 did not escape its AA5B owner
    QByteArray bad=sensorWire; bad[bad.size()-3]=char(bad.at(bad.size()-3)^1);
    inject(bad+sensorWire); CHECK(sensor==2 && can==1);
    SensorFrame hb; hb.command=1; hb.target=1; hb.flags=2; hb.payload=QByteArray(5,0);
    QByteArray heartbeatWire=encodeSensorFrame(hb);
    heartbeatWire[1]=char(0x58); heartbeatWire[4]=0; heartbeatWire[5]=0; heartbeatWire[23]=char(0x58);
    const quint16 crc=SensorWire::crc16(heartbeatWire.mid(1,20));
    heartbeatWire[21]=char(crc); heartbeatWire[22]=char(crc>>8);
    inject(heartbeatWire+sensorWire+canWire); CHECK(heartbeat==1 && sensor==3 && can==2);
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    testWire(); testRequests(); testService(); testRouter();
    std::cout << "sensor protocol/service/router: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
