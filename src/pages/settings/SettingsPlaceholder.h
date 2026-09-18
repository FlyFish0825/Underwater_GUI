#pragma once

#include "data/store/FirmwareHistoryStore.h"

#include <QWidget>

class QLabel;

namespace rov
{

class SettingsPlaceholder final : public QWidget
{
    Q_OBJECT

  public:
    explicit SettingsPlaceholder(QWidget *parent = nullptr);

  private:
    void clearHistoryWithConfirmation();

    FirmwareHistoryStore m_historyStore;
    QLabel *m_historyStatus = nullptr;
};

} // namespace rov
