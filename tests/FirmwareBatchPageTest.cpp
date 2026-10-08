#include "pages/firmware/FirmwarePage.h"
#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QAbstractButton>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QDebug>

int main(int argc, char **argv)
{
    fluent::prepareHighDpiApplication();
    QApplication app(argc, argv);
    fluent::initializeResources();
    QFile theme(QStringLiteral(":/theme/theme.qss"));
    if (theme.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    app.setStyleSheet(app.styleSheet() + QStringLiteral("QWidget { font-family: 'Microsoft YaHei UI'; }"));
    QScrollArea scroll;
    scroll.setAttribute(Qt::WA_DontShowOnScreen);
    scroll.setWidgetResizable(true);
    auto *page = new rov::FirmwarePage(nullptr, false);
    scroll.setWidget(page);
    scroll.resize(1500, 1100);
    scroll.show();
    for (auto *button : page->findChildren<QAbstractButton *>())
        if (button->text() == QStringLiteral("多节点并行升级"))
            button->click();
    app.processEvents();
    int batchButtons = 0;
    for (auto *button : page->findChildren<QPushButton *>())
        if (button->property("batchCommand").toBool())
        {
            ++batchButtons;
            if (!button->isVisible() || button->isEnabled())
            {
                qCritical() << "Batch controls must be visible and disabled offline";
                return 1;
            }
        }
    auto *service = page->findChild<rov::BootloaderService *>();
    if (batchButtons != 8 || service == nullptr || page->communicationService()->isOpen())
        return 1;
    for (quint8 node = 8; node > 0; --node)
    {
        rov::BootResponse reply;
        reply.nodeId = node;
        reply.command = rov::BootCommand::GetVersion;
        reply.status = rov::BootStatus::Idle;
        reply.data = QByteArray::fromHex("0102");
        reply.data.append(static_cast<char>(node));
        service->hostResponseReceived(reply);
    }
    for (quint8 node = 1; node <= 8; ++node)
    {
        const QString expected = QStringLiteral("当前版本：v1.2.%1").arg(node);
        bool found = false;
        for (auto *label : page->findChildren<QLabel *>())
            found |= label->text() == expected;
        if (!found)
        {
            qCritical() << "Missing version for node" << node;
            return 1;
        }
    }
    // 错误版本回包不能污染之前正确的节点版本。
    rov::BootResponse error;
    error.nodeId = 8;
    error.command = rov::BootCommand::GetVersion;
    error.status = rov::BootStatus::Error;
    error.data = QByteArray::fromHex("090909");
    service->hostResponseReceived(error);
    for (auto *label : page->findChildren<QLabel *>())
        if (label->text() == QStringLiteral("当前版本：v9.9.9"))
            return 1;
    app.processEvents();
    scroll.grab().save(QCoreApplication::applicationDirPath() + QStringLiteral("/firmware-batch-wide.png"));
    scroll.resize(1000, 1000);
    app.processEvents();
    scroll.grab().save(QCoreApplication::applicationDirPath() + QStringLiteral("/firmware-batch-narrow.png"));
    page->closeAuxiliaryWindows();
    qInfo() << "Firmware batch page: 8 controls and 8 independent versions passed";
    return 0;
}
