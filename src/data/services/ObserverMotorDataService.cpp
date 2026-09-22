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
    for (quint8 nodeId = ObserverMotorProtocol::kFirstNodeId;
         nodeId <= ObserverMotorProtocol::kLastNodeId; ++nodeId)
    {
        ObserverMotorNodeSnapshot node;
        node.nodeId = nodeId;
        m_nodes.append(node);
        m_lastSeenMs.append(-1);
    }
    ObserverMotorFleetSnapshot fleet;
    fleet.nodes = m_nodes;
    emit snapshotChanged(fleet);
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

void ObserverMotorDataService::publishPending()
{
    if (m_dirtyNodes.isEmpty())
        return;
    const QSet<quint8> dirty = m_dirtyNodes;
    m_dirtyNodes.clear();
    for (const quint8 nodeId : dirty)
        emit nodeSnapshotChanged(nodeId, nodeSnapshot(nodeId));
    emit snapshotChanged(snapshot());
}

void ObserverMotorDataService::refreshFreshness()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (int index = 0; index < m_nodes.size(); ++index)
    {
        if (!m_nodes[index].online || m_lastSeenMs.at(index) < 0
            || nowMs - m_lastSeenMs.at(index) <= 2500)
            continue;
        m_nodes[index].online = false;
        m_nodes[index].stamp.freshness = DataFreshness::Offline;
        m_nodes[index].stamp.reason = QStringLiteral("超过 2.5 秒未收到节点心跳或反馈");
        markDirty(m_nodes.at(index).nodeId);
    }
}

} // namespace rov
