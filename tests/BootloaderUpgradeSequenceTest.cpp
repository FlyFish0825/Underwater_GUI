#include "communication/bootloader/BootloaderDownloadController.h"
#include "communication/bootloader/BootloaderService.h"
#include "communication/bootloader/BootloaderUpgradeSequence.h"
#include "communication/service/BootloaderCommunicationService.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTemporaryDir>

using namespace rov;

namespace
{
bool require(const bool condition, const char *message)
{
    if (!condition)
        qCritical() << message;
    return condition;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QVector<quint8> ordered = BootloaderUpgradeSequence::orderedTargets(
        {8, 4, 1, 2}, 1, 8);
    if (!require(ordered == QVector<quint8>({1, 2, 4, 8}),
                 "Canary/普通节点/Guard 顺序错误")
        || !require(BootloaderUpgradeSequence::orderedTargets({1, 1, 8}, 1, 8).isEmpty(),
                    "重复节点应拒绝")
        || !require(BootloaderUpgradeSequence::orderedTargets({1, 7}, 1, 8).isEmpty(),
                    "缺少 Guard 应拒绝")
        || !require(BootloaderUpgradeSequence::orderedTargets({1, 8}, 1, 1).isEmpty(),
                    "Canary 与 Guard 相同应拒绝"))
        return 1;

    QTemporaryDir directory;
    if (!require(directory.isValid(), "临时目录创建失败"))
        return 1;
    const QString firmwarePath = directory.filePath(QStringLiteral("image.bin"));
    QFile firmware(firmwarePath);
    if (!require(firmware.open(QIODevice::WriteOnly), "临时 BIN 创建失败"))
        return 1;
    firmware.write("test-image");
    firmware.close();

    BootloaderCommunicationService communication;
    BootloaderService service(&communication);
    BootloaderDownloadController controller(&service);
    BootloaderUpgradeSequence sequence(&controller);
    int startedCount = 0;
    int finishedCount = 0;
    bool sequenceSuccess = true;
    QObject::connect(&sequence, &BootloaderUpgradeSequence::nodeStarted,
                     [&startedCount](quint8, int, int) { ++startedCount; });
    QObject::connect(&sequence, &BootloaderUpgradeSequence::finished,
                     [&finishedCount, &sequenceSuccess](bool success, const QString &)
                     {
                         ++finishedCount;
                         sequenceSuccess = success;
                     });
    const bool accepted = sequence.start({1, 8}, 1, 8, firmwarePath);
    if (!require(!accepted, "未连接时启动应同步失败")
        || !require(startedCount == 1, "同步启动失败后不能启动后续 Guard")
        || !require(finishedCount == 1 && !sequenceSuccess,
                    "同步启动失败必须只发出一次失败完成信号")
        || !require(!sequence.isRunning(), "失败后队列必须回到空闲"))
        return 1;

    BootloaderUpgradeSequence canceledSequence(&controller);
    int cancelFinishCount = 0;
    bool cancelSuccess = true;
    QObject::connect(&canceledSequence, &BootloaderUpgradeSequence::nodeStarted,
                     &canceledSequence,
                     [&canceledSequence](quint8, int, int) { canceledSequence.cancel(); });
    QObject::connect(&canceledSequence, &BootloaderUpgradeSequence::finished,
                     [&cancelFinishCount, &cancelSuccess](bool success, const QString &)
                     {
                         ++cancelFinishCount;
                         cancelSuccess = success;
                     });
    if (!require(!canceledSequence.start({1, 8}, 1, 8, firmwarePath),
                 "Canary 开始回调中取消应阻止发送")
        || !require(cancelFinishCount == 1 && !cancelSuccess,
                    "开始前取消只能完成一次且应失败结束")
        || !require(!canceledSequence.isRunning(), "取消后队列必须回到空闲"))
        return 1;
    return 0;
}
