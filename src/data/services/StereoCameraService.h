#pragma once

#include "contracts/vision/VisionContract.h"
#include <QObject>
#include <memory>

namespace rov
{
// Independent receive-only image connection. Never uses the MCU gateway service.
class StereoCameraService final : public QObject
{
    Q_OBJECT
  public:
    explicit StereoCameraService(QObject *parent = nullptr);
    ~StereoCameraService() override;
    void startCamera(const QString &host, quint16 port = 9001);
    void stopCamera();

  signals:
    void snapshotChanged(const VisionSnapshot &snapshot);
    void frameReady(const StereoCameraFrame &frame);
    void errorOccurred(const QString &message);

  private:
    void publishLatest();
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace rov
