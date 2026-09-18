#include "data/store/FirmwareHistoryStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace rov
{

FirmwareHistoryStore::FirmwareHistoryStore()
{
    // 历史记录与程序放在一起，便于携带整个程序目录或直接查看数据文件。
    const QString directory = QCoreApplication::applicationDirPath();
    m_filePath = QDir(directory).filePath(QStringLiteral("firmware_history.json"));

    // 兼容旧版本：首次切换目录时，把原 AppData 文件迁移到程序目录。
    if (!QFile::exists(m_filePath))
    {
        const QString legacyDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const QString legacyPath = QDir(legacyDirectory).filePath(QStringLiteral("firmware_history.json"));
        if (QFile::exists(legacyPath))
            QFile::copy(legacyPath, m_filePath);
    }
}

QVector<FirmwareHistoryEntry> FirmwareHistoryStore::load() const
{
    QVector<FirmwareHistoryEntry> entries;
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return entries;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isArray())
        return entries;
    for (const auto value : document.array())
    {
        const QJsonObject object = value.toObject();
        FirmwareHistoryEntry entry;
        entry.timestamp = QDateTime::fromString(object.value(QStringLiteral("timestamp")).toString(),
                                                Qt::ISODateWithMs);
        entry.message = object.value(QStringLiteral("message")).toString();
        if (entry.timestamp.isValid() && !entry.message.isEmpty())
            entries.append(entry);
    }
    return entries;
}

void FirmwareHistoryStore::append(const QString &message)
{
    auto entries = load();
    entries.append({QDateTime::currentDateTime(), message});
    constexpr int maxEntries = 2000;
    while (entries.size() > maxEntries)
        entries.removeFirst();
    QJsonArray array;
    for (const auto &entry : entries)
    {
        QJsonObject object;
        object.insert(QStringLiteral("timestamp"), entry.timestamp.toString(Qt::ISODateWithMs));
        object.insert(QStringLiteral("message"), entry.message);
        array.append(object);
    }
    QFile file(m_filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
}

bool FirmwareHistoryStore::clear()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    file.write("[]\n");
    return file.error() == QFile::NoError;
}

QString FirmwareHistoryStore::filePath() const
{
    return m_filePath;
}

} // namespace rov
