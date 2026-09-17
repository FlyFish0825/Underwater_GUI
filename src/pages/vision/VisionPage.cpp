#include "pages/vision/VisionPage.h"

#include "preview/PreviewData.h"
#include "ui/common/UiPrimitives.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{

QWidget *infoRow(const QString &label, QLabel *&value)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(rov::makeLabel(label, QStringLiteral("mutedLabel")));
    value = rov::makeLabel(QStringLiteral("-"), QStringLiteral("bodyValue"));
    layout->addWidget(value);
    layout->addStretch();
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
                                   QStringLiteral("相机与图像处理（集成进行中）。"),
                                   QStringLiteral("演示 · 相机未连接")));

    auto *mainRow = new QHBoxLayout;
    mainRow->setSpacing(12);
    auto *cameraCard = new CardWidget(QStringLiteral("相机预览"), IconKind::Camera);
    auto *preview = new QFrame;
    preview->setObjectName(QStringLiteral("card"));
    preview->setMinimumSize(520, 365);
    auto *previewLayout = new QVBoxLayout(preview);
    previewLayout->setAlignment(Qt::AlignCenter);
    auto *cameraIcon = new IconWidget(IconKind::Camera);
    cameraIcon->setFixedSize(65, 65);
    previewLayout->addWidget(cameraIcon, 0, Qt::AlignCenter);
    previewLayout->addWidget(
        makeLabel(QStringLiteral("等待来自 Nano 的视频流"), QStringLiteral("metricValue")), 0,
        Qt::AlignCenter);
    previewLayout->addWidget(
        makeLabel(QStringLiteral("相机集成进行中。"), QStringLiteral("pageSubtitle")), 0,
        Qt::AlignCenter);
    cameraCard->contentLayout()->addWidget(preview, 1);
    auto *modeCard = new CardWidget(QStringLiteral("显示模式（规划）"), IconKind::Vision);
    auto *modes = new QHBoxLayout;
    const QStringList modeNames = {QStringLiteral("原始"),     QStringLiteral("去畸变"),
                                   QStringLiteral("折射校正"), QStringLiteral("边缘"),
                                   QStringLiteral("检测"),     QStringLiteral("位姿")};
    for (int i = 0; i < modeNames.size(); ++i)
    {
        auto *mode = makeButton(modeNames.at(i), i == 0 ? QStringLiteral("primaryButton")
                                                        : QStringLiteral("softButton"));
        modes->addWidget(mode);
        connect(mode, &QPushButton::clicked, this,
                [this, modeName = modeNames.at(i)]()
                {
                    emit visionModeRequested(VisionModeRequest{modeName});
                    logRequest(QStringLiteral("请求视觉模式：%1").arg(modeName));
                });
    }
    modeCard->contentLayout()->addLayout(modes);
    cameraCard->contentLayout()->addWidget(modeCard);
    mainRow->addWidget(cameraCard, 7);

    auto *side = new QVBoxLayout;
    side->setSpacing(12);
    auto *connection = new CardWidget(QStringLiteral("相机连接"), IconKind::Camera);
    connection->contentLayout()->addWidget(infoRow(QStringLiteral("相机设备"), m_cameraDevice));
    connection->contentLayout()->addWidget(infoRow(QStringLiteral("分辨率"), m_resolution));
    connection->contentLayout()->addWidget(infoRow(QStringLiteral("帧率"), m_frameRate));
    connection->contentLayout()->addWidget(infoRow(QStringLiteral("像素格式"), m_pixelFormat));
    side->addWidget(connection);

    auto *stream = new CardWidget(QStringLiteral("流状态"), IconKind::Waveform);
    stream->contentLayout()->addWidget(infoRow(QStringLiteral("视频流"), m_streamState));
    stream->contentLayout()->addWidget(infoRow(QStringLiteral("最后帧"), m_lastFrame));
    stream->contentLayout()->addWidget(infoRow(QStringLiteral("实时帧率"), m_liveFrameRate));
    stream->contentLayout()->addWidget(infoRow(QStringLiteral("延迟"), m_latency));
    side->addWidget(stream);

    auto *node = new CardWidget(QStringLiteral("视觉节点"), IconKind::Firmware);
    node->contentLayout()->addWidget(infoRow(QStringLiteral("节点状态"), m_nodeState));
    node->contentLayout()->addWidget(infoRow(QStringLiteral("处理模式"), m_processingMode));
    node->contentLayout()->addWidget(infoRow(QStringLiteral("已加载模型"), m_modelsLoaded));
    side->addWidget(node);

    auto *roadmap = new CardWidget(QStringLiteral("开发路线图"), IconKind::List);
    const QStringList roadmapItems = {
        QStringLiteral("相机视频流（Nano → 上位机）"), QStringLiteral("图像去畸变 / 折射校正"),
        QStringLiteral("目标检测（浮标、工具、标记物）"),
        QStringLiteral("位姿估计（PnP、视觉 SLAM）"), QStringLiteral("录像与快照")};
    for (const QString &item : roadmapItems)
    {
        roadmap->contentLayout()->addWidget(
            makeLabel(QStringLiteral("○  %1").arg(item), QStringLiteral("bodyValue")));
    }
    side->addWidget(roadmap);
    mainRow->addLayout(side, 4);
    root->addLayout(mainRow, 1);

    m_requestLog =
        makeLabel(QStringLiteral("暂无视觉请求；相机暂未连接。"), QStringLiteral("mutedLabel"));
    root->addWidget(m_requestLog);
    setSnapshot(visionPreview());
}

void VisionPage::setSnapshot(const VisionSnapshot &snapshot)
{
    m_snapshot = snapshot;
    refreshView();
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
}

void VisionPage::logRequest(const QString &message)
{
    if (m_requestLog != nullptr)
    {
        m_requestLog->setText(QStringLiteral("最近请求：%1 · 不执行相机操作").arg(message));
    }
}

} // namespace rov
