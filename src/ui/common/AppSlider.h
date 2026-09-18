#pragma once

#include <FluentQt/BasicInput.h>

namespace rov
{

// 滑块适配器：保持 QSlider 的方向、范围和值变化语义。
class AppSlider final : public fluent::basicinput::Slider
{
    Q_OBJECT

  public:
    explicit AppSlider(Qt::Orientation orientation = Qt::Horizontal,
                       QWidget *parent = nullptr);
    explicit AppSlider(QWidget *parent);
};

} // namespace rov
