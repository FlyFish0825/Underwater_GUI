#include "ui/common/AppCheckBox.h"

namespace rov
{

AppCheckBox::AppCheckBox(const QString &text, QWidget *parent)
    : fluent::basicinput::CheckBox(text, parent)
{
}

AppCheckBox::AppCheckBox(QWidget *parent) : fluent::basicinput::CheckBox(parent)
{
}

} // namespace rov
