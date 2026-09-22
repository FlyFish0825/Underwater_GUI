#pragma once

#include "contracts/dashboard/DashboardContract.h"
#include "contracts/vision/VisionContract.h"

#include <QImage>
#include <QWidget>

class QLabel;
class QComboBox;
class QPushButton;
class QEvent;
class QHideEvent;
class QResizeEvent;

namespace rov
{

class VisionPage final : public QWidget
{
    Q_OBJECT

  public:
    explicit VisionPage(QWidget *parent = nullptr);

    void setSnapshot(const VisionSnapshot &snapshot);
    void setAvailableCameras(const QStringList &deviceNames);
    void setCameraFrame(const QImage &frame);
    void showCameraError(const QString &message);

  signals:
    void visionModeRequested(const VisionModeRequest &request);
    void cameraControlRequested(const CameraControlRequest &request);
    void manualControlRequested(const SixDofControlRequest &request);
    void holdPositionRequested();
    void disarmRequested();

  private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void refreshView();
    void updatePreviewPixmap();
    void saveCurrentFrame();
    void setManualAxis(int axis, double value);
    void releaseManualControl();
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
    QLabel *m_preview = nullptr;
    QComboBox *m_deviceSelect = nullptr;
    QPushButton *m_startCamera = nullptr;
    QPushButton *m_stopCamera = nullptr;
    QPushButton *m_saveFrame = nullptr;
    QImage m_lastImage;
    SixDofControlRequest m_manualControl;
};

} // namespace rov
