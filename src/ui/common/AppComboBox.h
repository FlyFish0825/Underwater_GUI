#pragma once

#include <FluentQt/BasicInput.h>

namespace rov
{

// 下拉框适配器：保留 QComboBox 的模型、索引和信号契约。
class AppComboBox final : public fluent::basicinput::ComboBox
{
    Q_OBJECT

  public:
    explicit AppComboBox(QWidget *parent = nullptr);
};

} // namespace rov
