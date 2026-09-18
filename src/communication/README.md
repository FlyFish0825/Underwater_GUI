# 通信层边界

当前已实现 Windows USB CDC 虚拟串口传输和 CAN 网关协议解析。传输层负责
VID/PID 枚举、打开/关闭、收发字节和错误通知；统一接收入口先按协议族和
长度路由 AA55、AA58、AA59。协议层负责 AA55 CAN 帧、AA58 心跳以及 AA59
连续数据 Credit/ACK 的增量拆包与 CRC 校验，页面不得复制这些底层细节。

Bootloader 命令服务位于 `communication/bootloader`：它只处理 G431 的 Host
CONTROL、Node RESPONSE 和 Peer Control CAN payload。零散控制帧通过 AA55
发送，APP DATA 窗口通过 AA59 BEGIN/DATA_BLOCK/END 状态机发送，不直接
依赖 USB CDC 读写实现。
