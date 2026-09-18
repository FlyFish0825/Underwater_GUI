#pragma once

#include <FluentQt/StatusInfo.h>

namespace rov
{

// 线性进度条适配器：提供与 QProgressBar 相同的核心范围和值操作。
class AppProgressBar final : public fluent::status_info::ProgressBar
{
    Q_OBJECT

  public:
    explicit AppProgressBar(QWidget *parent = nullptr);
};

} // namespace rov
