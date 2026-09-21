#pragma once

#include "contracts/common/ContractTypes.h"

#include <QMetaType>
#include <QVector>

namespace rov
{

struct ObserverMotorNodeSnapshot
{
    quint8 nodeId = 0;
    bool online = false;
    bool debugMode = false;
    QString state = QStringLiteral("IDLE");
    qint16 speedRpm = 0;
    double iqA = 0.0;
    double busVoltageV = 0.0;
    double temperatureC = 0.0;
    bool currentCalibrationDone = false;
    bool speedLoopEnabled = false;
    bool voltageLimited = false;
    quint8 feedbackSequence = 0;
    double pllElectricalSpeedRadPerSec = 0.0;
    double phaseCurrentU_A = 0.0;
    double phaseCurrentV_A = 0.0;
    double phaseCurrentW_A = 0.0;
    double idA = 0.0;
    double udV = 0.0;
    double uqV = 0.0;
    double observerElectricalAngleDeg = 0.0;
    quint32 debugStatusFlags = 0;
    DataStamp stamp;
};

struct ObserverMotorFleetSnapshot
{
    QVector<ObserverMotorNodeSnapshot> nodes;
    DataStamp stamp;
};

} // namespace rov

Q_DECLARE_METATYPE(rov::ObserverMotorNodeSnapshot)
Q_DECLARE_METATYPE(rov::ObserverMotorFleetSnapshot)
