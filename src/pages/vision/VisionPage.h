#pragma once

#include "contracts/vision/VisionContract.h"

#include <QWidget>

class QLabel;

namespace rov
{

class VisionPage final : public QWidget
{
    Q_OBJECT

  public:
    explicit VisionPage(QWidget *parent = nullptr);

    void setSnapshot(const VisionSnapshot &snapshot);

  signals:
    void visionModeRequested(const VisionModeRequest &request);

  private:
    void refreshView();
    void logRequest(const QString &message);

    VisionSnapshot m_snapshot;
    QLabel *m_cameraDevice = nullptr;
    QLabel *m_resolution = nullptr;
    QLabel *m_frameRate = nullptr;
    QLabel *m_liveFrameRate = nullptr;
    QLabel *m_pixelFormat = nullptr;
    QLabel *m_streamState = nullptr;
    QLabel *m_lastFrame = nullptr;
    QLabel *m_latency = nullptr;
    QLabel *m_nodeState = nullptr;
    QLabel *m_processingMode = nullptr;
    QLabel *m_modelsLoaded = nullptr;
    QLabel *m_requestLog = nullptr;
};

} // namespace rov
