#pragma once

#include "communication/protocol/CanGatewayProtocol.h"
#include "contracts/dashboard/DashboardContract.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QJsonValue>
#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>

namespace rov
{

/**
 * @brief 后台科研记录器。
 *
 * 生产者只做一次有界入队，文件格式化和磁盘写入在独立线程完成。
 * 原始 CAN 帧始终保留，解析字段作为便于分析的冗余列写入。
 */
class ResearchDataRecorder final : public QThread
{
    Q_OBJECT

  public:
    explicit ResearchDataRecorder(QObject *parent = nullptr);
    ~ResearchDataRecorder() override;

    bool startRecording(const QString &path, QString *error = nullptr);
    void stopRecording();
    bool isRecording() const;
    QString lastDirectory() const;

    void recordFrame(const CanGatewayFrame &frame, const QString &direction = QStringLiteral("rx"));
    void recordDashboardSnapshot(const DashboardSnapshot &snapshot);
    void recordControlInput(const SixDofControlRequest &request);
    void recordEvent(const QString &eventType, const QString &message);

  signals:
    void statusChanged(bool active, quint64 accepted, quint64 dropped, const QString &path);
    void errorOccurred(const QString &message);

  protected:
    void run() override;

  public:
    enum class RecordType
    {
        Frame,
        Dashboard,
        Input,
        Event
    };

  private:
    struct PendingRecord
    {
        RecordType type = RecordType::Event;
        qint64 monotonicUs = 0;
        QDateTime utc;
        QString source;
        CanGatewayFrame frame;
        DashboardSnapshot snapshot;
        SixDofControlRequest input;
        QString eventType;
        QString message;
    };

    bool enqueue(PendingRecord record);
    PendingRecord makeBaseRecord(RecordType type, const QString &source) const;
    void writeBatch(QFile &jsonl, QFile &csv, const QVector<PendingRecord> &batch, quint64 &written,
                    QString &error);
    static QJsonObject toJson(const PendingRecord &record);
    static QString csvValue(const QJsonValue &value);
    static QByteArray csvHeader();
    static QByteArray csvRow(const QJsonObject &object);

    mutable QMutex m_mutex;
    QWaitCondition m_wait;
    QQueue<PendingRecord> m_queue;
    QString m_sessionPath;
    QString m_lastDirectory;
    QDateTime m_startedUtc;
    QElapsedTimer m_elapsed;
    bool m_accepting = false;
    bool m_workerStarted = false;
    bool m_stopRequested = false;
    quint64 m_accepted = 0;
    quint64 m_dropped = 0;
    static constexpr int kQueueLimit = 20000;
};

} // namespace rov
