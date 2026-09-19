#pragma once

#include "contracts/common/ContractTypes.h"

#include <QMetaType>
#include <QString>
#include <QVector>

namespace rov
{

struct DebugSeries
{
    // Stable channel ID from the future service metadata table.
    QString id;
    QString name;
    QString unit;
    QVector<double> samples;
    DataStamp stamp;
};

struct MotorDebugSnapshot
{
    DemoContext demo;
    DataStamp motorStamp;
    QString selectedMotorId;
    QString selectedMotorLabel;
    QString state;
    double rpm = 0.0;
    double currentA = 0.0;
    double voltageV = 0.0;
    double temperatureC = 0.0;
    QString fault;
    double currentKp = 0.20;
    double currentKi = 0.05;
    double observerGain = 0.10;
    double currentLimitA = 20.0;
    QVector<DebugSeries> series;
    QStringList recentLog;
};

struct MotorParameterRequest
{
    QString motorId;
    double currentKp = 0.0;
    double currentKi = 0.0;
    double observerGain = 0.0;
    double currentLimitA = 0.0;
};

struct MotorCaptureRequest
{
    QString motorId;
    int sampleRateHz = 1000;
    int durationSeconds = 10;
};

struct MotorSpeedControlRequest
{
    QString motorId;
    int targetRpm = 0;
    bool enabled = false;
};

} // namespace rov

Q_DECLARE_METATYPE(rov::MotorDebugSnapshot)
Q_DECLARE_METATYPE(rov::MotorParameterRequest)
Q_DECLARE_METATYPE(rov::MotorCaptureRequest)
Q_DECLARE_METATYPE(rov::MotorSpeedControlRequest)
