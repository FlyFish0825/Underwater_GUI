#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>

namespace rov
{
class ByteTransport : public QObject
{
    Q_OBJECT
  public:
    explicit ByteTransport(QObject *parent = nullptr) : QObject(parent) {}
    ~ByteTransport() override = default;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual QString portName() const = 0;
    virtual bool writeBytes(const QByteArray &bytes) = 0;
  signals:
    void bytesReceived(const QByteArray &bytes);
    void opened(const QString &endpoint);
    void closed();
    void errorOccurred(const QString &message);
};
}
