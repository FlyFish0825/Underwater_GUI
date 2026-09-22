#pragma once

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

} // namespace rov
