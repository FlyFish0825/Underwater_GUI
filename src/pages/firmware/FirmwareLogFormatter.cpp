#include "pages/firmware/FirmwareLogFormatter.h"

#include <QByteArray>
#include <QHash>

namespace rov
{

namespace
{

QString commandNameZh(const QString &name)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("GET_VERSION"), QStringLiteral("读取版本")},
        {QStringLiteral("GET_DEVICE_ID"), QStringLiteral("读取设备 ID")},
        {QStringLiteral("GET_INFO"), QStringLiteral("读取设备信息")},
        {QStringLiteral("ENTER_BOOT"), QStringLiteral("进入 Bootloader")},
        {QStringLiteral("SET_GUARD"), QStringLiteral("设置升级保护")},
        {QStringLiteral("RELEASE_GUARD"), QStringLiteral("解除升级保护")},
        {QStringLiteral("SESSION_BEGIN"), QStringLiteral("开始升级会话")},
        {QStringLiteral("SESSION_CRC32"), QStringLiteral("校验会话 CRC32")},
        {QStringLiteral("ERASE"), QStringLiteral("擦除 Flash")},
        {QStringLiteral("WRITE"), QStringLiteral("写入固件")},
        {QStringLiteral("READ"), QStringLiteral("读取固件")},
        {QStringLiteral("VERIFY"), QStringLiteral("校验固件")},
        {QStringLiteral("WRITE_END"), QStringLiteral("结束写入")},
        {QStringLiteral("ABORT"), QStringLiteral("中止操作")},
        {QStringLiteral("JUMP_APP"), QStringLiteral("启动 APP")},
        {QStringLiteral("RESET"), QStringLiteral("复位节点")},
        {QStringLiteral("GET_STATUS"), QStringLiteral("读取运行状态")},
        {QStringLiteral("VERIFY_REQUEST"), QStringLiteral("请求校验")},
        {QStringLiteral("VERIFY_RESULT"), QStringLiteral("返回校验结果")},
        {QStringLiteral("MISSING_COUNT"), QStringLiteral("查询缺失块数量")},
        {QStringLiteral("MISSING_ITEM"), QStringLiteral("查询缺失块")},
        {QStringLiteral("PROVIDER_GRANT"), QStringLiteral("授权数据提供者")},
        {QStringLiteral("COORDINATOR_CLAIM"), QStringLiteral("协调器声明")},
        {QStringLiteral("PROVIDER_ASSIGN"), QStringLiteral("分配数据提供者")},
        {QStringLiteral("PROVIDER_DONE"), QStringLiteral("数据提供者完成")},
        {QStringLiteral("REPAIR_ROUND_END"), QStringLiteral("结束修复轮次")},
        {QStringLiteral("RECOVERY_READY"), QStringLiteral("恢复就绪")},
        {QStringLiteral("RECOVERY_FAILED"), QStringLiteral("恢复失败")},
        {QStringLiteral("GUARD_UPDATE_BEGIN"), QStringLiteral("开始更新保护")},
        {QStringLiteral("GUARD_UPDATE_READY"), QStringLiteral("保护更新就绪")},
        {QStringLiteral("ROLLBACK_REQUEST"), QStringLiteral("请求回滚")},
        {QStringLiteral("ROLLBACK_SIZE_LO"), QStringLiteral("回滚大小低字")},
        {QStringLiteral("ROLLBACK_SIZE_HI"), QStringLiteral("回滚大小高字")},
        {QStringLiteral("ROLLBACK_CRC_LO"), QStringLiteral("回滚 CRC 低字")},
        {QStringLiteral("ROLLBACK_CRC_HI"), QStringLiteral("回滚 CRC 高字")},
        {QStringLiteral("ROLLBACK_BEGIN"), QStringLiteral("开始回滚")},
        {QStringLiteral("ROLLBACK_PREPARED"), QStringLiteral("回滚准备完成")},
        {QStringLiteral("FULL_STREAM"), QStringLiteral("完整流传输")},
        {QStringLiteral("COMMIT_PREPARE"), QStringLiteral("准备提交")},
        {QStringLiteral("COMMIT_ACK"), QStringLiteral("提交确认")},
        {QStringLiteral("COMMIT_EXECUTE"), QStringLiteral("执行提交")},
    };
    return names.value(name, QStringLiteral("命令 %1").arg(name));
}

QString statusNameZh(const QString &name)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("IDLE"), QStringLiteral("空闲")},
        {QStringLiteral("ERASE"), QStringLiteral("擦除中")},
        {QStringLiteral("WRITE"), QStringLiteral("写入中")},
        {QStringLiteral("VERIFY"), QStringLiteral("校验中")},
        {QStringLiteral("READY"), QStringLiteral("就绪")},
        {QStringLiteral("REPAIR"), QStringLiteral("修复中")},
        {QStringLiteral("GUARD"), QStringLiteral("保护中")},
        {QStringLiteral("ERROR"), QStringLiteral("错误")},
    };
    return names.value(name, name);
}

QString errorNameZh(const quint8 error)
{
    static const QHash<int, QString> names = {
        {0x00, QStringLiteral("无错误")},
        {0x01, QStringLiteral("帧校验失败")},
        {0x02, QStringLiteral("帧长度错误")},
        {0x03, QStringLiteral("地址非法")},
        {0x04, QStringLiteral("当前状态不允许该操作")},
        {0x05, QStringLiteral("Flash 擦除失败")},
        {0x06, QStringLiteral("Flash 写入失败")},
        {0x07, QStringLiteral("配置数据错误")},
        {0x08, QStringLiteral("APP 镜像无效")},
        {0x09, QStringLiteral("数据大小错误")},
        {0x0A, QStringLiteral("升级保护已锁定")},
        {0x0B, QStringLiteral("数据顺序错误")},
        {0x0C, QStringLiteral("镜像 CRC 不匹配")},
        {0x0D, QStringLiteral("数据提供者来源错误")},
        {0x0E, QStringLiteral("设备忙")},
        {0x0F, QStringLiteral("受保护区域，禁止访问")},
        {0x10, QStringLiteral("操作已中止")},
        {0x11, QStringLiteral("接收缓冲区溢出")},
        {0x12, QStringLiteral("升级会话错误")},
        {0x13, QStringLiteral("协调器错误")},
        {0x14, QStringLiteral("协同恢复失败")},
        {0x15, QStringLiteral("提交失败")},
        {0x16, QStringLiteral("APP 试运行超时")},
    };
    return names.value(error, QStringLiteral("未知错误"));
}

QByteArray responseData(const QString &body)
{
    const int marker = body.indexOf(QStringLiteral("DATA="));
    if (marker < 0)
        return {};
    const QString hex = body.mid(marker + 5).trimmed();
    const QByteArray frame = QByteArray::fromHex(hex.toLatin1());
    return frame.size() >= 7 ? frame.mid(3, 4) : QByteArray();
}

quint32 littleEndian32(const QByteArray &bytes)
{
    if (bytes.size() < 4)
        return 0;
    return static_cast<quint32>(static_cast<quint8>(bytes.at(0)))
           | (static_cast<quint32>(static_cast<quint8>(bytes.at(1))) << 8U)
           | (static_cast<quint32>(static_cast<quint8>(bytes.at(2))) << 16U)
           | (static_cast<quint32>(static_cast<quint8>(bytes.at(3))) << 24U);
}

QString responseResultZh(const QString &command, const QString &status, const QByteArray &data)
{
    if (status == QStringLiteral("ERROR"))
    {
        const quint8 error = data.isEmpty() ? 0 : static_cast<quint8>(data.at(0));
        return QStringLiteral("失败 · %1 · 错误码：0x%2")
            .arg(error == 0 ? QStringLiteral("设备未提供具体错误原因") : bootErrorNameZh(error))
            .arg(error, 2, 16, QLatin1Char('0')).toUpper();
    }
    if (command == QStringLiteral("GET_VERSION") && data.size() >= 3)
        return QStringLiteral("Bootloader 版本：v%1.%2.%3")
            .arg(static_cast<quint8>(data.at(0)))
            .arg(static_cast<quint8>(data.at(1)))
            .arg(static_cast<quint8>(data.at(2)));
    if (command == QStringLiteral("GET_DEVICE_ID") && data.size() >= 4)
        return QStringLiteral("设备 ID：0x%1")
            .arg(littleEndian32(data), 8, 16, QLatin1Char('0')).toUpper();
    if (command == QStringLiteral("GET_INFO") && data.size() >= 4)
        return QStringLiteral("APP：%1 · 配置：%2")
            .arg(static_cast<quint8>(data.at(2)) ? QStringLiteral("有效") : QStringLiteral("无效"))
            .arg(static_cast<quint8>(data.at(3)) ? QStringLiteral("有效") : QStringLiteral("无效"));
    if (command == QStringLiteral("GET_STATUS") && data.size() >= 3)
    {
        static const QHash<int, QString> statuses = {
            {0, QStringLiteral("空闲")},   {1, QStringLiteral("擦除中")},
            {2, QStringLiteral("写入中")}, {3, QStringLiteral("校验中")},
            {4, QStringLiteral("就绪")},   {5, QStringLiteral("错误")},
            {6, QStringLiteral("修复中")}, {7, QStringLiteral("保护中")},
        };
        const int state = static_cast<quint8>(data.at(0));
        const quint8 error = static_cast<quint8>(data.at(1));
        return QStringLiteral("设备状态：%1 · 错误：%2（错误码：0x%3） · 进度：%4")
            .arg(statuses.value(state, QStringLiteral("未知(%1)").arg(state)))
            .arg(error == 0 ? QStringLiteral("无") : bootErrorNameZh(error))
            .arg(error, 2, 16, QLatin1Char('0')).toUpper()
            .arg(static_cast<quint8>(data.at(2)));
    }
    if (command == QStringLiteral("MISSING_COUNT") && data.size() >= 2)
    {
        const quint16 count = static_cast<quint16>(static_cast<quint8>(data.at(0)))
                              | (static_cast<quint16>(static_cast<quint8>(data.at(1))) << 8U);
        return QStringLiteral("缺失数据块：%1 个").arg(count);
    }
    if (command == QStringLiteral("ENTER_BOOT"))
        return QStringLiteral("已接受，等待设备复位进入 Bootloader");
    if (command == QStringLiteral("JUMP_APP"))
        return QStringLiteral("已接受，设备正在跳转到 APP");
    if (command == QStringLiteral("RESET"))
        return QStringLiteral("已接受，设备正在复位");
    if (command == QStringLiteral("ABORT"))
        return QStringLiteral("升级操作已中止");
    if (command == QStringLiteral("ERASE"))
        return QStringLiteral("APP 区域擦除完成");
    if (command == QStringLiteral("WRITE"))
        return QStringLiteral("数据块写入确认");
    if (command == QStringLiteral("VERIFY"))
        return QStringLiteral("镜像校验通过");
    if (command == QStringLiteral("WRITE_END"))
        return QStringLiteral("写入结束，等待最终校验");
    if (command == QStringLiteral("SESSION_BEGIN"))
        return QStringLiteral("升级会话已建立");
    if (command == QStringLiteral("SESSION_CRC32"))
        return QStringLiteral("会话 CRC32 已确认");
    if (command == QStringLiteral("SET_GUARD"))
        return QStringLiteral("升级保护已设置");
    if (command == QStringLiteral("RELEASE_GUARD"))
        return QStringLiteral("升级保护已释放");
    return QStringLiteral("设备已正常回复");
}

} // namespace

QString bootErrorNameZh(const quint8 errorCode)
{
    return errorNameZh(errorCode);
}

QString formatFirmwareLogHtml(const QString &timestampedMessage)
{
    QString body = timestampedMessage;
    QString time;
    if (body.startsWith(QLatin1Char('[')))
    {
        const int end = body.indexOf(QLatin1Char(']'));
        if (end > 0)
        {
            time = body.left(end + 1);
            body = body.mid(end + 1).trimmed();
        }
    }

    QString summary = body;
    QString raw;
    QString color = QStringLiteral("#5b7390");
    const QStringList parts = body.split(QStringLiteral(" · "));
    if (body.startsWith(QStringLiteral("TX Node")) && parts.size() >= 2)
    {
        const QString node = parts.at(0).mid(QStringLiteral("TX Node").size());
        summary = QStringLiteral("发送 · 节点 %1 · %2").arg(node, commandNameZh(parts.at(1)));
        color = QStringLiteral("#2369c8");
        raw = body;
    }
    else if (body.startsWith(QStringLiteral("TX Peer")) && parts.size() >= 2)
    {
        summary = QStringLiteral("发送 · Peer · %1").arg(commandNameZh(parts.at(1)));
        color = QStringLiteral("#2369c8");
        raw = body;
    }
    else if (body.startsWith(QStringLiteral("RX ")) && parts.size() >= 3)
    {
        const QString status = parts.at(2).trimmed();
        summary = QStringLiteral("接收 · 节点 %1 · %2 · 状态：%3 · %4")
                      .arg(parts.at(0).mid(3), commandNameZh(parts.at(1)), statusNameZh(status),
                           responseResultZh(parts.at(1), status, responseData(body)));
        color = status == QStringLiteral("ERROR") ? QStringLiteral("#d64545")
                                                    : QStringLiteral("#078d4a");
        raw = body;
    }
    else if (body.startsWith(QStringLiteral("Peer · ")) && parts.size() >= 2)
    {
        summary = QStringLiteral("接收 · Peer · %1").arg(commandNameZh(parts.at(1)));
        color = QStringLiteral("#078d4a");
        raw = body;
    }
    else if (body.startsWith(QStringLiteral("解析到 CAN")))
    {
        summary = QStringLiteral("接收 · CAN 网关帧");
        color = QStringLiteral("#078d4a");
        raw = body;
    }
    else if (body.contains(QStringLiteral("错误")) || body.contains(QStringLiteral("失败"))
             || body.contains(QStringLiteral("超时")) || body.contains(QStringLiteral("未连接"))
             || body.contains(QStringLiteral("断开")))
    {
        color = QStringLiteral("#d64545");
    }

    QString html = QStringLiteral("<div style=\"color:%1; margin:1px 0;\"><b>%2</b> %3")
                       .arg(color, time.toHtmlEscaped(), summary.toHtmlEscaped());
    if (!raw.isEmpty())
    {
        html += QStringLiteral("<br/><span style=\"color:#8798aa;\">原始：%1</span>")
                    .arg(raw.toHtmlEscaped());
    }
    return html + QStringLiteral("</div>");
}

} // namespace rov
