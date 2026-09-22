#include "pages/settings/SettingsPlaceholder.h"

#include "ui/common/UiPrimitives.h"

#include <QHBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace rov
{

namespace
{

constexpr quint32 kDefaultCanNominalBitrate = 1000000U;
constexpr quint32 kDefaultCanDataBitrate = 8000000U;
constexpr auto kCanNominalBitrateKey = "communication/canNominalBitrate";
constexpr auto kCanDataBitrateKey = "communication/canDataBitrate";

struct CanBitrateOption
{
    const char *label;
    quint32 value;
};

QString bitrateText(const quint32 bitrate)
{
    if (bitrate >= 1000000U)
        return QStringLiteral("%1 Mbit/s").arg(bitrate / 1000000U);
    return QStringLiteral("%1 kbit/s").arg(bitrate / 1000U);
}

constexpr CanBitrateOption kCanBitrateOptions[] = {
    {"50 kbit/s", 50000U},     {"100 kbit/s", 100000U},  {"125 kbit/s", 125000U},
    {"250 kbit/s", 250000U},   {"500 kbit/s", 500000U},  {"800 kbit/s", 800000U},
    {"1 Mbit/s", kDefaultCanNominalBitrate},
};

constexpr CanBitrateOption kCanDataBitrateOptions[] = {
    {"500 kbit/s", 500000U}, {"1 Mbit/s", 1000000U}, {"2 Mbit/s", 2000000U},
    {"4 Mbit/s", 4000000U}, {"5 Mbit/s", 5000000U}, {"8 Mbit/s", 8000000U},
};

} // namespace

SettingsPlaceholder::SettingsPlaceholder(QWidget *connectionBar, QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(10);
    if (connectionBar != nullptr)
        root->addWidget(connectionBar);
    root->addWidget(makePageHeader(QStringLiteral("设置"),
                                   QStringLiteral("配置 USB-CAN 网关通信参数。"),
                                   QStringLiteral("通信配置")));
    auto *communicationCard = new CardWidget(QStringLiteral("通信配置"), IconKind::Settings);
    auto *communicationRow = new QHBoxLayout;
    communicationRow->setContentsMargins(0, 0, 0, 0);
    communicationRow->setSpacing(12);
    communicationRow->addWidget(makeLabel(QStringLiteral("CAN 仲裁速率")));
    m_canBitrateCombo = new QComboBox(communicationCard);
    m_canBitrateCombo->setObjectName(QStringLiteral("canNominalBitrateCombo"));
    for (const auto &option : kCanBitrateOptions)
        m_canBitrateCombo->addItem(QString::fromUtf8(option.label), option.value);
    const QSettings settings;
    const quint32 savedNominal = settings.value(kCanNominalBitrateKey,
                                                kDefaultCanNominalBitrate)
                                     .toUInt();
    const int savedIndex = m_canBitrateCombo->findData(savedNominal);
    m_canBitrateCombo->setCurrentIndex(savedIndex >= 0 ? savedIndex
                                                       : m_canBitrateCombo->findData(
                                                             kDefaultCanNominalBitrate));
    m_canBitrateCombo->setMinimumWidth(150);
    communicationRow->addWidget(m_canBitrateCombo);
    communicationRow->addWidget(makeLabel(QStringLiteral("CAN 数据段速率")));
    m_canDataBitrateCombo = new QComboBox(communicationCard);
    m_canDataBitrateCombo->setObjectName(QStringLiteral("canDataBitrateCombo"));
    for (const auto &option : kCanDataBitrateOptions)
        m_canDataBitrateCombo->addItem(QString::fromUtf8(option.label), option.value);
    const quint32 savedData = settings.value(kCanDataBitrateKey, kDefaultCanDataBitrate).toUInt();
    const int savedDataIndex = m_canDataBitrateCombo->findData(savedData);
    m_canDataBitrateCombo->setCurrentIndex(
        savedDataIndex >= 0 ? savedDataIndex
                            : m_canDataBitrateCombo->findData(kDefaultCanDataBitrate));
    m_canDataBitrateCombo->setMinimumWidth(150);
    communicationRow->addWidget(m_canDataBitrateCombo);
    auto *applyCanBitrate = makeButton(QStringLiteral("应用到网关"), QStringLiteral("primaryButton"));
    m_applyCanBitrateButton = applyCanBitrate;
    m_canBitrateCombo->setEnabled(false);
    m_canDataBitrateCombo->setEnabled(false);
    applyCanBitrate->setEnabled(false);
    communicationRow->addWidget(applyCanBitrate);
    communicationRow->addStretch();
    communicationCard->contentLayout()->addLayout(communicationRow);
    m_canBitrateStatus = makeLabel(QStringLiteral("未连接网关；保存值仅在收到成功回复后更新"),
                                   QStringLiteral("mutedLabel"));
    communicationCard->contentLayout()->addWidget(m_canBitrateStatus);
    QObject::connect(applyCanBitrate, &QPushButton::clicked, this,
                     &SettingsPlaceholder::requestSelectedCanBitrate);
    root->addWidget(communicationCard);

    auto *historyCard = new CardWidget(QStringLiteral("历史记录管理"), IconKind::List);
    auto *historyRow = new QHBoxLayout;
    historyRow->setContentsMargins(0, 0, 0, 0);
    historyRow->setSpacing(12);
    const int historyCount = m_historyStore.load().size();
    m_historyStatus = makeLabel(
        QStringLiteral("当前保存 %1 条 · 文件位于程序目录").arg(historyCount),
        QStringLiteral("mutedLabel"));
    historyRow->addWidget(m_historyStatus, 1);
    auto *clearHistory = makeButton(QStringLiteral("清空历史记录"), QStringLiteral("dangerButton"));
    clearHistory->setToolTip(QStringLiteral("需要连续确认两次，才会清空固件升级历史记录"));
    historyRow->addWidget(clearHistory);
    historyCard->contentLayout()->addLayout(historyRow);
    QObject::connect(clearHistory, &QPushButton::clicked, this,
                     &SettingsPlaceholder::clearHistoryWithConfirmation);

    root->addWidget(historyCard);
    root->addStretch();
}

void SettingsPlaceholder::setGatewayConnected(const bool connected)
{
    m_gatewayConnected = connected;
    const bool controlsEnabled = connected && !m_canBitrateRequestPending;
    if (m_canBitrateCombo != nullptr)
        m_canBitrateCombo->setEnabled(controlsEnabled);
    if (m_canDataBitrateCombo != nullptr)
        m_canDataBitrateCombo->setEnabled(controlsEnabled);

    if (m_canBitrateStatus != nullptr)
        m_canBitrateStatus->setText(connected ? QStringLiteral("网关已连接")
                                              : QStringLiteral("未连接网关"));

    if (m_applyCanBitrateButton != nullptr)
        m_applyCanBitrateButton->setEnabled(controlsEnabled);
}

void SettingsPlaceholder::onCanBitrateConfigured(const quint16 sequence, const quint8 status,
                                                  const quint32 nominalBps,
                                                  const quint32 dataBps)
{
    Q_UNUSED(sequence)
    if (status != 0U)
    {
        onCanBitrateError(QStringLiteral("网关拒绝 CAN 速率配置（状态码 %1）").arg(status));
        return;
    }

    QSettings settings;
    settings.setValue(kCanNominalBitrateKey, nominalBps);
    settings.setValue(kCanDataBitrateKey, dataBps);
    settings.sync();
    if (m_canBitrateCombo != nullptr)
    {
        const int index = m_canBitrateCombo->findData(nominalBps);
        if (index >= 0)
            m_canBitrateCombo->setCurrentIndex(index);
    }
    if (m_canDataBitrateCombo != nullptr)
    {
        const int index = m_canDataBitrateCombo->findData(dataBps);
        if (index >= 0)
            m_canDataBitrateCombo->setCurrentIndex(index);
    }
    if (m_canBitrateStatus != nullptr)
        m_canBitrateStatus->setText(QStringLiteral("已应用：仲裁 %1 · 数据段 %2")
                                        .arg(bitrateText(nominalBps), bitrateText(dataBps)));
    m_canBitrateRequestPending = false;
    if (m_canBitrateCombo != nullptr)
        m_canBitrateCombo->setEnabled(m_gatewayConnected);
    if (m_canDataBitrateCombo != nullptr)
        m_canDataBitrateCombo->setEnabled(m_gatewayConnected);
    if (m_applyCanBitrateButton != nullptr)
        m_applyCanBitrateButton->setEnabled(m_gatewayConnected);
}

void SettingsPlaceholder::onCanBitrateError(const QString &message)
{
    if (m_canBitrateStatus != nullptr)
        m_canBitrateStatus->setText(QStringLiteral("应用失败：%1").arg(message));
    m_canBitrateRequestPending = false;
    if (m_canBitrateCombo != nullptr)
        m_canBitrateCombo->setEnabled(m_gatewayConnected);
    if (m_canDataBitrateCombo != nullptr)
        m_canDataBitrateCombo->setEnabled(m_gatewayConnected);
    if (m_applyCanBitrateButton != nullptr)
        m_applyCanBitrateButton->setEnabled(m_gatewayConnected);
}

void SettingsPlaceholder::requestSelectedCanBitrate()
{
    if (!m_gatewayConnected || m_canBitrateRequestPending || m_canBitrateCombo == nullptr ||
        m_canDataBitrateCombo == nullptr)
        return;
    const quint32 nominalBps = m_canBitrateCombo->currentData().toUInt();
    const quint32 dataBps = m_canDataBitrateCombo->currentData().toUInt();
    if (nominalBps == 0U || dataBps == 0U)
        return;
    m_canBitrateRequestPending = true;
    if (m_canBitrateStatus != nullptr)
        m_canBitrateStatus->setText(QStringLiteral("正在应用 CAN 速率…"));
    if (m_applyCanBitrateButton != nullptr)
        m_applyCanBitrateButton->setEnabled(false);
    m_canBitrateCombo->setEnabled(false);
    m_canDataBitrateCombo->setEnabled(false);
    emit canBitrateApplyRequested(nominalBps, dataBps);
}

void SettingsPlaceholder::clearHistoryWithConfirmation()
{
    const int count = m_historyStore.load().size();
    if (count == 0)
    {
        m_historyStatus->setText(QStringLiteral("当前没有可清空的历史记录"));
        return;
    }

    const auto first = QMessageBox::warning(
        this, QStringLiteral("确认清空历史记录"),
        QStringLiteral("即将清空 %1 条固件升级历史记录。\n是否继续？").arg(count),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (first != QMessageBox::Yes)
        return;

    const auto second = QMessageBox::critical(
        this, QStringLiteral("请再次确认"),
        QStringLiteral("历史记录清空后无法恢复。\n确定要永久清空吗？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (second != QMessageBox::Yes)
        return;

    if (m_historyStore.clear())
        m_historyStatus->setText(QStringLiteral("历史记录已清空"));
    else
        m_historyStatus->setText(QStringLiteral("清空失败：无法写入历史记录文件"));
}

} // namespace rov
