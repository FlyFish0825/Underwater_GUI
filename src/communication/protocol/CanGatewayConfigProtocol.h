#pragma once

#include <QByteArray>
#include <QString>

namespace rov
{

enum class CanGatewayConfigStatus : quint8
{
    Ok = 0x00U,
    UnsupportedRate = 0x01U,
    ApplyFailed = 0x02U,
    Timeout = 0xFEU,
    TransportError = 0xFFU,
};

struct CanGatewayConfigRequest
{
    quint16 sequence = 0;
    quint32 nominalBitrate = 0;
    quint32 dataBitrate = 0;
};

struct CanGatewayConfigResponse
{
    quint16 sequence = 0;
    CanGatewayConfigStatus status = CanGatewayConfigStatus::ApplyFailed;
    quint32 nominalBitrate = 0;
    quint32 dataBitrate = 0;
};

bool isSupportedCanNominalBitrate(quint32 bitrate);
bool isSupportedCanDataBitrate(quint32 bitrate);

quint8 canGatewayConfigCrc8(const QByteArray &bytes);

QByteArray encodeCanGatewayConfigRequest(const CanGatewayConfigRequest &request);
bool decodeCanGatewayConfigRequest(const QByteArray &packet,
                                   CanGatewayConfigRequest &request,
                                   QString *error = nullptr);

QByteArray encodeCanGatewayConfigResponse(const CanGatewayConfigResponse &response);
bool decodeCanGatewayConfigResponse(const QByteArray &packet,
                                    CanGatewayConfigResponse &response,
                                    QString *error = nullptr);

bool isCanGatewayConfigResponsePacket(const QByteArray &packet);
QString canGatewayConfigStatusText(CanGatewayConfigStatus status);

} // namespace rov
