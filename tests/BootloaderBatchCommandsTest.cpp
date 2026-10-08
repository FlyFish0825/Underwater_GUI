#include "communication/bootloader/BootloaderBatchCommands.h"
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

using namespace rov;

namespace
{
bool require(bool condition, const char *message)
{
    if (!condition)
        qCritical() << message;
    return condition;
}
BootResponse response(quint8 node, BootCommand command)
{
    BootResponse value;
    value.nodeId = node;
    value.command = command;
    value.status = BootStatus::Idle;
    value.data = QByteArray::fromHex("01020304");
    return value;
}
void waitFinished(BootloaderBatchCommands &batch, int maximumMs)
{
    QEventLoop loop;
    QObject::connect(&batch, &BootloaderBatchCommands::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(maximumMs, &loop, &QEventLoop::quit);
    loop.exec();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QVector<quint8> sent;
    QVector<BootCommand> commands;
    QVector<quint8> modes;
    BootloaderBatchCommands batch([&](quint8 node, BootCommand command, quint8 mode) {
        sent.append(node);
        commands.append(command);
        modes.append(mode);
        return true;
    });
    QString result;
    QObject::connect(&batch, &BootloaderBatchCommands::finished,
                     [&](const QString &message) { result = message; });
    const QVector<quint8> all = {1, 2, 3, 4, 5, 6, 7, 8};
    if (!require(batch.start(all, {BootCommand::GetVersion}), "start all nodes")
        || !require(sent == all, "all 8 sends precede any response or timeout")
        || !require(!batch.start({1}, {BootCommand::Reset}), "reject overlap"))
        return 1;
    batch.handleResponse(response(1, BootCommand::GetStatus));
    batch.handleResponse(response(9, BootCommand::GetVersion));
    auto shortReply = response(1, BootCommand::GetVersion);
    shortReply.data.clear();
    batch.handleResponse(shortReply);
    for (int i = 8; i >= 2; --i)
        batch.handleResponse(response(static_cast<quint8>(i), BootCommand::GetVersion));
    if (!require(batch.isRunning(), "wrong command, unknown node and short reply must not finish"))
        return 1;
    batch.handleResponse(response(1, BootCommand::GetVersion));
    if (!require(!batch.isRunning() && result.contains(QStringLiteral("响应确认 8 项")),
                 "out-of-order replies update all nodes"))
        return 1;
    if (!require(!batch.start({1, 1}, {BootCommand::Reset}), "duplicate target rejected")
        || !require(!batch.start({0}, {BootCommand::Reset}), "broadcast rejected")
        || !require(!batch.start({}, {BootCommand::Reset}), "empty selection rejected")
        || !require(!batch.start(all, {BootCommand::Erase}), "erase excluded from batch"))
        return 1;
    sent.clear();
    batch.start({2, 8}, {BootCommand::JumpApp});
    if (!require(sent == QVector<quint8>({2, 8}) && modes.last() == 1,
                 "subset and protected Trial jump"))
        return 1;
    batch.cancel();
    batch.handleResponse(response(2, BootCommand::JumpApp));
    if (!require(!batch.isRunning(), "cancel ignores late response"))
        return 1;

    sent.clear();
    batch.start({1, 8}, {BootCommand::GetVersion, BootCommand::GetInfo, BootCommand::GetStatus});
    for (BootCommand command : {BootCommand::GetVersion, BootCommand::GetInfo, BootCommand::GetStatus})
    {
        batch.handleResponse(response(8, command));
        batch.handleResponse(response(1, command));
    }
    if (!require(sent.size() == 6 && !batch.isRunning(), "refresh advances through all query rounds"))
        return 1;

    batch.start({1, 8}, {BootCommand::GetVersion});
    auto error = response(1, BootCommand::GetVersion);
    error.status = BootStatus::Error;
    error.errorCode = 3;
    batch.handleResponse(error);
    batch.handleResponse(error); // 重复回包不能重复计数。
    waitFinished(batch, 3000);
    if (!require(!batch.isRunning() && result.contains(QStringLiteral("失败 1 项"))
                 && result.contains(QStringLiteral("未确认 1 项")), "error and timeout counted independently"))
        return 1;

    sent.clear();
    commands.clear();
    batch.start(all, {BootCommand::EnterBoot});
    QTimer::singleShot(1300, &batch, [&]() {
        for (quint8 node : all)
            batch.handleResponse(response(node, BootCommand::GetVersion));
    });
    waitFinished(batch, 3500);
    if (!require(sent.size() == 16 && commands.at(8) == BootCommand::GetVersion
                 && !batch.isRunning() && result.contains(QStringLiteral("响应确认 8 项")),
                 "enter Boot probes all 8 after reboot without waiting for nonexistent ACK"))
        return 1;

    int attempts = 0;
    BootloaderBatchCommands failed([&](quint8, BootCommand, quint8) { ++attempts; return false; });
    failed.start(all, {BootCommand::GetVersion});
    if (!require(attempts == 8 && !failed.isRunning(), "failed node must not block other sends"))
        return 1;
    BootloaderBatchCommands *syncPtr = nullptr;
    BootloaderBatchCommands synchronous([&](quint8 node, BootCommand command, quint8) {
        syncPtr->handleResponse(response(node, command));
        return true;
    });
    syncPtr = &synchronous;
    synchronous.start(all, {BootCommand::GetVersion, BootCommand::GetInfo});
    if (!require(!synchronous.isRunning(), "synchronous responses must not reenter an incomplete send round"))
        return 1;
    qInfo() << "Bootloader batch command regression passed";
    return 0;
}
