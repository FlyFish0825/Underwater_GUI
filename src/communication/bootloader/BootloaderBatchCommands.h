#pragma once

#include "communication/bootloader/BootloaderTypes.h"
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <functional>

namespace rov
{
// 一轮向全部目标发送，再统一等待回包；离线节点不会拖住其他节点的发送。
class BootloaderBatchCommands final : public QObject
{
    Q_OBJECT
  public:
    using Sender = std::function<bool(quint8, BootCommand, quint8)>;
    explicit BootloaderBatchCommands(Sender sender, QObject *parent = nullptr);
    bool start(const QVector<quint8> &targets, const QVector<BootCommand> &commands);
    bool isRunning() const { return m_running; }
    void handleResponse(const BootResponse &response);
    void cancel();

  signals:
    void nodeResult(quint8 target, const QString &message);
    void finished(const QString &message);

  private:
    void sendRound();
    void advance();
    void finish();
    Sender m_sender;
    QTimer m_timer;
    QVector<quint8> m_targets;
    QVector<BootCommand> m_commands;
    QSet<quint8> m_pending;
    int m_round = 0;
    int m_confirmed = 0;
    int m_failed = 0;
    int m_unconfirmed = 0;
    bool m_running = false;
    bool m_sending = false;
};
}
