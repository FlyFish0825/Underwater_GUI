#pragma once

#include <QMetaType>
#include <QString>
#include <QVariant>
#include <array>

namespace rov
{
// Stable business IDs; never derive a sensor ID from its table row.
constexpr quint8 kImuSensor = 1;
constexpr quint8 kDepthSensor = 2;

enum class SensorOperation : quint8
{
    GetInfo = 0x01, GetStatus = 0x02, GetParameter = 0x03, SetParameter = 0x04,
    SaveConfig = 0x05, RestoreDefaults = 0x06, StartStream = 0x07, StopStream = 0x08,
    Calibrate = 0x09, SelfTest = 0x0A, Reboot = 0x0B, ZeroDepth = 0x0C
};

enum class SensorResult : quint8
{
    Ok = 0, Unsupported = 1, BadValue = 2, Busy = 3, Timeout = 4, Offline = 5,
    IoError = 6, Unconfirmed = 7, NotReady = 8, PinBlocked = 9
};

namespace SensorStatus
{
constexpr quint32 Online = 1U << 0;
constexpr quint32 RawValid = 1U << 1;
constexpr quint32 QuaternionValid = 1U << 2;
constexpr quint32 EulerValid = 1U << 3;
constexpr quint32 PressureValid = 1U << 4;
constexpr quint32 TemperatureValid = 1U << 5;
constexpr quint32 DepthValid = 1U << 6;
constexpr quint32 ZeroValid = 1U << 7;
constexpr quint32 PromValid = 1U << 8;
constexpr quint32 PinBlocked = 1U << 9;
constexpr quint32 ConfigUnknown = 1U << 10;
constexpr quint32 ModelConfirmed = 1U << 11;
}

// Fixed 02BA business constraint shared by the view and service. See host protocol section 5.1.
// Driver rounds maximum conversion time UP to ms before adding both timing margins.
constexpr int depth02baMaxRateHz(int osr)
{
    switch (osr)
    {
    case 256: case 512: case 1024: return 100;
    case 2048: return 71;
    case 4096: return 45;
    case 8192: return 25;
    default: return 0; // Unknown is not a usable configuration.
    }
}

struct SensorRequest
{
    quint8 target = kImuSensor;
    SensorOperation operation = SensorOperation::GetInfo;
    quint16 parameterId = 0;
    QVariant value;
    quint8 calibrationType = 1;
    quint8 calibrationAction = 1;
    double referenceTemperatureC = 25.0;
    // Host-only paired intent for depth SET 0001/0101; both zero = ordinary single parameter.
    // The service sequences existing GET/SET commands; these fields never extend a wire payload.
    quint16 samplingOsr = 0;
    quint16 samplingRateHz = 0;
};

// Only a matched OK START/STOP reply confirms forwarding state; reconnect resets it.
// STOP controls measurement forwarding. The backend still samples and sends 0x83 status.
enum class SensorStreamState { Unknown, Running, Stopped };

struct SensorDeviceState
{
    bool infoKnown = false;
    bool online = false;
    bool pending = false;
    SensorStreamState streamState = SensorStreamState::Unknown;
    quint8 model = 0;
    quint32 capabilities = 0;
    quint32 status = 0;
    quint32 goodFrames = 0;
    quint32 errors = 0;
    // Latest telemetry stream SEQ, not the device's completed-sample counter.
    quint32 sequence = 0;
    bool statusKnown = false;
    quint32 sampleSequence = 0;
    // Device-reported sample age advanced by host monotonic time; -1 = no sample/unknown.
    qint64 sampleAgeMs = -1;
    QString name = QStringLiteral("--");
    QString firmware = QStringLiteral("--");
    QString lastCommand = QStringLiteral("尚未发送命令");
    // Bounded, latest command only. Request is the submitted encoding, not proof of a successful write.
    // Reply is populated only for the matching TARGET/CMD/SEQ; telemetry never overwrites it.
    QString lastRequestHex;
    QString lastReplyHex;
};

// Host observation only: Matches never changes the device's UNCONFIRMED result.
enum class ImuRateCheck { NotRequested, Observing, Matches, Differs, Insufficient, Cancelled };

struct SensorSnapshot
{
    bool connected = false;
    // Index 0 is IMU, index 1 is depth; TARGET is not this array index.
    std::array<SensorDeviceState, 2> devices{};
    std::array<double, 3> accelG{}, gyroRadS{}, magProtocolUnits{}, eulerDeg{};
    std::array<double, 4> quaternionWxyz{};
    bool rawValid = false;
    bool quaternionValid = false;
    bool eulerValid = false;
    bool depthRawValid = false; // Depth D1/D2 validity; independent of IMU rawValid.
    bool pressureValid = false;
    bool temperatureValid = false;
    bool depthValid = false;
    bool zeroValid = false; // Device zero configuration exists; not proof of a received P0 value.
    bool surfacePressureValid = false; // P0 from a fresh DEPTH_DATA with ZERO_VALID.
    double pressurePa = 0.0;
    double temperatureC = 0.0;
    double depthRawM = 0.0;
    double depthFilteredM = 0.0;
    double surfacePressurePa = 0.0;
    quint32 rawAdcD1 = 0;
    quint32 rawAdcD2 = 0;
    quint32 rawTimestampUs = 0;
    quint32 attitudeTimestampUs = 0;
    quint32 depthTimestampUs = 0; // D2 read completion, device uptime us (u32 wrap).
    qint64 rawAgeMs = -1;
    qint64 attitudeAgeMs = -1; // Euler group; a raw frame cannot refresh this age.
    qint64 quaternionAgeMs = -1;
    // Received, validated frames per second over a rolling ~2 s window; -1 = unavailable.
    // Uses device timestamps, not SEQ deltas (the target shares SEQ among all stream groups).
    double imuRawRateHz = -1.0;
    double imuAttitudeRateHz = -1.0;
    ImuRateCheck imuRateCheck = ImuRateCheck::NotRequested;
    int imuRequestedRateHz = 0;
    double imuRateObservedHz = -1.0;
    QString imuRateMessage = QStringLiteral("尚未提交频率设置");
    qint64 depthAgeMs = -1; // Age since production, including USB delivery age; -1 = absent.
};

struct SensorParameterFeedback
{
    quint8 target = 0;
    quint16 parameterId = 0;
    QVariant value;
    bool confirmed = false;
    QString message; // Nonempty with invalid value: explicit read/write failure, not a cached zero.
};

} // namespace rov

Q_DECLARE_METATYPE(rov::SensorRequest)
Q_DECLARE_METATYPE(rov::SensorSnapshot)
Q_DECLARE_METATYPE(rov::SensorParameterFeedback)
