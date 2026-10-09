#pragma once

#include "contracts/common/ContractTypes.h"

#include <QMetaType>
#include <QImage>
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
    bool connectionRequested = false; // Includes connecting and automatic retry.
    bool frameAvailable = false; // Only a fresh, decoded frame can be saved/displayed.
    bool stereo = false;
    QString nominalFrameRate;
};

struct StereoCameraFrame
{
    QImage stitched;
    QImage left;
    QImage right;
    quint64 sequence = 0; // Nano output sequence; gaps and reset on reconnect are allowed.
    quint64 receiptNs = 0; // Nano ROS receipt time, ns; not exposure or a Windows timestamp.
};

enum class CameraSource { RobotStereo, Local };

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
    CameraSource source = CameraSource::RobotStereo;
    QString host = QStringLiteral("192.168.20.70");
    quint16 port = 9001;
};

} // namespace rov

Q_DECLARE_METATYPE(rov::VisionSnapshot)
Q_DECLARE_METATYPE(rov::VisionModeRequest)
Q_DECLARE_METATYPE(rov::CameraControlRequest)
Q_DECLARE_METATYPE(rov::StereoCameraFrame)
