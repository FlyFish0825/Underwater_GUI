#pragma once

#include "data/store/FirmwareHistoryStore.h"

#include <QWidget>
#include <QPointer>

#include <QtGlobal>

class QLabel;
class QComboBox;
class QPushButton;
class QShowEvent;

namespace rov
{

class SettingsPlaceholder final : public QWidget
{
    Q_OBJECT

  public:
    explicit SettingsPlaceholder(QWidget *connectionBar = nullptr, QWidget *parent = nullptr);
    ~SettingsPlaceholder() override;

    void setGatewayConnected(bool connected);
    void setTcpControlState(bool connected, const QString &owner, bool owned,
                            const QString &detail = QString());
    void showTcpControlWarning(const QString &title, const QString &message);
    void onCanBitrateConfigured(quint16 sequence, quint8 status, quint32 nominalBps,
                                quint32 dataBps);
    void onCanBitrateError(const QString &message);

  signals:
    void canBitrateApplyRequested(quint32 nominalBps, quint32 dataBps);
    void tcpControlClaimRequested();
    void tcpControlTakeoverRequested();
    void tcpControlReleaseRequested();

  private:
    void clearHistoryWithConfirmation();
    void showHistory();
    void refreshHistoryPreview();
    void requestSelectedCanBitrate();

    FirmwareHistoryStore m_historyStore;
    QLabel *m_historyStatus = nullptr;
    QLabel *m_historyPreview = nullptr;
    QLabel *m_canBitrateStatus = nullptr;
    QLabel *m_tcpControlStatus = nullptr;
    QComboBox *m_canBitrateCombo = nullptr;
    QComboBox *m_canDataBitrateCombo = nullptr;
    QPushButton *m_applyCanBitrateButton = nullptr;
    QPushButton *m_tcpTakeoverButton = nullptr;
    QPushButton *m_tcpReleaseButton = nullptr;
    QPushButton *m_tcpClaimButton = nullptr;
    bool m_tcpControlConnected = false;
    bool m_tcpControlOwned = false;
    bool m_gatewayConnected = false;
    bool m_canBitrateRequestPending = false;
    QPointer<class FirmwareHistoryDialog> m_historyDialog;

  protected:
    void showEvent(QShowEvent *event) override;
};

} // namespace rov
