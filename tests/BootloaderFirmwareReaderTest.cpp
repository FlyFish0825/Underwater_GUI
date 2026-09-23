#include "communication/bootloader/BootloaderFirmwareReader.h"

#include "communication/bootloader/BootloaderProtocol.h"

#include <QCoreApplication>
#include <QDebug>

using namespace rov;

namespace
{
bool require(const bool condition, const char *message)
{
    if (!condition)
        qCritical() << message;
    return condition;
}

void writeLe16(QByteArray &bytes, const int offset, const quint16 value)
{
    bytes[offset] = static_cast<char>(value & 0xFFU);
    bytes[offset + 1] = static_cast<char>((value >> 8U) & 0xFFU);
}

void writeLe32(QByteArray &bytes, const int offset, const quint32 value)
{
    for (int i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<char>((value >> (i * 8)) & 0xFFU);
}

QByteArray validRecord()
{
    QByteArray record(84, '\0');
    writeLe32(record, 0, 0x31474643U); // CFG1
    writeLe16(record, 4, 1U);
    writeLe16(record, 6, 84U);
    record[8] = 3;
    writeLe32(record, 36, 4097U);
    writeLe32(record, 40, 0x12345678U);
    record[44] = 1;
    writeLe32(record, 80, BootloaderProtocol::crc32Mpeg2(record.left(80)));
    return record;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QByteArray record = validRecord();
    quint32 appSize = 0;
    quint32 appCrc = 0;
    QString error;

    if (!require(BootloaderFirmwareReader::parseConfigMetadata(record, 3, appSize, appCrc,
                                                               &error),
                 "valid metadata must parse")
        || !require(appSize == 4097U && appCrc == 0x12345678U,
                    "metadata size and app CRC must be read exactly")
        || !require(!BootloaderFirmwareReader::parseConfigMetadata(record, 4, appSize, appCrc,
                                                                   &error),
                    "metadata from a different node must be rejected"))
        return 1;

    QByteArray badRecord = record;
    badRecord[60] = static_cast<char>(badRecord.at(60) ^ 1);
    if (!require(!BootloaderFirmwareReader::parseConfigMetadata(badRecord, 3, appSize, appCrc,
                                                                &error),
                 "corrupt metadata CRC must be rejected"))
        return 1;

    badRecord = record;
    writeLe32(badRecord, 36, 0x1A801U);
    writeLe32(badRecord, 80, BootloaderProtocol::crc32Mpeg2(badRecord.left(80)));
    if (!require(!BootloaderFirmwareReader::parseConfigMetadata(badRecord, 3, appSize, appCrc,
                                                                &error),
                 "APP size outside the application partition must be rejected"))
        return 1;

    return 0;
}
