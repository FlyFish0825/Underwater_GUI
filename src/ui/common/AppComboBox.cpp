#include "ui/common/AppComboBox.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QWidget>

namespace rov
{

AppComboBox::AppComboBox(QWidget *parent) : QComboBox(parent)
{
    // Fluent ComboBox 的自绘 Flyout 在 QGraphicsProxyWidget 页面容器中无法可靠弹出；
    // 原生 QComboBox 使用 Qt 自己的弹出窗口，保证节点、日志筛选和高级命令选择可操作。
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(30);
}

void AppComboBox::showPopup()
{
    QComboBox::showPopup();

    // Qt 会在 showPopup() 内创建私有 popup 容器。不要替换 view 或手动
    // 计算几何位置，只把已经创建的顶层窗口提升到代理页面之上。
    if (QAbstractItemView *popupView = view())
    {
        if (QWidget *popup = popupView->window())
        {
            popup->raise();
            popup->activateWindow();
        }
    }
    if (QApplication::focusWidget() == nullptr)
        setFocus(Qt::OtherFocusReason);
}

} // namespace rov
