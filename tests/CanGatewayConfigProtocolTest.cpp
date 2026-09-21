#include "communication/protocol/CanGatewayConfigProtocol.h"

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

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    CanGatewayConfigRequest request;
    request.sequence = 2;
    request.nominalBitrate = 500000;
    request.dataBitrate = 5000000;
    const QByteArray packet = encodeCanGatewayConfigRequest(request);
    if (!require(packet == QByteArray::fromHex(
                     "AA5510020000000000800120A10700404B4C000255AA"),
                 "SET_BITRATE 请求编码不匹配真实网关示例"))
        return 1;

    CanGatewayConfigRequest decodedRequest;
    if (!require(decodeCanGatewayConfigRequest(packet, decodedRequest),
                 "SET_BITRATE 请求解码失败")
        || !require(decodedRequest.sequence == 2
                        && decodedRequest.nominalBitrate == 500000
                        && decodedRequest.dataBitrate == 5000000,
                    "SET_BITRATE 请求字段错误"))
        return 1;

    CanGatewayConfigResponse response;
    response.sequence = 2;
    response.status = CanGatewayConfigStatus::Ok;
    response.nominalBitrate = 500000;
    response.dataBitrate = 5000000;
    const QByteArray responsePacket = encodeCanGatewayConfigResponse(response);
    CanGatewayConfigResponse decodedResponse;
    if (!require(decodeCanGatewayConfigResponse(responsePacket, decodedResponse),
                 "0x81 配置回复解码失败")
        || !require(decodedResponse.sequence == response.sequence
                        && decodedResponse.status == CanGatewayConfigStatus::Ok
                        && decodedResponse.nominalBitrate == 500000
                        && decodedResponse.dataBitrate == 5000000,
                    "0x81 配置回复字段错误"))
        return 1;

    QByteArray corrupted = responsePacket;
    corrupted[12] = static_cast<char>(static_cast<quint8>(corrupted.at(12)) ^ 0x01U);
    QString error;
    if (!require(!decodeCanGatewayConfigResponse(corrupted, decodedResponse, &error),
                 "CRC 错误回复不应通过"))
        return 1;

    QByteArray badStatus = responsePacket;
    badStatus[11] = static_cast<char>(0x7FU);
    badStatus[20] = static_cast<char>(canGatewayConfigCrc8(badStatus.mid(2, 18)));
    if (!require(!decodeCanGatewayConfigResponse(badStatus, decodedResponse, &error),
                 "未知状态码不应通过"))
        return 1;

    CanGatewayConfigRequest badRequest = request;
    badRequest.nominalBitrate = 333333;
    if (!require(encodeCanGatewayConfigRequest(badRequest).isEmpty(),
                 "不支持的仲裁速率不应编码"))
        return 1;

    qInfo() << "CanGatewayConfigProtocolTest: PASS";
    return 0;
}
