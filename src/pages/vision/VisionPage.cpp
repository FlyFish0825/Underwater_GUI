#include "pages/vision/VisionPage.h"

#include "ui/common/AppComboBox.h"
#include "ui/common/AppLineEdit.h"
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
#include <QSettings>
#include <QShowEvent>
#include <QSpinBox>
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
                                   QStringLiteral("机器人双目画面与本机相机实时预览。"),
                                   QStringLiteral("相机")));

    auto *mainRow = new QHBoxLayout;
    mainRow->setSpacing(12);

    auto *cameraCard = new CardWidget(QStringLiteral("相机预览"), IconKind::Camera);
    auto *previews = new QWidget;
    previews->setMinimumSize(520, 365);
    auto *previewRow = new QHBoxLayout(previews);
    previewRow->setContentsMargins(0, 0, 0, 0);
    auto *leftPanel = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    m_leftTitle = makeLabel(QStringLiteral("左目"), QStringLiteral("mutedLabel"));
    leftLayout->addWidget(m_leftTitle);
    m_preview = new QLabel(QStringLiteral("点击启动预览，连接机器人双目相机"));
    m_preview->setObjectName(QStringLiteral("cameraLeftPreview"));
    m_preview->setProperty("class", QStringLiteral("card"));
    m_preview->setWordWrap(true);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    leftLayout->addWidget(m_preview, 1);
    m_rightPanel = new QWidget;
    auto *rightLayout = new QVBoxLayout(m_rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(makeLabel(QStringLiteral("右目"), QStringLiteral("mutedLabel")));
    m_rightPreview = new QLabel(QStringLiteral("等待相机画面"));
    m_rightPreview->setObjectName(QStringLiteral("cameraRightPreview"));
    m_rightPreview->setAlignment(Qt::AlignCenter);
    m_rightPreview->setWordWrap(true);
    m_rightPreview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    rightLayout->addWidget(m_rightPreview, 1);
    previewRow->addWidget(leftPanel, 1);
    previewRow->addWidget(m_rightPanel, 1);
    cameraCard->contentLayout()->addWidget(previews, 1);
    m_preview->installEventFilter(this);
    m_rightPreview->installEventFilter(this);

    auto *sourceRow = new QHBoxLayout;
    m_sourceSelect = new AppComboBox;
    m_sourceSelect->setObjectName(QStringLiteral("cameraSource"));
    m_sourceSelect->addItems({QStringLiteral("机器人双目"), QStringLiteral("本机相机")});
    sourceRow->addWidget(m_sourceSelect);
    m_endpointControls = new QWidget;
    auto *endpointRow = new QHBoxLayout(m_endpointControls);
    endpointRow->setContentsMargins(0, 0, 0, 0);
    QSettings settings;
    m_cameraHost = new AppLineEdit(settings.value(QStringLiteral("camera/host"),
                                                  QStringLiteral("192.168.20.70")).toString());
    m_cameraHost->setObjectName(QStringLiteral("cameraHost"));
    m_cameraHost->setPlaceholderText(QStringLiteral("机器人地址"));
    m_cameraPort = new QSpinBox;
    m_cameraPort->setObjectName(QStringLiteral("cameraPort"));
    m_cameraPort->setRange(1, 65535);
    m_cameraPort->setValue(settings.value(QStringLiteral("camera/port"), 9001).toInt());
    endpointRow->addWidget(m_cameraHost, 1);
    endpointRow->addWidget(m_cameraPort);
    sourceRow->addWidget(m_endpointControls, 1);
    cameraCard->contentLayout()->addLayout(sourceRow);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(8);
    m_deviceSelect = new AppComboBox;
    m_deviceSelect->setObjectName(QStringLiteral("localCameraDevice"));
    m_deviceSelect->setMinimumWidth(220);
    m_deviceSelect->addItem(QStringLiteral("正在检测相机…"));
    controls->addWidget(m_deviceSelect, 1);
    auto *refresh = makeButton(QStringLiteral("刷新设备"), QStringLiteral("softButton"));
    m_startCamera = makeButton(QStringLiteral("启动预览"), QStringLiteral("primaryButton"));
    m_stopCamera = makeButton(QStringLiteral("停止"), QStringLiteral("softButton"));
    m_saveFrame = makeButton(QStringLiteral("保存当前帧"), QStringLiteral("softButton"));
    m_startCamera->setObjectName(QStringLiteral("cameraStart"));
    m_stopCamera->setObjectName(QStringLiteral("cameraStop"));
    m_saveFrame->setObjectName(QStringLiteral("cameraSaveFrame"));
    m_stopCamera->setEnabled(false);
    m_saveFrame->setEnabled(false);
    controls->addWidget(refresh);
    controls->addWidget(m_startCamera);
    controls->addWidget(m_stopCamera);
    controls->addWidget(m_saveFrame);
    cameraCard->contentLayout()->addLayout(controls);
    m_deviceSelect->hide();
    refresh->hide();

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

    m_requestLog = makeLabel(QStringLiteral("点击启动预览，连接机器人双目相机。"), QStringLiteral("mutedLabel"));
    root->addWidget(m_requestLog);

    connect(refresh, &QPushButton::clicked, this,
            [this]()
            {
                emit cameraControlRequested(cameraRequest(CameraControlAction::RefreshDevices));
                logRequest(QStringLiteral("刷新相机设备"));
            });
    connect(m_startCamera, &QPushButton::clicked, this,
            [this]()
            {
                QSettings settings;
                settings.setValue(QStringLiteral("camera/host"), m_cameraHost->text().trimmed());
                settings.setValue(QStringLiteral("camera/port"), m_cameraPort->value());
                emit cameraControlRequested(cameraRequest(CameraControlAction::Start));
                logRequest(QStringLiteral("启动相机预览"));
            });
    connect(m_stopCamera, &QPushButton::clicked, this,
            [this]()
            {
                emit cameraControlRequested(cameraRequest(CameraControlAction::Stop));
                logRequest(QStringLiteral("停止相机预览"));
            });
    connect(m_saveFrame, &QPushButton::clicked, this, &VisionPage::saveCurrentFrame);
    connect(m_sourceSelect, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, refresh](int index) {
                m_source = index == 0 ? CameraSource::RobotStereo : CameraSource::Local;
                const bool robot = m_source == CameraSource::RobotStereo;
                m_endpointControls->setVisible(robot);
                m_deviceSelect->setVisible(!robot);
                refresh->setVisible(!robot);
                m_rightPanel->setVisible(robot);
                m_leftTitle->setText(robot ? QStringLiteral("左目") : QStringLiteral("本机相机"));
                m_snapshot = {};
                m_snapshot.streamState = QStringLiteral("未启动");
                refreshView();
                emit cameraSourceRequested(m_source);
            });

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
    refreshView();
    if (m_source == CameraSource::Local)
        logRequest(deviceNames.isEmpty() ? QStringLiteral("未发现相机")
                                         : QStringLiteral("发现 %1 个相机").arg(deviceNames.size()));
}

void VisionPage::setCameraFrame(const QImage &frame)
{
    if (frame.isNull())
        return;
    m_lastImage = frame;
    m_leftImage = frame;
    m_rightImage = {};
    m_saveFrame->setEnabled(true);
    updatePreviewPixmap();
}

void VisionPage::setStereoFrame(const StereoCameraFrame &frame)
{
    if (frame.stitched.isNull()) return;
    m_lastImage = frame.stitched;
    m_leftImage = frame.left;
    m_rightImage = frame.right;
    m_saveFrame->setEnabled(true);
    updatePreviewPixmap();
}

CameraControlRequest VisionPage::cameraRequest(CameraControlAction action) const
{
    CameraControlRequest request;
    request.action = action;
    request.source = m_source;
    request.deviceIndex = m_deviceSelect->currentIndex();
    request.host = m_cameraHost->text().trimmed();
    request.port = static_cast<quint16>(m_cameraPort->value());
    return request;
}

void VisionPage::showCameraError(const QString &message)
{
    logRequest(QStringLiteral("相机错误：%1").arg(message));
}

bool VisionPage::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_preview || watched == m_rightPreview) && event->type() == QEvent::Resize)
        updatePreviewPixmap();
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

void VisionPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updatePreviewPixmap();
}

void VisionPage::refreshView()
{
    m_cameraDevice->setText(m_snapshot.cameraDevice);
    m_resolution->setText(m_snapshot.resolution);
    m_frameRate->setText(m_snapshot.nominalFrameRate.isEmpty() ? QStringLiteral("--")
                                                             : m_snapshot.nominalFrameRate);
    m_liveFrameRate->setText(m_snapshot.frameRate);
    m_pixelFormat->setText(m_snapshot.pixelFormat);
    m_streamState->setText(m_snapshot.streamState);
    m_lastFrame->setText(m_snapshot.lastFrame);
    m_latency->setText(m_snapshot.latency);
    m_nodeState->setText(m_snapshot.nodeState);
    m_processingMode->setText(m_snapshot.processingMode);
    m_modelsLoaded->setText(m_snapshot.modelsLoaded);
    const bool active = m_snapshot.connected || m_snapshot.connectionRequested;
    m_startCamera->setEnabled(!active && (m_source == CameraSource::RobotStereo ||
        (m_deviceSelect->count() > 0 && m_deviceSelect->itemText(0) != QStringLiteral("未发现相机"))));
    m_stopCamera->setEnabled(active);
    m_endpointControls->setEnabled(!active);
    m_deviceSelect->setEnabled(!active);
    if (!m_snapshot.frameAvailable)
    {
        m_lastImage = {};
        m_leftImage = {};
        m_rightImage = {};
        m_preview->setPixmap({});
        m_rightPreview->setPixmap({});
        m_preview->setText(m_snapshot.streamState);
        m_rightPreview->setText(m_snapshot.streamState);
        m_saveFrame->setEnabled(false);
    }
}

void VisionPage::updatePreviewPixmap()
{
    if (!isVisible() || m_leftImage.isNull() || m_preview == nullptr)
        return;
    const auto render = [](QLabel *preview, const QImage &image) {
        const QSize targetSize = preview->contentsRect().size() - QSize(12, 12);
        if (!image.isNull() && targetSize.width() > 0 && targetSize.height() > 0)
            preview->setPixmap(QPixmap::fromImage(image).scaled(
                targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    };
    render(m_preview, m_leftImage);
    render(m_rightPreview, m_rightImage);
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
