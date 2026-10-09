#include "data/services/StereoCameraService.h"
#include "StereoCameraTestSupport.h"
#include "communication/protocol/StereoCameraProtocol.h"
#include "communication/service/BootloaderCommunicationService.h"
#include <QGuiApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <iostream>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    try
    {
        const QByteArray first = stereoPacket(0x123456789abcdef0ULL);
        rov::StereoCameraParser parser;
        int parsed = 0;
        QString error;
        auto receive = [&](rov::StereoCameraPacket frame)
        {
            ++parsed;
            require(frame.sequence == 0x123456789abcdef0ULL && frame.receiptNs == 2,
                    "big endian 64-bit fields");
            require(frame.width == 640 && frame.height == 180 && frame.jpeg == first.mid(40),
                    "header and exact JPEG");
        };
        for (char byte : first)
            require(parser.feed(QByteArray(1, byte), receive, &error), "byte-wise fragmentation");
        require(parsed == 1 && parser.bufferedBytes() == 0, "one fragmented frame");
        require(parser.feed(first + first + first.left(39), receive, &error), "glued frames");
        require(parsed == 3 && parser.bufferedBytes() == 39, "partial final header retained");
        parser.clear();
        const auto reject = [&](int offset, quint32 value, int size)
        {
            QByteArray bad = first.left(40);
            auto *p = reinterpret_cast<uchar *>(bad.data());
            if (size == 2)
                qToBigEndian<quint16>(value, p + offset);
            else
                qToBigEndian<quint32>(value, p + offset);
            require(!parser.feed(bad, receive, &error) && parser.bufferedBytes() == 0,
                    "invalid header rejected and parser cleared");
        };
        reject(0, 0, 4);
        reject(4, 2, 2);
        reject(6, 3, 2);
        reject(36, 1, 4);
        for (quint32 value : {0U, 1U, 3U, 8194U})
            reject(24, value, 4);
        for (quint32 value : {0U, 4097U})
            reject(28, value, 4);
        for (quint32 value : {0U, 8388609U, 0xffffffffU})
            reject(32, value, 4);
        QByteArray maximum = first.left(40);
        qToBigEndian<quint32>(rov::StereoCameraParser::MaxJpegSize,
                              reinterpret_cast<uchar *>(maximum.data()) + 32);
        int largeFrames = 0;
        maximum.append(QByteArray(rov::StereoCameraParser::MaxJpegSize, 'x'));
        require(parser.feed(
                    maximum,
                    [&](rov::StereoCameraPacket p)
                    {
                        ++largeFrames;
                        require(p.jpeg.size() == rov::StereoCameraParser::MaxJpegSize,
                                "exact maximum size");
                    },
                    &error),
                "maximum packet accepted by framing layer");
        require(largeFrames == 1 && parser.bufferedBytes() == 0, "bounded maximum buffer");

        QTcpServer cameraServer, gatewayServer;
        require(cameraServer.listen(QHostAddress::LocalHost, 0) &&
                    gatewayServer.listen(QHostAddress::LocalHost, 0),
                "local endpoints");
        rov::BootloaderCommunicationService gateway;
        require(gateway.openTcp(QStringLiteral("127.0.0.1"), gatewayServer.serverPort(), false),
                "parallel MCU read-only session");
        require(until([&] { return gateway.isOpen() && gatewayServer.hasPendingConnections(); }),
                "MCU connected");
        QTcpSocket *gatewayPeer = gatewayServer.nextPendingConnection();
        rov::StereoCameraService camera;
        rov::VisionSnapshot status;
        rov::StereoCameraFrame last;
        int delivered = 0, errors = 0;
        QObject::connect(&camera, &rov::StereoCameraService::snapshotChanged,
                         [&](const rov::VisionSnapshot &s) { status = s; });
        QObject::connect(&camera, &rov::StereoCameraService::frameReady,
                         [&](const rov::StereoCameraFrame &f)
                         {
                             last = f;
                             ++delivered;
                         });
        QObject::connect(&camera, &rov::StereoCameraService::errorOccurred,
                         [&](const QString &) { ++errors; });
        camera.startCamera(QStringLiteral("127.0.0.1"), cameraServer.serverPort());
        require(until([&] { return status.connected && cameraServer.hasPendingConnections(); }),
                "camera connected");
        auto *peer = cameraServer.nextPendingConnection();
        require(status.streamState == QStringLiteral("等待相机画面") && !status.frameAvailable,
                "connected is distinct from valid picture");
        require(until([&] { return status.streamState == QStringLiteral("画面超时"); }),
                "no initial image timeout");
        require(status.connected && !status.frameAvailable,
                "silence keeps TCP but invalidates image");
        const QByteArray packet = stereoPacket(5);
        peer->write(packet.left(7));
        peer->flush();
        until([] { return false; }, 40);
        require(delivered == 0, "no premature partial decode");
        peer->write(packet.mid(7));
        peer->flush();
        require(until([&] { return last.sequence == 5; }), "fragmented JPEG decoded");
        require(last.left.size() == QSize(320, 180) && last.right.size() == QSize(320, 180),
                "same-frame eye split");
        require(last.left.pixelColor(10, 10).blue() > 150 &&
                    last.right.pixelColor(10, 10).green() > 150,
                "no rotation or eye exchange");
        require(status.cameraStamp.freshness == rov::DataFreshness::Fresh && status.frameAvailable,
                "freshness uses local receipt clock despite Nano timestamp of 2");
        peer->write(stereoPacket(8) + stereoPacket(12) + stereoPacket(20));
        peer->flush();
        require(until([&] { return last.sequence == 20; }), "latest glued frame and sequence gaps");
        const int beforePause = delivered;
        QByteArray burst;
        for (int i = 21; i <= 100; ++i)
            burst += stereoPacket(i);
        peer->write(burst);
        peer->flush();
        require(peer->waitForBytesWritten(1000) || peer->bytesToWrite() == 0, "burst sent");
        QThread::msleep(350); // Deliberately stall GUI publication while receiver/decoder run.
        QCoreApplication::processEvents();
        require(delivered - beforePause <= 1, "GUI mailbox depth one after stalled event loop");
        require(until([&] { return last.sequence == 100; }), "latest frame survives GUI stall");
        require(peer->bytesAvailable() == 0, "image client sends zero application bytes");
        require(until([&] { return status.streamState == QStringLiteral("画面超时"); }),
                "fresh image becomes stale");
        require(!status.frameAvailable, "stale image unavailable");

        QByteArray badSize = stereoPacket(101);
        qToBigEndian<quint32>(642, reinterpret_cast<uchar *>(badSize.data()) + 24);
        peer->write(badSize);
        peer->flush();
        require(until([&] { return !status.connected && errors > 0; }),
                "internal JPEG dimension mismatch disconnects");
        require(gateway.isOpen() && gatewayPeer->state() == QAbstractSocket::ConnectedState,
                "camera failure leaves MCU session alone");
        require(
            until([&] { return cameraServer.hasPendingConnections() && status.connected; }, 4000),
            "camera reconnect");
        peer = cameraServer.nextPendingConnection();
        peer->write(stereoPacket(1));
        peer->flush();
        require(until([&] { return last.sequence == 1 && status.frameAvailable; }),
                "server restart may reset sequence");
        peer->write(stereoPacket(2).left(63));
        peer->flush();
        until([] { return false; }, 40);
        peer->abort();
        require(until([&] { return !status.connected; }), "partial-frame disconnect");
        require(
            until([&] { return cameraServer.hasPendingConnections() && status.connected; }, 4000),
            "partial reconnect");
        peer = cameraServer.nextPendingConnection();
        peer->write(stereoPacket(500));
        peer->flush();
        require(until([&] { return last.sequence == 500; }), "old half-frame discarded");
        QByteArray corrupt = stereoPacket(501);
        corrupt.replace(40, corrupt.size() - 40, QByteArray(corrupt.size() - 40, 'x'));
        peer->write(corrupt);
        peer->flush();
        require(until([&] { return !status.connected; }), "corrupt JPEG disconnects");
        require(
            until([&] { return cameraServer.hasPendingConnections() && status.connected; }, 4000),
            "reconnect after corrupt JPEG");
        peer = cameraServer.nextPendingConnection();
        QByteArray truncated = stereoPacket(502);
        truncated.chop(2); // Remove JPEG EOI while keeping TCP framing internally consistent.
        qToBigEndian<quint32>(truncated.size() - 40,
                              reinterpret_cast<uchar *>(truncated.data()) + 32);
        peer->write(truncated);
        peer->flush();
        require(until([&] { return !status.connected; }),
                "truncated JPEG must not be repaired/displayed");
        camera.stopCamera();
        require(!status.connectionRequested && !status.frameAvailable,
                "stop cancels retry and invalidates picture");
        until([] { return false; }, 1300);
        require(!cameraServer.hasPendingConnections() && !status.connected,
                "no reconnect after manual stop");
        require(gateway.isOpen(), "manual camera stop preserves MCU connection");
        gateway.close();
        std::cout << "PASS UWSC framing, bounded latest-frame pipeline, JPEG, freshness, "
                     "reconnect, stop, independent MCU\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
