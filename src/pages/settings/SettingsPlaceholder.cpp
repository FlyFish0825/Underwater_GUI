#include "pages/settings/SettingsPlaceholder.h"

#include "ui/firmware_history/FirmwareHistoryDialog.h"
#include "ui/common/AppComboBox.h"
#include "ui/common/UiPrimitives.h"

#include <QHBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSizePolicy>
#include <QSettings>
#include <QShowEvent>
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
    communicationCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *communicationRow = new QHBoxLayout;
    communicationRow->setContentsMargins(0, 0, 0, 0);
    communicationRow->setSpacing(12);
    communicationRow->addWidget(makeLabel(QStringLiteral("CAN 仲裁速率")));
    m_canBitrateCombo = new AppComboBox(communicationCard);
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
    communicationRow->addWidget(makeLabel(QStringLiteral("CAN 数据速率")));
    m_canDataBitrateCombo = new AppComboBox(communicationCard);
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
    applyCanBitrate->setEnabled(false);
    communicationRow->addWidget(applyCanBitrate);
    m_canBitrateStatus = makeLabel(QStringLiteral("未连接网关"), QStringLiteral("mutedLabel"));
    communicationRow->addWidget(m_canBitrateStatus);
    communicationRow->addStretch();
    communicationCard->contentLayout()->addLayout(communicationRow);
    QObject::connect(applyCanBitrate, &QPushButton::clicked, this,
                     &SettingsPlaceholder::requestSelectedCanBitrate);
    root->addWidget(communicationCard);

    auto *historyCard = new CardWidget(QStringLiteral("历史记录管理"), IconKind::List);
    historyCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    historyCard->contentLayout()->setContentsMargins(14, 8, 14, 9);
    historyCard->contentLayout()->setSpacing(4);
    m_historyStatus = makeLabel(QString(), QStringLiteral("mutedLabel"));
    auto *openHistory = makeButton(QStringLiteral("打开历史记录"), QStringLiteral("softButton"));
    historyCard->headerLayout()->addStretch();
    historyCard->headerLayout()->addWidget(openHistory);
    auto *clearHistory = makeButton(QStringLiteral("清空历史记录"), QStringLiteral("dangerButton"));
    clearHistory->setToolTip(QStringLiteral("清空固件升级历史记录"));
    historyCard->headerLayout()->addWidget(clearHistory);
    m_historyPreview = makeLabel(QString(), QStringLiteral("mutedLabel"));
    m_historyPreview->setWordWrap(true);
    m_historyPreview->setTextFormat(Qt::RichText);
    m_historyPreview->setMaximumHeight(82);
    m_historyPreview->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    historyCard->contentLayout()->addWidget(m_historyPreview);
    historyCard->contentLayout()->addWidget(m_historyStatus);
    refreshHistoryPreview();
    QObject::connect(openHistory, &QPushButton::clicked, this, &SettingsPlaceholder::showHistory);
    QObject::connect(clearHistory, &QPushButton::clicked, this,
                     &SettingsPlaceholder::clearHistoryWithConfirmation);

    root->addWidget(historyCard);
    root->addStretch();
}

SettingsPlaceholder::~SettingsPlaceholder()
{
    if (m_historyDialog != nullptr)
    {
        m_historyDialog->setAttribute(Qt::WA_DeleteOnClose, false);
        m_historyDialog->close();
        delete m_historyDialog.data();
        m_historyDialog = nullptr;
    }
}

void SettingsPlaceholder::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshHistoryPreview();
}

void SettingsPlaceholder::setGatewayConnected(const bool connected)
{
    m_gatewayConnected = connected;
    const bool selectionEnabled = !m_canBitrateRequestPending;
    if (m_canBitrateCombo != nullptr)
        m_canBitrateCombo->setEnabled(selectionEnabled);
    if (m_canDataBitrateCombo != nullptr)
        m_canDataBitrateCombo->setEnabled(selectionEnabled);

    if (m_canBitrateStatus != nullptr)
        m_canBitrateStatus->setText(connected ? QStringLiteral("网关已连接")
                                              : QStringLiteral("网关未连接"));

    if (m_applyCanBitrateButton != nullptr)
        m_applyCanBitrateButton->setEnabled(connected && !m_canBitrateRequestPending);
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
        m_canBitrateCombo->setEnabled(true);
    if (m_canDataBitrateCombo != nullptr)
        m_canDataBitrateCombo->setEnabled(true);
    if (m_applyCanBitrateButton != nullptr)
        m_applyCanBitrateButton->setEnabled(m_gatewayConnected);
}

void SettingsPlaceholder::onCanBitrateError(const QString &message)
{
    if (m_canBitrateStatus != nullptr)
        m_canBitrateStatus->setText(QStringLiteral("应用失败：%1").arg(message));
    m_canBitrateRequestPending = false;
    if (m_canBitrateCombo != nullptr)
        m_canBitrateCombo->setEnabled(true);
    if (m_canDataBitrateCombo != nullptr)
        m_canDataBitrateCombo->setEnabled(true);
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
        QMessageBox information;
        information.setWindowTitle(QStringLiteral("历史记录"));
        information.setText(QStringLiteral("当前没有可清空的历史记录。"));
        information.setIcon(QMessageBox::Information);
        information.setStandardButtons(QMessageBox::Ok);
        information.setWindowFlag(Qt::Window, true);
        information.setWindowModality(Qt::ApplicationModal);
        information.exec();
        return;
    }

    // Settings lives inside MainWindow's QGraphicsProxyWidget. A QMessageBox parented to this
    // page can be embedded/scaled by the proxy and end up behind the viewport, making a click
    // appear to do nothing. Keep the confirmation as an independent application-modal window.
    QMessageBox confirmation;
    confirmation.setWindowTitle(QStringLiteral("确认清空历史记录"));
    confirmation.setText(QStringLiteral("即将清空 %1 条固件升级历史记录。\n此操作无法撤销，是否继续？")
                             .arg(count));
    confirmation.setIcon(QMessageBox::Warning);
    confirmation.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    confirmation.setDefaultButton(QMessageBox::No);
    confirmation.setWindowFlag(Qt::Window, true);
    confirmation.setWindowModality(Qt::ApplicationModal);
    if (confirmation.exec() != QMessageBox::Yes)
        return;

    const bool cleared = m_historyStore.clear();
    refreshHistoryPreview();
    m_historyStatus->setText(cleared ? QStringLiteral("历史记录已清空")
                                     : QStringLiteral("清空失败：无法写入历史记录文件"));
    QMessageBox result;
    result.setWindowTitle(QStringLiteral("历史记录"));
    result.setText(cleared ? QStringLiteral("历史记录已清空。")
                           : QStringLiteral("清空失败：无法写入历史记录文件。"));
    result.setIcon(cleared ? QMessageBox::Information : QMessageBox::Critical);
    result.setStandardButtons(QMessageBox::Ok);
    result.setWindowFlag(Qt::Window, true);
    result.setWindowModality(Qt::ApplicationModal);
    result.exec();
}

void SettingsPlaceholder::showHistory()
{
    if (m_historyDialog != nullptr && m_historyDialog->isVisible())
    {
        m_historyDialog->raise();
        m_historyDialog->activateWindow();
        return;
    }
    if (m_historyDialog != nullptr)
    {
        m_historyDialog->deleteLater();
        m_historyDialog = nullptr;
    }
    m_historyDialog = new FirmwareHistoryDialog(&m_historyStore, nullptr);
    m_historyDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_historyDialog->setWindowFlag(Qt::Window, true);
    m_historyDialog->setWindowModality(Qt::ApplicationModal);
    QWidget *owner = window();
    if (owner != nullptr)
    {
        const QSize ownerSize = owner->size();
        m_historyDialog->resize(qMax(900, ownerSize.width() * 3 / 5),
                                qMax(600, ownerSize.height() * 3 / 5));
        m_historyDialog->move(owner->frameGeometry().center() - m_historyDialog->rect().center());
    }
    m_historyDialog->open();
    m_historyDialog->raise();
    m_historyDialog->activateWindow();
}

void SettingsPlaceholder::refreshHistoryPreview()
{
    if (m_historyPreview == nullptr || m_historyStatus == nullptr)
        return;
    const auto entries = m_historyStore.load();
    m_historyStatus->setText(QStringLiteral("共 %1 条记录 · 最新记录在前").arg(entries.size()));
    QStringList lines;
    const int first = qMax(0, entries.size() - 3);
    for (int index = entries.size() - 1; index >= first; --index)
    {
        const auto &entry = entries.at(index);
        lines.append(QStringLiteral("<div>[%1] %2</div>")
                         .arg(entry.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                              entry.message.toHtmlEscaped()));
    }
    if (lines.isEmpty())
        lines.append(QStringLiteral("<span style='color:#8798aa'>暂无历史记录</span>"));
    m_historyPreview->setText(lines.join(QString()));
}

} // namespace rov
