#pragma once

#include "contracts/dashboard/DashboardContract.h"
#include "contracts/firmware/FirmwareContract.h"
#include "contracts/manipulator/ManipulatorContract.h"
#include "contracts/motor_debug/MotorDebugContract.h"
#include "contracts/vision/VisionContract.h"

namespace rov
{

DashboardSnapshot dashboardPreview();
MotorDebugSnapshot motorDebugPreview();
FirmwareSnapshot firmwarePreview();
ManipulatorSnapshot manipulatorPreview();
VisionSnapshot visionPreview();

} // namespace rov
