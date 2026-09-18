#include "ui/common/AppLineEdit.h"

namespace rov
{

AppLineEdit::AppLineEdit(QWidget *parent) : fluent::textfields::LineEdit(parent)
{
}

AppLineEdit::AppLineEdit(const QString &text, QWidget *parent)
    : fluent::textfields::LineEdit(parent)
{
    setText(text);
}

} // namespace rov
