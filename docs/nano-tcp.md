# Nano TCP 网关连接

[返回 README](../README.md)

## 索引

- [连接操作](#连接操作)
- [运行库与构建](#运行库与构建)
- [通信与重连](#通信与重连)
- [验证和恢复点](#验证和恢复点)

## 连接操作

Nano 提供固定地址的原始串口 TCP 服务，上位机主动连接，客户端电脑不需要固定 IP 或安装 ROS。
在“设置”页连接栏选择 `Nano TCP`，输入 Nano 地址和端口，点击“连接”。当前现场示例：

```text
Nano 地址：192.168.20.70
端口：9000
允许发送：首次只读查看时不要勾选
```

连接方式、地址和端口会保存。重新打开应用后点击连接；意外断线会自动重试，点击“断开”取消重连。
“允许发送”不跨应用重启保存；需要发送时必须同时在 Nano 开启 `tcp_allow_tx=true`。
只读连接继续接收并解析原有心跳、CAN 和传感器数据，但所有发包入口被拒绝。
切回 `USB CDC` 保留原来的设备扫描与串口操作。

Nano 端由 UnderWater-Robot-ROS2 的现有网关独占串口，新增 TCP 模块旁路转发原始字节。
默认端口关闭，`tcp.launch.xml` 默认监听 9000。双向模式禁止同时开启 ROS 电机命令发送，
并关闭本地深度命令写入；不要在同一串口另起一个网关或在两端同时控制。

当前协议是可信局域网内的明文原始字节流，不含公网鉴权或加密。

## 运行库与构建

继续使用锁定的 Qt 5.15.19 / MinGW 8.1.0，只新增现有 Qt SDK 自带的 Network 模块。
主程序仍输出到 `build/gui/rov_ui.exe`。标准构建和原运行库部署后，补充网络 DLL：

```powershell
& .\tools\deploy-tcp-runtime.ps1
```

此脚本只把锁定 SDK 中的 `Qt5Network.dll` 复制到程序目录。测试目录可显式指定：

```powershell
& .\tools\deploy-tcp-runtime.ps1 -AppDirectory .\build\test\gui
```

## 通信与重连

```text
Nano 原始串口 TCP → TcpTransport → BootloaderCommunicationService
                                   → 原协议 → 数据服务 → 原页面
```

`ByteTransport` 是串口/TCP 共用的字节接口。页面只提交连接参数，不解析网络数据。
TCP 不添加额外帧头，半帧和粘包由原增量协议解析器处理。CAN、Bootloader 和传感器的线协议不变。

- 局域网连接明确绕过系统 HTTP 代理，不修改系统代理配置。
- 连接超时 3 秒，接收静默超时 5 秒；断线后按 1～5 秒间隔重试。
- 一次只选择一个传输通道。TCP 模式不会被后台 USB 扫描覆盖。
- 断线清空 TCP 待发数据、协议半帧、设备时间展开器以及未完成的配置/升级传输状态。
- 电机调试页同时清除运行状态、目标速度和延迟发速定时器；单节点升级、读取和数据窗口也会取消。
- 重连不会重发上次命令，不自动继续固件升级；新的控制动作必须由用户重新发起。
- 待发字节最多 256 KiB，超过则断开并丢弃，避免内存增长或旧命令延迟执行。
- Nano 当前只接受一个客户端，不允许第二台电脑抢占已有连接。

“连接成功”和心跳表示通信建立，不能证明电机已执行请求。无传感器时，页面仍应显示缺测/离线，
不会用假数据补齐。TCP 断线的停止动作依赖 Nano 与设备链路，不能替代现场独立急停。

## 验证和恢复点

修改前已在原分支保存提交 `7c79bba50249c4361feb59cb5a1dcaadd5735f75`，
提交后验证工作区干净，再创建 `codex/nano-tcp-transport` 分支开发。
该提交保留原有传感器、协议、记录器和固件页工作，可用于对比 TCP 接入前后的差异。
不要对仍有其他工作的目录直接执行破坏性回退。

按 README 的隔离配置，在 `build/test/` 构建并运行：

```powershell
cmake --build .\build\test\gui --target rov_tcp_transport_test rov_nano_connection_page_test --parallel 2
ctest --test-dir .\build\test\gui -R 'rov_(tcp_transport|nano_connection_page)_test' --output-on-failure
```

`rov_tcp_transport_test` 使用本机临时 TCP 服务器验证原始字节、双向写入、只读拒绝、自动重连、静默超时和半帧清理。
`rov_nano_connection_page_test` 默认只连接本机测试服务器，不打开真实串口。
网络探针 `rov_nano_tcp_probe` 不加入自动 CTest，只有显式调用才连接局域网设备，默认只读：

```powershell
. .\toolchain\env\activate.ps1
.\build\test\gui\rov_nano_tcp_probe.exe 192.168.20.70 9000
```

`--echo-fixture` 只用于伪串口测试端：会发送测试 CAN 封装，不能用于真实设备的只读验证。
构建、日志和截图都在 `build/test/gui/`；真实 CAN 网关测试不发送电机运行命令。
