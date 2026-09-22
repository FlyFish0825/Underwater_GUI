#include "pages/settings/SettingsPlaceholder.h"
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QDebug>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    QCoreApplication::setOrganizationName("RovSettingsTest");
    QCoreApplication::setApplicationName("CanBitrate");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    rov::SettingsPlaceholder page;
    auto *nominal = page.findChild<QComboBox *>("canNominalBitrateCombo");
    auto *data = page.findChild<QComboBox *>("canDataBitrateCombo");
    auto *apply = page.findChild<QPushButton *>("primaryButton");
    const auto check = [](bool ok, const char *message) {
        if (!ok) qCritical() << message;
        return ok;
    };
    if (!check(nominal && data && apply, "Missing settings controls")) return 1;
    if (!check(nominal->isEnabled() && data->isEnabled() && !apply->isEnabled(),
               "Disconnected rate selectors must remain available")) return 1;
    int requests = 0;
    quint32 sentNominal = 0, sentData = 0;
    QObject::connect(&page, &rov::SettingsPlaceholder::canBitrateApplyRequested,
                     [&](quint32 n, quint32 d) { ++requests; sentNominal = n; sentData = d; });
    page.setGatewayConnected(true);
    if (!check(apply->isEnabled() && nominal->isEnabled() && data->isEnabled()
                   && requests == 0, "Connection must enable controls without changing bus rates")) return 1;
    nominal->setCurrentIndex(nominal->findData(500000U));
    data->setCurrentIndex(data->findData(2000000U));
    apply->click();
    if (!check(requests == 1 && sentNominal == 500000U && sentData == 2000000U
                   && !apply->isEnabled() && !nominal->isEnabled() && !data->isEnabled(),
               "Request must include both selected rates and lock controls")) return 1;
    page.onCanBitrateError("timeout");
    if (!check(apply->isEnabled() && data->isEnabled()
                   && !QSettings().contains("communication/canDataBitrate"),
               "Failure must allow retry without saving")) return 1;
    apply->click();
    page.onCanBitrateConfigured(1, 0, sentNominal, sentData);
    if (!check(QSettings().value("communication/canNominalBitrate").toUInt() == 500000U
                   && QSettings().value("communication/canDataBitrate").toUInt() == 2000000U,
               "Acknowledgement must save both rates")) return 1;
    page.setGatewayConnected(false);
    if (!check(!apply->isEnabled() && nominal->isEnabled() && data->isEnabled(),
               "Disconnect must only disable applying rates")) return 1;
    qInfo() << "CanBitrateSettingsTest: PASS";
    return 0;
}
