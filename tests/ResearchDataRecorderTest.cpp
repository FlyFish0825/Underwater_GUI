#include "data/recording/ResearchDataRecorder.h"
#include "communication/protocol/ObserverMotorProtocol.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

using namespace rov;

namespace
{

bool require(const bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

void appendLe16(QByteArray &bytes, const qint16 value)
{
    const quint16 raw = static_cast<quint16>(value);
    bytes.append(static_cast<char>(raw & 0xFFU));
    bytes.append(static_cast<char>((raw >> 8U) & 0xFFU));
}

void appendUnsignedLe16(QByteArray &bytes, const quint16 value)
{
    bytes.append(static_cast<char>(value & 0xFFU));
    bytes.append(static_cast<char>((value >> 8U) & 0xFFU));
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!require(temporary.isValid(), "temporary directory unavailable"))
        return 1;

    const QString path = temporary.filePath(QStringLiteral("research_session.jsonl"));
    ResearchDataRecorder recorder;
    QString error;
    if (!require(recorder.startRecording(path, &error), "recorder failed to start"))
        return 1;

    QByteArray feedback;
    appendLe16(feedback, 1234);
    appendUnsignedLe16(feedback, 65535U);
    appendLe16(feedback, 2410);
    appendUnsignedLe16(feedback, 65535U);
    feedback.resize(12);
    feedback[8] = static_cast<char>(ObserverMotorProtocol::MotorState::ClosedLoop);
    feedback[9] = 0x03;
    feedback[10] = 9;

    CanGatewayFrame frame;
    frame.sequence = 77;
    frame.canId = 0x201U;
    frame.flags = ObserverMotorProtocol::kCanFdFlags;
    frame.data = feedback;
    recorder.recordFrame(frame);

    SixDofControlRequest input;
    input.surge = 0.5;
    input.heave = -0.25;
    recorder.recordControlInput(input);

    DashboardSnapshot snapshot;
    snapshot.demo.isDemo = false;
    snapshot.depthM = 12.75;
    snapshot.rollDeg = 1.25;
    recorder.recordDashboardSnapshot(snapshot);
    recorder.recordEvent(QStringLiteral("test_marker"), QStringLiteral("科研记录测试"));
    recorder.stopRecording();

    const QByteArray jsonl = readAll(path);
    const QByteArray csv = readAll(temporary.filePath(QStringLiteral("research_session.csv")));
    const QByteArray meta =
        readAll(temporary.filePath(QStringLiteral("research_session.meta.json")));
    if (!require(jsonl.contains("\"motor_speed_rpm\":1234"), "motor rpm missing") ||
        !require(jsonl.contains("\"motor_bus_current_a\":10"), "motor current missing") ||
        !require(jsonl.contains("\"motor_temperature_c\":150"), "motor temperature missing") ||
        !require(jsonl.contains("\"input_surge\":0.5"), "control input missing") ||
        !require(jsonl.contains("\"depth_m\":12.75"), "depth missing") ||
        !require(jsonl.contains("test_marker"), "event missing") ||
        !require(csv.contains("phase_current_u_a"), "csv schema missing debug current") ||
        !require(csv.contains("imu_accel_x"), "csv schema missing imu") ||
        !require(meta.contains("\"dropped_records\""), "metadata missing drop count"))
        return 1;

    qInfo() << "ResearchDataRecorderTest: PASS";
    return 0;
}
