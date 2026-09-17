#include "ui/common/AnimatedNavButton.h"

#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

namespace rov
{

AnimatedNavButton::AnimatedNavButton(const QString &title, const QString &description,
                                     const IconKind icon, const QString &assetPath,
                                     const int columns, const int rows, QWidget *parent)
    : QToolButton(parent), m_title(title), m_description(description), m_fallbackIcon(icon)
{
    // 正常状态只显示图标，文字放到悬停浮层，避免改变侧栏宽度或挤动页面布局。
    setObjectName(QStringLiteral("navButton"));
    setAccessibleName(title);
    setToolTip(title);
    setText(QString());
    setIconSize(QSize(22, 22));
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setCheckable(true);
    setAutoExclusive(true);
    setMinimumHeight(48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    loadFrames(assetPath, columns, rows);
    m_animationTimer = new QTimer(this);
    m_animationTimer->setInterval(30);
    connect(m_animationTimer, &QTimer::timeout, this, &AnimatedNavButton::advanceAnimation);
    connect(this, &QAbstractButton::toggled, this,
            [this](const bool) { updateDisplayedFrame(); });

    refreshTheme();
    updateDisplayedFrame();
}

void AnimatedNavButton::refreshTheme()
{
    rebuildTintedFrames();
    updateDisplayedFrame();
}

void AnimatedNavButton::enterEvent(QEvent *event)
{
    QToolButton::enterEvent(event);
    if (m_frames.size() > 1)
    {
        // 从当前帧继续前进；如果此时正在反向播放，方向会即时反转。
        m_targetFrame = m_frames.size() - 1;
        m_direction = 1;
        m_animationTimer->start();
    }
    showHoverCard();
}

void AnimatedNavButton::leaveEvent(QEvent *event)
{
    QToolButton::leaveEvent(event);
    if (m_frames.size() > 1)
    {
        // 离开时从当前帧立即反向回到静态首帧，不跳回、不重新正向播放。
        m_targetFrame = 0;
        m_direction = -1;
        m_animationTimer->start();
    }
    hideHoverCard();
}

void AnimatedNavButton::changeEvent(QEvent *event)
{
    QToolButton::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
    {
        refreshTheme();
    }
}

void AnimatedNavButton::loadFrames(const QString &assetPath, const int columns, const int rows)
{
    const QImage sheet(assetPath);
    if (sheet.isNull() || columns <= 0 || rows <= 0)
    {
        return;
    }

    // 按资源实际宽高切格，支持 4×2 和 8×1，不把单帧像素尺寸写死在控件里。
    for (int row = 0; row < rows; ++row)
    {
        const int y0 = row * sheet.height() / rows;
        const int y1 = (row + 1) * sheet.height() / rows;
        for (int column = 0; column < columns; ++column)
        {
            const int x0 = column * sheet.width() / columns;
            const int x1 = (column + 1) * sheet.width() / columns;
            const int side = qMin(x1 - x0, y1 - y0);
            const QRect frameRect(x0 + (x1 - x0 - side) / 2,
                                  y0 + (y1 - y0 - side) / 2, side, side);
            QImage frame = sheet.copy(frameRect).convertToFormat(
                QImage::Format_ARGB32_Premultiplied);
            if (!frame.isNull())
            {
                m_frames.append(frame);
            }
        }
    }
}

void AnimatedNavButton::rebuildTintedFrames()
{
    m_normalFrames.clear();
    m_activeFrames.clear();
    if (m_frames.isEmpty())
    {
        return;
    }

    // 只从当前 Qt 调色板读取颜色，资源本身只承担单色 alpha 蒙版。
    const QPalette currentPalette = palette();
    QColor normalColor = currentPalette.color(QPalette::Active, QPalette::ButtonText);
    QColor activeColor = currentPalette.color(QPalette::Active, QPalette::Highlight);
    if (!normalColor.isValid())
    {
        normalColor = currentPalette.color(QPalette::Text);
    }
    if (!activeColor.isValid())
    {
        activeColor = normalColor;
    }

    for (const QImage &frame : m_frames)
    {
        m_normalFrames.append(tintFrame(frame, normalColor));
        m_activeFrames.append(tintFrame(frame, activeColor));
    }
}

void AnimatedNavButton::updateDisplayedFrame()
{
    if (m_frames.isEmpty())
    {
        setIcon(makeIcon(m_fallbackIcon));
        return;
    }

    m_currentFrame = qBound(0, m_currentFrame, m_frames.size() - 1);
    const QVector<QPixmap> &frames = isChecked() ? m_activeFrames : m_normalFrames;
    if (!frames.isEmpty())
    {
        setIcon(QIcon(frames.at(m_currentFrame)));
    }
}

void AnimatedNavButton::advanceAnimation()
{
    if (m_frames.isEmpty() || m_direction == 0 || m_currentFrame == m_targetFrame)
    {
        m_animationTimer->stop();
        return;
    }

    m_currentFrame += m_direction;
    m_currentFrame = qBound(0, m_currentFrame, m_frames.size() - 1);
    updateDisplayedFrame();
    if (m_currentFrame == m_targetFrame)
    {
        m_animationTimer->stop();
    }
}

QPixmap AnimatedNavButton::tintFrame(const QImage &frame, const QColor &color) const
{
    QPixmap tinted = QPixmap::fromImage(frame);
    QPainter painter(&tinted);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    return tinted;
}

void AnimatedNavButton::showHoverCard()
{
    if (m_hoverCard == nullptr)
    {
        m_hoverCard = new QFrame(window());
        m_hoverCard->setObjectName(QStringLiteral("navTooltip"));
        m_hoverCard->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *layout = new QVBoxLayout(m_hoverCard);
        layout->setContentsMargins(10, 7, 10, 7);
        layout->setSpacing(2);
        auto *title = new QLabel(m_title, m_hoverCard);
        title->setObjectName(QStringLiteral("navTooltipTitle"));
        auto *description = new QLabel(m_description, m_hoverCard);
        description->setObjectName(QStringLiteral("navTooltipDescription"));
        description->setWordWrap(true);
        layout->addWidget(title);
        layout->addWidget(description);
        m_hoverCard->adjustSize();

        m_hoverOpacity = new QGraphicsOpacityEffect(m_hoverCard);
        m_hoverCard->setGraphicsEffect(m_hoverOpacity);
        m_hoverFade = new QPropertyAnimation(m_hoverOpacity, "opacity", this);
        m_hoverFade->setDuration(140);
    }

    updateHoverCardPosition();
    m_hoverCard->show();
    m_hoverCard->raise();
    m_hoverFade->stop();
    m_hoverFade->setStartValue(0.0);
    m_hoverFade->setEndValue(1.0);
    m_hoverFade->start();
}

void AnimatedNavButton::hideHoverCard()
{
    if (m_hoverCard == nullptr)
    {
        return;
    }
    m_hoverFade->stop();
    m_hoverFade->setStartValue(m_hoverOpacity->opacity());
    m_hoverFade->setEndValue(0.0);
    connect(m_hoverFade, &QPropertyAnimation::finished, m_hoverCard, &QWidget::hide,
            Qt::UniqueConnection);
    m_hoverFade->start();
}

void AnimatedNavButton::updateHoverCardPosition()
{
    if (m_hoverCard == nullptr || window() == nullptr)
    {
        return;
    }
    const QPoint topRight = mapTo(window(), QPoint(width() + 8, 0));
    m_hoverCard->adjustSize();
    m_hoverCard->move(topRight);
}

} // namespace rov
