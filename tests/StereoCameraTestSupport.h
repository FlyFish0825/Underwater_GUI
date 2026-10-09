#pragma once
#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QThread>
#include <QtEndian>
#include <functional>
#include <stdexcept>

inline void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
inline bool until(const std::function<bool()> &condition, int ms = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
    {
        QCoreApplication::processEvents();
        if (condition())
            return true;
        QThread::msleep(2);
    }
    return false;
}
inline QByteArray stereoPacket(quint64 sequence = 1, int width = 640, int height = 180)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(QColor(40, 90, 170));
    QPainter painter(&image);
    painter.fillRect(width / 2, 0, width / 2, height, QColor(20, 170, 100));
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPixelSize(24);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, width / 2, height), Qt::AlignCenter,
                     QStringLiteral("LEFT / %1").arg(sequence));
    painter.drawText(QRect(width / 2, 0, width / 2, height), Qt::AlignCenter,
                     QStringLiteral("RIGHT / %1").arg(sequence));
    painter.end();
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    require(image.save(&buffer, "JPEG", 85), "JPEG encoder available");
    QByteArray header(40, '\0');
    auto *p = reinterpret_cast<uchar *>(header.data());
    header.replace(0, 4, "UWSC");
    qToBigEndian<quint16>(1, p + 4);
    qToBigEndian<quint16>(1, p + 6);
    qToBigEndian<quint64>(sequence, p + 8);
    qToBigEndian<quint64>(2, p + 16);
    qToBigEndian<quint32>(width, p + 24);
    qToBigEndian<quint32>(height, p + 28);
    qToBigEndian<quint32>(jpeg.size(), p + 32);
    return header + jpeg;
}
