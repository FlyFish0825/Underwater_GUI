#include "ui/common/AppFluentButton.h"

namespace rov
{

AppFluentButton::AppFluentButton(const QString &text, QWidget *parent)
    : fluent::basicinput::Button(text, parent)
{
}

AppFluentButton::AppFluentButton(QWidget *parent) : fluent::basicinput::Button(parent)
{
}

} // namespace rov
