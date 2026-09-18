#include "ui/common/AppSlider.h"

namespace rov
{

AppSlider::AppSlider(Qt::Orientation orientation, QWidget *parent)
    : fluent::basicinput::Slider(orientation, parent)
{
}

AppSlider::AppSlider(QWidget *parent) : fluent::basicinput::Slider(parent)
{
}

} // namespace rov
