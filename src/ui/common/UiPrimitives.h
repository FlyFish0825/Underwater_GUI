#pragma once

#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

class QBoxLayout;
class QVBoxLayout;

namespace rov
{

enum class IconKind
{
    Brand,
    Dashboard,
    Motor,
    Firmware,
    Manipulator,
    Vision,
    Settings,
    Status,
    Waveform,
    File,
    Camera,
    Alarm,
    Action,
    Arm,
    List
};

class IconWidget final : public QWidget
{
  public:
    explicit IconWidget(IconKind kind, QWidget *parent = nullptr);

    QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    IconKind m_kind;
};

QIcon makeIcon(IconKind kind);

class CardWidget final : public QFrame
{
  public:
    explicit CardWidget(const QString &title = QString(), IconKind icon = IconKind::Status,
                        QWidget *parent = nullptr);

    QVBoxLayout *contentLayout() const;
    QLabel *titleLabel() const;

  private:
    QVBoxLayout *m_contentLayout = nullptr;
    QLabel *m_titleLabel = nullptr;
};

QLabel *makeLabel(const QString &text, const QString &objectName = QString());
QLabel *makeMetricLabel(const QString &text);
QLabel *makeMetricValue(const QString &text);
QLabel *makeStatusPill(const QString &text, const QString &statusObjectName);
QPushButton *makeButton(const QString &text, const QString &objectName = QString(),
                        QWidget *parent = nullptr);
QWidget *makePageHeader(const QString &title, const QString &subtitle, const QString &demoText);

} // namespace rov
