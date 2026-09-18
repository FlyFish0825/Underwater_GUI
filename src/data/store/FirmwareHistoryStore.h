#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace rov
{

struct FirmwareHistoryEntry
{
    QDateTime timestamp;
    QString message;
};

/**
 * @brief 固件页历史事件的轻量本地存储。
 *
 * 历史记录写入程序可执行文件同目录下的 JSON 文件，与实时日志显示缓冲
 * 分离。程序重启后仍可通过历史记录窗口查看，清理实时日志不会删除该文件。
 */
class FirmwareHistoryStore final
{
  public:
    FirmwareHistoryStore();

    void append(const QString &message);
    bool clear();
    QVector<FirmwareHistoryEntry> load() const;
    QString filePath() const;

  private:
    QString m_filePath;
};

} // namespace rov
