#pragma once

#include <FluentQt/BasicInput.h>

namespace rov
{

// 复选框适配器：保持 QCheckBox 的信号和状态语义，替换视觉呈现。
class AppCheckBox final : public fluent::basicinput::CheckBox
{
    Q_OBJECT

  public:
    explicit AppCheckBox(const QString &text, QWidget *parent = nullptr);
    explicit AppCheckBox(QWidget *parent = nullptr);
};

} // namespace rov
