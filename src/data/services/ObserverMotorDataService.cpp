#include "data/services/ObserverMotorDataService.h"

#include "communication/protocol/ObserverMotorProtocol.h"

#include <QDateTime>

namespace rov
{

namespace
{

constexpr int nodeIndex(const quint8 nodeId)
{
    return static_cast<int>(nodeId) - 1;
}

DataStamp validStamp()
{
    DataStamp stamp;
    stamp.validity = DataValidity::Valid;
    stamp.freshness = DataFreshness::Fresh;
    return stamp;
}

} // namespace

ObserverMotorDataService::ObserverMotorDataService(QObject *parent) : QObject(parent)
{
    m_publishTimer.setInterval(50);
    m_publishTimer.setSingleShot(false);
    connect(&m_publishTimer, &QTimer::timeout, this, &ObserverMotorDataService::publishPending);
    m_publishTimer.start();

    m_freshnessTimer.setInterval(250);
    m_freshnessTimer.setSingleShot(false);
    connect(&m_freshnessTimer, &QTimer::timeout, this,
            &ObserverMotorDataService::refreshFreshness);
    m_freshnessTimer.start();
    reset();
}

void ObserverMotorDataService::reset()
{
    m_nodes.clear();
    m_lastSeenMs.clear();
    m_dirtyNodes.clear();
    m_dirtyCalibrationNodes.clear();
    m_calibrations.clear();
    m_calibrationRequestMs.clear();
    for (quint8 nodeId = ObserverMotorProtocol::kFirstNodeId;
         nodeId <= ObserverMotorProtocol::kLastNodeId; ++nodeId)
    {
        ObserverMotorNodeSnapshot node;
        node.nodeId = nodeId;
        m_nodes.append(node);
        m_lastSeenMs.append(-1);
        MotorCalibrationSnapshot calibration;
        calibration.nodeId = nodeId;
        m_calibrations.append(calibration);
        m_calibrationRequestMs.append(-1);
    }
    ObserverMotorFleetSnapshot fleet;
    fleet.nodes = m_nodes;
    emit snapshotChanged(fleet);
    for (const auto &calibration : m_calibrations)
        emit calibrationSnapshotChanged(calibration.nodeId, calibration);
}

bool ObserverMotorDataService::handleCanFrame(const CanGatewayFrame &frame)
{
    ObserverMotorProtocol::DecodedFrame decoded;
    QString error;
    if (!ObserverMotorProtocol::decode(frame, decoded, &error))
    {
        if (!error.isEmpty())
            emit protocolError(error);
        return error.isEmpty() ? false : true;
    }

    if (decoded.kind == ObserverMotorProtocol::FrameKind::ReservedReply)
        return true;

    const quint8 nodeId = decoded.nodeId;
    if (nodeId < ObserverMotorProtocol::kFirstNodeId
        || nodeId > ObserverMotorProtocol::kLastNodeId)
        return false;
    ObserverMotorNodeSnapshot &node = m_nodes[nodeIndex(nodeId)];
    node.nodeId = nodeId;
    node.online = true;
    node.stamp = validStamp();
    m_lastSeenMs[nodeIndex(nodeId)] = QDateTime::currentMSecsSinceEpoch();

    if (decoded.kind == ObserverMotorProtocol::FrameKind::Feedback)
    {
        const auto &feedback = decoded.feedback;
        node.debugMode = false;
        node.state = ObserverMotorProtocol::stateText(feedback.state);
        node.speedRpm = feedback.speedRpm;
        node.currentA = feedback.busCurrentA;
        node.busVoltageV = feedback.busVoltageV;
        node.temperatureC = feedback.temperatureC;
        node.currentCalibrationDone = feedback.currentCalibrationDone;
        node.speedLoopEnabled = feedback.speedLoopEnabled;
        node.voltageLimited = feedback.voltageLimited;
        node.feedbackSequence = feedback.sequence;
    }
    else if (decoded.kind == ObserverMotorProtocol::FrameKind::Heartbeat)
    {
        const auto &heartbeat = decoded.heartbeat;
        node.debugMode = heartbeat.debugMode;
        node.state = ObserverMotorProtocol::stateText(heartbeat.state);
        node.temperatureC = heartbeat.temperatureC;
        node.currentCalibrationDone = heartbeat.currentCalibrationDone;
        node.voltageLimited = heartbeat.voltageLimited;
        node.feedbackSequence = heartbeat.feedbackSequence;
    }
    else if (decoded.kind == ObserverMotorProtocol::FrameKind::Debug)
    {
        const auto &debug = decoded.debug;
        node.debugMode = true;
        node.state = ObserverMotorProtocol::stateText(debug.state);
        node.speedRpm = debug.speedRpm;
        node.currentA = debug.iqA;
        node.busVoltageV = debug.busVoltageV;
        node.temperatureC = debug.temperatureC;
        node.pllElectricalSpeedRadPerSec = debug.pllElectricalSpeedRadPerSec;
        node.phaseCurrentU_A = debug.phaseCurrentU_A;
        node.phaseCurrentV_A = debug.phaseCurrentV_A;
        node.phaseCurrentW_A = debug.phaseCurrentW_A;
        node.idA = debug.idA;
        node.udV = debug.udV;
        node.uqV = debug.uqV;
        node.observerElectricalAngleDeg = debug.observerElectricalAngleDeg;
        node.debugStatusFlags = debug.statusFlags;
        node.currentCalibrationDone = (debug.statusFlags & 0x01U) != 0;
        node.speedLoopEnabled = (debug.statusFlags & 0x02U) != 0;
        node.voltageLimited = (debug.statusFlags & 0x04U) != 0;
        node.feedbackSequence = debug.sequence;
    }
    else if (decoded.kind == ObserverMotorProtocol::FrameKind::Calibration)
    {
        const auto &calibration = decoded.calibration;
        MotorCalibrationSnapshot &snapshot = m_calibrations[nodeIndex(nodeId)];
        snapshot.nodeId = nodeId;
        snapshot.online = true;
        snapshot.event = calibration.event;
        snapshot.action = calibration.action;
        snapshot.stage = calibration.stage;
        snapshot.phase = calibration.phase;
        snapshot.error = calibration.error;
        snapshot.validMask = calibration.validMask;
        snapshot.sequence = calibration.sequence;
        snapshot.hardwareError = calibration.hardwareError;
        snapshot.rsOhm = calibration.rsOhm;
        snapshot.rAbOhm = calibration.rAbOhm;
        snapshot.rBcOhm = calibration.rBcOhm;
        snapshot.rCaOhm = calibration.rCaOhm;
        snapshot.rAOhm = calibration.rAOhm;
        snapshot.rBOhm = calibration.rBOhm;
        snapshot.rCOhm = calibration.rCOhm;
        snapshot.lsAbUh = calibration.lsAbUh;
        snapshot.lsBcUh = calibration.lsBcUh;
        snapshot.lsCaUh = calibration.lsCaUh;
        snapshot.stamp = validStamp();
        if (snapshot.awaitingReply && snapshot.requestSequence == calibration.sequence
            && snapshot.requestAction == calibration.action)
        {
            snapshot.awaitingReply = false;
            snapshot.replyTimedOut = false;
        }
        if (calibration.event == 1U || calibration.event == 7U)
        {
            snapshot.taskActive = true;
            snapshot.taskAction = calibration.action;
            snapshot.taskSequence = calibration.sequence;
        }
        else if ((calibration.event == 2U || calibration.event == 4U || calibration.event == 5U)
                 && calibration.sequence == snapshot.taskSequence
                 && calibration.action == snapshot.taskAction)
        {
            snapshot.taskActive = false;
        }
        // Stop has two acknowledgments. A stop confirmation also resolves a
        // lost original STOPPED; a RAM read must not replace the task identity.
        if (calibration.event == 2U && calibration.action == 6U && calibration.error == 0U)
            snapshot.taskActive = false;
        if (calibration.event == 6U)
            snapshot.taskActive = calibration.stage != 0U;
        markCalibrationDirty(nodeId);
    }
    else
    {
        return false;
    }

    markDirty(nodeId);
    return true;
}

ObserverMotorNodeSnapshot ObserverMotorDataService::nodeSnapshot(const quint8 nodeId) const
{
    if (nodeId < ObserverMotorProtocol::kFirstNodeId
        || nodeId > ObserverMotorProtocol::kLastNodeId)
        return {};
    return m_nodes.at(nodeIndex(nodeId));
}

MotorCalibrationSnapshot ObserverMotorDataService::calibrationSnapshot(const quint8 nodeId) const
{
    if (nodeId < ObserverMotorProtocol::kFirstNodeId
        || nodeId > ObserverMotorProtocol::kLastNodeId)
        return {};
    auto result = m_calibrations.at(nodeIndex(nodeId));
    result.motor = m_nodes.at(nodeIndex(nodeId));
    result.online = result.motor.online;
    return result;
}

QVector<MotorCalibrationSnapshot> ObserverMotorDataService::calibrationSnapshots() const
{
    QVector<MotorCalibrationSnapshot> results;
    for (const auto &value : m_calibrations)
        results.append(calibrationSnapshot(value.nodeId));
    return results;
}

bool ObserverMotorDataService::canRequestCalibration(const quint8 nodeId, const quint8 action,
                                                     QString *error) const
{
    if (error != nullptr)
        error->clear();
    if (nodeId < 1U || nodeId > 8U || action < 1U || action > 8U)
    {
        if (error != nullptr) *error = QStringLiteral("辨识节点或操作无效");
        return false;
    }
    const auto value = calibrationSnapshot(nodeId);
    // Stop remains accessible while awaiting acceptance or terminal events.
    if (action == 6U)
        return true;
    if (value.awaitingReply || (action != 7U && (value.taskActive || value.replyTimedOut)))
    {
        if (error != nullptr)
            *error = QStringLiteral("节点有待确认请求或活动任务，请先读取结果或停止辨识");
        return false;
    }
    return true;
}

void ObserverMotorDataService::noteCalibrationRequest(const quint8 nodeId, const quint8 action,
                                                    const quint16 sequence)
{
    if (nodeId < 1U || nodeId > 8U)
        return;
    auto &value = m_calibrations[nodeIndex(nodeId)];
    value.awaitingReply = true;
    value.replyTimedOut = false;
    value.requestAction = action;
    value.requestSequence = sequence;
    m_calibrationRequestMs[nodeIndex(nodeId)] = QDateTime::currentMSecsSinceEpoch();
    markCalibrationDirty(nodeId);
}

ObserverMotorFleetSnapshot ObserverMotorDataService::snapshot() const
{
    ObserverMotorFleetSnapshot result;
    result.nodes = m_nodes;
    for (const auto &node : m_nodes)
    {
        if (node.stamp.validity == DataValidity::Valid)
        {
            result.stamp = node.stamp;
            break;
        }
    }
    return result;
}

void ObserverMotorDataService::markDirty(const quint8 nodeId)
{
    m_dirtyNodes.insert(nodeId);
}

void ObserverMotorDataService::markCalibrationDirty(const quint8 nodeId)
{
    m_dirtyCalibrationNodes.insert(nodeId);
}

void ObserverMotorDataService::publishPending()
{
    if (m_dirtyNodes.isEmpty() && m_dirtyCalibrationNodes.isEmpty())
        return;
    const QSet<quint8> dirty = m_dirtyNodes;
    const QSet<quint8> dirtyCalibration = m_dirtyCalibrationNodes;
    m_dirtyNodes.clear();
    m_dirtyCalibrationNodes.clear();
    for (const quint8 nodeId : dirty)
        emit nodeSnapshotChanged(nodeId, nodeSnapshot(nodeId));
    emit snapshotChanged(snapshot());
    for (const quint8 nodeId : dirtyCalibration)
        emit calibrationSnapshotChanged(nodeId, calibrationSnapshot(nodeId));
}

void ObserverMotorDataService::refreshFreshness()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (int index = 0; index < m_nodes.size(); ++index)
    {
        auto &calibration = m_calibrations[index];
        if (calibration.awaitingReply && nowMs - m_calibrationRequestMs.at(index) > 2500)
        {
            calibration.awaitingReply = false;
            calibration.replyTimedOut = true;
            markCalibrationDirty(calibration.nodeId);
        }
        if (!m_nodes[index].online || m_lastSeenMs.at(index) < 0
            || nowMs - m_lastSeenMs.at(index) <= 2500)
            continue;
        m_nodes[index].online = false;
        m_nodes[index].stamp.freshness = DataFreshness::Offline;
        m_nodes[index].stamp.reason = QStringLiteral("超过 2.5 秒未收到节点心跳或反馈");
        m_calibrations[index].stamp.freshness = DataFreshness::Offline;
        markDirty(m_nodes.at(index).nodeId);
    }
}

} // namespace rov
