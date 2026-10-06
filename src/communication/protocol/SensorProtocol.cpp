#include "communication/protocol/SensorProtocol.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace rov
{
namespace SensorWire
{
quint16 read16(const QByteArray &d, const int o)
{
    return quint16(quint8(d.at(o))) | (quint16(quint8(d.at(o + 1))) << 8U);
}
quint32 read32(const QByteArray &d, const int o)
{
    return quint32(read16(d, o)) | (quint32(read16(d, o + 2)) << 16U);
}
float readFloat(const QByteArray &d, const int o)
{
    const quint32 bits = read32(d, o);
    float value;
    static_assert(sizeof(value) == sizeof(bits), "IEEE754 binary32 required");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
void append16(QByteArray &d, const quint16 v)
{
    d.append(char(v & 255U)); d.append(char(v >> 8U));
}
void append32(QByteArray &d, const quint32 v)
{
    append16(d, quint16(v)); append16(d, quint16(v >> 16U));
}
void appendFloat(QByteArray &d, const float v)
{
    quint32 bits;
    std::memcpy(&bits, &v, sizeof(bits));
    append32(d, bits);
}
quint16 crc16(const QByteArray &d)
{
    quint16 crc = 0xFFFFU;
    for (const char byte : d)
    {
        crc ^= quint16(quint8(byte)) << 8U;
        for (int bit = 0; bit < 8; ++bit)
            crc = quint16((crc & 0x8000U) ? ((crc << 1U) ^ 0x1021U) : (crc << 1U));
    }
    return crc;
}
}

QByteArray encodeSensorFrame(const SensorFrame &f)
{
    if ((f.target != kImuSensor && f.target != kDepthSensor)
        || f.payload.size() > SensorWire::kMaxPayload
        || (f.flags != 1 && f.flags != 2 && f.flags != 6 && f.flags != 8))
        return {};
    QByteArray p;
    p.reserve(f.payload.size() + SensorWire::kOverhead);
    p.append(char(0xAA)); p.append(char(0x5B)); p.append(char(1));
    p.append(char(f.command)); p.append(char(f.flags)); p.append(char(f.target));
    SensorWire::append32(p, f.sequence);
    SensorWire::append16(p, quint16(f.payload.size()));
    SensorWire::append32(p, f.timestampUs);
    p.append(f.payload);
    SensorWire::append16(p, SensorWire::crc16(p.mid(1)));
    p.append(char(0x5B)); p.append(char(0xAA));
    return p;
}

bool decodeSensorFrame(const QByteArray &p, SensorFrame &out, QString *error)
{
    const auto fail = [error](const QString &text) { if (error) *error = text; return false; };
    if (error) error->clear();
    if (p.size() < SensorWire::kOverhead || quint8(p.at(0)) != 0xAA || quint8(p.at(1)) != 0x5B)
        return fail(QStringLiteral("AA5B 帧头或最小长度错误"));
    const int n = SensorWire::read16(p, 10);
    if (n > SensorWire::kMaxPayload || p.size() != n + SensorWire::kOverhead)
        return fail(QStringLiteral("AA5B 负载长度错误"));
    const quint8 flags = quint8(p.at(4));
    const quint8 target = quint8(p.at(5));
    if (quint8(p.at(2)) != 1 || (target != kImuSensor && target != kDepthSensor)
        || (flags != 1 && flags != 2 && flags != 6 && flags != 8))
        return fail(QStringLiteral("AA5B 版本、目标或标志不支持"));
    if (quint8(p.at(18 + n)) != 0x5B || quint8(p.at(19 + n)) != 0xAA)
        return fail(QStringLiteral("AA5B 帧尾错误"));
    if (SensorWire::read16(p, 16 + n) != SensorWire::crc16(p.mid(1, 15 + n)))
        return fail(QStringLiteral("AA5B CRC 错误"));
    SensorFrame f;
    f.command = quint8(p.at(3)); f.flags = flags; f.target = target;
    f.sequence = SensorWire::read32(p, 6); f.timestampUs = SensorWire::read32(p, 12);
    f.payload = p.mid(16, n);
    out = f;
    return true;
}

bool makeSensorRequestPayload(const SensorRequest &r, QByteArray &p, QString &error)
{
    p.clear(); error.clear();
    const auto fail = [&error](const QString &s) { error = s; return false; };
    if (r.target != kImuSensor && r.target != kDepthSensor)
        return fail(QStringLiteral("无效传感器；禁止广播配置"));
    const quint8 cmd = quint8(r.operation);
    if (cmd < 1 || cmd > 0x0C)
        return fail(QStringLiteral("不支持的传感器命令"));
    if (r.operation == SensorOperation::ZeroDepth && r.target != kDepthSensor)
        return fail(QStringLiteral("归零只适用于深度计"));
    if (r.operation == SensorOperation::Calibrate)
    {
        if (r.target != kImuSensor || r.calibrationType < 1 || r.calibrationType > 3
            || r.calibrationAction > 1 || (r.calibrationType == 3 && r.calibrationAction != 1)
            || !std::isfinite(r.referenceTemperatureC)
            || r.referenceTemperatureC < -100 || r.referenceTemperatureC > 150)
            return fail(QStringLiteral("校准类型、动作或参考温度非法"));
        p.append(char(r.calibrationType)); p.append(char(r.calibrationAction));
        SensorWire::append16(p, quint16(qint16(std::lround(r.referenceTemperatureC * 100.0))));
        return true;
    }
    if (r.operation != SensorOperation::GetParameter && r.operation != SensorOperation::SetParameter)
        return true;

    quint8 type = 0;
    const bool depth = r.target == kDepthSensor;
    switch (r.parameterId)
    {
    case 0x0001: type = 4; break;
    case 0x0003: if (!depth) type = 2; break;
    case 0x0101: if (depth) type = 4; break;
    case 0x0102: case 0x0103: case 0x0104: if (depth) type = 7; break;
    case 0x0105: if (depth) type = 2; break;
    default: break;
    }
    if (!type) return fail(QStringLiteral("该传感器不支持此参数"));
    SensorWire::append16(p, r.parameterId);
    if (r.operation == SensorOperation::GetParameter) return true;
    bool ok = false;
    const double v = r.value.toDouble(&ok);
    if (!ok || !std::isfinite(v)) return fail(QStringLiteral("参数必须是有限数值"));
    if (type != 7 && v != std::floor(v)) return fail(QStringLiteral("该参数必须为整数"));
    bool valid = false;
    switch (r.parameterId)
    {
    case 0x0001: valid = depth ? v >= 1 && v <= 100 : v >= 10 && v <= 100; break;
    case 0x0003: valid = v == 6 || v == 9; break;
    case 0x0101: valid = v == 256 || v == 512 || v == 1024 || v == 2048 || v == 4096 || v == 8192; break;
    case 0x0102: valid = v >= 900 && v <= 1300; break;
    case 0x0103: valid = v >= 10000 && v <= 200000; break;
    case 0x0104: valid = v >= 0 && v <= double(0.99f); break;
    case 0x0105: valid = v == 0 || v == 2 || v == 30; break;
    default: break;
    }
    if (!valid) return fail(QStringLiteral("参数超出允许范围"));
    p.append(char(type)); p.append(char(type == 2 ? 1 : type == 4 ? 2 : 4));
    if (type == 2) p.append(char(quint8(v)));
    else if (type == 4) SensorWire::append16(p, quint16(v));
    else SensorWire::appendFloat(p, float(v));
    return true;
}

bool decodeSensorParameter(const QByteArray &p, quint16 &id, QVariant &value)
{
    if (p.size() < 5) return false;
    const quint8 type = quint8(p.at(2));
    const int n = quint8(p.at(3));
    if (p.size() != n + 4) return false;
    QVariant v;
    if (type == 2 && n == 1) v = quint8(p.at(4));
    else if (type == 4 && n == 2) v = SensorWire::read16(p, 4);
    else if (type == 6 && n == 4) v = SensorWire::read32(p, 4);
    else if (type == 7 && n == 4)
    {
        const float f = SensorWire::readFloat(p, 4);
        if (!std::isfinite(f)) return false;
        v = double(f);
    }
    else return false;
    id = SensorWire::read16(p, 0); value = v;
    return true;
}

QString sensorResultText(const SensorResult result)
{
    switch (result)
    {
    case SensorResult::Ok: return QStringLiteral("设备确认完成");
    case SensorResult::Unsupported: return QStringLiteral("设备不支持（接口已预留）");
    case SensorResult::BadValue: return QStringLiteral("设备拒绝：参数、长度或采样周期非法");
    case SensorResult::Busy: return QStringLiteral("设备忙，未执行新请求");
    case SensorResult::Timeout: return QStringLiteral("等待设备超时，未确认执行结果");
    case SensorResult::Offline: return QStringLiteral("传感器离线");
    case SensorResult::IoError: return QStringLiteral("设备通信失败");
    case SensorResult::Unconfirmed: return QStringLiteral("已发送/缓存值：设备无应答，未确认生效");
    case SensorResult::NotReady: return QStringLiteral("尚未就绪或无可确认的数据");
    case SensorResult::PinBlocked: return QStringLiteral("IMU 配置发送未启用或引脚被占用，命令未执行");
    }
    return QStringLiteral("未知设备结果");
}

QVector<SensorFrame> SensorDecoder::feed(const QByteArray &bytes)
{
    QVector<SensorFrame> result;
    // Byte-wise ingestion keeps retained storage below a single maximum frame,
    // including adversarial input containing unlimited noise or oversized lengths.
    for (const char byte : bytes)
    {
        m_buffer.append(byte);
        while (!m_buffer.isEmpty())
        {
            if (quint8(m_buffer.at(0)) != 0xAA)
            { m_buffer.remove(0, 1); continue; }
            if (m_buffer.size() < 2) break;
            if (quint8(m_buffer.at(1)) != 0x5B)
            { m_buffer.remove(0, 1); continue; }
            if (m_buffer.size() < 12) break;
            const int n = SensorWire::read16(m_buffer, 10);
            if (n > SensorWire::kMaxPayload || quint8(m_buffer.at(2)) != 1)
            { ++m_errors; m_buffer.remove(0, 1); continue; }
            const int total = n + SensorWire::kOverhead;
            if (m_buffer.size() < total) break;
            SensorFrame frame;
            if (decodeSensorFrame(m_buffer.left(total), frame))
            { result.append(frame); m_buffer.remove(0, total); }
            else { ++m_errors; m_buffer.remove(0, 1); }
        }
    }
    return result;
}
void SensorDecoder::reset() { m_buffer.clear(); m_errors = 0; }
} // namespace rov
