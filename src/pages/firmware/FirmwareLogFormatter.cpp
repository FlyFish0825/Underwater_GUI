#include "pages/firmware/FirmwareLogFormatter.h"

#include <QByteArray>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

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
        {QStringLiteral("JUMP_APP"), QStringLiteral("安全试运行 APP")},
        {QStringLiteral("RESET"), QStringLiteral("复位节点")},
        {QStringLiteral("GET_STATUS"), QStringLiteral("读取运行状态")},
        {QStringLiteral("WINDOW_STATUS"), QStringLiteral("窗口写入确认/状态")},
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
        return QStringLiteral("已接受，正在安全试运行 APP，等待 ENTER_BOOT 返回验证");
    if (command == QStringLiteral("RESET"))
        return QStringLiteral("已接受，设备正在复位");
    if (command == QStringLiteral("ABORT"))
        return QStringLiteral("升级操作已中止");
    if (command == QStringLiteral("ERASE"))
        return QStringLiteral("APP 区域擦除完成");
    if (command == QStringLiteral("WINDOW_STATUS") && data.size() == 4)
        return QStringLiteral("下一待写序号：%1 · 窗口：%2 包 · 可用窗口：%3")
            .arg(static_cast<quint8>(data.at(0)) | (static_cast<quint16>(static_cast<quint8>(data.at(1))) << 8U))
            .arg(static_cast<quint8>(data.at(2))).arg(static_cast<quint8>(data.at(3)));
    if (command == QStringLiteral("WRITE") && data.size() == 4)
        return QStringLiteral("写入会话已建立 · 区域：%1 · 总包数：%2 · 窗口：%3 包")
            .arg(static_cast<quint8>(data.at(0)))
            .arg(static_cast<quint8>(data.at(1)) | (static_cast<quint16>(static_cast<quint8>(data.at(2))) << 8U))
            .arg(static_cast<quint8>(data.at(3)));
    if (command == QStringLiteral("WRITE_END") && status == QStringLiteral("WRITE"))
        return QStringLiteral("设备仍在提交 Flash，等待窗口确认后重试结束写入");
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

    const auto tag = [](const QString &text, const QString &foreground, const QString &background)
    {
        return QStringLiteral("<span style=\"color:%1;background:%2;border:1px solid %2;"
                              "border-radius:3px;padding:0 3px;margin-right:2px;font-size:11px;\">%3</span>")
            .arg(foreground, background, text.toHtmlEscaped());
    };
    const auto rawLine = [](const QString &raw)
    {
        return QStringLiteral("<div style=\"color:#7b91aa;font-family:Consolas,'Courier New',monospace;"
                              "font-size:11px;line-height:1.15;white-space:pre-wrap;margin:0 0 0 6px;\">原始：%1</div>")
            .arg(raw.toHtmlEscaped());
    };
    const auto bytesFromData = [](const QString &data)
    {
        const QByteArray bytes = QByteArray::fromHex(data.simplified().toLatin1());
        return bytes;
    };
    const auto byteLength = [&bytesFromData](const QString &data)
    {
        const QByteArray bytes = bytesFromData(data);
        return bytes.isEmpty() && !data.trimmed().isEmpty() ? data.simplified().split(' ').size()
                                                            : bytes.size();
    };
    const auto baseLine = [&time](const QString &tags, const QString &title)
    {
        return QStringLiteral("<div style=\"color:#304f70;margin:1px 0;line-height:1.15;\">%1 "
                              "<span style=\"color:#365d85;\">%2</span>")
            .arg(time.toHtmlEscaped(), tags + title.toHtmlEscaped());
    };

    const QStringList parts = body.split(QStringLiteral(" · "));
    const QString command = parts.size() >= 2 ? parts.at(1).trimmed() : QString();
    const QString status = parts.size() >= 3 ? parts.at(2).trimmed() : QString();
    const bool textError = body.contains(QStringLiteral("错误"))
                           || body.contains(QStringLiteral("失败"))
                           || body.contains(QStringLiteral("超时"))
                           || body.contains(QStringLiteral("未连接"))
                           || body.contains(QStringLiteral("断开"))
                           || body.contains(QStringLiteral("错误码"))
                           || body.contains(QStringLiteral("非法"));
    const bool textWarning = body.contains(QStringLiteral("等待"))
                             || body.contains(QStringLiteral("重试"))
                             || body.contains(QStringLiteral("缺少"))
                             || body.contains(QStringLiteral("未响应"))
                             || body.contains(QStringLiteral("未识别"))
                             || body.contains(QStringLiteral("自动探测"))
                             || command == QStringLiteral("ABORT")
                             || command == QStringLiteral("ENTER_BOOT")
                             || command == QStringLiteral("JUMP_APP")
                             || status == QStringLiteral("REPAIR")
                             || status == QStringLiteral("GUARD");

    // 网关帧日志包含完整的 CAN ID、长度、序号、标志位和数据，单独拆成结构化标签，
    // 但原始文本仍然保留，便于现场排查和历史记录复核。
    static const QRegularExpression gatewayPattern(
        QStringLiteral("^解析到\\s+CAN\\s+(0x[0-9A-Fa-f]+)\\s+(\\d+)\\s+字节\\s+SEQ=(\\d+)\\s+"
                       "FLAGS=0x([0-9A-Fa-f]+)\\s+DATA=(.*)$"));
    // 兼容旧历史记录中可能附带的“信息”前缀，只要包含有效 CAN 网关帧文本就结构化显示。
    const int gatewayOffset = body.indexOf(QStringLiteral("解析到 CAN"));
    const QString gatewayText = gatewayOffset >= 0 ? body.mid(gatewayOffset) : body;
    const QRegularExpressionMatch gateway = gatewayPattern.match(gatewayText);
    if (gateway.hasMatch())
    {
        bool flagsOk = false;
        const uint flags = gateway.captured(4).toUInt(&flagsOk, 16);
        const bool isFd = flagsOk && (flags & 0x02U) != 0U;
        const bool isBrs = flagsOk && (flags & 0x04U) != 0U;
        const bool isExtended = flagsOk && (flags & 0x01U) != 0U;
        const QString frameKind = isFd ? QStringLiteral("CAN FD") : QStringLiteral("Classic CAN");
        const QString lengthLabel = isFd ? QStringLiteral("LEN %1").arg(gateway.captured(2))
                                         : QStringLiteral("DLC %1").arg(gateway.captured(2));
        const QString idLabel = QStringLiteral("ID %1").arg(gateway.captured(1).toUpper());
        const QString gatewayTagColor = QStringLiteral("#087f5b");
        const QString gatewayBg = QStringLiteral("#e9f8ef");
        const bool gatewayError = gateway.captured(1).compare(QStringLiteral("0x000007FA"), Qt::CaseInsensitive) == 0
                                  || gateway.captured(1).compare(QStringLiteral("0x000007FB"), Qt::CaseInsensitive) == 0
                                  || gateway.captured(1).compare(QStringLiteral("0x000007FC"), Qt::CaseInsensitive) == 0;
        const bool gatewayWarning = gateway.captured(1).compare(QStringLiteral("0x000007FD"), Qt::CaseInsensitive) == 0
                                    || gateway.captured(1).compare(QStringLiteral("0x000007FE"), Qt::CaseInsensitive) == 0;
        const QString stateColor = gatewayError ? QStringLiteral("#cf3030")
                                                : gatewayWarning ? QStringLiteral("#a46300")
                                                                  : gatewayTagColor;
        QString html = baseLine(
            tag(QStringLiteral("接收 RX"), stateColor,
                gatewayError ? QStringLiteral("#fff0f0")
                             : gatewayWarning ? QStringLiteral("#fff7e5") : gatewayBg)
                + tag(frameKind, isFd ? QStringLiteral("#2167b2") : QStringLiteral("#5b6f83"),
                      isFd ? QStringLiteral("#eaf3ff") : QStringLiteral("#f1f4f7"))
                + tag(isExtended ? QStringLiteral("EXT") : QStringLiteral("STD"),
                      isFd ? QStringLiteral("#2167b2") : QStringLiteral("#5b6f83"),
                      isFd ? QStringLiteral("#eaf3ff") : QStringLiteral("#f1f4f7"))
                + tag(idLabel, QStringLiteral("#304f70"), QStringLiteral("#eef4fa"))
                + tag(lengthLabel, QStringLiteral("#304f70"), QStringLiteral("#eef4fa"))
                + (isBrs ? tag(QStringLiteral("BRS"), QStringLiteral("#2167b2"), QStringLiteral("#eaf3ff"))
                         : QString()),
            QStringLiteral("网关帧 · %1 · %2")
                .arg(isExtended ? QStringLiteral("扩展帧") : QStringLiteral("标准帧"),
                     QStringLiteral("SEQ=%1").arg(gateway.captured(3))));
        html += rawLine(body);
        return html + QStringLiteral("</div>");
    }

    static const QRegularExpression txPattern(
        QStringLiteral("^TX Node\\s*(\\d+)\\s+·\\s+([^·]+)\\s+·\\s+CAN=(0x[0-9A-Fa-f]+)\\s+"
                       "·\\s+DATA=(.*)$"));
    const QRegularExpressionMatch tx = txPattern.match(body);
    if (tx.hasMatch())
    {
        const QString commandName = tx.captured(2).trimmed();
        const QString data = tx.captured(4).trimmed();
        const QString stateColor = textError ? QStringLiteral("#cf3030")
                                             : textWarning ? QStringLiteral("#a46300")
                                                           : QStringLiteral("#2167b2");
        QString html = baseLine(
            tag(QStringLiteral("发送 TX"), stateColor,
                textError ? QStringLiteral("#fff0f0")
                          : textWarning ? QStringLiteral("#fff7e5") : QStringLiteral("#eaf3ff"))
                + tag(QStringLiteral("STD"), QStringLiteral("#5b6f83"), QStringLiteral("#f1f4f7"))
                + tag(QStringLiteral("DLC %1").arg(byteLength(data)), QStringLiteral("#304f70"), QStringLiteral("#eef4fa"))
                + tag(QStringLiteral("ID %1").arg(tx.captured(3).toUpper()), QStringLiteral("#304f70"), QStringLiteral("#eef4fa")),
            QStringLiteral("节点 %1 · %2").arg(tx.captured(1), commandNameZh(commandName)));
        html += rawLine(body);
        return html + QStringLiteral("</div>");
    }

    if (body.startsWith(QStringLiteral("TX Peer")) && parts.size() >= 2)
    {
        const QString color = textError ? QStringLiteral("#cf3030")
                                        : textWarning ? QStringLiteral("#a46300")
                                                      : QStringLiteral("#2167b2");
        QString html = baseLine(tag(QStringLiteral("发送 TX"), color,
                                    textError ? QStringLiteral("#fff0f0")
                                              : textWarning ? QStringLiteral("#fff7e5") : QStringLiteral("#eaf3ff")),
                                QStringLiteral("Peer · %1").arg(commandNameZh(parts.at(1))));
        html += rawLine(body);
        return html + QStringLiteral("</div>");
    }

    if (body.startsWith(QStringLiteral("RX ")) && parts.size() >= 3)
    {
        const QString result = responseResultZh(parts.at(1), status, responseData(body));
        const bool responseError = status.compare(QStringLiteral("ERROR"), Qt::CaseInsensitive) == 0
                                   || status.contains(QStringLiteral("错误"))
                                   || status.contains(QStringLiteral("失败"))
                                   || body.contains(QStringLiteral("错误码"));
        const QString color = responseError ? QStringLiteral("#cf3030")
                                            : (textWarning ? QStringLiteral("#a46300") : QStringLiteral("#087f5b"));
        QString html = baseLine(tag(QStringLiteral("接收 RX"), color,
                                    responseError ? QStringLiteral("#fff0f0")
                                                  : textWarning ? QStringLiteral("#fff7e5") : QStringLiteral("#e9f8ef"))
                                      + tag(QStringLiteral("STD"), QStringLiteral("#5b6f83"), QStringLiteral("#f1f4f7")),
                                QStringLiteral("节点 %1 · %2 · 状态：%3 · %4")
                                    .arg(parts.at(0).mid(3), commandNameZh(parts.at(1)), statusNameZh(status), result));
        html += rawLine(body);
        return html + QStringLiteral("</div>");
    }

    if (body.startsWith(QStringLiteral("Peer · ")) && parts.size() >= 2)
    {
        const QString color = textError ? QStringLiteral("#cf3030")
                                        : textWarning ? QStringLiteral("#a46300")
                                                      : QStringLiteral("#087f4a");
        QString html = baseLine(tag(QStringLiteral("接收 RX"), color,
                                    textError ? QStringLiteral("#fff0f0")
                                              : textWarning ? QStringLiteral("#fff7e5") : QStringLiteral("#e9f8ef")),
                                QStringLiteral("Peer · %1").arg(commandNameZh(parts.at(1))));
        html += rawLine(body);
        return html + QStringLiteral("</div>");
    }

    const QString color = textError ? QStringLiteral("#cf3030")
                                    : textWarning ? QStringLiteral("#a46300")
                                                  : QStringLiteral("#5b7390");
    const QString background = textError ? QStringLiteral("#fff0f0")
                                         : textWarning ? QStringLiteral("#fff7e5") : QStringLiteral("#f1f4f7");
    QString html = baseLine(tag(textError ? QStringLiteral("错误") : textWarning ? QStringLiteral("警告")
                                                                        : QStringLiteral("信息"),
                              color, background),
                            body);
    return html + QStringLiteral("</div>");
}

} // namespace rov
