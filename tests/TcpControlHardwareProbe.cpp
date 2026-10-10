// Manually invoked field probe using the production GUI service. Never starts motors or zeroes depth.
#include "communication/service/BootloaderCommunicationService.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>
#include <QTimer>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 2) { QTextStream(stderr) << "Usage: probe HOST\n"; return 2; }
    rov::BootloaderCommunicationService service;
    rov::SensorFrame query;
    query.target = rov::kDepthSensor;
    query.command = 2; query.flags = 1; query.sequence = 0x13572468U;
    const auto bytes = rov::encodeSensorFrame(query);
    int phase = 0;
    bool busy = false, reply = false;
    QElapsedTimer clock; clock.start();
    auto fail = [&](const QString &message) {
        QTextStream(stderr) << "FAIL " << message << Qt::endl;
        service.close(); app.exit(1);
    };
    QObject::connect(&service, &rov::BootloaderCommunicationService::tcpControlRequestRejected,
        [&](const QString &owner, const QString &message) {
            QTextStream(stdout) << "BUSY " << owner << " " << message << Qt::endl;
            busy = owner == QStringLiteral("NANO") && message.contains(QStringLiteral("TAKEOVER"));
        });
    QObject::connect(&service, &rov::BootloaderCommunicationService::sensorFrameReceived,
        [&](const rov::SensorFrame &frame) {
            if (frame.target == 2 && frame.command == 0x42 && frame.sequence == query.sequence
                && frame.flags == 2 && frame.payload.size() == 21 && frame.payload.at(0) == 0) {
                reply = true;
                QTextStream(stdout) << "RAW_STATUS_ACK bytes=" << bytes.size()
                    << " status=" << QString::number(rov::SensorWire::read32(frame.payload, 1), 16)
                    << Qt::endl;
            }
        });
    QObject::connect(&service, &rov::BootloaderCommunicationService::errorOccurred,
        [&](const QString &message) { QTextStream(stdout) << "SERVICE " << message << Qt::endl; });
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        if (clock.elapsed() > 90000) { fail(QStringLiteral("bounded field probe timed out")); return; }
        switch (phase) {
        case 0:
            if (service.isOpen() && service.tcpControlConnected()
                && service.tcpControlOwner() == QStringLiteral("NANO")) {
                if (service.sendRawBytes(bytes)) { fail(QStringLiteral("read-only write was allowed")); return; }
                if (!service.requestTcpControl(false)) { fail(QStringLiteral("claim submission failed")); return; }
                phase = 1;
            }
            break;
        case 1:
            if (busy) {
                if (!service.requestTcpControl(true)) { fail(QStringLiteral("takeover submission failed")); return; }
                phase = 2;
            }
            break;
        case 2:
            if (service.writesAllowed()) {
                QTextStream(stdout) << "GUI_OWNED UART_AUTHENTICATED" << Qt::endl;
                if (!service.sendRawBytes(bytes)) { fail(QStringLiteral("raw status query rejected")); return; }
                phase = 3;
            }
            break;
        case 3:
            if (reply) {
                if (!service.releaseTcpControl()) { fail(QStringLiteral("release submission failed")); return; }
                phase = 4;
            }
            break;
        case 4:
            if (service.tcpControlOwner() == QStringLiteral("NANO") && !service.writesAllowed()) {
                QTextStream(stdout) << "GUI_RELEASED NANO_OWNS" << Qt::endl;
                if (!service.requestTcpControl(true)) { fail(QStringLiteral("second takeover failed")); return; }
                phase = 5;
            }
            break;
        case 5:
            if (service.writesAllowed()) {
                QTextStream(stdout) << "READY_NANO_TEST: call Nano CLAIM then TAKEOVER" << Qt::endl;
                phase = 6;
            }
            break;
        case 6:
            if (service.tcpControlOwner() == QStringLiteral("NANO") && !service.writesAllowed()) {
                if (service.sendRawBytes(bytes)) { fail(QStringLiteral("revoked session wrote bytes")); return; }
                QTextStream(stdout) << "PASS GUI busy/takeover/raw GET_STATUS/release/Nano revocation; motor_run=0 zero=0"
                    << Qt::endl;
                phase = 7; service.close(); app.exit(0);
            }
            break;
        default: break;
        }
    });
    timer.start(50);
    service.openTcp(args[1], 9000, false, 9002);
    return app.exec();
}
