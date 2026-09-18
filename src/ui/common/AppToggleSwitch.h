#pragma once

#include <FluentQt/BasicInput.h>

namespace rov
{

// 应用层开关适配器：只负责提供稳定的工程入口，不在这里加入业务状态。
class AppToggleSwitch final : public fluent::basicinput::ToggleSwitch
{
    Q_OBJECT

  public:
    explicit AppToggleSwitch(QWidget *parent = nullptr);
};

} // namespace rov
