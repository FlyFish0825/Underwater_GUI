#pragma once

#include <FluentQt/StatusInfo.h>

namespace rov
{

// 应用层通知条适配器：用于后续将提示消息接入统一的页面反馈区域。
class AppInfoBar final : public fluent::status_info::InfoBar
{
    Q_OBJECT

  public:
    explicit AppInfoBar(QWidget *parent = nullptr);
};

} // namespace rov
