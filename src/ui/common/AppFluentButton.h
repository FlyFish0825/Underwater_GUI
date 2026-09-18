#pragma once

#include <FluentQt/BasicInput.h>

namespace rov
{

// 应用层按钮适配器：统一保留 Fluent-Qt 的交互语义，避免业务层直接依赖第三方命名空间。
class AppFluentButton final : public fluent::basicinput::Button
{
    Q_OBJECT

  public:
    explicit AppFluentButton(const QString &text, QWidget *parent = nullptr);
    explicit AppFluentButton(QWidget *parent = nullptr);
};

} // namespace rov
