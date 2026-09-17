#pragma once

#include "ui/common/UiPrimitives.h"

#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QToolButton>
#include <QVector>

class QEvent;
class QFrame;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QTimer;

namespace rov
{

// 可复用的侧栏动画导航按钮。
// 该控件只负责图标帧、主题着色和悬停提示，不负责页面切换或通信业务。
class AnimatedNavButton final : public QToolButton
{
  public:
    AnimatedNavButton(const QString &title, const QString &description, IconKind icon,
                      const QString &assetPath, int columns, int rows,
                      QWidget *parent = nullptr);

    // 主题或样式表改变后重新生成着色缓存，避免把颜色写死在动画组件中。
    void refreshTheme();

  protected:
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    void loadFrames(const QString &assetPath, int columns, int rows);
    void rebuildTintedFrames();
    void updateDisplayedFrame();
    void advanceAnimation();
    void showHoverCard();
    void hideHoverCard();
    void updateHoverCardPosition();
    QPixmap tintFrame(const QImage &frame, const QColor &color) const;

    QString m_title;
    QString m_description;
    IconKind m_fallbackIcon;
    QVector<QImage> m_frames;
    QVector<QPixmap> m_normalFrames;
    QVector<QPixmap> m_activeFrames;
    QTimer *m_animationTimer = nullptr;
    QFrame *m_hoverCard = nullptr;
    QGraphicsOpacityEffect *m_hoverOpacity = nullptr;
    QPropertyAnimation *m_hoverFade = nullptr;
    int m_currentFrame = 0;
    int m_targetFrame = 0;
    int m_direction = 0;
};

} // namespace rov
