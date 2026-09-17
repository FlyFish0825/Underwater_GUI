#pragma once

#include "contracts/common/ContractTypes.h"

#include <QMetaType>
#include <QString>
#include <QVector>

namespace rov
{

struct ThrusterTelemetry
{
    // Stable business ID, not a table row and not a protocol target byte.
    QString id;
    QString label;
    // Feedback: revolutions per minute, display range -6000..6000 rpm.
    double rpm = 0.0;
    // Feedback: phase current, display range 0..80 A.
    double currentA = 0.0;
    // Feedback: motor temperature, display range -20..120 °C.
    double temperatureC = 0.0;
    QString status;
    DataStamp stamp;
};

struct DashboardSnapshot
{
    DemoContext demo;
    DataStamp systemStamp;
    QVector<ThrusterTelemetry> thrusters;

    // Feedback values: depth metres and attitude degrees.
    double depthM = 0.0;
    double rollDeg = 0.0;
    double pitchDeg = 0.0;
    double yawDeg = 0.0;
    double busVoltageV = 0.0;
    double internalTemperatureC = 0.0;
    QString robotMode;
    bool armed = false;
    bool leakDetected = false;
    bool connected = false;
    bool canControl = false;
    QString controlUnavailableReason;
    int thrustLimitPercent = 70;
    int alarmCount = 0;
    QVector<QString> alarms;
};

struct SixDofControlRequest
{
    // User intent only. Ranges are normalized -1.0..1.0; no allocation here.
    double surge = 0.0;
    double sway = 0.0;
    double heave = 0.0;
    double roll = 0.0;
    double pitch = 0.0;
    double yaw = 0.0;
};

struct ThrustLimitRequest
{
    int percent = 70; // User intent, range 0..100 percent.
};

} // namespace rov

Q_DECLARE_METATYPE(rov::DashboardSnapshot)
Q_DECLARE_METATYPE(rov::SixDofControlRequest)
Q_DECLARE_METATYPE(rov::ThrustLimitRequest)
