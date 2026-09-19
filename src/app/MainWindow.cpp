#include "app/MainWindow.h"

#include "pages/dashboard/DashboardPage.h"
#include "pages/firmware/FirmwarePage.h"
#include "pages/manipulator/ManipulatorPage.h"
#include "pages/motor_debug/MotorDebugPage.h"
#include "data/services/ObserverMotorDataService.h"
#include "pages/settings/SettingsPlaceholder.h"
#include "pages/vision/VisionPage.h"
#include "ui/common/AnimatedNavButton.h"
#include "ui/common/UiPrimitives.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QFrame>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QtMath>
#include <QVector>

#include <functional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{

constexpr int kMinimumWindowWidth = 980;
constexpr int kMinimumWindowHeight = 660;
constexpr double kWindowAspectRatio = 1800.0 / 1211.0;

class TitleBarFrame final : public QFrame
{
  public:
    explicit TitleBarFrame(QWidget *parent = nullptr) : QFrame(parent)
    {
        setMouseTracking(true);
    }

    void setDoubleClickHandler(std::function<void()> handler)
    {
        m_doubleClickHandler = std::move(handler);
    }

    void setRestoreFromMaximizedHandler(std::function<void()> handler)
    {
        m_restoreFromMaximizedHandler = std::move(handler);
    }

  protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            m_dragging = true;
            m_wasMaximized = window()->isMaximized();
            if (m_wasMaximized)
            {
                m_restoreRatio =
                    qBound(0.1, static_cast<double>(event->pos().x()) / qMax(1, width()), 0.9);
                m_restoreOffsetY = event->pos().y();
            }
            else
            {
                m_dragOffset = event->globalPos() - window()->frameGeometry().topLeft();
            }
        }
        QFrame::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_dragging && (event->buttons() & Qt::LeftButton))
        {
            if (window()->isMaximized() && m_wasMaximized)
            {
                if (m_restoreFromMaximizedHandler)
                {
                    m_restoreFromMaximizedHandler();
                }
                m_wasMaximized = false;
                const int restoredX =
                    event->globalPos().x() - static_cast<int>(window()->width() * m_restoreRatio);
                const int restoredY = event->globalPos().y() - m_restoreOffsetY;
                window()->move(restoredX, restoredY);
            }
            else if (!window()->isMaximized())
            {
                window()->move(event->globalPos() - m_dragOffset);
            }
        }
        QFrame::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_dragging = false;
        m_wasMaximized = false;
        QFrame::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_doubleClickHandler)
        {
            m_doubleClickHandler();
        }
        QFrame::mouseDoubleClickEvent(event);
    }

  private:
    QPoint m_dragOffset;
    bool m_dragging = false;
    bool m_wasMaximized = false;
    double m_restoreRatio = 0.5;
    int m_restoreOffsetY = 0;
    std::function<void()> m_doubleClickHandler;
    std::function<void()> m_restoreFromMaximizedHandler;
};

QString navigationAsset(const rov::IconKind icon)
{
    switch (icon)
    {
    case rov::IconKind::Dashboard:
        return QStringLiteral(":/icons/nav/dashboard.png");
    case rov::IconKind::Motor:
        return QStringLiteral(":/icons/nav/motor_debug.png");
    case rov::IconKind::Firmware:
        return QStringLiteral(":/icons/nav/firmware.png");
    case rov::IconKind::Manipulator:
        return QStringLiteral(":/icons/nav/manipulator.png");
    case rov::IconKind::Vision:
        return QStringLiteral(":/icons/nav/vision.png");
    case rov::IconKind::Settings:
        return QStringLiteral(":/icons/nav/settings.png");
    default:
        return QString();
    }
}

QToolButton *navigationButton(const QString &text, const QString &description,
                              const rov::IconKind icon, QWidget *parent)
{
    const QString asset = navigationAsset(icon);
    auto *button = new rov::AnimatedNavButton(text, description, icon, asset,
                                              8, 1, 0, parent);
    return button;
}

QSize initialWindowSize(QScreen *screen)
{
    if (screen == nullptr)
    {
        return QSize(1440, qRound(1440.0 / kWindowAspectRatio));
    }
    const QRect available = screen->availableGeometry();
    const int maxWidth = qMax(kMinimumWindowWidth, available.width() - 32);
    const int maxHeight = qMax(kMinimumWindowHeight, available.height() - 32);
    int width = qMin(1440, maxWidth);
    int height = qRound(width / kWindowAspectRatio);
    if (height > maxHeight)
    {
        height = maxHeight;
        width = qRound(height * kWindowAspectRatio);
    }
    return QSize(qMax(kMinimumWindowWidth, width), qMax(kMinimumWindowHeight, height));
}

QLabel *statusDotLabel(const QString &text, const QString &color)
{
    auto *label = new QLabel(QStringLiteral("●  %1").arg(text));
    label->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(color));
    return label;
}

} // namespace

namespace rov
{

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowTitle(QStringLiteral("水下机器人上位机"));
    setContentsMargins(1, 1, 1, 1);
    setMinimumSize(kMinimumWindowWidth, kMinimumWindowHeight);
    resize(initialWindowSize(QGuiApplication::primaryScreen()));

    auto *root = new QFrame(this);
    root->setObjectName(QStringLiteral("appRoot"));
    setCentralWidget(root);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *topBar = new TitleBarFrame(root);
    topBar->setObjectName(QStringLiteral("topBar"));
    // 顶部工具区保持紧凑，把垂直空间优先留给页面主体。
    topBar->setFixedHeight(44);
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(20, 0, 18, 0);
    topLayout->setSpacing(10);
    auto *brandIcon = new IconWidget(IconKind::Brand, topBar);
    brandIcon->setFixedSize(28, 28);
    topLayout->addWidget(brandIcon);
    topLayout->addWidget(
        makeLabel(QStringLiteral("水下机器人上位机"), QStringLiteral("brandLabel")));
    topLayout->addStretch();
    topLayout->addWidget(statusDotLabel(QStringLiteral("演示 / 离线"), QStringLiteral("#078d4a")));
    topLayout->addWidget(
        makeLabel(QStringLiteral("|  ROV-001  |  演示数据"), QStringLiteral("mutedLabel")));

    auto *minimize = new QToolButton(topBar);
    minimize->setObjectName(QStringLiteral("windowButton"));
    minimize->setText(QStringLiteral("—"));
    minimize->setFixedSize(42, 44);
    minimize->setFocusPolicy(Qt::NoFocus);
    auto *maximize = new QToolButton(topBar);
    maximize->setObjectName(QStringLiteral("windowButton"));
    maximize->setText(QStringLiteral("□"));
    maximize->setFixedSize(42, 44);
    maximize->setFocusPolicy(Qt::NoFocus);
    auto *close = new QToolButton(topBar);
    close->setObjectName(QStringLiteral("closeButton"));
    close->setText(QStringLiteral("×"));
    close->setFixedSize(42, 44);
    close->setFocusPolicy(Qt::NoFocus);
    topLayout->addWidget(minimize);
    topLayout->addWidget(maximize);
    topLayout->addWidget(close);
    const auto toggleMaximized = [this, maximize]()
    {
        if (isMaximized())
        {
            showNormal();
            maximize->setText(QStringLiteral("□"));
        }
        else
        {
            showMaximized();
            maximize->setText(QStringLiteral("❐"));
        }
    };
    topBar->setDoubleClickHandler(toggleMaximized);
    topBar->setRestoreFromMaximizedHandler(
        [this, maximize]()
        {
            showNormal();
            maximize->setText(QStringLiteral("□"));
        });
    connect(minimize, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(maximize, &QToolButton::clicked, this, toggleMaximized);
    connect(close, &QToolButton::clicked, this, &QWidget::close);
    rootLayout->addWidget(topBar);

    auto *body = new QWidget(root);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    auto *sidebar = new QFrame(body);
    sidebar->setObjectName(QStringLiteral("sideBar"));
    // 侧栏采用图片导航样式，悬停名称由按钮浮层显示，把页面空间留给主体内容。
    sidebar->setFixedWidth(76);
    auto *navLayout = new QVBoxLayout(sidebar);
    navLayout->setContentsMargins(8, 14, 8, 12);
    navLayout->setSpacing(6);
    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    const struct NavEntry
    {
        QString label;
        QString description;
        IconKind icon;
    } entries[] = {{QStringLiteral("总览"), QStringLiteral("机器人状态 · 控制"),
                    IconKind::Dashboard},
                   {QStringLiteral("电机调试"), QStringLiteral("实时波形 · FOC 参数"), IconKind::Motor},
                   {QStringLiteral("固件升级"), QStringLiteral("Bootloader · CAN 节点"),
                    IconKind::Firmware},
                   {QStringLiteral("机械臂"), QStringLiteral("关节 · 笛卡尔控制"), IconKind::Manipulator},
                   {QStringLiteral("视觉"), QStringLiteral("相机 · 图像处理"), IconKind::Vision},
                   {QStringLiteral("设置"), QStringLiteral("系统 · 通信配置"), IconKind::Settings}};
    QVector<QToolButton *> buttons;
    for (int i = 0; i < 6; ++i)
    {
        auto *button = navigationButton(entries[i].label, entries[i].description, entries[i].icon,
                                        sidebar);
        m_navGroup->addButton(button, i);
        navLayout->addWidget(button);
        buttons.append(button);
    }
    navLayout->addStretch();
    bodyLayout->addWidget(sidebar);

    m_pages = new QStackedWidget;
    m_pages->setObjectName(QStringLiteral("pageStack"));
    m_pages->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // 页面先无父级创建，再按导航顺序显式加入 QStackedWidget。
    // 避免 QStackedWidget 在滚动容器重新挂载页面时自动改变索引。
    auto *dashboard = new DashboardPage;
    auto *motorDebug = new MotorDebugPage;
    auto *firmware = new FirmwarePage;
    auto *manipulator = new ManipulatorPage;
    auto *vision = new VisionPage;
    auto *settings = new SettingsPlaceholder;
    const auto scrollablePage = [](QWidget *page, const QString &objectName)
    {
        auto *scroll = new QScrollArea;
        scroll->setObjectName(objectName);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setWidget(page);
        return scroll;
    };
    auto *motorDebugScroll =
        scrollablePage(motorDebug, QStringLiteral("motorDebugPageScroll"));
    auto *firmwareScroll = scrollablePage(firmware, QStringLiteral("firmwarePageScroll"));
    m_pages->addWidget(dashboard);
    m_pages->addWidget(motorDebugScroll);
    m_pages->addWidget(firmwareScroll);
    m_pages->addWidget(manipulator);
    m_pages->addWidget(vision);
    m_pages->addWidget(settings);

    // 组合根把同一条网关 CAN 帧流交给数据服务；UI 页面只订阅快照，
    // 不直接接触 USB CDC、AA55 或 CAN ID。
    m_motorData = new ObserverMotorDataService(this);
    connect(firmware->communicationService(), &BootloaderCommunicationService::frameReceived,
            this,
            [this](const CanGatewayFrame &frame)
            {
                if (m_motorData != nullptr)
                    m_motorData->handleCanFrame(frame);
            });
    connect(firmware->communicationService(), &BootloaderCommunicationService::closed,
            m_motorData, &ObserverMotorDataService::reset);
    // 机械臂、视觉和设置页保留原有的小屏幕等比保护。
    // 电机调试与固件升级页改由页面内部自适应，避免宽屏下整页缩小后两侧留白。
    for (QWidget *page : {static_cast<QWidget *>(manipulator), static_cast<QWidget *>(vision),
                          static_cast<QWidget *>(settings)})
    {
        page->setProperty("fitViewportScale", true);
    }
    m_pageView = new QGraphicsView(body);
    m_pageView->setObjectName(QStringLiteral("pageView"));
    m_pageView->setFrameShape(QFrame::NoFrame);
    m_pageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 页面固定设计尺寸不再垂直居中，顶部与标题栏紧贴，避免窗口上方留下大块空白。
    m_pageView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_pageView->setInteractive(true);
    m_pageScene = new QGraphicsScene(m_pageView);
    m_pageScene->setBackgroundBrush(Qt::NoBrush);
    m_pageProxy = m_pageScene->addWidget(m_pages);
    m_pageView->setScene(m_pageScene);
    bodyLayout->addWidget(m_pageView, 1);
    // USB CDC 连接检查属于全局状态，不放在 Bootloader 页面内部。
    if (QWidget *connectionBar = firmware->connectionBar())
    {
        rootLayout->insertWidget(1, connectionBar, 0);
    }
    rootLayout->addWidget(body, 1);

    auto *footer = new QFrame(root);
    footer->setObjectName(QStringLiteral("bottomBar"));
    footer->setFixedHeight(34);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(18, 0, 18, 0);
    footerLayout->setSpacing(18);
    m_footerStatus = statusDotLabel(QStringLiteral("系统就绪 · 演示"), QStringLiteral("#078d4a"));
    footerLayout->addWidget(m_footerStatus);
    footerLayout->addWidget(
        makeLabel(QStringLiteral("2026-09-16 18:21:04"), QStringLiteral("mutedLabel")));
    footerLayout->addStretch();
    m_footerLog =
        makeLabel(QStringLiteral("日志：信息   |   设备：未连接"), QStringLiteral("mutedLabel"));
    footerLayout->addWidget(m_footerLog);
    rootLayout->addWidget(footer);

    connect(m_navGroup, &QButtonGroup::idClicked, this, &MainWindow::selectPage);
    buttons.at(0)->setChecked(true);
    m_pages->setCurrentIndex(0);

    connect(dashboard, &DashboardPage::armRequested, this,
            [this]() { handleRequest(QStringLiteral("总览：请求解锁电机")); });
    connect(dashboard, &DashboardPage::disarmRequested, this,
            [this]() { handleRequest(QStringLiteral("总览：请求停用电机")); });
    connect(dashboard, &DashboardPage::holdPositionRequested, this,
            [this]() { handleRequest(QStringLiteral("总览：请求保持位置")); });
    connect(dashboard, &DashboardPage::surfaceRequested, this,
            [this]() { handleRequest(QStringLiteral("总览：请求紧急上浮")); });
    connect(motorDebug, &MotorDebugPage::parameterWriteRequested, this,
            [this](const MotorParameterRequest &)
            { handleRequest(QStringLiteral("电机调试：请求写入参数")); });
    connect(motorDebug, &MotorDebugPage::captureRequested, this,
            [this](const MotorCaptureRequest &)
            { handleRequest(QStringLiteral("电机调试：请求采集数据")); });
    connect(motorDebug, &MotorDebugPage::speedControlRequested, this,
            [this](const MotorSpeedControlRequest &request)
            {
                handleRequest(QStringLiteral("电机调试：%1 Node %2 · %3 rpm")
                                  .arg(request.enabled ? QStringLiteral("启动")
                                                       : QStringLiteral("停止"))
                                  .arg(request.motorId)
                                  .arg(request.targetRpm));
            });
    connect(firmware, &FirmwarePage::upgradeRequested, this,
            [this](const FirmwareUpgradeRequest &)
            { handleRequest(QStringLiteral("固件升级：已记录升级请求；未连接 Bootloader")); });
    connect(firmware, &FirmwarePage::verifyRequested, this,
            [this]() { handleRequest(QStringLiteral("固件升级：已记录校验请求")); });
    connect(manipulator, &ManipulatorPage::homeRequested, this,
            [this]() { handleRequest(QStringLiteral("机械臂：请求归零")); });
    connect(manipulator, &ManipulatorPage::stopRequested, this,
            [this]() { handleRequest(QStringLiteral("机械臂：请求停止")); });
    connect(vision, &VisionPage::visionModeRequested, this,
            [this](const VisionModeRequest &)
            { handleRequest(QStringLiteral("视觉：已记录显示模式请求")); });

    updatePageViewport();
    QTimer::singleShot(0, this, [this]() { updatePageViewport(); });
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updatePageViewport();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (auto *firmware = findChild<FirmwarePage *>())
        firmware->closeAuxiliaryWindows();
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!m_screenSignalConnected && windowHandle() != nullptr)
    {
        m_screenSignalConnected = true;
        connect(windowHandle(), &QWindow::screenChanged, this,
                [this](QScreen *)
                {
                    QTimer::singleShot(0, this,
                                       [this]()
                                       {
                                           fitNormalGeometryToScreen();
                                           updatePageViewport();
                                       });
                });
    }
    fitNormalGeometryToScreen();
    updatePageViewport();
}

void MainWindow::fitNormalGeometryToScreen()
{
    if (isMaximized() || isFullScreen() || windowHandle() == nullptr)
    {
        return;
    }
    QScreen *screen = windowHandle()->screen();
    if (screen == nullptr)
    {
        return;
    }

    // 只受屏幕可用区域限制，不再额外扣除 16px 边距，避免窗口尚未铺满屏幕
    // 就达到放大上限；宽高比仍由 WM_SIZING 严格保持。
    QRect available = screen->availableGeometry();
    QSize fitted = frameGeometry().size();
    int width = qMax(kMinimumWindowWidth, fitted.width());
    int height = qRound(width / kWindowAspectRatio);
    if (width > available.width() || height > available.height())
    {
        height = qMin(height, available.height());
        width = qRound(height * kWindowAspectRatio);
        width = qMin(width, available.width());
        height = qRound(width / kWindowAspectRatio);
    }
    fitted = QSize(qMax(kMinimumWindowWidth, width), qMax(kMinimumWindowHeight, height));
    if (fitted != frameGeometry().size())
    {
        resize(fitted);
    }

    QRect target(frameGeometry().topLeft(), fitted);
    if (!available.contains(target.topLeft()) || !available.contains(target.bottomRight()))
    {
        target.moveLeft(
            qBound(available.left(), target.left(), available.right() - fitted.width() + 1));
        target.moveTop(
            qBound(available.top(), target.top(), available.bottom() - fitted.height() + 1));
        move(target.topLeft());
    }
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" && message != nullptr && result != nullptr &&
        !isMaximized() && !isFullScreen())
    {
        auto *msg = static_cast<MSG *>(message);
        if (msg->message == WM_SIZING && msg->lParam != 0)
        {
            auto *rect = reinterpret_cast<RECT *>(msg->lParam);
            const LONG proposedWidth = qMax<LONG>(1, rect->right - rect->left);
            const LONG proposedHeight = qMax<LONG>(1, rect->bottom - rect->top);
            LONG width = proposedWidth;
            LONG height = proposedHeight;
            const auto edge = static_cast<UINT>(msg->wParam);

            if (edge == WMSZ_LEFT || edge == WMSZ_RIGHT)
            {
                height = qRound(width / kWindowAspectRatio);
                const LONG centerY = (rect->top + rect->bottom) / 2;
                rect->top = centerY - height / 2;
                rect->bottom = rect->top + height;
            }
            else if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM)
            {
                width = qRound(height * kWindowAspectRatio);
                const LONG centerX = (rect->left + rect->right) / 2;
                rect->left = centerX - width / 2;
                rect->right = rect->left + width;
            }
            else
            {
                width = qMax<LONG>(width, static_cast<LONG>(qRound(height * kWindowAspectRatio)));
                height = qRound(width / kWindowAspectRatio);
            }

            if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT)
            {
                rect->left = rect->right - width;
            }
            else
            {
                rect->right = rect->left + width;
            }
            if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT)
            {
                rect->top = rect->bottom - height;
            }
            else
            {
                rect->bottom = rect->top + height;
            }

            *result = TRUE;
            return true;
        }
        if (msg->message == WM_NCHITTEST)
        {
            const QPoint local = mapFromGlobal(QCursor::pos());
            constexpr int border = 6;
            const bool left = local.x() >= 0 && local.x() < border;
            const bool right = local.x() >= width() - border && local.x() < width();
            const bool top = local.y() >= 0 && local.y() < border;
            const bool bottom = local.y() >= height() - border && local.y() < height();
            if (left && top)
            {
                *result = HTTOPLEFT;
                return true;
            }
            if (right && top)
            {
                *result = HTTOPRIGHT;
                return true;
            }
            if (left && bottom)
            {
                *result = HTBOTTOMLEFT;
                return true;
            }
            if (right && bottom)
            {
                *result = HTBOTTOMRIGHT;
                return true;
            }
            if (left)
            {
                *result = HTLEFT;
                return true;
            }
            if (right)
            {
                *result = HTRIGHT;
                return true;
            }
            if (top)
            {
                *result = HTTOP;
                return true;
            }
            if (bottom)
            {
                *result = HTBOTTOM;
                return true;
            }
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::updatePageViewport()
{
    if (m_pages == nullptr || m_pageView == nullptr || m_pageScene == nullptr ||
        m_pageProxy == nullptr)
    {
        return;
    }
    const QSize viewportSize = m_pageView->viewport()->size();
    if (viewportSize.isEmpty())
    {
        return;
    }

    // 总览页始终铺满视口；复杂页面只有在其真实最小高度超出小窗口时才等比缩放，
    // 这样不会在正常尺寸下留下大块空白，也不会让底部表格/日志被裁切。
    QSize pageSize = viewportSize;
    qreal scale = 1.0;
    if (QWidget *currentPage = m_pages->currentWidget(); currentPage != nullptr &&
        currentPage->property("fitViewportScale").toBool())
    {
        const QSize required = currentPage->minimumSizeHint();
        if (required.width() > viewportSize.width() || required.height() > viewportSize.height())
        {
            pageSize = required.expandedTo(viewportSize);
            scale = qMin(static_cast<qreal>(viewportSize.width()) / pageSize.width(),
                         static_cast<qreal>(viewportSize.height()) / pageSize.height());
        }
    }
    m_pages->setMinimumSize(pageSize);
    m_pages->resize(pageSize);
    m_pageProxy->setTransform(QTransform::fromScale(scale, scale));
    m_pageProxy->setPos((viewportSize.width() - pageSize.width() * scale) / 2.0, 0.0);
    m_pageScene->setSceneRect(QRectF(QPointF(0, 0), QSizeF(viewportSize)));
}

void MainWindow::handleRequest(const QString &message)
{
    m_footerLog->setText(QStringLiteral("日志：请求   |   %1").arg(message));
    m_footerStatus->setText(QStringLiteral("●  已记录请求 · 演示"));
}

void MainWindow::selectPage(const int index)
{
    setPageIndex(index);
}

void MainWindow::setPageIndex(const int index)
{
    if (m_pages != nullptr && index >= 0 && index < m_pages->count())
    {
        m_pages->setCurrentIndex(index);
        updatePageViewport();
        if (m_navGroup != nullptr && m_navGroup->button(index) != nullptr)
        {
            m_navGroup->button(index)->setChecked(true);
        }
    }
}

} // namespace rov
