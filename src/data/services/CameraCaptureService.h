#pragma once

#include "contracts/vision/VisionContract.h"

#include <QImage>
#include <QObject>
#include <QStringList>

#include <memory>

namespace rov
{

class CameraCaptureService final : public QObject
{
    Q_OBJECT

  public:
    explicit CameraCaptureService(QObject *parent = nullptr);
    ~CameraCaptureService() override;

  public slots:
    void refreshDevices();
    void startCamera(int deviceIndex);
    void stopCamera();

  signals:
    void devicesChanged(const QStringList &deviceNames);
    void snapshotChanged(const VisionSnapshot &snapshot);
    void frameReady(const QImage &frame);
    void errorOccurred(const QString &message);

  private slots:
    void acceptFrame(const QImage &frame);

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    VisionSnapshot m_snapshot;
    qint64 m_fpsWindowStartMs = 0;
    int m_fpsWindowFrames = 0;
};

} // namespace rov
