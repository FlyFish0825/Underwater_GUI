# 协议层边界

`CanGatewayProtocol` 实现 `CAN_To_Uart` 的 AA55 字节流增量拆包、CRC8（多项式
`0x07`）校验、CAN 帧解析和编码。协议对象只包含数据，不包含 QWidget、日志
文案或页面 ID；后续 Bootloader 的 CONTROL/RESPONSE 命令应继续放在本层或
独立的协议服务中。
