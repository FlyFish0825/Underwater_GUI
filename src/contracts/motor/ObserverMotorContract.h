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
    // Normal feedback supplies estimated bus current; debug feedback supplies Iq.
    double currentA = 0.0;
    double busVoltageV = 0.0;
    // MCU internal temperature: normal feedback uses -20..150 C full scale.
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

struct MotorCalibrationSnapshot
{
    quint8 nodeId = 0;
    bool online = false;
    // Live status follows ordinary/debug feedback and heartbeat, not result age.
    ObserverMotorNodeSnapshot motor;
    bool taskActive = false;
    quint8 taskAction = 0;
    quint16 taskSequence = 0;
    bool awaitingReply = false;
    bool replyTimedOut = false;
    quint8 requestAction = 0;
    quint16 requestSequence = 0;
    quint8 event = 0;
    quint8 action = 0;
    quint8 stage = 0;
    quint8 phase = 0;
    quint8 error = 0;
    quint8 validMask = 0;
    quint16 sequence = 0;
    quint8 hardwareError = 0;
    double rsOhm = 0.0;
    double rAbOhm = 0.0;
    double rBcOhm = 0.0;
    double rCaOhm = 0.0;
    double rAOhm = 0.0;
    double rBOhm = 0.0;
    double rCOhm = 0.0;
    double lsAbUh = 0.0;
    double lsBcUh = 0.0;
    double lsCaUh = 0.0;
    // Reserved for the next identification family; no protocol value yet.
    double fluxLinkageWb = 0.0;
    DataStamp stamp;

    quint8 requiredValidMask() const
    {
        switch (action)
        {
        case 0x01: return 0x01;
        case 0x02: return 0x0E;
        case 0x03: return 0x02;
        case 0x04: return 0x04;
        case 0x05: return 0x08;
        case 0x08: return 0x0F;
        default: return 0;
        }
    }

    bool targetCompleted() const
    {
        const quint8 required = requiredValidMask();
        return event == 0x02 && error == 0 && required != 0
               && (validMask & required) == required;
    }
};

} // namespace rov

Q_DECLARE_METATYPE(rov::ObserverMotorNodeSnapshot)
Q_DECLARE_METATYPE(rov::ObserverMotorFleetSnapshot)
Q_DECLARE_METATYPE(rov::MotorCalibrationSnapshot)
