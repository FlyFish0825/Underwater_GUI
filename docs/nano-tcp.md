# Nano TCP 网关连接

[返回 README](../README.md)

2026-10-10 已完成真实机器人验收：使用 GUI 正式通信服务申请、接管、释放并验证原始深度 GET_STATUS 往返；Nano 普通申请在 GUI 占用时失败，明确接管成功，GUI 旧会话写权限撤销。只发送一次 20 字节状态查询，未发送电机运行、归零或参数写入。Settings、管理连接、传输等完整 CTest 21/21 通过。

手动实机验收目标为 `rov_tcp_control_hardware_probe`（位于 `build/test/gui`，不会自动加入 CTest）。它在 GUI 接管/释放/再接管后输出 `READY_NANO_TEST`，此时在 Nano 执行普通申请再明确接管，程序会检查旧 GUI 会话不能继续写入并返回 PASS。不要在用户正在控制或升级时运行此探针。

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
上位机控制：首次只读查看时不要勾选
```

Nano 默认持有串口控制权。GUI 勾选“上位机控制”会申请控制权；若 Nano 正占用，GUI 会弹出提示，
要求用户在“机器人控制权”卡片中明确点击“接管控制权”。接管后，9000 会重新连接并验证本次令牌，
随后通过 Nano 向底层发送原始串口字节。取消勾选会把控制权交回 Nano。
连接方式、地址和端口会保存，控制权不会跨应用重启保存。Nano 可通过 `/underwater_robot/gateway/control`
服务执行 `CLAIM`、`TAKEOVER` 和 `RELEASE`，GUI 与 Nano 始终只有一个控制者。
切回 `USB CDC` 保留原来的设备扫描与串口操作。

Nano 端由 UnderWater-Robot-ROS2 的现有网关独占串口。TCP 9000 传原始串口数据，9002 管理
控制权；所有 ROS 写入和电脑下发字节都由网关按唯一所有者仲裁。不要在同一串口另起一个网关。

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
Nano 控制端口 9002 → TcpControlTransport → 独占控制权状态
Nano 原始端口 9000 → TcpTransport → BootloaderCommunicationService
                                      → 原协议 → 数据服务 → 原页面
```

`ByteTransport` 是串口/TCP 共用的字节接口。页面只提交类型化控制请求，不解析网络数据。
9000 的 GUI 写会话先用令牌握手，Nano 消除握手行后按原样转发串口字节；应用数据没有新封装，
半帧和粘包仍由原增量协议解析器处理。CAN、Bootloader 和传感器的线协议不变。

- 局域网连接明确绕过系统 HTTP 代理，不修改系统代理配置。
- 连接超时 3 秒，接收静默超时 5 秒；断线后按 1～5 秒间隔重试。
- 一次只选择一个传输通道。TCP 模式不会被后台 USB 扫描覆盖。
- 断线清空 TCP 待发数据、协议半帧、设备时间展开器以及未完成的配置/升级传输状态。
- 电机调试页同时清除运行状态、目标速度和延迟发速定时器；单节点升级、读取和数据窗口也会取消。
- 控制端口每秒发送一次会话心跳，Nano 在 3 秒没有心跳后撤销令牌并恢复 Nano 控制。
- 串口重连不会重发上次命令，不自动继续固件升级；新的控制动作必须由用户重新发起。
- 待发字节最多 256 KiB，超过则断开并丢弃，避免内存增长或旧命令延迟执行。
- Nano 的 9000 端口一次只接纳一个客户端；控制权由 9002 显式申请、接管与释放。
- GUI 控制会话需要持续心跳；断线后撤销令牌、关闭写入会话并将控制权返回 Nano。

“连接成功”和心跳表示通信建立，不能证明电机已执行请求。无传感器时，页面仍应显示缺测/离线，
不会用假数据补齐。TCP 断线的停止动作依赖 Nano 与设备链路，不能替代现场独立急停。

## 验证和恢复点

修改前已在原分支保存提交 `7c79bba50249c4361feb59cb5a1dcaadd5735f75`，
提交后验证工作区干净，再创建 `codex/nano-tcp-transport` 分支开发。
该提交保留原有传感器、协议、记录器和固件页工作，可用于对比 TCP 接入前后的差异。
不要对仍有其他工作的目录直接执行破坏性回退。

按 README 的隔离配置，在 `build/test/` 构建并运行：

```powershell
cmake --build .\build\test\gui --target rov_tcp_transport_test rov_tcp_control_transport_test rov_tcp_control_settings_test rov_nano_connection_page_test --parallel 2
ctest --test-dir .\build\test\gui -R 'rov_(tcp_transport|tcp_control|nano_connection_page)_test' --output-on-failure
```

`rov_tcp_transport_test` 和 `rov_tcp_control_transport_test` 使用本机临时 TCP 服务验证所有权申请、接管、释放、令牌心跳、原始字节握手、双向写入、只读拒绝、自动重连和半帧清理。
`rov_tcp_control_settings_test` 校验 Nano/上位机/断线三个状态下的按钮行为。
`rov_nano_connection_page_test` 默认只连接本机测试服务器，不打开真实串口。
网络探针 `rov_nano_tcp_probe` 不加入自动 CTest，只有显式调用才连接局域网设备，默认只读：

```powershell
. .\toolchain\env\activate.ps1
.\build\test\gui\rov_nano_tcp_probe.exe 192.168.20.70 9000
```

`rov_nano_tcp_probe` 永远只接收，不会申请控制权或向 Nano 发送串口数据。
构建、日志和截图都在 `build/test/gui/`；真实 CAN 网关测试不发送电机运行命令。

控制权实机验收须先让 Nano 持有控制权，关闭其他 GUI 的 9000 连接，然后在 Windows 运行：

```powershell
. .\toolchain\env\activate.ps1
cmake --build .\build\test\gui --target rov_tcp_control_hardware_probe --parallel 2
.\build\test\gui\rov_tcp_control_hardware_probe.exe 192.168.20.70
```

看到 `READY_NANO_TEST` 后，在已加载正式 ROS 环境的 Nano 终端依次运行：

```bash
ros2 service call /underwater_robot/gateway/control underwater_interfaces/srv/ControlOwnership "{action: 0}"
ros2 service call /underwater_robot/gateway/control underwater_interfaces/srv/ControlOwnership "{action: 1}"
```

第一条应返回 `success=false, owner=2`，第二条应返回 `success=true, owner=1`；Windows 探针应返回 PASS。探针有 90 秒总超时，只发一次深度 GET_STATUS，不校零、不发电机运行指令。2026-10-10 的只读探针另实收 77520 字节、12 个心跳，下发字节数为 0。
