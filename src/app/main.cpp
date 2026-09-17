#include "app/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QStringList>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication application(argc, argv);

    QFile theme(QStringLiteral(":/theme/theme.qss"));
    if (theme.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        application.setStyleSheet(QString::fromUtf8(theme.readAll()));
    }

    rov::MainWindow window;
    int pageIndex = 0;
    for (int i = 1; i < argc - 1; ++i)
    {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--page"))
        {
            pageIndex = QString::fromLocal8Bit(argv[i + 1]).toInt();
            break;
        }
    }
    window.setPageIndex(pageIndex);

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

    for (int i = 1; i < argc - 1; ++i)
    {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--capture"))
        {
            const QString output = QString::fromLocal8Bit(argv[i + 1]);
            QTimer::singleShot(700, &window,
                               [&window, output, &application]()
                               {
                                   window.grab().save(output);
                                   application.quit();
                               });
            break;
        }
    }

    return application.exec();
}
