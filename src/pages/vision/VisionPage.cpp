#include "pages/vision/VisionPage.h"

#include "ui/common/AppComboBox.h"
#include "ui/common/UiPrimitives.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace
{

QWidget *infoRow(const QString &label, QLabel *&value)
{
    auto *row = new QWidget;
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto *name = rov::makeLabel(label, QStringLiteral("mutedLabel"));
    name->setFixedWidth(72);
    name->setWordWrap(true);
    name->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(name);
    value = rov::makeLabel(QStringLiteral("--"), QStringLiteral("bodyValue"));
    value->setWordWrap(true);
    value->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(value, 1);
    return row;
}

} // namespace

namespace rov
{

VisionPage::VisionPage(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(10);
    root->addWidget(makePageHeader(QStringLiteral("视觉"),
                                   QStringLiteral("本机相机实时预览与图像处理入口。"),
                                   QStringLiteral("Windows DirectShow")));

    auto *mainRow = new QHBoxLayout;
    mainRow->setSpacing(12);

    auto *cameraCard = new CardWidget(QStringLiteral("相机预览"), IconKind::Camera);
    m_preview = new QLabel(QStringLiteral("请选择相机并启动预览"));
    m_preview->setObjectName(QStringLiteral("card"));
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(520, 365);
    m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cameraCard->contentLayout()->addWidget(m_preview, 1);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(8);
    m_deviceSelect = new AppComboBox;
    m_deviceSelect->setMinimumWidth(220);
    m_deviceSelect->addItem(QStringLiteral("正在检测相机…"));
    controls->addWidget(m_deviceSelect, 1);
    auto *refresh = makeButton(QStringLiteral("刷新设备"), QStringLiteral("softButton"));
    m_startCamera = makeButton(QStringLiteral("启动预览"), QStringLiteral("primaryButton"));
    m_stopCamera = makeButton(QStringLiteral("停止"), QStringLiteral("softButton"));
    m_saveFrame = makeButton(QStringLiteral("保存当前帧"), QStringLiteral("softButton"));
    m_stopCamera->setEnabled(false);
    m_saveFrame->setEnabled(false);
    controls->addWidget(refresh);
    controls->addWidget(m_startCamera);
    controls->addWidget(m_stopCamera);
    controls->addWidget(m_saveFrame);
    cameraCard->contentLayout()->addLayout(controls);

    auto *modeCard = new CardWidget(QStringLiteral("显示模式"), IconKind::Vision);
    auto *modes = new QGridLayout;
    modes->setContentsMargins(0, 0, 0, 0);
    modes->setHorizontalSpacing(6);
    modes->setVerticalSpacing(6);
    const QStringList modeNames = {QStringLiteral("原始"),     QStringLiteral("去畸变"),
                                   QStringLiteral("折射校正"), QStringLiteral("边缘"),
                                   QStringLiteral("检测"),     QStringLiteral("位姿")};
    for (int i = 0; i < modeNames.size(); ++i)
    {
        auto *mode = makeButton(modeNames.at(i), i == 0 ? QStringLiteral("primaryButton")
                                                        : QStringLiteral("softButton"));
        modes->addWidget(mode, i / 2, i % 2);
        connect(mode, &QPushButton::clicked, this,
                [this, modeName = modeNames.at(i)]()
                {
                    emit visionModeRequested(VisionModeRequest{modeName});
                    logRequest(QStringLiteral("请求视觉模式：%1").arg(modeName));
                });
    }
    modeCard->contentLayout()->addLayout(modes);

    auto *controlCard = new CardWidget(QStringLiteral("机器人运动控制"), IconKind::Action);
    auto *controlHint =
        makeLabel(QStringLiteral("按住按钮运动，松开立即归零"), QStringLiteral("mutedLabel"));
    controlCard->contentLayout()->addWidget(controlHint);
    auto *controlGrid = new QGridLayout;
    controlGrid->setContentsMargins(0, 0, 0, 0);
    controlGrid->setHorizontalSpacing(6);
    controlGrid->setVerticalSpacing(6);
    const QStringList positiveNames = {QStringLiteral("前进"), QStringLiteral("右移"),
                                       QStringLiteral("上浮"), QStringLiteral("右滚"),
                                       QStringLiteral("抬头"), QStringLiteral("右转")};
    const QStringList negativeNames = {QStringLiteral("后退"), QStringLiteral("左移"),
                                       QStringLiteral("下潜"), QStringLiteral("左滚"),
                                       QStringLiteral("低头"), QStringLiteral("左转")};
    for (int axis = 0; axis < 6; ++axis)
    {
        auto addControlButton = [this, controlGrid, axis](const QString &text, const int row,
                                                          const double value)
        {
            auto *button = makeButton(text, QStringLiteral("softButton"));
            button->setFocusPolicy(Qt::NoFocus);
            connect(button, &QPushButton::pressed, this,
                    [this, axis, value, text]()
                    {
                        setManualAxis(axis, value);
                        logRequest(QStringLiteral("机器人控制：%1").arg(text));
                    });
            connect(button, &QPushButton::released, this,
                    [this, axis]() { setManualAxis(axis, 0.0); });
            controlGrid->addWidget(button, row, axis);
        };
        addControlButton(positiveNames.at(axis), 0, 1.0);
        addControlButton(negativeNames.at(axis), 1, -1.0);
        controlGrid->setColumnStretch(axis, 1);
    }
    auto *hold = makeButton(QStringLiteral("保持位置"), QStringLiteral("primaryButton"));
    auto *stop = makeButton(QStringLiteral("紧急停机"), QStringLiteral("dangerButton"));
    controlGrid->addWidget(hold, 0, 6);
    controlGrid->addWidget(stop, 1, 6);
    controlGrid->setColumnStretch(6, 1);
    connect(hold, &QPushButton::clicked, this,
            [this]()
            {
                releaseManualControl();
                emit holdPositionRequested();
                logRequest(QStringLiteral("请求保持位置"));
            });
    connect(stop, &QPushButton::clicked, this,
            [this]()
            {
                releaseManualControl();
                emit disarmRequested();
                logRequest(QStringLiteral("请求紧急停机"));
            });
    controlCard->contentLayout()->addLayout(controlGrid);
    cameraCard->contentLayout()->addWidget(controlCard);
    mainRow->addWidget(cameraCard, 1);

    auto *sidePanel = new QWidget;
    sidePanel->setMinimumWidth(300);
    sidePanel->setMaximumWidth(400);
    sidePanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto *side = new QVBoxLayout(sidePanel);
    side->setContentsMargins(0, 0, 0, 0);
    side->setSpacing(12);
    auto *connection = new CardWidget(QStringLiteral("相机连接"), IconKind::Camera);
    connection->contentLayout()->addWidget(infoRow(QStringLiteral("相机设备"), m_cameraDevice));
    connection->contentLayout()->addWidget(infoRow(QStringLiteral("分辨率"), m_resolution));
    connection->contentLayout()->addWidget(infoRow(QStringLiteral("标称帧率"), m_frameRate));
    connection->contentLayout()->addWidget(infoRow(QStringLiteral("像素格式"), m_pixelFormat));
    side->addWidget(connection);

    auto *stream = new CardWidget(QStringLiteral("流状态"), IconKind::Waveform);
    stream->contentLayout()->addWidget(infoRow(QStringLiteral("视频流"), m_streamState));
    stream->contentLayout()->addWidget(infoRow(QStringLiteral("最后帧"), m_lastFrame));
    stream->contentLayout()->addWidget(infoRow(QStringLiteral("实时帧率"), m_liveFrameRate));
    stream->contentLayout()->addWidget(infoRow(QStringLiteral("采集方式"), m_latency));
    side->addWidget(stream);

    auto *node = new CardWidget(QStringLiteral("视觉处理"), IconKind::Firmware);
    node->contentLayout()->addWidget(infoRow(QStringLiteral("采集后端"), m_nodeState));
    node->contentLayout()->addWidget(infoRow(QStringLiteral("处理模式"), m_processingMode));
    node->contentLayout()->addWidget(infoRow(QStringLiteral("已加载模型"), m_modelsLoaded));
    side->addWidget(node);
    side->addWidget(modeCard);
    side->addStretch();
    mainRow->addWidget(sidePanel);
    root->addLayout(mainRow, 1);

    m_requestLog = makeLabel(QStringLiteral("正在检测本机相机。"), QStringLiteral("mutedLabel"));
    root->addWidget(m_requestLog);

    connect(refresh, &QPushButton::clicked, this,
            [this]()
            {
                emit cameraControlRequested(
                    CameraControlRequest{CameraControlAction::RefreshDevices, -1});
                logRequest(QStringLiteral("刷新相机设备"));
            });
    connect(m_startCamera, &QPushButton::clicked, this,
            [this]()
            {
                emit cameraControlRequested(
                    CameraControlRequest{CameraControlAction::Start, m_deviceSelect->currentIndex()});
                logRequest(QStringLiteral("启动相机预览"));
            });
    connect(m_stopCamera, &QPushButton::clicked, this,
            [this]()
            {
                emit cameraControlRequested(CameraControlRequest{CameraControlAction::Stop, -1});
                logRequest(QStringLiteral("停止相机预览"));
            });
    connect(m_saveFrame, &QPushButton::clicked, this, &VisionPage::saveCurrentFrame);

    qApp->installEventFilter(this);
    VisionSnapshot initialSnapshot;
    initialSnapshot.cameraStamp.freshness = DataFreshness::Offline;
    initialSnapshot.cameraDevice = QStringLiteral("-");
    initialSnapshot.resolution = QStringLiteral("-");
    initialSnapshot.frameRate = QStringLiteral("-");
    initialSnapshot.pixelFormat = QStringLiteral("-");
    initialSnapshot.streamState = QStringLiteral("无视频流");
    initialSnapshot.lastFrame = QStringLiteral("-");
    initialSnapshot.latency = QStringLiteral("-");
    initialSnapshot.nodeState = QStringLiteral("未运行");
    initialSnapshot.processingMode = QStringLiteral("-");
    initialSnapshot.modelsLoaded = QStringLiteral("-");
    setSnapshot(initialSnapshot);
}

void VisionPage::setSnapshot(const VisionSnapshot &snapshot)
{
    m_snapshot = snapshot;
    refreshView();
}

void VisionPage::setAvailableCameras(const QStringList &deviceNames)
{
    const int previousIndex = m_deviceSelect->currentIndex();
    const QSignalBlocker blocker(m_deviceSelect);
    m_deviceSelect->clear();
    if (deviceNames.isEmpty())
        m_deviceSelect->addItem(QStringLiteral("未发现相机"));
    else
        m_deviceSelect->addItems(deviceNames);
    m_deviceSelect->setCurrentIndex(qBound(0, previousIndex, m_deviceSelect->count() - 1));
    m_startCamera->setEnabled(!deviceNames.isEmpty() && !m_snapshot.connected);
    logRequest(deviceNames.isEmpty() ? QStringLiteral("未发现相机")
                                     : QStringLiteral("发现 %1 个相机").arg(deviceNames.size()));
}

void VisionPage::setCameraFrame(const QImage &frame)
{
    if (frame.isNull())
        return;
    m_lastImage = frame;
    m_saveFrame->setEnabled(true);
    updatePreviewPixmap();
}

void VisionPage::showCameraError(const QString &message)
{
    logRequest(QStringLiteral("相机错误：%1").arg(message));
}

bool VisionPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationDeactivate)
        releaseManualControl();
    return QWidget::eventFilter(watched, event);
}

void VisionPage::hideEvent(QHideEvent *event)
{
    releaseManualControl();
    QWidget::hideEvent(event);
}

void VisionPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updatePreviewPixmap();
}

void VisionPage::refreshView()
{
    m_cameraDevice->setText(m_snapshot.cameraDevice);
    m_resolution->setText(m_snapshot.resolution);
    m_frameRate->setText(m_snapshot.frameRate);
    m_liveFrameRate->setText(m_snapshot.frameRate);
    m_pixelFormat->setText(m_snapshot.pixelFormat);
    m_streamState->setText(m_snapshot.streamState);
    m_lastFrame->setText(m_snapshot.lastFrame);
    m_latency->setText(m_snapshot.latency);
    m_nodeState->setText(m_snapshot.nodeState);
    m_processingMode->setText(m_snapshot.processingMode);
    m_modelsLoaded->setText(m_snapshot.modelsLoaded);
    m_startCamera->setEnabled(!m_snapshot.connected && m_deviceSelect->count() > 0 &&
                              m_deviceSelect->itemText(0) != QStringLiteral("未发现相机"));
    m_stopCamera->setEnabled(m_snapshot.connected);
}

void VisionPage::updatePreviewPixmap()
{
    if (m_lastImage.isNull() || m_preview == nullptr)
        return;
    const QSize targetSize = m_preview->contentsRect().size() - QSize(12, 12);
    m_preview->setPixmap(QPixmap::fromImage(m_lastImage).scaled(
        targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void VisionPage::saveCurrentFrame()
{
    if (m_lastImage.isNull())
        return;
    const QString fileName = QStringLiteral("rov_camera_%1.png")
                                 .arg(QDateTime::currentDateTime().toString(
                                     QStringLiteral("yyyyMMdd_HHmmss")));
    const QString initialPath =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QLatin1Char('/') +
        fileName;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存当前帧"), initialPath,
        QStringLiteral("PNG 图像 (*.png);;JPEG 图像 (*.jpg *.jpeg);;BMP 图像 (*.bmp)"));
    if (path.isEmpty())
        return;
    logRequest(m_lastImage.save(path) ? QStringLiteral("当前帧已保存：%1").arg(path)
                                      : QStringLiteral("保存当前帧失败：%1").arg(path));
}

void VisionPage::setManualAxis(const int axis, const double value)
{
    switch (axis)
    {
    case 0:
        m_manualControl.surge = value;
        break;
    case 1:
        m_manualControl.sway = value;
        break;
    case 2:
        m_manualControl.heave = value;
        break;
    case 3:
        m_manualControl.roll = value;
        break;
    case 4:
        m_manualControl.pitch = value;
        break;
    case 5:
        m_manualControl.yaw = value;
        break;
    default:
        return;
    }
    emit manualControlRequested(m_manualControl);
}

void VisionPage::releaseManualControl()
{
    const bool active = m_manualControl.surge != 0.0 || m_manualControl.sway != 0.0 ||
                        m_manualControl.heave != 0.0 || m_manualControl.roll != 0.0 ||
                        m_manualControl.pitch != 0.0 || m_manualControl.yaw != 0.0;
    if (!active)
        return;
    m_manualControl = SixDofControlRequest{};
    emit manualControlRequested(m_manualControl);
}

void VisionPage::logRequest(const QString &message)
{
    if (m_requestLog != nullptr)
        m_requestLog->setText(QStringLiteral("相机状态：%1").arg(message));
}

} // namespace rov
