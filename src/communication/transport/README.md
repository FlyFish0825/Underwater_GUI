# 传输层边界

新增 `ByteTransport` 公共字节接口和 `TcpTransport`：上位机可主动连接 Nano 固定地址的 TCP 服务，
支持只读、显式发送、自动重连、收发边界和断线清队列。协议解析仍在原服务/协议层。
使用方法与恢复点见 [Nano TCP 连接](../../../docs/nano-tcp.md)。

`SerialTransport` 使用 Windows 原生串口 API 接管 USB CDC 虚拟串口，按
`VID_0483/PID_5740` 枚举设备，并以非阻塞轮询方式提供字节接收。USB CDC
不依赖实际波特率，代码中的 115200 仅用于满足 Win32 串口接口。传输层不
知道固件、CAN 命令、页面或控件。
