#include "ui/common/AppLineEdit.h"

namespace rov
{

AppLineEdit::AppLineEdit(QWidget *parent) : QLineEdit(parent)
{
    // 原生编辑器在 QGraphicsProxyWidget 和独立高级命令窗口中都保持标准焦点、
    // 鼠标和输入法行为，避免 Fluent 自绘编辑器出现“看得到但无法输入”。
    setMinimumHeight(30);
}

AppLineEdit::AppLineEdit(const QString &text, QWidget *parent)
    : QLineEdit(parent)
{
    setMinimumHeight(30);
    setText(text);
}

} // namespace rov
