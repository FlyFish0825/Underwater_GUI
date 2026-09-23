#pragma once

#include "communication/bootloader/BootloaderTypes.h"

#include <QByteArray>
#include <QObject>
#include <QTimer>

namespace rov
{

class BootloaderCommunicationService;
class BootloaderService;

/** Reads the verified APP image through the Bootloader READ command. */
class BootloaderFirmwareReader final : public QObject
{
    Q_OBJECT

  public:
    explicit BootloaderFirmwareReader(BootloaderService *bootloader,
                                      BootloaderCommunicationService *communication,
                                      QObject *parent = nullptr);

    bool start(quint8 target);
    void cancel();
    bool isRunning() const { return m_running; }

    /** Parse and validate the V1.3 persisted Boot_Config_t metadata record. */
    static bool parseConfigMetadata(const QByteArray &record, quint8 expectedNode,
                                    quint32 &appSize, quint32 &appCrc32,
                                    QString *error = nullptr);

  signals:
    void progressChanged(quint8 target, int percent);
    void finished(bool success, QByteArray image, QString message);

  private:
    enum class Phase
    {
        Idle,
        WaitingEnterBoot,
        WaitingVersion,
        ReadingMetadata,
        ReadingApp,
    };

    void handleResponse(const BootResponse &response);
    void sendVersionProbe();
    bool sendRead(quint32 address, quint8 length);
    void handleReadResponse(const BootResponse &response);
    void requestNextAppChunk();
    void finish(bool success, const QString &message, bool abortPendingRead = false);
    void onReadTimeout();

    BootloaderService *m_bootloader = nullptr;
    BootloaderCommunicationService *m_communication = nullptr;
    QTimer m_bootEntryTimer;
    QTimer m_responseTimer;
    Phase m_phase = Phase::Idle;
    bool m_running = false;
    bool m_readInFlight = false;
    quint8 m_target = 0;
    int m_versionAttempts = 0;
    quint8 m_requestLength = 0;
    quint16 m_requestReceived = 0;
    quint32 m_requestAddress = 0;
    quint32 m_expectedImageSize = 0;
    quint32 m_expectedImageCrc32 = 0;
    QByteArray m_requestData;
    QByteArray m_configRecord;
    QByteArray m_image;
};

} // namespace rov
