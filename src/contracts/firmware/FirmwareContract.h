#pragma once

#include "contracts/common/ContractTypes.h"

#include <QMetaType>
#include <QString>
#include <QVector>

namespace rov
{

enum class FirmwareState
{
    Idle,
    Preparing,
    Programming,
    Verifying,
    Completed,
    Failed,
    Aborted
};

struct FirmwareNode
{
    // Stable node ID is business data; it is not a table row index.
    QString nodeId;
    QString deviceName;
    QString currentVersion;
    QString targetVersion;
    bool online = false;
    FirmwareState state = FirmwareState::Idle;
    int progressPercent = 0; // 0..100 percent.
};

struct FirmwareSnapshot
{
    DemoContext demo;
    QVector<FirmwareNode> nodes;
    QString fileName;
    QString fileVersion;
    QString fileSize;
    QString checksum;
    QString fileDescription;
    FirmwareState state = FirmwareState::Idle;
    QString resultText;
    QStringList log;
};

struct FirmwareFileRequest
{
    QString path;
};

struct FirmwareUpgradeRequest
{
    QVector<QString> nodeIds;
    QString firmwarePath;
};

} // namespace rov

Q_DECLARE_METATYPE(rov::FirmwareSnapshot)
Q_DECLARE_METATYPE(rov::FirmwareFileRequest)
Q_DECLARE_METATYPE(rov::FirmwareUpgradeRequest)
