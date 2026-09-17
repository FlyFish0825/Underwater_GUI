#pragma once

#include "contracts/common/ContractTypes.h"

#include <QMetaType>
#include <QString>
#include <QVector>

namespace rov
{

struct JointState
{
    QString id;
    QString label;
    double targetDeg = 0.0; // User target / degrees; not device feedback.
    double actualDeg = 0.0; // Feedback / degrees.
    double minDeg = -180.0;
    double maxDeg = 180.0;
    double currentA = 0.0;
    double temperatureC = 0.0;
    bool enabled = false;
    DataStamp stamp;
};

struct ManipulatorSnapshot
{
    DemoContext demo;
    DataStamp armStamp;
    QVector<JointState> joints;
    QString systemState;
    QString controlMode;
    QString faultStatus;
    bool canControl = false;
    QString controlUnavailableReason;
};

struct JointTargetRequest
{
    QString jointId;
    double target = 0.0;
};

struct GripperRequest
{
    bool open = true;
};

} // namespace rov

Q_DECLARE_METATYPE(rov::ManipulatorSnapshot)
Q_DECLARE_METATYPE(rov::JointTargetRequest)
Q_DECLARE_METATYPE(rov::GripperRequest)
