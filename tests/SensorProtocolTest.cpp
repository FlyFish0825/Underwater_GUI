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
    service.handleFrame(response); CHECK(lastResult == SensorResult::IoError && feedbackCount == 2 && !feedback.value.isValid());
    r.target = 2; r.parameterId = 0x0104; r.value = 0.99;
    CHECK(service.request(r));
    auto depthAck = replyFor(sent.last(), SensorResult::Ok, sent.last().payload);
    depthAck.timestampUs = 0xFFFFFE00U; // next depth sample is after this ACK, across the u32 wrap
    service.handleFrame(depthAck);
    CHECK(lastResult == SensorResult::Ok && feedbackCount == 3 && feedback.confirmed);
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

// Golden bytes are copied from docs/ms5837-aa5b-host-protocol.md (device capture,
// 2026-10-06). Their CRCs are NOT produced by the implementation under test.
static SensorFrame measuredDepthFrame()
{
    const QByteArray packet = QByteArray::fromHex(
        "aa5b018208025d030000200088a8626c"
        "3309000080b1c547713dc44100000000"
        "0000000000000000ff436000c31c7800"
        "9d9f5baa");
    SensorFrame frame;
    CHECK(decodeSensorFrame(packet, frame));
    CHECK(encodeSensorFrame(frame) == packet);
    return frame;
}
static void testMeasuredDepthProtocol()
{
    const auto measured = measuredDepthFrame();
    CHECK(measured.target == kDepthSensor && measured.command == 0x82 && measured.flags == 8);
    CHECK(measured.sequence == 861 && measured.timestampUs == 1818405000U && measured.payload.size() == 32);
    CHECK(SensorWire::read32(measured.payload, 0) == 0x933);
    CHECK(SensorWire::readFloat(measured.payload, 4) == 101219.0f);
    CHECK(std::abs(SensorWire::readFloat(measured.payload, 8) - 24.53f) < 0.0001f);
    CHECK(SensorWire::read32(measured.payload, 24) == 6308863U);
    CHECK(SensorWire::read32(measured.payload, 28) == 7871683U);
    const QByteArray wire = encodeSensorFrame(measured);
    for (int split = 0; split <= wire.size(); ++split)
    {
        SensorDecoder decoder;
        auto decoded = decoder.feed(wire.left(split)); decoded += decoder.feed(wire.mid(split));
        CHECK(decoded.size() == 1 && decoder.bufferedBytes() == 0);
    }
    SensorFrame info, status;
    CHECK(decodeSensorFrame(QByteArray::fromHex(
        "aa5b01410202015100001f0068d5a97c"
        "000202380c00004d533538333700000000"
        "000000000000686f73742d763100b23d5baa"), info));
    CHECK(info.payload.size() == 31 && SensorWire::read32(info.payload, 3) == 0xC38U);
    CHECK(info.payload.mid(7, 6) == "MS5837" && info.payload.mid(23, 7) == "host-v1");
    CHECK(decodeSensorFrame(QByteArray::fromHex(
        "aa5b01420202025100001500d0d5b47c"
        "003309000051cc00001500000051cc00000000000031cc5baa"), status));
    CHECK(status.payload.size() == 21 && SensorWire::read32(status.payload, 5) == 52305U);
    SensorFrame request; request.target = kDepthSensor; request.command = 1; request.sequence = 0x5101;
    CHECK(encodeSensorFrame(request).toHex() == "aa5b0101010201510000000000000000a5fb5baa");
    request.command = 2; request.sequence = 0x5102;
    CHECK(encodeSensorFrame(request).toHex() == "aa5b0102010202510000000000000000cf855baa");

    SensorRequest r; r.target = kDepthSensor; r.operation = SensorOperation::SetParameter;
    r.parameterId = 1; QByteArray payload; QString error;
    for (int hz : {1, 25, 50, 51, 100})
    { r.value = hz; CHECK(makeSensorRequestPayload(r, payload, error)); CHECK(SensorWire::read16(payload, 4) == hz); }
    for (int hz : {0, 101, 200}) { r.value = hz; CHECK(!makeSensorRequestPayload(r, payload, error)); }
    r.parameterId = 0x0102;
    for (double density : {900.0, 1029.0, 1300.0}) { r.value = density; CHECK(makeSensorRequestPayload(r, payload, error)); }
    r.value = 5000; CHECK(!makeSensorRequestPayload(r, payload, error));
    r.parameterId = 0x0105; r.value = 2;
    CHECK(makeSensorRequestPayload(r, payload, error) && payload.toHex() == "0501020102");
    r.operation = static_cast<SensorOperation>(0x0D);
    CHECK(!makeSensorRequestPayload(r, payload, error)); // unimplemented proposal stays unavailable
}
static void testDepthSnapshotsAndFeedback()
{
    SensorDataService service; QVector<SensorFrame> sent;
    SensorParameterFeedback feedback;
    SensorResult lastResult = SensorResult::Ok;
    int completions = 0;
    service.setSender([&](const SensorFrame &f) { sent.append(f); return true; });
    QObject::connect(&service, &SensorDataService::parameterReceived, [&](const SensorParameterFeedback &f) { feedback = f; });
    QObject::connect(&service, &SensorDataService::commandFinished,
                     [&](quint8, quint32, SensorResult r, const QString &) { lastResult = r; ++completions; });
    service.setConnected(true);
    auto data = measuredDepthFrame();
    SensorRequest r; r.target = kDepthSensor; r.operation = SensorOperation::GetInfo;
    CHECK(service.request(r));
    const QByteArray info = QByteArray::fromHex("0202380c00004d533538333700000000000000000000686f73742d763100");
    auto ack = replyFor(sent.last(), SensorResult::Ok, info); ack.timestampUs = data.timestampUs;
    service.handleFrame(ack);
    CHECK(lastResult == SensorResult::Ok && service.snapshot().devices[1].model == 2);
    service.handleFrame(data);
    auto s = service.snapshot();
    CHECK(s.pressureValid && s.temperatureValid && s.depthRawValid);
    CHECK(!s.depthValid && !s.zeroValid && s.pressurePa == 101219.0);
    CHECK(s.rawAdcD1 == 6308863 && s.rawAdcD2 == 7871683);
    CHECK(s.depthTimestampUs == 1818405000U && s.depthAgeMs >= 0 && s.depthAgeMs < 100);
    CHECK(!s.devices[1].statusKnown); // a data frame has no acquisition counters

    SensorFrame stats; stats.flags = 8; stats.target = kDepthSensor; stats.command = 0x83;
    stats.sequence = 862; stats.timestampUs = data.timestampUs;
    stats.payload = QByteArray::fromHex("33090000a3b100001e000000a3b1000000000000");
    service.handleFrame(stats); s = service.snapshot();
    CHECK(s.devices[1].statusKnown && s.devices[1].sampleSequence == 45475);
    CHECK(s.devices[1].goodFrames == 45475 && s.devices[1].errors == 0 && s.devices[1].sequence == 862);
    CHECK(s.devices[1].sampleAgeMs >= 30 && s.devices[1].sampleAgeMs < 130);
    CHECK(s.depthTimestampUs == data.timestampUs); // status generation time cannot replace sample time
    r.operation = SensorOperation::GetStatus; CHECK(service.request(r));
    ack = replyFor(sent.last(), SensorResult::Ok, stats.payload); ack.timestampUs = data.timestampUs;
    service.handleFrame(ack);
    CHECK(service.snapshot().devices[1].sequence == 862); // reply request SEQ is not stream SEQ

    // TARGET=1 status is routed separately, even with the same command/SEQ.
    SensorRequest imu; imu.operation = SensorOperation::GetStatus; CHECK(service.request(imu));
    const QByteArray never = QByteArray::fromHex("0006000000000000ffffffff0000000000000000");
    ack = replyFor(sent.last(), SensorResult::Ok, never); ack.timestampUs = data.timestampUs;
    service.handleFrame(ack);
    stats.target = kImuSensor; stats.payload = never; service.handleFrame(stats);
    CHECK(!service.snapshot().devices[0].online && service.snapshot().devices[0].sampleAgeMs == -1);
    CHECK(service.snapshot().devices[1].online && service.snapshot().pressureValid);
    stats.target = kDepthSensor; ++stats.sequence;
    stats.payload = QByteArray::fromHex("3309000000000000ffffffff0000000000000000");
    service.handleFrame(stats);
    CHECK(service.snapshot().devices[1].sampleAgeMs == -1);

    r.operation = SensorOperation::GetParameter; r.parameterId = 0x0103; CHECK(service.request(r));
    ack = replyFor(sent.last(), SensorResult::NotReady); ack.timestampUs = data.timestampUs;
    service.handleFrame(ack);
    CHECK(lastResult == SensorResult::NotReady && !feedback.value.isValid() && !feedback.confirmed);
    CHECK(feedback.parameterId == 0x0103 && feedback.message.contains("NOT_READY"));
    CHECK(service.snapshot().pressureValid); // not an I/O failure or a disconnect
    r.parameterId = 0x0001; CHECK(service.request(r));
    ack = replyFor(sent.last(), SensorResult::Ok, QByteArray::fromHex("010004026400")); ack.timestampUs = data.timestampUs;
    service.handleFrame(ack);
    CHECK(lastResult == SensorResult::Ok && feedback.confirmed && feedback.value.toInt() == 100);
    r.operation = SensorOperation::SetParameter; r.value = 100; CHECK(service.request(r));
    ack = replyFor(sent.last(), SensorResult::BadValue); ack.timestampUs = data.timestampUs;
    service.handleFrame(ack);
    CHECK(lastResult == SensorResult::BadValue && !feedback.value.isValid());
    CHECK(service.snapshot().pressureValid); // rejected write leaves the old measurement/config intact

    // Successful model change updates the descriptor, invalidates old values, and waits for newer samples.
    r.parameterId = 0x0105; r.value = 30; CHECK(service.request(r));
    ack = replyFor(sent.last(), SensorResult::Ok, sent.last().payload); ack.timestampUs = data.timestampUs + 1000;
    service.handleFrame(ack);
    CHECK(service.snapshot().devices[1].model == 30 && !service.snapshot().pressureValid);
    data.sequence = 864; service.handleFrame(data);
    CHECK(!service.snapshot().pressureValid && service.snapshot().depthAgeMs == -1);
    data.timestampUs = ack.timestampUs + 1000; data.sequence = 865;
    data.payload.replace(0, 4, QByteArray::fromHex("f3090000")); // real zero depth may validly be 0.0
    service.handleFrame(data);
    CHECK(service.snapshot().depthValid && service.snapshot().depthRawM == 0.0);
    auto older = data; older.sequence = 864; older.payload.replace(4, 4, QByteArray::fromHex("00000000"));
    service.handleFrame(older); CHECK(service.snapshot().pressurePa == 101219.0);
    data.sequence = 866; data.payload.replace(0, 4, QByteArray::fromHex("f30d0000")); // contradictory CONFIG_UNKNOWN
    service.handleFrame(data);
    CHECK(!service.snapshot().pressureValid && !service.snapshot().temperatureValid && !service.snapshot().depthValid);
    data.sequence = 867; data.payload.replace(0, 4, QByteArray::fromHex("f1090000")); // no RAW_VALID
    service.handleFrame(data); CHECK(!service.snapshot().depthRawValid);
    data.sequence = 868; data.payload.chop(1); service.handleFrame(data);
    CHECK(service.snapshot().devices[1].sequence == 867); // malformed body cannot update a snapshot
    const int before = completions;
    r.operation = SensorOperation::GetStatus; CHECK(service.request(r));
    ack = replyFor(sent.last(), SensorResult::NotReady, QByteArray(1, '\0')); service.handleFrame(ack);
    CHECK(completions == before + 1 && lastResult == SensorResult::IoError);
    service.setConnected(false);
    CHECK(!service.snapshot().depthRawValid && !service.snapshot().pressureValid && !service.snapshot().zeroValid);
    service.setConnected(true); CHECK(service.snapshot().depthAgeMs == -1 && !service.snapshot().devices[1].statusKnown);
}
static void testDepthSourceFreshnessAndWrap()
{
    SensorDataService service; SensorFrame sent;
    service.setSender([&](const SensorFrame &f) { sent = f; return true; });
    service.setConnected(true);
    SensorRequest r; r.target = kDepthSensor; r.operation = SensorOperation::GetStatus;
    CHECK(service.request(r));
    auto ack = replyFor(sent, SensorResult::Ok, QByteArray::fromHex("f309000001000000000000000100000000000000"));
    ack.timestampUs = 100000000; service.handleFrame(ack);
    auto data = depthFrame(0x9F3); data.timestampUs = 97600000; data.sequence = 1;
    service.handleFrame(data);
    CHECK(service.snapshot().depthValid && service.snapshot().depthAgeMs >= 2400);
    waitMs(180);
    SensorFrame status; status.command = 0x83; status.flags = 8; status.target = kDepthSensor;
    status.sequence = 2; status.timestampUs = 100180000; status.payload = ack.payload.mid(1);
    service.handleFrame(status);
    auto s = service.snapshot();
    CHECK(s.devices[1].online && s.depthAgeMs > 2500); // Qt coarse timers may wake slightly early
    CHECK(!s.pressureValid && !s.temperatureValid && !s.depthValid && !s.depthRawValid);
    CHECK(s.depthTimestampUs == 97600000); // status heartbeat did not make an old sample fresh
    service.setConnected(false); service.setConnected(true);
    CHECK(service.request(r)); ack = replyFor(sent, SensorResult::Ok, status.payload);
    ack.timestampUs = 0xFFFFFF00U; service.handleFrame(ack);
    data.timestampUs = 0x00000300U; data.sequence = 0xFFFFFFFFU; service.handleFrame(data);
    CHECK(service.snapshot().depthValid && service.snapshot().depthTimestampUs == 0x300U);
    data.timestampUs = 0x00000700U; data.sequence = 0; service.handleFrame(data);
    CHECK(service.snapshot().depthValid && service.snapshot().devices[1].sequence == 0);
    data.timestampUs = 0x00000300U; data.sequence = 0xFFFFFFFFU; service.handleFrame(data);
    CHECK(service.snapshot().depthTimestampUs == 0x700U); // reordered packet around wrap is ignored
}

// One-shot discovery reads use the existing service timer and the existing request lane.
static void testDepthAutomaticReadback()
{
    SensorDataService service;
    QVector<SensorFrame> sent;
    QHash<quint16, SensorParameterFeedback> feedback;
    QElapsedTimer deviceClock; deviceClock.start();
    bool zero = false, failStatus = false, failSend = false;
    service.setSender([&](const SensorFrame &request) {
        sent.append(request);
        if (request.command == 2 && failSend) return false;
        QByteArray body;
        SensorResult result = SensorResult::Ok;
        if (request.command == 1)
            body = QByteArray::fromHex("0202380c00004d533538333700000000000000000000686f73742d763100");
        else if (request.command == 2)
        {
            if (failStatus) result = SensorResult::IoError;
            else
            {
                SensorWire::append32(body, zero ? 0x9F3 : 0x933);
                for (quint32 value : {123U, 20U, 123U, 0U}) SensorWire::append32(body, value);
            }
        }
        else if (request.command == 3)
        {
            const quint16 id = SensorWire::read16(request.payload, 0);
            if (id == 0x0103 && !zero) result = SensorResult::NotReady;
            else
            {
                SensorRequest value; value.target = kDepthSensor;
                value.operation = SensorOperation::SetParameter; value.parameterId = id;
                switch (id)
                {
                case 0x0001: value.value = 25; break;
                case 0x0101: value.value = 4096; break;
                case 0x0102: value.value = 1029.0; break;
                case 0x0103: value.value = 101325.0; break;
                case 0x0104: value.value = 0.0; break;
                case 0x0105: value.value = 2; break;
                default: result = SensorResult::Unsupported; break;
                }
                QString error;
                if (result == SensorResult::Ok) CHECK(makeSensorRequestPayload(value, body, error));
            }
        }
        else if (request.command == 0x0C) zero = true;
        else CHECK(request.command == 8); // Only this test explicitly requests STOP_STREAM.
        auto reply = replyFor(request, result, body);
        reply.timestampUs = 1000000U + quint32(deviceClock.elapsed() * 1000);
        service.handleFrame(reply); // Deliberately synchronous: pending must already be installed.
        return true;
    });
    QObject::connect(&service, &SensorDataService::parameterReceived,
                     [&](const SensorParameterFeedback &f) { feedback.insert(f.parameterId, f); });
    const auto awaitCount = [&](int count) {
        QElapsedTimer deadline; deadline.start();
        while (sent.size() < count && deadline.elapsed() < 2000) waitMs(20);
    };
    service.setConnected(true);
    SensorRequest r; r.target = kDepthSensor; r.operation = SensorOperation::GetInfo;
    CHECK(service.request(r));
    awaitCount(8);
    CHECK(sent.size() == 8 && feedback.size() == 6);
    if (sent.size() != 8) return;
    CHECK(sent[0].command == 1 && sent[1].command == 2);
    const QVector<quint16> ids{0x0001, 0x0101, 0x0102, 0x0103, 0x0104, 0x0105};
    for (int i = 0; i < sent.size(); ++i)
    {
        CHECK(sent[i].target == kDepthSensor && sent[i].flags == 1);
        CHECK(sent[i].command >= 1 && sent[i].command <= 3); // No automatic write/zero/stream switch.
        if (i) CHECK(quint32(sent[i].sequence - sent[i - 1].sequence) == 1);
        if (i >= 2) CHECK(sent[i].payload.size() == 2 && SensorWire::read16(sent[i].payload, 0) == ids[i - 2]);
    }
    CHECK(feedback[0x0001].confirmed && feedback[0x0001].value.toInt() == 25);
    CHECK(feedback[0x0101].confirmed && feedback[0x0101].value.toInt() == 4096);
    CHECK(feedback[0x0102].value.toDouble() == 1029.0 && feedback[0x0105].value.toInt() == 2);
    CHECK(feedback[0x0104].confirmed && feedback[0x0104].value.toDouble() == 0.0);
    CHECK(!feedback[0x0103].value.isValid() && feedback[0x0103].message.contains("NOT_READY"));
    CHECK(service.snapshot().devices[1].statusKnown && !service.snapshot().devices[1].pending);
    waitMs(150); CHECK(sent.size() == 8); // Finished, not a background polling loop.

    r.operation = SensorOperation::ZeroDepth;
    CHECK(service.request(r)); awaitCount(11);
    CHECK(sent.size() == 11);
    CHECK(feedback[0x0103].confirmed && feedback[0x0103].value.toDouble() == 101325.0);
    CHECK(service.snapshot().zeroValid && !service.snapshot().surfacePressureValid);
    CHECK(service.snapshot().depthAgeMs == -1); // Parameter readback is not a measurement.
    if (sent.size() != 11) return;
    CHECK(sent[9].command == 2 && sent[10].command == 3 && SensorWire::read16(sent[10].payload, 0) == 0x0103);

    int before = sent.size(); r.operation = SensorOperation::GetInfo;
    CHECK(service.request(r));
    r.operation = SensorOperation::StopStream; CHECK(service.request(r));
    waitMs(200); CHECK(sent.size() == before + 2); // Explicit operation cancels remaining one-shot reads.
    before = sent.size(); r.operation = SensorOperation::GetInfo; CHECK(service.request(r));
    service.setConnected(false); service.setConnected(true);
    waitMs(200); CHECK(sent.size() == before + 1); // Old connection reads must not leak into a new session.

    before = sent.size(); failStatus = true; CHECK(service.request(r)); awaitCount(before + 2);
    waitMs(150); CHECK(sent.size() == before + 2 && !service.snapshot().devices[1].pending);
    before = sent.size(); failStatus = false; failSend = true; CHECK(service.request(r)); awaitCount(before + 2);
    waitMs(150); CHECK(sent.size() == before + 2 && !service.snapshot().devices[1].pending);
}

static void testDepthStatusAndValueSeparation()
{
    SensorDataService service; SensorFrame sent;
    service.setSender([&](const SensorFrame &f) { sent = f; return true; });
    service.setConnected(true);
    SensorRequest r; r.target = kDepthSensor; r.operation = SensorOperation::GetStatus;
    CHECK(service.request(r));
    QByteArray stats = QByteArray::fromHex("330900007b000000140000007b00000000000000");
    auto ack = replyFor(sent, SensorResult::Ok, stats); ack.timestampUs = 1000000U;
    service.handleFrame(ack);
    auto sample = depthFrame(0x933); sample.timestampUs = 1000000U; sample.sequence = 100;
    sample.payload.replace(20, 4, QByteArray(4, '\0')); // Documented no-zero placeholder.
    service.handleFrame(sample);
    CHECK(service.snapshot().pressureValid && !service.snapshot().zeroValid && !service.snapshot().surfacePressureValid);
    SensorFrame status; status.target = kDepthSensor; status.command = 0x83; status.flags = 8;
    status.timestampUs = 1001000U; status.sequence = 101; status.payload = stats;
    status.payload.replace(0, 4, QByteArray::fromHex("f3090000"));
    service.handleFrame(status);
    CHECK(service.snapshot().zeroValid && !service.snapshot().surfacePressureValid);
    CHECK(service.snapshot().surfacePressurePa == 0.0); // Still a placeholder; UI must keep --.
    sample.sequence = 102; sample.timestampUs = 1002000U;
    sample.payload.replace(0, 4, QByteArray::fromHex("f3090000"));
    QByteArray p0; SensorWire::appendFloat(p0, 101325.0f); sample.payload.replace(20, 4, p0);
    service.handleFrame(sample);
    CHECK(service.snapshot().surfacePressureValid && service.snapshot().surfacePressurePa == 101325.0);

    // A newer 0x82 may arrive before an older 0x83. Per-command ordering alone is insufficient.
    sample.sequence = 200; sample.timestampUs = 1003000U; service.handleFrame(sample);
    status.sequence = 199; status.timestampUs = 1002000U; status.payload.replace(0, 4, QByteArray(4, '\0'));
    service.handleFrame(status);
    CHECK(service.snapshot().pressureValid && service.snapshot().surfacePressureValid);
    CHECK(service.snapshot().devices[1].sequence == 200 && service.snapshot().devices[1].status == 0x9F3);
    status.sequence = 201; status.timestampUs = 1004000U;
    status.payload.replace(0, 4, QByteArray::fromHex("f30d0000")); // ConfigUnknown overrides stale P0.
    service.handleFrame(status);
    CHECK(!service.snapshot().surfacePressureValid && !service.snapshot().pressureValid);
    sample.sequence = 210; sample.timestampUs = 1005000U; service.handleFrame(sample);
    CHECK(service.snapshot().surfacePressureValid && service.snapshot().devices[1].sequence == 210);

    service.setConnected(false); service.setConnected(true); CHECK(service.request(r));
    ack = replyFor(sent, SensorResult::Ok, stats); ack.timestampUs = 0xFFFFFF00U; service.handleFrame(ack);
    sample.sequence = 0xFFFFFFFFU; sample.timestampUs = 0xFFFFFF00U; service.handleFrame(sample);
    status.sequence = 0; status.timestampUs = 0x300U; status.payload.replace(0, 4, QByteArray::fromHex("f3090000"));
    service.handleFrame(status);
    CHECK(service.snapshot().devices[1].sequence == 0 && service.snapshot().surfacePressureValid);
    auto replay = sample; replay.payload.replace(20, 4, QByteArray(4, '\0')); service.handleFrame(replay);
    CHECK(service.snapshot().surfacePressurePa == 101325.0 && service.snapshot().devices[1].sequence == 0);
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

static void testDepthRatePacketAndCommandTrace()
{
    SensorRequest request; request.target = kDepthSensor;
    request.operation = SensorOperation::SetParameter; request.parameterId = 0x0001; request.value = 100;
    QByteArray payload; QString error;
    CHECK(makeSensorRequestPayload(request, payload, error));
    CHECK(payload == QByteArray::fromHex("010004026400"));
    SensorFrame example; example.command = 0x04; example.flags = 1; example.target = kDepthSensor;
    example.sequence = 79763011U; example.payload = payload;
    const QByteArray golden = QByteArray::fromHex("aa5b010401024316c1040600000000000100040264001e7c5baa");
    CHECK(encodeSensorFrame(example) == golden);
    SensorFrame decoded;
    CHECK(decodeSensorFrame(golden, decoded));
    CHECK(decoded.sequence == 79763011U && decoded.target == 2 && decoded.command == 4);
    CHECK(decoded.payload == payload && SensorWire::read16(golden, 22) == 0x7C1E);
    std::cout << "100 Hz TX reconstruction (SEQ 79763011, NOT a historical capture): "
              << golden.toHex(' ').toUpper().constData() << "\n";

    SensorDataService service; SensorFrame sent;
    service.setSender([&](const SensorFrame &f) { sent = f; return true; });
    service.setConnected(true);
    CHECK(service.request(request));
    const QString tx = QString::fromLatin1(encodeSensorFrame(sent).toHex(' ').toUpper());
    CHECK(service.snapshot().devices[1].lastRequestHex == tx);
    CHECK(service.snapshot().devices[1].lastReplyHex.isEmpty());
    auto reply = replyFor(sent, SensorResult::BadValue);
    auto wrong = reply; ++wrong.sequence; service.handleFrame(wrong);
    CHECK(service.snapshot().devices[1].lastReplyHex.isEmpty());
    service.handleFrame(reply);
    const QString rx = QString::fromLatin1(encodeSensorFrame(reply).toHex(' ').toUpper());
    CHECK(service.snapshot().devices[1].lastReplyHex == rx);
    CHECK(service.snapshot().devices[1].lastRequestHex == tx);
    CHECK(!service.snapshot().devices[1].pending);
    auto sample = depthFrame(0x9F3); sample.timestampUs = 0; service.handleFrame(sample);
    CHECK(service.snapshot().devices[1].lastRequestHex == tx && service.snapshot().devices[1].lastReplyHex == rx);
    request.operation = SensorOperation::GetParameter;
    CHECK(service.request(request));
    CHECK(service.snapshot().devices[1].lastReplyHex.isEmpty());
    service.setConnected(false);
    CHECK(service.snapshot().devices[1].lastRequestHex.isEmpty() && service.snapshot().devices[1].lastReplyHex.isEmpty());
}


// The simulated device checks each intermediate pair against an independent table.
static int referenceDepthMaximum(int osr)
{
    const int oversampling[] = {256, 512, 1024, 2048, 4096, 8192};
    const int ceilings[] = {100, 100, 100, 71, 45, 25};
    for (int i = 0; i < 6; ++i) if (oversampling[i] == osr) return ceilings[i];
    return 0;
}
static SensorRequest pairedSampling(int osr, int rate)
{
    SensorRequest r; r.target = kDepthSensor; r.operation = SensorOperation::SetParameter;
    r.parameterId = 0x0001; r.value = rate;
    r.samplingOsr = quint16(osr); r.samplingRateHz = quint16(rate);
    return r;
}
struct SamplingDeviceFixture
{
    SensorDataService service;
    int model = 2, osr = 4096, rate = 25, writes = 0;
    int rejectWrite = 0, failSend = 0, dropCommand = 0;
    bool wrongEcho = false, wrongFinalRead = false;
    QVector<SensorFrame> sent;
    QVector<quint16> writeIds;
    SensorResult outcome = SensorResult::Ok;
    SamplingDeviceFixture()
    {
        service.setConnected(true);
        QObject::connect(&service, &SensorDataService::commandFinished, &service,
            [this](quint8, quint32, SensorResult r, const QString &) { outcome = r; });
        service.setSender([this](const SensorFrame &f) {
            sent.append(f);
            CHECK(f.target == kDepthSensor && (f.command == 3 || f.command == 4));
            CHECK(f.payload.size() == (f.command == 3 ? 2 : 6));
            if (failSend == sent.size()) return false;
            if (dropCommand == sent.size()) return true;
            const quint16 id = SensorWire::read16(f.payload, 0);
            SensorResult result = SensorResult::Ok;
            if (f.command == 4)
            {
                ++writes; writeIds.append(id);
                const int value = SensorWire::read16(f.payload, 4);
                const int nextOsr = id == 0x0101 ? value : osr;
                const int nextRate = id == 0x0001 ? value : rate;
                CHECK(id == 0x0001 || id == 0x0101); // no implicit model/zero/stream commands
                CHECK(nextRate >= 1 && nextRate <= referenceDepthMaximum(nextOsr));
                if (rejectWrite == writes) result = SensorResult::BadValue;
                else { osr = nextOsr; rate = nextRate; }
            }
            QByteArray tuple;
            if (result == SensorResult::Ok)
            {
                SensorWire::append16(tuple, id);
                tuple.append(char(id == 0x0105 ? 2 : 4)); tuple.append(char(id == 0x0105 ? 1 : 2));
                int value = id == 0x0105 ? model : id == 0x0101 ? osr : rate;
                if (wrongEcho && f.command == 4) --value;
                if (wrongFinalRead && writes && f.command == 3 && id == 0x0001) --value;
                if (id == 0x0105) tuple.append(char(value)); else SensorWire::append16(tuple, quint16(value));
            }
            service.handleFrame(replyFor(f, result, tuple));
            return true;
        });
    }
    void tick()
    {
        auto *timer = service.findChild<QTimer *>();
        CHECK(timer != nullptr);
        if (timer) CHECK(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
    }
    void drain() { for (int i = 0; i < 10 && service.snapshot().devices[1].pending; ++i) tick(); }
};
static void testDepthPairedSampling()
{
    for (int osr : {256, 512, 1024, 2048, 4096, 8192})
        CHECK(depth02baMaxRateHz(osr) == referenceDepthMaximum(osr));
    CHECK(depth02baMaxRateHz(0) == 0 && depth02baMaxRateHz(3000) == 0);
    // All OSR-to-OSR transitions, both minimum and maximum current/target rates.
    for (int from : {256, 512, 1024, 2048, 4096, 8192})
    for (int to : {256, 512, 1024, 2048, 4096, 8192})
    for (int oldRate : {1, referenceDepthMaximum(from)})
    for (int newRate : {1, referenceDepthMaximum(to)})
    {
        SamplingDeviceFixture f; f.osr = from; f.rate = oldRate;
        CHECK(f.service.request(pairedSampling(to, newRate)));
        CHECK(f.service.snapshot().devices[1].pending && f.sent.isEmpty());
        f.drain();
        CHECK(f.osr == to && f.rate == newRate && f.writes <= 2);
        CHECK(!f.service.snapshot().devices[1].pending && f.outcome == SensorResult::Ok);
        CHECK(f.service.snapshot().devices[1].lastCommand.contains(QStringLiteral("组合已回读确认")));
        CHECK(f.sent.size() >= 5 && f.sent.size() <= 7);
        CHECK(f.sent[f.sent.size()-2].command == 3 && SensorWire::read16(f.sent[f.sent.size()-2].payload,0) == 0x0101);
        CHECK(f.sent.last().command == 3 && SensorWire::read16(f.sent.last().payload,0) == 0x0001);
        if (f.writes == 2)
            CHECK(f.writeIds.first() == (newRate <= referenceDepthMaximum(from) ? 0x0001 : 0x0101));
    }
    // Invalid pairs never enter the serial path, including direct callers bypassing the UI.
    for (const auto &r : {pairedSampling(4096,100), pairedSampling(2048,72), pairedSampling(8192,26),
                         pairedSampling(3000,25), pairedSampling(512,0), pairedSampling(512,101)})
    {
        SamplingDeviceFixture f;
        CHECK(!f.service.request(r) && f.sent.isEmpty());
        CHECK(!f.service.snapshot().devices[1].pending);
    }
    {
        SamplingDeviceFixture f; auto r = pairedSampling(512,100); r.target = kImuSensor;
        CHECK(!f.service.request(r) && f.sent.isEmpty());
        r.target = kDepthSensor; r.value = 25;
        CHECK(!f.service.request(r) && f.sent.isEmpty());
        f.service.setConnected(false);
        CHECK(!f.service.request(pairedSampling(512,100)) && f.sent.isEmpty());
    }
    for (int failing : {1,2})
    {
        SamplingDeviceFixture f; f.rejectWrite = failing;
        CHECK(f.service.request(pairedSampling(512,100))); f.drain();
        CHECK(f.writes == failing && f.outcome == SensorResult::BadValue);
        CHECK(f.rate == 25 && f.osr == (failing == 1 ? 4096 : 512));
        CHECK(!f.service.snapshot().devices[1].pending);
        CHECK(f.service.snapshot().devices[1].lastCommand.contains(QStringLiteral("联动设置中断")));
        const int count = f.sent.size(); f.tick(); f.tick(); CHECK(f.sent.size() == count);
    }
    {
        SamplingDeviceFixture f; f.model = 0;
        CHECK(f.service.request(pairedSampling(512,100))); f.drain();
        CHECK(f.writes == 0 && f.outcome == SensorResult::BadValue && !f.service.snapshot().devices[1].pending);
    }
    {
        SamplingDeviceFixture f; f.wrongEcho = true;
        CHECK(f.service.request(pairedSampling(512,100))); f.drain();
        CHECK(f.writes == 1 && f.outcome == SensorResult::IoError && f.rate == 25);
    }
    {
        SamplingDeviceFixture f; f.wrongFinalRead = true;
        CHECK(f.service.request(pairedSampling(512,100))); f.drain();
        CHECK(f.writes == 2 && f.outcome == SensorResult::IoError);
        CHECK(!f.service.snapshot().devices[1].lastCommand.contains(QStringLiteral("组合已回读确认")));
    }
    {
        SamplingDeviceFixture f; f.failSend = 4;
        CHECK(f.service.request(pairedSampling(512,100))); f.drain();
        CHECK(f.writes == 0 && f.outcome == SensorResult::IoError && !f.service.snapshot().devices[1].pending);
    }
    {
        SamplingDeviceFixture f;
        CHECK(f.service.request(pairedSampling(512,100)));
        SensorRequest competing; competing.target = kDepthSensor; competing.operation = SensorOperation::ZeroDepth;
        CHECK(!f.service.request(competing)); f.tick(); f.tick(); f.tick(); f.tick(); // first SET confirmed
        CHECK(f.osr == 512 && f.rate == 25 && f.service.snapshot().devices[1].pending);
        const int sent = f.sent.size();
        f.service.setConnected(false); f.service.setConnected(true); f.tick();
        CHECK(f.sent.size() == sent && !f.service.snapshot().devices[1].pending); // no resumed writes
    }
    {
        SamplingDeviceFixture f; f.dropCommand = 4;
        CHECK(f.service.request(pairedSampling(512,100))); f.drain();
        CHECK(f.service.snapshot().devices[1].pending && f.sent.size() == 4);
        waitMs(4250);
        CHECK(f.outcome == SensorResult::Timeout && !f.service.snapshot().devices[1].pending);
        f.tick(); CHECK(f.sent.size() == 4 && f.writes == 0);
    }
}


// Explicit startup policy fixture; no serial port. ZERO consumes the latest valid pressure,
// and the view becomes valid only when a subsequent DEPTH_DATA arrives.
struct DepthStartupFixture
{
    SensorDataService service;
    QElapsedTimer clock;
    QVector<SensorFrame> sent;
    int model = 0, modelWrites = 0, zeroWrites = 0;
    bool zero = false, online = true, rejectModel = false, rejectZero = false, dropZero = false;
    float pressure = 101187.0f, p0 = 0.0f;
    quint32 streamSequence = 0;
    DepthStartupFixture(bool enabled = true)
    {
        clock.start();
        service.findChild<QTimer *>()->stop();
        service.setDepthStartupEnabled(enabled);
        service.setSender([this](const SensorFrame &f) {
            sent.append(f);
            CHECK(f.target == kDepthSensor);
            SensorResult result = SensorResult::Ok;
            QByteArray body;
            if (f.command == 1)
            {
                body.append(char(2)); body.append(char(model)); SensorWire::append32(body,0xC38);
                body.append(QByteArray("MS5837").leftJustified(16,'\0'));
                body.append(QByteArray("host-v1").leftJustified(8,'\0'));
            }
            else if (f.command == 2)
            {
                SensorWire::append32(body,flags()); SensorWire::append32(body,10);
                SensorWire::append32(body,0); SensorWire::append32(body,10); SensorWire::append32(body,0);
            }
            else if (f.command == 3 || f.command == 4)
            {
                const quint16 id = SensorWire::read16(f.payload,0);
                if (f.command == 4)
                {
                    if (id == 0x0105)
                    {
                        ++modelWrites;
                        CHECK(f.payload.toHex() == "0501020102");
                        if (rejectModel) result = SensorResult::BadValue;
                        else { model=2; zero=false; p0=0; }
                    }
                    else if (id == 0x0103) { p0=SensorWire::readFloat(f.payload,4); zero=true; }
                    else CHECK(false); // Startup may never change OSR/rate/density/filter.
                }
                if (result == SensorResult::Ok)
                {
                    if (id == 0x0103 && !zero) result=SensorResult::NotReady;
                    else
                    {
                        SensorRequest value; value.target=kDepthSensor; value.operation=SensorOperation::SetParameter;
                        value.parameterId=id;
                        value.value=id==0x0001?25.0:id==0x0101?4096.0:id==0x0102?1029.0:
                                    id==0x0103?p0:id==0x0105?double(model):0.0;
                        QString error; CHECK(makeSensorRequestPayload(value,body,error));
                    }
                }
            }
            else if (f.command == 0x0C)
            {
                ++zeroWrites; CHECK(f.payload.isEmpty());
                CHECK(online && model==2 && pressure>=10000 && pressure<=200000);
                if (dropZero) return true;
                if (rejectZero) result=SensorResult::Busy;
                else { zero=true; p0=pressure; }
            }
            else CHECK(false);
            auto ack=replyFor(f,result,body); ack.timestampUs=nowUs(); service.handleFrame(ack);
            return true;
        });
        service.setConnected(true);
    }
    quint32 nowUs() const { return 1000000U+quint32(clock.elapsed()*1000); }
    quint32 flags() const
    {
        quint32 status=SensorStatus::PromValid;
        status|=model==2?SensorStatus::ModelConfirmed:SensorStatus::ConfigUnknown;
        if (online) { status|=SensorStatus::Online|SensorStatus::RawValid;
            if(model==2)status|=SensorStatus::PressureValid|SensorStatus::TemperatureValid; }
        if(zero)status|=SensorStatus::ZeroValid;
        if(zero && online && model==2)status|=SensorStatus::DepthValid;
        return status;
    }
    void discover()
    {
        SensorRequest info; info.target=kDepthSensor; info.operation=SensorOperation::GetInfo;
        CHECK(service.request(info));
    }
    void tick(int n=1)
    {
        for(int i=0;i<n;++i)CHECK(QMetaObject::invokeMethod(service.findChild<QTimer *>(),"timeout",Qt::DirectConnection));
    }
    void sample(bool stale=false)
    {
        auto f=depthFrame(flags()); f.sequence=++streamSequence; f.timestampUs=nowUs()-(stale?3000000U:0U);
        f.payload.clear(); SensorWire::append32(f.payload,flags());
        const float depth=zero?(pressure-p0)/(1029.0f*9.80665f):0.0f;
        for(float value:{model==2?pressure:0.0f,model==2?25.0f:0.0f,depth,depth,p0})
            SensorWire::appendFloat(f.payload,value);
        SensorWire::append32(f.payload,6300000); SensorWire::append32(f.payload,7800000);
        service.handleFrame(f);
    }
};
static void testDepthStartupReference()
{
    {
        DepthStartupFixture f;
        f.sample(); f.tick(3); CHECK(f.sent.isEmpty()); // no handshake, no configuration
        f.discover(); f.tick(20);
        CHECK(f.modelWrites==1 && f.zeroWrites==0 && !f.service.snapshot().depthValid);
        f.sample(true); f.tick(); CHECK(f.zeroWrites==0); // never calibrate from queued/stale data
        f.sample(); CHECK(f.service.snapshot().pressureValid && !f.service.snapshot().depthValid);
        f.tick(); CHECK(f.zeroWrites==1 && f.p0==f.pressure);
        CHECK(!f.service.snapshot().depthValid); // ACK alone does not manufacture a zero reading
        f.sample(); f.tick(10);
        CHECK(f.service.snapshot().depthValid && f.service.snapshot().depthFilteredM==0.0);
        CHECK(f.service.snapshot().surfacePressureValid && f.service.snapshot().surfacePressurePa==101187.0);
        f.pressure+=1000; f.sample(); f.tick(10);
        CHECK(f.zeroWrites==1 && f.service.snapshot().depthFilteredM>0.09);
        // Reopening the GUI/USB underwater preserves an established reference, not a new water surface.
        f.service.setConnected(false); f.service.setConnected(true); f.discover(); f.tick(20); f.sample(); f.tick(2);
        CHECK(f.modelWrites==1 && f.zeroWrites==1 && f.p0==101187.0f);
        CHECK(f.service.snapshot().depthValid && f.service.snapshot().depthFilteredM>0.09);
        // A real power cycle loses model/zero, so the next connection gets a new one-shot startup.
        f.service.setConnected(false); f.model=0; f.zero=false; f.p0=0;
        f.service.setConnected(true); f.discover(); f.tick(20); f.sample(); f.tick(); f.sample();
        CHECK(f.modelWrites==2 && f.zeroWrites==2 && f.service.snapshot().depthFilteredM==0.0);
    }
    {
        DepthStartupFixture f(false); f.model=2; f.discover(); f.sample(); f.tick(20);
        CHECK(f.modelWrites==0 && f.zeroWrites==0 && !f.service.snapshot().depthValid); // probes stay read-only
    }
    {
        DepthStartupFixture f; f.model=2; f.zero=true; f.p0=100187;
        f.discover(); f.tick(20); f.sample(); f.tick();
        CHECK(f.zeroWrites==0 && f.modelWrites==0 && f.service.snapshot().depthFilteredM>0.09);
    }
    {
        DepthStartupFixture f; f.model=2; f.online=false; f.discover(); f.tick(20); f.sample(); f.tick(5);
        CHECK(f.zeroWrites==0 && !f.service.snapshot().depthValid && !f.service.snapshot().devices[1].online);
        f.online=true; f.sample(); f.tick(); CHECK(f.zeroWrites==1); // genuine sensor recovery
    }
    for(float pressure:{9999.0f,200001.0f})
    {
        DepthStartupFixture f; f.model=2; f.pressure=pressure; f.discover(); f.tick(20); f.sample(); f.tick(4);
        CHECK(f.zeroWrites==0 && !f.service.snapshot().depthValid);
    }
    {
        DepthStartupFixture f; f.rejectModel=true; f.discover(); f.tick(25);
        CHECK(f.modelWrites==1 && f.zeroWrites==0); // rejected initialization is not retried
    }
    {
        DepthStartupFixture f; f.model=2; f.rejectZero=true; f.discover(); f.tick(20); f.sample(); f.tick(20);
        CHECK(f.zeroWrites==1 && !f.service.snapshot().depthValid);
        f.sample(); f.tick(5); CHECK(f.zeroWrites==1);
    }
    {
        DepthStartupFixture f; f.model=2; f.dropZero=true; f.discover(); f.tick(20); f.sample(); f.tick();
        CHECK(f.zeroWrites==1 && f.service.snapshot().devices[1].pending);
        waitMs(4200); f.tick(5); CHECK(f.zeroWrites==1 && !f.service.snapshot().devices[1].pending);
    }
    {
        DepthStartupFixture f; f.model=2; f.discover(); f.tick(20);
        SensorRequest manual; manual.target=kDepthSensor; manual.operation=SensorOperation::SetParameter;
        manual.parameterId=0x0103; manual.value=101000;
        CHECK(f.service.request(manual)); f.sample(); f.tick(20);
        CHECK(f.zeroWrites==0 && f.p0==101000 && f.service.snapshot().depthValid);
        manual.operation=SensorOperation::ZeroDepth;
        CHECK(f.service.request(manual)); f.sample(); f.tick(10);
        CHECK(f.zeroWrites==1 && f.service.snapshot().depthFilteredM==0.0); // manual recalibration remains usable
    }
    {
        DepthStartupFixture f; f.model=2; f.discover(); f.tick(20);
        f.service.setConnected(false); f.sample(); f.tick(20);
        CHECK(f.zeroWrites==0 && !f.service.snapshot().depthValid);
    }
    {
        DepthStartupFixture f; f.model=2; f.discover(); f.tick(20);
        waitMs(10100); f.tick(); f.sample(); f.tick(5);
        CHECK(f.zeroWrites==0 && !f.service.snapshot().depthValid); // no delayed surprise zero much later
    }
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    testDepthStartupReference();
    testDepthRatePacketAndCommandTrace();
    testDepthPairedSampling();
    testWire(); testRequests(); testService(); testMeasuredDepthProtocol();
    testDepthSnapshotsAndFeedback(); testDepthSourceFreshnessAndWrap();
    testDepthAutomaticReadback(); testDepthStatusAndValueSeparation(); testRouter();
    std::cout << "sensor protocol/service/router: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
