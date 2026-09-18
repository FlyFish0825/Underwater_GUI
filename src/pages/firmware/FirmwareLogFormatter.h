#pragma once

#include <QString>

namespace rov
{

/**
 * @brief 将日志原始文本解析为带颜色的中文 HTML。
 *
 * 历史记录只保存原始消息，实时窗口和历史窗口均在显示时调用此函数，
 * 确保两处的中文摘要、发送/接收颜色和原始帧格式保持一致。
 */
QString formatFirmwareLogHtml(const QString &timestampedMessage);

/** @brief 返回 Bootloader 错误码的中文含义；数值 0 表示未发生错误。 */
QString bootErrorNameZh(quint8 errorCode);

} // namespace rov
