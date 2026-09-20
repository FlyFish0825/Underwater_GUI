#pragma once

#include <QLineEdit>

namespace rov
{

// 单行输入框适配器：保持 QLineEdit 的文本、校验和编辑信号契约。
class AppLineEdit final : public QLineEdit
{
    Q_OBJECT

  public:
    explicit AppLineEdit(QWidget *parent = nullptr);
    explicit AppLineEdit(const QString &text, QWidget *parent = nullptr);
};

} // namespace rov
