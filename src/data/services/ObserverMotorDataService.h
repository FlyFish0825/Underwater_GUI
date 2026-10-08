#pragma once

#include "communication/protocol/CanGatewayProtocol.h"
#include "contracts/motor/ObserverMotorContract.h"

#include <QObject>
#include <QElapsedTimer>
#include <QSet>
#include <QTimer>
#include <array>

namespace rov
{

class ObserverMotorDataService final : public QObject
{
    Q_OBJECT

  public:
    explicit ObserverMotorDataService(QObject *parent = nullptr);

    bool handleCanFrame(const CanGatewayFrame &frame);
    void reset();
    ObserverMotorNodeSnapshot nodeSnapshot(quint8 nodeId) const;
    ObserverMotorFleetSnapshot snapshot() const;
    MotorCalibrationSnapshot calibrationSnapshot(quint8 nodeId) const;
    QVector<MotorCalibrationSnapshot> calibrationSnapshots() const;
    bool canRequestCalibration(quint8 nodeId, quint8 action, QString *error = nullptr) const;
    void noteCalibrationRequest(quint8 nodeId, quint8 action, quint16 sequence);

  signals:
    void nodeSnapshotChanged(quint8 nodeId, const rov::ObserverMotorNodeSnapshot &snapshot);
    void snapshotChanged(const rov::ObserverMotorFleetSnapshot &snapshot);
    void calibrationSnapshotChanged(quint8 nodeId,
                                    const rov::MotorCalibrationSnapshot &snapshot);
    void protocolError(const QString &message);

  private:
    struct TemperatureAverage
    {
        std::array<double, 100> samples{};
        double sum = 0.0;
        int count = 0;
        int next = 0;
        qint64 lastSampleMs = -1;
        bool highResolution = false;
    };

    double averageTemperature(quint8 nodeId, double temperatureC, bool highResolution);
    void publishPending();
    void refreshFreshness();
    void markDirty(quint8 nodeId);
    void markCalibrationDirty(quint8 nodeId);

    QVector<ObserverMotorNodeSnapshot> m_nodes;
    QVector<qint64> m_lastSeenMs;
    QVector<TemperatureAverage> m_temperatureAverages;
    QElapsedTimer m_temperatureClock;
    QSet<quint8> m_dirtyNodes;
    QSet<quint8> m_dirtyCalibrationNodes;
    QVector<MotorCalibrationSnapshot> m_calibrations;
    QVector<qint64> m_calibrationRequestMs;
    QTimer m_publishTimer;
    QTimer m_freshnessTimer;
};

} // namespace rov
