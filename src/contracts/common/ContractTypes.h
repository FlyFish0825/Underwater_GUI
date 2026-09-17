#pragma once

#include <QDateTime>
#include <QString>

namespace rov
{

enum class DataFreshness
{
    Unknown,
    Fresh,
    Stale,
    Offline
};

enum class DataValidity
{
    Unknown,
    Valid,
    Invalid
};

struct DataStamp
{
    DataValidity validity = DataValidity::Unknown;
    DataFreshness freshness = DataFreshness::Unknown;
    quint32 timestampUs = 0;
    QString reason;
};

struct DemoContext
{
    bool isDemo = true;
    QString label = QStringLiteral("DEMO • NO DEVICE CONNECTION");
    QDateTime lastUpdate =
        QDateTime::fromString(QStringLiteral("2026-09-16T18:21:04"), Qt::ISODate);
};

} // namespace rov
