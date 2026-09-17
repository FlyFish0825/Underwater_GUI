#include "app/MainWindow.h"

#include "pages/dashboard/DashboardPage.h"
#include "pages/firmware/FirmwarePage.h"
#include "pages/manipulator/ManipulatorPage.h"
#include "pages/motor_debug/MotorDebugPage.h"
#include "pages/settings/SettingsPlaceholder.h"
#include "pages/vision/VisionPage.h"
#include "ui/common/UiPrimitives.h"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace
{

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

QToolButton *navigationButton(const QString &text, const rov::IconKind icon, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("navButton"));
    button->setText(text);
    button->setIcon(rov::makeIcon(icon));
    button->setIconSize(QSize(22, 22));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setCheckable(true);
    button->setAutoExclusive(true);
    button->setMinimumHeight(48);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return button;
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
    resize(1440, 900);
    setMinimumSize(1180, 720);

    auto *root = new QWidget(this);
    root->setObjectName(QStringLiteral("appRoot"));
    setCentralWidget(root);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *topBar = new TitleBarFrame(root);
    topBar->setObjectName(QStringLiteral("topBar"));
    topBar->setFixedHeight(48);
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
    minimize->setFixedSize(42, 48);
    minimize->setFocusPolicy(Qt::NoFocus);
    auto *maximize = new QToolButton(topBar);
    maximize->setObjectName(QStringLiteral("windowButton"));
    maximize->setText(QStringLiteral("□"));
    maximize->setFixedSize(42, 48);
    maximize->setFocusPolicy(Qt::NoFocus);
    auto *close = new QToolButton(topBar);
    close->setObjectName(QStringLiteral("closeButton"));
    close->setText(QStringLiteral("×"));
    close->setFixedSize(42, 48);
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
    sidebar->setFixedWidth(216);
    auto *navLayout = new QVBoxLayout(sidebar);
    navLayout->setContentsMargins(8, 14, 8, 12);
    navLayout->setSpacing(5);
    m_navGroup = new QButtonGroup(this);
    m_navGroup->setExclusive(true);
    const struct NavEntry
    {
        QString label;
        IconKind icon;
    } entries[] = {{QStringLiteral("总览"), IconKind::Dashboard},
                   {QStringLiteral("电机调试"), IconKind::Motor},
                   {QStringLiteral("固件升级"), IconKind::Firmware},
                   {QStringLiteral("机械臂"), IconKind::Manipulator},
                   {QStringLiteral("视觉"), IconKind::Vision},
                   {QStringLiteral("设置"), IconKind::Settings}};
    QVector<QToolButton *> buttons;
    for (int i = 0; i < 6; ++i)
    {
        auto *button = navigationButton(entries[i].label, entries[i].icon, sidebar);
        m_navGroup->addButton(button, i);
        navLayout->addWidget(button);
        buttons.append(button);
    }
    navLayout->addStretch();
    navLayout->addWidget(
        makeLabel(QStringLiteral("ROV-UI-2.1-integrated"), QStringLiteral("mutedLabel")));
    bodyLayout->addWidget(sidebar);

    m_pages = new QStackedWidget(body);
    m_pages->setObjectName(QStringLiteral("pageStack"));
    m_pages->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto *dashboard = new DashboardPage(m_pages);
    auto *motorDebug = new MotorDebugPage(m_pages);
    auto *firmware = new FirmwarePage(m_pages);
    auto *manipulator = new ManipulatorPage(m_pages);
    auto *vision = new VisionPage(m_pages);
    auto *settings = new SettingsPlaceholder(m_pages);
    m_pages->addWidget(dashboard);
    m_pages->addWidget(motorDebug);
    m_pages->addWidget(firmware);
    m_pages->addWidget(manipulator);
    m_pages->addWidget(vision);
    m_pages->addWidget(settings);
    m_pages->setMinimumSize(QSize());
    m_pageScroll = new QScrollArea(body);
    m_pageScroll->setObjectName(QStringLiteral("pageScroll"));
    m_pageScroll->setFrameShape(QFrame::NoFrame);
    m_pageScroll->setWidgetResizable(false);
    m_pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_pageScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_pageScroll->setWidget(m_pages);
    bodyLayout->addWidget(m_pageScroll, 1);
    rootLayout->addWidget(body, 1);

    auto *footer = new QFrame(root);
    footer->setObjectName(QStringLiteral("bottomBar"));
    footer->setFixedHeight(40);
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

    const bool compactWindow = width() < 1360 || height() < 820;
    m_pages->setMinimumSize(compactWindow ? QSize(920, 820) : QSize());
    updatePageViewport();
    QTimer::singleShot(0, this, [this]() { updatePageViewport(); });
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updatePageViewport();
}

void MainWindow::updatePageViewport()
{
    if (m_pages == nullptr || m_pageScroll == nullptr)
    {
        return;
    }
    const bool compactWindow = width() < 1360 || height() < 820;
    m_pages->setMinimumSize(compactWindow ? QSize(920, 820) : QSize());
    m_pageScroll->setHorizontalScrollBarPolicy(compactWindow ? Qt::ScrollBarAsNeeded
                                                             : Qt::ScrollBarAlwaysOff);
    m_pageScroll->setVerticalScrollBarPolicy(compactWindow ? Qt::ScrollBarAsNeeded
                                                           : Qt::ScrollBarAlwaysOff);
    const QSize viewportSize = m_pageScroll->viewport()->size();
    if (viewportSize.isEmpty())
    {
        return;
    }
    if (compactWindow)
    {
        m_pages->resize(qMax(920, viewportSize.width()), qMax(820, viewportSize.height()));
    }
    else
    {
        m_pages->resize(viewportSize);
    }
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
        if (m_navGroup != nullptr && m_navGroup->button(index) != nullptr)
        {
            m_navGroup->button(index)->setChecked(true);
        }
    }
}

} // namespace rov
