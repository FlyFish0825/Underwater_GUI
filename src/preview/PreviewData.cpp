#include "preview/PreviewData.h"

#include <QtMath>

namespace
{

rov::DataStamp freshStamp(const quint32 timestampUs = 182104000)
{
    rov::DataStamp stamp;
    stamp.validity = rov::DataValidity::Valid;
    stamp.freshness = rov::DataFreshness::Fresh;
    stamp.timestampUs = timestampUs;
    return stamp;
}

rov::DataStamp staleStamp()
{
    rov::DataStamp stamp = freshStamp(181904000);
    stamp.freshness = rov::DataFreshness::Stale;
    stamp.reason = QStringLiteral("演示样本已过期");
    return stamp;
}

} // namespace

namespace rov
{

DashboardSnapshot dashboardPreview()
{
    DashboardSnapshot snapshot;
    snapshot.systemStamp = freshStamp();
    snapshot.connected = false;
    snapshot.canControl = false;
    snapshot.controlUnavailableReason = QStringLiteral("仅演示：未连接设备");
    snapshot.depthM = 12.4;
    snapshot.depthValid = true;
    snapshot.rollDeg = 0.5;
    snapshot.attitudeValid = true;
    snapshot.pitchDeg = -1.2;
    snapshot.yawDeg = 178.6;
    snapshot.busVoltageV = 24.1;
    snapshot.busVoltageValid = true;
    snapshot.internalTemperatureC = 28.3;
    snapshot.internalTemperatureValid = true;
    snapshot.robotMode = QStringLiteral("手动");
    snapshot.armed = true;
    snapshot.leakDetected = false;
    snapshot.thrustLimitPercent = 70;
    snapshot.alarmCount = 0;
    const QStringList ids = {QStringLiteral("thruster1"), QStringLiteral("thruster2"),
                             QStringLiteral("thruster3"), QStringLiteral("thruster4"),
                             QStringLiteral("thruster5"), QStringLiteral("thruster6"),
                             QStringLiteral("thruster7"), QStringLiteral("thruster8")};
    const QStringList labels = {
        QStringLiteral("T1 · 前左 · 水平"), QStringLiteral("T2 · 前右 · 水平"),
        QStringLiteral("T3 · 后左 · 水平"), QStringLiteral("T4 · 后右 · 水平"),
        QStringLiteral("T5 · 前左 · 垂向"), QStringLiteral("T6 · 前右 · 垂向"),
        QStringLiteral("T7 · 后左 · 垂向"), QStringLiteral("T8 · 后右 · 垂向")};
    const double rpm[] = {1200.0, 1180.0, 950.0, 960.0, 720.0, 715.0, 705.0, 710.0};
    const double current[] = {1.2, 1.1, 0.9, 0.9, 0.8, 0.8, 0.8, 0.8};
    const double temperature[] = {26.0, 25.0, 24.0, 24.0, 25.0, 25.0, 24.0, 24.0};
    for (int i = 0; i < kDashboardThrusterCount; ++i)
    {
        ThrusterTelemetry item;
        item.id = ids.at(i);
        item.label = labels.at(i);
        item.rpm = rpm[i];
        item.currentA = current[i];
        item.temperatureC = temperature[i];
        item.status = QStringLiteral("在线");
        item.stamp = freshStamp();
        snapshot.thrusters.append(item);
    }
    return snapshot;
}

MotorDebugSnapshot motorDebugPreview()
{
    MotorDebugSnapshot snapshot;
    snapshot.motorStamp = freshStamp();
    snapshot.selectedMotorId = QStringLiteral("thruster_fl");
    snapshot.selectedMotorLabel = QStringLiteral("推进器 1（FL）");
    snapshot.state = QStringLiteral("运行中");
    snapshot.rpm = 1082.0;
    snapshot.currentA = 1.1;
    snapshot.voltageV = 24.1;
    snapshot.temperatureC = 25.3;
    snapshot.fault = QStringLiteral("无");
    snapshot.currentKp = 0.20;
    snapshot.currentKi = 0.05;
    snapshot.observerGain = 0.10;
    snapshot.currentLimitA = 20.0;
    const QStringList ids = {
        QStringLiteral("phase_current_u"), QStringLiteral("phase_current_v"),
        QStringLiteral("phase_current_w"), QStringLiteral("bus_voltage"),
        QStringLiteral("speed_rpm"),       QStringLiteral("pll_electrical_speed")};
    const QStringList names = {QStringLiteral("相电流 U"),  QStringLiteral("相电流 V"),
                               QStringLiteral("相电流 W"),  QStringLiteral("母线电压"),
                               QStringLiteral("转速"),      QStringLiteral("PLL 电速度")};
    const QStringList units = {QStringLiteral("A"), QStringLiteral("A"),   QStringLiteral("A"),
                               QStringLiteral("V"), QStringLiteral("rpm"), QStringLiteral("rad/s")};
    for (int channel = 0; channel < ids.size(); ++channel)
    {
        DebugSeries series;
        series.id = ids.at(channel);
        series.name = names.at(channel);
        series.unit = units.at(channel);
        series.sampleRateHz = 24.0;
        series.firstTimestampUs = 181904000;
        series.stamp = freshStamp();
        for (int i = 0; i < 180; ++i)
        {
            const double t = static_cast<double>(i) / 24.0;
            double value = 0.0;
            if (channel < 3)
            {
                const double phase = channel * 2.0 * M_PI / 3.0;
                value = 35.0 + 13.0 * qSin(t * 2.0 * M_PI + phase);
            }
            else if (channel == 3)
            {
                value = 35.0 + ((i / 18) % 2) * 13.0;
            }
            else if (channel == 4)
            {
                value = 1082.0 + 65.0 * qSin(t * 1.5) + 8.0 * qSin(t * 9.0);
            }
            else if (channel == 5)
            {
                value = 113.0 + 4.0 * qSin(t * 1.5);
            }
            series.samples.append(value);
        }
        snapshot.series.append(series);
    }
    snapshot.recentLog = QStringList()
                         << QStringLiteral("18:20:12  开始采集（10 秒）")
                         << QStringLiteral("18:19:45  参数已更新：Kp=0.20，Ki=0.05")
                         << QStringLiteral("18:19:02  电机已启动：推进器 1（FL）")
                         << QStringLiteral("18:17:02  采集已保存：preview_capture.csv");
    return snapshot;
}

FirmwareSnapshot firmwarePreview()
{
    FirmwareSnapshot snapshot;
    snapshot.fileName = QStringLiteral("rov_firmware_v2.1.0.bin");
    snapshot.fileVersion = QStringLiteral("v2.1.0");
    snapshot.fileSize = QStringLiteral("1.24 MB (1,302,528 bytes)");
    snapshot.checksum = QStringLiteral("a3f5e8c1d2b4…e7f9a2d3c");
    snapshot.fileDescription = QStringLiteral("演示固件元数据。不执行刷写操作。");
    const QStringList ids = {QStringLiteral("0x01"), QStringLiteral("0x02"), QStringLiteral("0x03"),
                             QStringLiteral("0x04"), QStringLiteral("0x05"), QStringLiteral("0x06"),
                             QStringLiteral("0x07"), QStringLiteral("0x08")};
    const QStringList names = {
        QStringLiteral("推进器 1（左前）"),   QStringLiteral("推进器 2（右前）"),
        QStringLiteral("推进器 3（左后）"),   QStringLiteral("推进器 4（右后）"),
        QStringLiteral("推进器 5（内左前）"), QStringLiteral("推进器 6（内右前）"),
        QStringLiteral("推进器 7（内左后）"), QStringLiteral("推进器 8（内右后）")};
    for (int i = 0; i < ids.size(); ++i)
    {
        FirmwareNode node;
        node.nodeId = ids.at(i);
        node.deviceName = names.at(i);
        node.currentVersion =
            i == 2 || i == 4 ? QStringLiteral("v2.0.0") : QStringLiteral("v2.0.1");
        node.targetVersion = QStringLiteral("v2.1.0");
        node.online = i != 5;
        node.state = i == 0 ? FirmwareState::Completed
                            : (i == 1 ? FirmwareState::Programming : FirmwareState::Idle);
        node.progressPercent = i == 0 ? 100 : (i == 1 ? 65 : 0);
        snapshot.nodes.append(node);
    }
    snapshot.log =
        QStringList() << QStringLiteral("[18:20:14] 信息  已选择固件：rov_firmware_v2.1.0.bin")
                      << QStringLiteral("[18:20:15] 信息  已选择 2 个演示节点")
                      << QStringLiteral("[18:20:17] 演示  未连接设备；仅记录请求");
    return snapshot;
}

ManipulatorSnapshot manipulatorPreview()
{
    ManipulatorSnapshot snapshot;
    snapshot.armStamp = freshStamp();
    snapshot.systemState = QStringLiteral("就绪");
    snapshot.controlMode = QStringLiteral("位置控制");
    snapshot.faultStatus = QStringLiteral("无故障");
    snapshot.canControl = false;
    snapshot.controlUnavailableReason = QStringLiteral("仅演示：未连接设备");
    const QStringList ids = {QStringLiteral("shoulder_pitch"), QStringLiteral("elbow_pitch"),
                             QStringLiteral("wrist_roll"), QStringLiteral("gripper")};
    const QStringList labels = {QStringLiteral("肩部"), QStringLiteral("肘部"),
                                QStringLiteral("腕部"), QStringLiteral("夹爪")};
    const double target[] = {12.4, -35.2, 18.6, 75.0};
    const double actual[] = {12.1, -35.0, 18.3, 75.0};
    const double min[] = {-90.0, -120.0, -180.0, 0.0};
    const double max[] = {90.0, 120.0, 180.0, 100.0};
    for (int i = 0; i < 4; ++i)
    {
        JointState joint;
        joint.id = ids.at(i);
        joint.label = labels.at(i);
        joint.targetDeg = target[i];
        joint.actualDeg = actual[i];
        joint.minDeg = min[i];
        joint.maxDeg = max[i];
        joint.currentA = i == 1 ? 1.4 : (i == 2 ? 0.8 : (i == 3 ? 0.4 : 1.2));
        joint.temperatureC = i == 1 ? 27.0 : (i == 2 ? 25.0 : (i == 3 ? 24.0 : 26.0));
        joint.enabled = true;
        joint.stamp = freshStamp();
        snapshot.joints.append(joint);
    }
    return snapshot;
}

VisionSnapshot visionPreview()
{
    VisionSnapshot snapshot;
    snapshot.cameraStamp = staleStamp();
    snapshot.cameraDevice = QStringLiteral("-");
    snapshot.resolution = QStringLiteral("-");
    snapshot.frameRate = QStringLiteral("-");
    snapshot.pixelFormat = QStringLiteral("-");
    snapshot.streamState = QStringLiteral("无视频流");
    snapshot.lastFrame = QStringLiteral("-");
    snapshot.latency = QStringLiteral("- 毫秒");
    snapshot.nodeState = QStringLiteral("未运行");
    snapshot.processingMode = QStringLiteral("-");
    snapshot.modelsLoaded = QStringLiteral("-");
    snapshot.connected = false;
    return snapshot;
}

} // namespace rov
