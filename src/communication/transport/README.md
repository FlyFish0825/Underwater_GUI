# 传输层边界

`SerialTransport` 使用 Windows 原生串口 API 接管 USB CDC 虚拟串口，按
`VID_0483/PID_5740` 枚举设备，并以非阻塞轮询方式提供字节接收。USB CDC
不依赖实际波特率，代码中的 115200 仅用于满足 Win32 串口接口。传输层不
知道固件、CAN 命令、页面或控件。
