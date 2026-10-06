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

struct SensorRequest
{
    quint8 target = kImuSensor;
    SensorOperation operation = SensorOperation::GetInfo;
    quint16 parameterId = 0;
    QVariant value;
    quint8 calibrationType = 1;
    quint8 calibrationAction = 1;
    double referenceTemperatureC = 25.0;
};

struct SensorDeviceState
{
    bool infoKnown = false;
    bool online = false;
    bool pending = false;
    quint8 model = 0;
    quint32 capabilities = 0;
    quint32 status = 0;
    quint32 goodFrames = 0;
    quint32 errors = 0;
    quint32 sequence = 0;
    QString name = QStringLiteral("--");
    QString firmware = QStringLiteral("--");
    QString lastCommand = QStringLiteral("尚未发送命令");
};

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
    bool pressureValid = false;
    bool temperatureValid = false;
    bool depthValid = false;
    bool zeroValid = false;
    double pressurePa = 0.0;
    double temperatureC = 0.0;
    double depthRawM = 0.0;
    double depthFilteredM = 0.0;
    double surfacePressurePa = 0.0;
    quint32 rawAdcD1 = 0;
    quint32 rawAdcD2 = 0;
    quint32 rawTimestampUs = 0;
    quint32 attitudeTimestampUs = 0;
    quint32 depthTimestampUs = 0;
    qint64 rawAgeMs = -1;
    qint64 attitudeAgeMs = -1;
    qint64 depthAgeMs = -1;
};

struct SensorParameterFeedback
{
    quint8 target = 0;
    quint16 parameterId = 0;
    QVariant value;
    bool confirmed = false;
};

} // namespace rov

Q_DECLARE_METATYPE(rov::SensorRequest)
Q_DECLARE_METATYPE(rov::SensorSnapshot)
Q_DECLARE_METATYPE(rov::SensorParameterFeedback)
