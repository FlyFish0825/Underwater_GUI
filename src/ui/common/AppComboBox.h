#pragma once

#include <QComboBox>

namespace rov
{

// 下拉框适配器：保留 QComboBox 的模型、索引和信号契约。
class AppComboBox final : public QComboBox
{
    Q_OBJECT

  public:
    explicit AppComboBox(QWidget *parent = nullptr);

  protected:
    // QComboBox 的弹出视图在 QGraphicsProxyWidget 页面中偶尔会被代理层
    // 保持在后台；显式提升/激活可确保点击箭头后列表可见且可键盘操作。
    void showPopup() override;
};

} // namespace rov
