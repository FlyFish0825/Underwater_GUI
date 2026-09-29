# 协议层边界

`CanGatewayProtocol` 实现 `CAN_To_Uart` 的 AA55 字节流增量拆包、CRC8（多项式
`0x07`）校验、CAN 帧解析和编码。协议对象只包含数据，不包含 QWidget、日志
文案或页面 ID；后续 Bootloader 的 CONTROL/RESPONSE 命令应继续放在本层或
独立的协议服务中。

`SystemHeartbeatProtocol` 独立解析每 1000 ms 发送的 `AA 58` System PING，
使用 CRC16-CCITT 校验。当前状态心跳的 `PAYLOAD_LEN=5`，完整帧为 25 字节，
payload 依次是 USB RX、USB TX、CAN RX、CAN TX 和 AA59 队列占用率；同时兼容
`PAYLOAD_LEN=0` 的旧 20 字节格式。心跳只用于判断 USB CDC 公共链路是否在线，
不转换成 CAN 帧，也不参与 Bootloader 状态机。

Observer Motor 参数辨识沿用现有 `0x100` 控制入口：`Command=0x40` 时 Byte3 是
Action，结果使用 `0x340 + NodeID` 的 64 字节 CAN FD 事件帧。协议层只负责字段校验
和 `float32` 解码；`ObserverMotorDataService` 负责按 NodeID 缓存 RAM 结果，页面通过
`MotorCalibrationSnapshot` 判断活动任务、超时和所需 Valid 位。停止确认、RAM 快照和
Rs 阶段完成事件不能直接当成完整辨识成功。
