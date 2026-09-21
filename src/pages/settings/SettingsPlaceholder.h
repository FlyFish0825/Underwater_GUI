#pragma once

#include "data/store/FirmwareHistoryStore.h"

#include <QWidget>

#include <QtGlobal>

class QLabel;
class QComboBox;
class QPushButton;

namespace rov
{

class SettingsPlaceholder final : public QWidget
{
    Q_OBJECT

  public:
    explicit SettingsPlaceholder(QWidget *connectionBar = nullptr, QWidget *parent = nullptr);

    void setGatewayConnected(bool connected);
    void onCanBitrateConfigured(quint16 sequence, quint8 status, quint32 nominalBps,
                                quint32 dataBps);
    void onCanBitrateError(const QString &message);

  signals:
    void canBitrateApplyRequested(quint32 nominalBps, quint32 dataBps);

  private:
    void clearHistoryWithConfirmation();
    void requestSelectedCanBitrate();

    FirmwareHistoryStore m_historyStore;
    QLabel *m_historyStatus = nullptr;
    QLabel *m_canBitrateStatus = nullptr;
    QComboBox *m_canBitrateCombo = nullptr;
    QComboBox *m_canDataBitrateCombo = nullptr;
    QPushButton *m_applyCanBitrateButton = nullptr;
    bool m_gatewayConnected = false;
    bool m_canBitrateRequestPending = false;
};

} // namespace rov
