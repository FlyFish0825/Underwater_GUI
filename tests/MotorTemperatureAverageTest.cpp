#include "communication/protocol/ObserverMotorProtocol.h"
#include "data/services/ObserverMotorDataService.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <deque>
#include <numeric>

using namespace rov;
namespace protocol = rov::ObserverMotorProtocol;
static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (false)

static void put16(QByteArray &bytes, int offset, quint16 value)
{
    bytes[offset] = char(value);
    bytes[offset + 1] = char(value >> 8);
}

static CanGatewayFrame temperature(quint8 node, double value, int kind = 0)
{
    CanGatewayFrame frame;
    frame.canId = (kind == 0 ? 0x200U : kind == 1 ? 0x300U : 0x280U) + node;
    frame.flags = kind == 2 ? 0 : 2;
    frame.data = QByteArray(kind == 0 ? 12 : kind == 1 ? 64 : 8, '\0');
    if (kind == 0)
        put16(frame.data, 6, quint16(std::lround((value + 20.0) * 65535.0 / 170.0)));
    else if (kind == 1)
        put16(frame.data, 20, quint16(qint16(std::lround(value * 10.0))));
    else
    {
        frame.data[0] = char(node);
        frame.data[4] = char(qint8(value));
        frame.data[7] = char(protocol::crc8(frame.data.left(7)));
    }
    return frame;
}

static bool near(double a, double b) { return std::abs(a - b) < 1e-8; }
static void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

static void testAverage()
{
    ObserverMotorDataService service;
    service.handleCanFrame(temperature(1, 30));
    CHECK(near(service.nodeSnapshot(1).temperatureC, 30));
    service.handleCanFrame(temperature(1, 50, 1));
    CHECK(near(service.nodeSnapshot(1).temperatureC, 40));
    service.handleCanFrame(temperature(2, 100));
    CHECK(near(service.nodeSnapshot(2).temperatureC, 100));
    service.handleCanFrame(temperature(1, 20, 2));
    CHECK(near(service.nodeSnapshot(1).temperatureC, 40));
    auto malformed = temperature(1, 90, 2);
    malformed.data[7] = char(quint8(malformed.data[7]) ^ 1U);
    service.handleCanFrame(malformed);
    CHECK(near(service.nodeSnapshot(1).temperatureC, 40));

    service.reset();
    for (int i = 0; i < 100; ++i) service.handleCanFrame(temperature(1, 30));
    service.handleCanFrame(temperature(1, 130));
    CHECK(near(service.nodeSnapshot(1).temperatureC, 31));
    for (int i = 0; i < 99; ++i) service.handleCanFrame(temperature(1, 130));
    CHECK(near(service.nodeSnapshot(1).temperatureC, 130));

    // 连续覆盖多轮环形窗口，与独立队列求和逐点比对。
    service.reset();
    std::deque<double> reference;
    for (int i = 0; i < 450; ++i)
    {
        const double value = (i * 17 % 900) / 10.0;
        reference.push_back(value);
        if (reference.size() > 100) reference.pop_front();
        service.handleCanFrame(temperature(1, value, 1));
        CHECK(near(service.nodeSnapshot(1).temperatureC,
                   std::accumulate(reference.begin(), reference.end(), 0.0) / reference.size()));
    }
    service.reset();
    service.handleCanFrame(temperature(1, 30, 2));
    service.handleCanFrame(temperature(1, 50, 2));
    CHECK(near(service.nodeSnapshot(1).temperatureC, 40));
    service.handleCanFrame(temperature(1, 30));
    CHECK(near(service.nodeSnapshot(1).temperatureC, 30));

    double published = -1;
    QObject::connect(&service, &ObserverMotorDataService::nodeSnapshotChanged,
                     [&published](quint8 node, const ObserverMotorNodeSnapshot &snapshot) {
        if (node == 1) published = snapshot.temperatureC;
    });
    waitMs(80);
    CHECK(near(published, 30));

    // 心跳保持在线，但高精度反馈超时后必须允许心跳接管。
    waitMs(1300);
    service.handleCanFrame(temperature(1, 50, 2));
    CHECK(near(service.nodeSnapshot(1).temperatureC, 30));
    waitMs(1300);
    service.handleCanFrame(temperature(1, 50, 2));
    CHECK(near(service.nodeSnapshot(1).temperatureC, 50));
    service.handleCanFrame(temperature(2, 100));
    waitMs(2800);
    CHECK(!service.nodeSnapshot(2).online);
    service.handleCanFrame(temperature(2, 30));
    CHECK(near(service.nodeSnapshot(2).temperatureC, 30));
}

static void replayCapture(const QString &path)
{
    QFile input(path);
    CHECK(input.open(QIODevice::ReadOnly));
    if (!input.isOpen()) return;
    CanGatewayDecoder decoder;
    ObserverMotorDataService service;
    const auto frames = decoder.feed(input.readAll());
    std::deque<double> recent;
    double rawMin = 1e9, rawMax = -1e9, meanMin = 1e9, meanMax = -1e9;
    int checked = 0;
    for (const auto &frame : frames)
    {
        protocol::DecodedFrame decoded;
        if (!protocol::decode(frame, decoded) || decoded.nodeId != 1) continue;
        service.handleCanFrame(frame);
        if (decoded.kind != protocol::FrameKind::Feedback) continue;
        recent.push_back(decoded.feedback.temperatureC);
        if (recent.size() > 100) recent.pop_front();
        const double expected = std::accumulate(recent.begin(), recent.end(), 0.0) / recent.size();
        const double actual = service.nodeSnapshot(1).temperatureC;
        CHECK(near(actual, expected));
        if (recent.size() == 100)
        {
            rawMin = qMin(rawMin, decoded.feedback.temperatureC);
            rawMax = qMax(rawMax, decoded.feedback.temperatureC);
            meanMin = qMin(meanMin, actual);
            meanMax = qMax(meanMax, actual);
            ++checked;
        }
    }
    CHECK(checked > 0);
    std::printf("Replay full-window samples=%d raw=%.6f..%.6f averaged=%.6f..%.6f\n",
                checked, rawMin, rawMax, meanMin, meanMax);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testAverage();
    if (argc == 2) replayCapture(QString::fromLocal8Bit(argv[1]));
    if (failures == 0) std::puts("MotorTemperatureAverageTest: PASS");
    return failures == 0 ? 0 : 1;
}
