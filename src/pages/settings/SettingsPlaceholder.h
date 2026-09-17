#pragma once

#include <QWidget>

namespace rov
{

class SettingsPlaceholder final : public QWidget
{
    Q_OBJECT

  public:
    explicit SettingsPlaceholder(QWidget *parent = nullptr);
};

} // namespace rov
