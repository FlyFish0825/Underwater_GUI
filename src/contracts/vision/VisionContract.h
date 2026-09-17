#pragma once

#include "contracts/common/ContractTypes.h"

#include <QMetaType>
#include <QString>

namespace rov
{

struct VisionSnapshot
{
    DemoContext demo;
    DataStamp cameraStamp;
    QString cameraDevice;
    QString resolution;
    QString frameRate;
    QString pixelFormat;
    QString streamState;
    QString lastFrame;
    QString latency;
    QString nodeState;
    QString processingMode;
    QString modelsLoaded;
    bool connected = false;
};

struct VisionModeRequest
{
    QString modeId;
};

} // namespace rov

Q_DECLARE_METATYPE(rov::VisionSnapshot)
Q_DECLARE_METATYPE(rov::VisionModeRequest)
