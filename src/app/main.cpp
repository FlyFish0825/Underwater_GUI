#include "app/MainWindow.h"

#include <FluentQt/FluentQt.h>

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QStringList>

int main(int argc, char *argv[])
{
    // Fluent-Qt 要求在 QApplication 创建前配置 High-DPI；这与现有的
    // PassThrough 策略一致，因此只复用配置，不改变现有窗口比例行为。
    fluent::prepareHighDpiApplication();
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setOrganizationName(QStringLiteral("UnderwaterGUI"));
    QCoreApplication::setApplicationName(QStringLiteral("UnderwaterGUI"));
    QApplication application(argc, argv);
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/project_logo.png")));
    fluent::initializeResources();

    QFile theme(QStringLiteral(":/theme/theme.qss"));
    if (theme.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        application.setStyleSheet(QString::fromUtf8(theme.readAll()));
    }

    rov::MainWindow window;
    for (int i = 1; i < argc - 1; ++i)
    {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--window-size"))
        {
            const QStringList parts = QString::fromLocal8Bit(argv[i + 1]).split('x');
            if (parts.size() == 2)
            {
                window.resize(parts.at(0).toInt(), parts.at(1).toInt());
            }
            break;
        }
    }
    window.show();

    return application.exec();
}
