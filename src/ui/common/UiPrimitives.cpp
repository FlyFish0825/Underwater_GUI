#include "ui/common/UiPrimitives.h"

#include "ui/common/AppFluentButton.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QtMath>

namespace
{

QPen iconPen(const QColor &color, qreal width = 2.0)
{
    QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    return pen;
}

} // namespace

namespace rov
{

IconWidget::IconWidget(const IconKind kind, QWidget *parent) : QWidget(parent), m_kind(kind)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

QIcon makeIcon(const IconKind kind)
{
    IconWidget source(kind);
    source.setAttribute(Qt::WA_TranslucentBackground);
    source.setStyleSheet(QStringLiteral("background: transparent;"));
    QPalette palette = source.palette();
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#35516f")));
    source.setPalette(palette);
    source.resize(24, 24);
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    source.render(&pixmap);
    return QIcon(pixmap);
}

QSize IconWidget::sizeHint() const
{
    return QSize(22, 22);
}

void IconWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(iconPen(palette().color(QPalette::Text), 1.8));
    painter.setBrush(Qt::NoBrush);
    const QRectF r = QRectF(3.0, 3.0, width() - 6.0, height() - 6.0);

    switch (m_kind)
    {
    case IconKind::Brand:
    case IconKind::Waveform:
    {
        QPainterPath path;
        path.moveTo(r.left(), r.center().y());
        path.cubicTo(r.left() + 4, r.top(), r.left() + 6, r.bottom(), r.center().x(),
                     r.center().y());
        path.cubicTo(r.right() - 6, r.top(), r.right() - 4, r.bottom(), r.right(), r.center().y());
        painter.drawPath(path);
        painter.drawLine(r.left(), r.center().y() + 5, r.center().x() - 2, r.center().y() + 5);
        painter.drawLine(r.center().x() + 2, r.center().y() + 5, r.right(), r.center().y() + 5);
        break;
    }
    case IconKind::Dashboard:
        painter.drawRect(r.adjusted(2, 2, -2, -2));
        painter.drawLine(r.center().x(), r.top() + 2, r.center().x(), r.bottom() - 2);
        painter.drawLine(r.left() + 2, r.center().y(), r.right() - 2, r.center().y());
        painter.drawEllipse(r.center(), 2, 2);
        break;
    case IconKind::Motor:
        painter.drawEllipse(r.adjusted(2, 2, -2, -2));
        painter.drawEllipse(r.adjusted(6, 6, -6, -6));
        for (int i = 0; i < 4; ++i)
        {
            const qreal x = r.center().x() + qCos(i * M_PI_2) * 7.0;
            const qreal y = r.center().y() + qSin(i * M_PI_2) * 7.0;
            painter.drawLine(r.center(), QPointF(x, y));
        }
        break;
    case IconKind::Firmware:
        painter.drawRoundedRect(r.adjusted(3, 3, -3, -3), 2, 2);
        painter.drawLine(r.left(), r.center().y(), r.left() + 3, r.center().y());
        painter.drawLine(r.right() - 3, r.center().y(), r.right(), r.center().y());
        painter.drawLine(r.center().x(), r.top(), r.center().x(), r.top() + 3);
        painter.drawLine(r.center().x(), r.bottom() - 3, r.center().x(), r.bottom());
        painter.drawEllipse(r.center(), 2, 2);
        break;
    case IconKind::Manipulator:
        painter.drawLine(r.left() + 3, r.bottom() - 2, r.left() + 6, r.center().y());
        painter.drawLine(r.left() + 6, r.center().y(), r.right() - 6, r.top() + 6);
        painter.drawLine(r.right() - 6, r.top() + 6, r.right() - 2, r.bottom() - 5);
        painter.drawEllipse(QPointF(r.left() + 6, r.center().y()), 2.5, 2.5);
        painter.drawEllipse(QPointF(r.right() - 6, r.top() + 6), 2.5, 2.5);
        break;
    case IconKind::Vision:
        painter.drawRoundedRect(r.adjusted(1, 4, -1, -4), 3, 3);
        painter.drawEllipse(r.center(), 3.5, 3.5);
        painter.drawLine(r.left() + 4, r.top() + 2, r.left() + 7, r.top() + 4);
        break;
    case IconKind::Settings:
        painter.drawEllipse(r.center(), 4, 4);
        for (int i = 0; i < 8; ++i)
        {
            const qreal a = i * M_PI / 4.0;
            painter.drawLine(r.center() + QPointF(qCos(a) * 5.5, qSin(a) * 5.5),
                             r.center() + QPointF(qCos(a) * 8.0, qSin(a) * 8.0));
        }
        break;
    case IconKind::Status:
        painter.drawEllipse(r.center(), 5, 5);
        break;
    case IconKind::File:
        painter.drawRect(r.adjusted(4, 2, -4, -2));
        painter.drawLine(r.right() - 7, r.top() + 2, r.right() - 7, r.top() + 7);
        painter.drawLine(r.right() - 7, r.top() + 7, r.right() - 3, r.top() + 7);
        break;
    case IconKind::Camera:
        painter.drawRoundedRect(r.adjusted(1, 5, -1, -3), 2, 2);
        painter.drawEllipse(r.center() + QPointF(0, 1), 3.5, 3.5);
        painter.drawRect(r.left() + 4, r.top() + 2, 5, 3);
        break;
    case IconKind::Alarm:
        painter.drawArc(r.adjusted(3, 3, -3, -2), 20 * 16, 140 * 16);
        painter.drawLine(r.left() + 3, r.bottom() - 3, r.right() - 3, r.bottom() - 3);
        painter.drawLine(r.center().x(), r.top() + 2, r.center().x(), r.bottom() - 6);
        painter.drawEllipse(QPointF(r.center().x(), r.bottom() - 3), 1, 1);
        break;
    case IconKind::Action:
        painter.drawLine(r.center().x(), r.top(), r.center().x(), r.bottom());
        painter.drawLine(r.left(), r.center().y(), r.right(), r.center().y());
        break;
    case IconKind::Arm:
        painter.drawLine(r.left() + 3, r.bottom() - 3, r.left() + 3, r.top() + 5);
        painter.drawLine(r.left() + 3, r.top() + 5, r.right() - 4, r.top() + 5);
        painter.drawEllipse(QPointF(r.left() + 3, r.top() + 5), 2, 2);
        painter.drawEllipse(QPointF(r.right() - 4, r.top() + 5), 2, 2);
        break;
    case IconKind::List:
        for (int i = 0; i < 3; ++i)
        {
            const qreal y = r.top() + 3 + i * 6;
            painter.drawEllipse(QPointF(r.left() + 2, y), 1, 1);
            painter.drawLine(r.left() + 6, y, r.right() - 1, y);
        }
        break;
    }
}

CardWidget::CardWidget(const QString &title, const IconKind icon, QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("card"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("cardHeader"));
    m_headerLayout = new QHBoxLayout(header);
    m_headerLayout->setContentsMargins(14, 10, 14, 9);
    m_headerLayout->setSpacing(8);
    auto *iconWidget = new IconWidget(icon, header);
    iconWidget->setFixedSize(20, 20);
    m_headerLayout->addWidget(iconWidget);
    m_titleLabel = new QLabel(title, header);
    m_titleLabel->setObjectName(QStringLiteral("cardTitle"));
    m_headerLayout->addWidget(m_titleLabel);
    m_headerLayout->addStretch();
    outer->addWidget(header);

    auto *content = new QWidget(this);
    m_contentLayout = new QVBoxLayout(content);
    m_contentLayout->setContentsMargins(14, 12, 14, 14);
    m_contentLayout->setSpacing(10);
    outer->addWidget(content, 1);
}

QVBoxLayout *CardWidget::contentLayout() const
{
    return m_contentLayout;
}

QHBoxLayout *CardWidget::headerLayout() const
{
    return m_headerLayout;
}

QLabel *CardWidget::titleLabel() const
{
    return m_titleLabel;
}

QLabel *makeLabel(const QString &text, const QString &objectName)
{
    auto *label = new QLabel(text);
    if (!objectName.isEmpty())
    {
        label->setObjectName(objectName);
    }
    return label;
}

QLabel *makeMetricLabel(const QString &text)
{
    return makeLabel(text, QStringLiteral("metricLabel"));
}

QLabel *makeMetricValue(const QString &text)
{
    return makeLabel(text, QStringLiteral("metricValue"));
}

QLabel *makeStatusPill(const QString &text, const QString &statusObjectName)
{
    return makeLabel(text, statusObjectName);
}

QPushButton *makeButton(const QString &text, const QString &objectName, QWidget *parent)
{
    auto *button = new AppFluentButton(text, parent);
    if (!objectName.isEmpty())
    {
        button->setObjectName(objectName);
    }
    // 旧 QSS 中的对象名继续保留给主题和测试使用；Fluent-Qt 自绘按钮需要
    // 同时设置自己的语义样式，否则 primaryButton 会退化成普通白色按钮。
    if (objectName == QStringLiteral("primaryButton"))
    {
        button->setFluentStyle(fluent::basicinput::Button::Accent);
    }
    else if (objectName == QStringLiteral("dangerButton"))
    {
        button->setCriticalOnHover(true);
    }
    return button;
}

QWidget *makePageHeader(const QString &title, const QString &subtitle, const QString &badgeText)
{
    auto *header = new QWidget;
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(2, 0, 2, 5);
    layout->setSpacing(12);
    auto *textColumn = new QWidget(header);
    auto *textLayout = new QVBoxLayout(textColumn);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    textLayout->addWidget(makeLabel(title, QStringLiteral("pageTitle")));
    textLayout->addWidget(makeLabel(subtitle, QStringLiteral("pageSubtitle")));
    layout->addWidget(textColumn);
    layout->addStretch();
    layout->addWidget(makeLabel(badgeText, QStringLiteral("pageBadge")), 0, Qt::AlignTop);
    return header;
}

} // namespace rov
