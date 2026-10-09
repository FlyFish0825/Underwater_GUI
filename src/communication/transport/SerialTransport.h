#pragma once

#include "communication/transport/ByteTransport.h"
#include <QVector>

namespace rov
{

struct SerialDeviceInfo
{
    QString portName;
    QString displayName;
    QString manufacturer;
    quint16 vendorId = 0;
    quint16 productId = 0;

    QString vidPidText() const;
};

class SerialTransport final : public ByteTransport
{
    Q_OBJECT

  public:
    explicit SerialTransport(QObject *parent = nullptr);
    ~SerialTransport() override;

    static QVector<SerialDeviceInfo> enumerate(quint16 vendorId = 0x0483,
                                                quint16 productId = 0x5740);

    bool open(const SerialDeviceInfo &device);
    void close() override;
    bool isOpen() const override;
    QString portName() const override;
    bool writeBytes(const QByteArray &bytes) override;

  private slots:
    void pollRead();

  private:
    class Impl;
    Impl *m_impl = nullptr;
};

} // namespace rov
