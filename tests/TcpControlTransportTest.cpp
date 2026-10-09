#include "communication/transport/TcpControlTransport.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include <functional>
#include <iostream>
#include <stdexcept>

using namespace rov;
namespace {
int checks = 0;
void check(const bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
bool until(const std::function<bool()> &condition, const int timeoutMs = 2500) {
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
        if (condition()) return true;
        QThread::msleep(2);
    }
    return condition();
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTcpServer server;
        check(server.listen(QHostAddress::LocalHost, 0), "listen on test control port");
        TcpControlTransport control;
        QString owner;
        QString lease;
        bool connected = false;
        int rejected = 0;
        QObject::connect(&control, &TcpControlTransport::stateChanged,
            [&](bool isConnected, const QString &newOwner, bool owned, const QString &) {
                connected = isConnected; owner = newOwner;
                if (!owned) lease.clear();
            });
        QObject::connect(&control, &TcpControlTransport::leaseTokenChanged,
                         [&](const QString &token) { lease = token; });
        QObject::connect(&control, &TcpControlTransport::requestRejected,
                         [&](const QString &, const QString &) { ++rejected; });
        control.open(QStringLiteral("127.0.0.1"), server.serverPort());
        check(until([&] { return connected && server.hasPendingConnections(); }),
              "control TCP connects");
        QTcpSocket *peer = server.nextPendingConnection();
        check(until([&] { return peer->bytesAvailable() > 0; }) && peer->readAll() == "STATE\n",
              "client starts with read-only ownership query");
        peer->write("STATE NANO\n"); peer->flush();
        check(until([&] { return owner == QStringLiteral("NANO"); }), "Nano owner state received");

        check(control.requestControl(false), "claim request accepted for transmission");
        check(until([&] { return peer->bytesAvailable() > 0; })
                  && peer->readAll() == "CLAIM GUI\n", "ordinary claim is sent");
        peer->write("BUSY NANO Robot control is occupied\n"); peer->flush();
        check(until([&] { return rejected == 1; }) && owner == QStringLiteral("NANO"),
              "busy response keeps ownership with Nano");

        check(control.requestControl(true), "takeover request accepted for transmission");
        check(until([&] { return peer->bytesAvailable() > 0; })
                  && peer->readAll() == "TAKEOVER GUI\n", "explicit takeover is sent");
        const QByteArray token = "0123456789abcdef0123456789abcdef";
        peer->write("OK GUI " + token + " GUI took over robot control\n"); peer->flush();
        check(until([&] { return control.ownsControl(); })
                  && lease == QString::fromLatin1(token), "takeover token is retained for authentication");

        check(until([&] { QCoreApplication::processEvents(); return peer->bytesAvailable() > 0; }, 1800),
              "owner heartbeat is emitted");
        check(peer->readAll() == "PING " + token + "\n", "heartbeat carries lease token");
        peer->write("HEARTBEAT GUI\n"); peer->flush();

        check(control.releaseControl(), "release request accepted");
        check(until([&] { return peer->bytesAvailable() > 0; })
                  && peer->readAll() == "RELEASE GUI\n", "release is sent");
        peer->write("OK NANO GUI released robot control\n"); peer->flush();
        check(until([&] { return !control.ownsControl(); }) && owner == QStringLiteral("NANO"),
              "GUI release returns to Nano");
        control.close();
        std::cout << "PASS " << checks << " TCP ownership client checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
