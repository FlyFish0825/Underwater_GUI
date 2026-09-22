#pragma once

#include "contracts/common/ContractTypes.h"

#include <QMetaType>
#include <QString>

namespace rov
{

struct VisionSnapshot
{
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

enum class CameraControlAction
{
    RefreshDevices,
    Start,
    Stop
};

struct CameraControlRequest
{
    CameraControlAction action = CameraControlAction::RefreshDevices;
    int deviceIndex = -1;
};

} // namespace rov

Q_DECLARE_METATYPE(rov::VisionSnapshot)
Q_DECLARE_METATYPE(rov::VisionModeRequest)
Q_DECLARE_METATYPE(rov::CameraControlRequest)
