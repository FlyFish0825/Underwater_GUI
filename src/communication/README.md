# 通信层边界

当前已实现 Windows USB CDC 虚拟串口传输和 CAN 网关协议解析。传输层负责
VID/PID 枚举、打开/关闭、收发字节和错误通知；协议层负责 AA55 帧的增量
拆包、CRC8 校验以及 CAN 帧编解码。Bootloader 命令、重连策略和业务服务仍
应在后续任务中增加，页面不得复制这些底层细节。

Bootloader 命令服务位于 `communication/bootloader`：它只处理 G431 的 Host
CONTROL、Node RESPONSE 和 Peer Control CAN payload，随后调用通信层发送，
不直接依赖 USB CDC 或 AA55。
