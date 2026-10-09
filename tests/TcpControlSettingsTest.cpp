#include "pages/settings/SettingsPlaceholder.h"

#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QFile>
#include <QFont>

#include <iostream>
#include <stdexcept>

int main(int argc, char **argv)
{
    fluent::prepareHighDpiApplication();
    QApplication app(argc, argv);
    fluent::initializeResources();
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    QCoreApplication::setOrganizationName(QStringLiteral("ROVTests"));
    QCoreApplication::setApplicationName(QStringLiteral("TcpControlSettingsTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QCoreApplication::applicationDirPath() + QStringLiteral("/settings-test"));
    QFile theme(QStringLiteral(":/theme/theme.qss"));
    if (theme.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(theme.readAll()));

    try {
        rov::SettingsPlaceholder settings;
        auto *claim = settings.findChild<QPushButton *>(QStringLiteral("tcpClaimControlButton"));
        auto *takeover = settings.findChild<QPushButton *>(QStringLiteral("tcpTakeoverControlButton"));
        auto *release = settings.findChild<QPushButton *>(QStringLiteral("tcpReleaseControlButton"));
        auto *state = settings.findChild<QLabel *>(QStringLiteral("tcpControlStatus"));
        if (!claim || !takeover || !release || !state)
            throw std::runtime_error("control ownership controls are missing");
        int claims = 0, takeovers = 0, releases = 0;
        QObject::connect(&settings, &rov::SettingsPlaceholder::tcpControlClaimRequested,
                         [&] { ++claims; });
        QObject::connect(&settings, &rov::SettingsPlaceholder::tcpControlTakeoverRequested,
                         [&] { ++takeovers; });
        QObject::connect(&settings, &rov::SettingsPlaceholder::tcpControlReleaseRequested,
                         [&] { ++releases; });

        if (claim->isEnabled() || takeover->isEnabled() || release->isEnabled())
            throw std::runtime_error("controls must stay disabled before Nano management connects");
        settings.setTcpControlState(true, QStringLiteral("NANO"), false,
                                    QStringLiteral("Nano 当前持有控制权"));
        if (!claim->isEnabled() || !takeover->isEnabled() || release->isEnabled()
            || state->text() != QStringLiteral("Nano 当前持有控制权"))
            throw std::runtime_error("Nano-owned state is not shown correctly");
        claim->click(); takeover->click();
        if (claims != 1 || takeovers != 1 || releases != 0)
            throw std::runtime_error("claim and takeover buttons emit separate requests");

        settings.setTcpControlState(true, QStringLiteral("GUI"), true,
                                    QStringLiteral("上位机独占控制权"));
        if (claim->isEnabled() || takeover->isEnabled() || !release->isEnabled())
            throw std::runtime_error("GUI-owned state exposes release only");
        release->click();
        if (releases != 1)
            throw std::runtime_error("release control request emitted");

        settings.setTcpControlState(false, QStringLiteral("UNKNOWN"), false,
                                    QStringLiteral("控制管理连接已断开"));
        if (claim->isEnabled() || takeover->isEnabled() || release->isEnabled())
            throw std::runtime_error("ownership controls disable when management disconnects");
        std::cout << "PASS TCP control settings controls and ownership states\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
