#include "ui/common/AnimatedNavButton.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPropertyAnimation>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace rov
{

AnimatedNavButton::AnimatedNavButton(const QString &title, const QString &description,
                                     const IconKind icon, const QString &assetPath,
                                     const int columns, const int rows, const int frameRow,
                                     QWidget *parent)
    : QToolButton(parent), m_title(title), m_description(description), m_fallbackIcon(icon),
      m_frameRow(frameRow)
{
    // 正常状态只显示图标，文字放到悬停浮层，避免改变侧栏宽度或挤动页面布局。
    setObjectName(QStringLiteral("navButton"));
    setAccessibleName(title);
    setText(QString());
    // 固定图标视口；每帧源画布均为 180×150，只在这里统一缩放一次。
    setIconSize(QSize(36, 32));
    // 侧栏默认是图片导航，中文名称只在悬停浮层中出现。
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setCheckable(true);
    setAutoExclusive(true);
    setFocusPolicy(Qt::NoFocus);
    setMinimumHeight(48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // 侧栏很窄，鼠标在按钮边缘切换时可能产生一个瞬时 leaveEvent。
    // 延迟隐藏可以消除这个事件抖动，但不会改变真正离开后的关闭行为。
    m_hoverHideTimer = new QTimer(this);
    m_hoverHideTimer->setSingleShot(true);
    connect(m_hoverHideTimer, &QTimer::timeout, this,
            [this]()
            {
                if (!underMouse())
                {
                    hideHoverCard();
                }
            });

    // 某些窗口切换、快速移出或顶层提示框切换场景下，Qt 可能延迟发送
    // leaveEvent。定期用真实鼠标位置校验，防止蓝色图标残留。
    m_hoverStateTimer = new QTimer(this);
    m_hoverStateTimer->setInterval(50);
    connect(m_hoverStateTimer, &QTimer::timeout, this,
            [this]()
            {
                if (underMouse())
                {
                    return;
                }

                if (m_hovering)
                {
                    m_hovering = false;
                    m_targetFrame = 0;
                    m_animationDirection = -1;
                    if (m_animationTimer != nullptr && m_frames.size() > 1)
                    {
                        m_animationTimer->start();
                    }
                    updateDisplayedFrame();
                }

                if (m_hoverCard != nullptr && m_hoverCard->isVisible())
                {
                    m_hoverCard->hide();
                }
            });
    m_hoverStateTimer->start();

    m_animationTimer = new QTimer(this);
    // 受控悬停动画，避免 30ms 播放过快导致人眼误判为位置抖动。
    // 验收时可设置 NAV_ANIMATION_SLOW_MS=250 逐帧慢放检查固定锚点。
    const int slowAnimationInterval = qEnvironmentVariableIntValue("NAV_ANIMATION_SLOW_MS");
    m_animationTimer->setInterval(slowAnimationInterval > 0 ? slowAnimationInterval : 120);
    connect(m_animationTimer, &QTimer::timeout, this, &AnimatedNavButton::advanceAnimation);

    loadFrames(assetPath, columns, rows, frameRow);
    // 导航图标保持稳定首帧，避免悬停时图片帧造成上下视觉跳动。
    connect(this, &QAbstractButton::toggled, this,
            [this](const bool) { updateDisplayedFrame(); });

    refreshTheme();
    updateDisplayedFrame();
}

AnimatedNavButton::~AnimatedNavButton()
{
    delete m_hoverCard;
    m_hoverCard = nullptr;
}

void AnimatedNavButton::refreshTheme()
{
    rebuildTintedFrames();
    updateDisplayedFrame();
}

void AnimatedNavButton::enterEvent(QEvent *event)
{
    QToolButton::enterEvent(event);
    m_hovering = true;
    if (m_hoverHideTimer != nullptr)
    {
        m_hoverHideTimer->stop();
    }
    m_targetFrame = qMax(0, m_frames.size() - 1);
    m_animationDirection = 1;
    if (m_animationTimer != nullptr && m_frames.size() > 1)
    {
        m_animationTimer->start();
    }
    updateDisplayedFrame();
    showHoverCard();
}

void AnimatedNavButton::leaveEvent(QEvent *event)
{
    QToolButton::leaveEvent(event);
    m_hovering = false;
    m_targetFrame = 0;
    m_animationDirection = -1;
    if (m_animationTimer != nullptr && m_frames.size() > 1)
    {
        m_animationTimer->start();
    }
    if (m_hoverHideTimer != nullptr)
    {
        m_hoverHideTimer->start(70);
    }
    else
    {
        hideHoverCard();
    }
}

void AnimatedNavButton::changeEvent(QEvent *event)
{
    QToolButton::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
    {
        refreshTheme();
    }
}

void AnimatedNavButton::loadFrames(const QString &assetPath, const int columns, const int rows,
                                   const int frameRow)
{
    const QImage sheet(assetPath);
    if (sheet.isNull() || columns <= 0 || rows <= 0)
    {
        return;
    }

    // 按固定网格切帧。每个输出 Sprite 的每一格都是完整 180×150 画布，
    // 不做 alpha bounding-box、tight crop、独立缩放或二次居中。
    const int selectedRow = qBound(0, frameRow, rows - 1);
    const int frameWidth = sheet.width() / columns;
    const int frameHeight = sheet.height() / rows;
    const int rowY0 = selectedRow * frameHeight;
    for (int column = 0; column < columns; ++column)
    {
        const QRect frameRect(column * frameWidth, rowY0, frameWidth, frameHeight);
        QImage frame = sheet.copy(frameRect).convertToFormat(
            QImage::Format_ARGB32_Premultiplied);
        if (!frame.isNull())
        {
            m_frames.append(frame);
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
    // 蓝色主动态只跟随鼠标所在按钮；页面选中状态不再抢占悬停颜色。
    const QVector<QPixmap> &frames = underMouse() ? m_activeFrames : m_normalFrames;
    if (!frames.isEmpty())
    {
        setIcon(QIcon(frames.at(m_currentFrame)));
    }
}

void AnimatedNavButton::advanceAnimation()
{
    if (m_frames.size() <= 1 || m_animationDirection == 0 ||
        m_currentFrame == m_targetFrame)
    {
        if (m_animationTimer != nullptr)
        {
            m_animationTimer->stop();
        }
        return;
    }

    m_currentFrame = qBound(0, m_currentFrame + m_animationDirection, m_frames.size() - 1);
    updateDisplayedFrame();
    if (m_currentFrame == m_targetFrame && m_animationTimer != nullptr)
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
        // 用 Qt::ToolTip 创建独立的无激活顶层窗口，避免被 QGraphicsView、页面层或
        // QMainWindow 的子控件遮挡；透明鼠标事件保证它不会抢走导航按钮的 hover 状态。
        m_hoverCard = new QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint |
                                             Qt::NoDropShadowWindowHint);
        m_hoverCard->setObjectName(QStringLiteral("navTooltip"));
        m_hoverCard->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_hoverCard->setAttribute(Qt::WA_ShowWithoutActivating);
        m_hoverCard->setAttribute(Qt::WA_TranslucentBackground, false);
        // 侧栏外侧仍有可用空间，给说明卡片一个稳定的宽度，避免不同文案
        // 在快速切换时反复改变尺寸和位置。
        m_hoverCard->setMinimumWidth(180);
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
        connect(m_hoverFade, &QPropertyAnimation::finished, this,
                [this]()
                {
                    // 淡出动画完成后只有在确实离开按钮时才隐藏，避免旧的 finished
                    // 信号在再次 hover 时把刚显示的中文说明框误隐藏。
                    if (!m_hovering && m_hoverOpacity->opacity() <= 0.01)
                    {
                        m_hoverCard->hide();
                    }
                });
    }

    // 每个导航按钮独立管理自己的顶层提示框。快速横向移动时，先清理其他
    // 导航按钮留下的卡片，保证屏幕上始终只有当前按钮的一张说明卡片。
    const QList<QWidget *> topLevelWidgets = QApplication::topLevelWidgets();
    for (QWidget *widget : topLevelWidgets)
    {
        if (widget != m_hoverCard && widget->objectName() == QStringLiteral("navTooltip"))
        {
            widget->hide();
        }
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
    m_hoverFade->start();
}

void AnimatedNavButton::updateHoverCardPosition()
{
    if (m_hoverCard == nullptr || window() == nullptr)
    {
        return;
    }
    m_hoverCard->adjustSize();
    const QPoint buttonTopRight = mapToGlobal(QPoint(width() + 8, 0));
    QPoint position = buttonTopRight;

    // 说明框优先显示在侧栏右侧；如果窗口贴近屏幕右边，则翻到按钮左侧，
    // 并把上下位置限制在工作区内，避免出现遮挡或被屏幕边界裁切。
    QScreen *screen = QGuiApplication::screenAt(mapToGlobal(rect().center()));
    if (screen != nullptr)
    {
        const QRect available = screen->availableGeometry();
        if (position.x() + m_hoverCard->width() > available.right() + 1)
        {
            position.setX(mapToGlobal(QPoint(-m_hoverCard->width() - 8, 0)).x());
        }
        position.setY(qBound(available.top() + 4, position.y(),
                             available.bottom() - m_hoverCard->height() - 4));
    }
    m_hoverCard->move(position);
}

} // namespace rov
