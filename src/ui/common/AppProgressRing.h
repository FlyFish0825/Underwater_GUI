#pragma once

#include <FluentQt/StatusInfo.h>

namespace rov
{

// 应用层进度环适配器：保留 Fluent-Qt 的不确定进度动画和确定进度 API。
class AppProgressRing final : public fluent::status_info::ProgressRing
{
    Q_OBJECT

  public:
    explicit AppProgressRing(QWidget *parent = nullptr);
};

} // namespace rov
