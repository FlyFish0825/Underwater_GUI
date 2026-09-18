# ROV Qt 上位机

这是水下机器人 ROV 的 Qt Widgets 上位机工程。当前已在固件升级页接入 **Windows USB CDC 虚拟串口发现、VID/PID 筛选、串口接管、心跳监视、CAN 网关 AA55 增量拆包、Bootloader 命令中心和可选择 Classic CAN/CAN FD 的 Legacy 单节点正式下载**；其他设备驱动仍按任务卡逐步实现。

## 开始开发前必须阅读

后续所有开发者、Agent 和协作者，开始任务前必须先阅读 `agent` 目录中的统一规范：

1. `agent/agent.md`：当前任务执行规则和检查要求；
2. `agent/ROV Qt 上位机集成版统一规范与Agent提示词 v2.1-integrated.md`：集成后的总规范、集成人提示词和各页面 Agent 提示词；
3. `agent/ROV上位机通信协议与分层架构规范_v1.0.md`：Transport、Protocol、Service/Data、UI 的分层边界；
4. `agent/示意图/`：Dashboard、电机调试、Bootloader、机械臂和双目视觉参考图。

不得跳过上述文档自行建立另一套工程结构、主题、页面接口或通信方式。任务卡与规范冲突时，必须先报告冲突和影响范围。

## 工程结构

```text
src/app/             主窗口和应用入口
src/pages/           Dashboard、Motor Debug、Firmware、Manipulator、Vision、Settings
src/contracts/       页面快照和请求契约
src/communication/   USB CDC 传输与 CAN 网关协议解析
src/data/            Service/Data 层占位
src/ui/              公共控件和主题
resources/           Qt 资源和统一 QSS
docs/                框架、工具链、契约文档
agent/               Agent 提示词、通信规范和参考图
```

依赖方向必须保持：

```text
UI → Service/Data → Protocol → Transport
```

页面不得自行实现串口或协议细节；固件页仅通过通信层提供的设备枚举、连接状态和解析结果接收数据。

## 编译与运行

使用项目已经锁定的工具链：

```powershell
. 'F:\file\BaiduSyncdisk\Project\Underwater_GUI\toolchain\env\activate.ps1'
& 'F:\file\BaiduSyncdisk\Project\Underwater_GUI\toolchain\env\build-gui.ps1'
```

编译结果位于 `build/gui/rov_ui.exe`。Qt 和 MinGW 运行库部署使用匹配当前工具链的 `deploy-qt.ps1`。

为了生成可重复的界面截图：

```powershell
& 'F:\file\BaiduSyncdisk\Project\Underwater_GUI\build\gui\rov_ui.exe' `
    --page 0 --capture 'F:\file\BaiduSyncdisk\Project\Underwater_GUI\build\gui\dashboard.png'
```

页面索引：0 Dashboard，1 Motor Debug，2 Firmware，3 Manipulator，4 Vision，5 Settings。

## Git 约定

`build/`、`toolchain/`、日志、缓存和编译产物只保留在本机，不提交到 Git。源码、文档、协议规范、资源和参考图可以提交。完整规则见根目录 `.gitignore`。

## USB CDC 通信

固件页按底层 `CAN_To_Uart` 工程筛选 `VID_0483`、`PID_5740`（十六进制）。启动或点击“刷新设备”后，若发现匹配设备会自动连接；找不到设备时仍可手动刷新和接管。USB CDC 不使用波特率；界面解析 `AA 55` CAN 网关帧并显示 CAN 数据，`AA 58` 心跳帧只在后台用于在线判断，不在日志中显示。心跳连续约 2.5 秒未收到时标记为疑似离线。点击“下载到选中节点”会按 `ENTER_BOOT → ERASE → WRITE → DATA → WRITE_END → VERIFY → JUMP_APP` 执行真实下载。

## 通信思路（当前实现）

发起人最新 `main` 的通信实现采用“传输层、协议层、业务服务、页面”逐层解耦的方式。页面不直接拼接串口字节、CAN ID 或 CRC，而是提交结构化业务请求；接收数据则沿相反方向解码后通过 Qt 信号返回页面。

```text
FirmwarePage
    ↓ 结构化命令/下载请求
BootloaderService
    ↓ 生成 Host CONTROL、Peer Control 或数据块
BootloaderCommunicationService
    ↓ 协议族路由、状态机、超时和 ACK/Credit
SerialTransport
    ↓ Windows USB CDC 原始字节
STM32H750 CAN_To_Uart
    ↓ CAN / CAN FD
STM32G431 Bootloader
```

USB CDC 的接收入口先按帧头、长度和帧尾分离协议族，再交给独立解码器：

| 协议 | 用途 | 校验/特点 |
| --- | --- | --- |
| `AA55` | CAN 网关短帧、Bootloader 控制帧和响应 | 增量拆包、CRC8；适合零散命令 |
| `AA58` | H750 与上位机公共链路心跳 | 固定 20 字节、CRC16-CCITT；只判断链路在线，不转换成 CAN |
| `AA59` | 连续固件数据的网关流控 | CRC16、`BEGIN → DATA_BLOCK → END`、Credit/累计 ACK |

固件下载刻意分成两条不同的控制链：`ENTER_BOOT`、`ERASE`、`WRITE`、`VERIFY` 等零散命令走 `AA55`；连续 APP 数据走 `AA59`。`AA59` 的 Credit/ACK 只证明数据已被 H750 接收并转发，G431 自己的 Flash 提交窗口仍由 `WRITE` 返回值和 `WINDOW_STATUS` 确认，不能用网关 ACK 冒充节点写入成功。

下载控制器是异步状态机：先等待节点进入 Bootloader，再擦除、建立写入窗口、按 Credit 发送数据、查询/重发未提交窗口，最后执行 `WRITE_END → VERIFY → JUMP_APP`。ACK 会去重，超时先查询窗口而不是盲目重发或擦除；连续无进展达到上限后停止并保留错误信息。控制帧始终使用 Classic CAN 8 字节，数据面才根据页面选择使用 Classic CAN 八片分片或 CAN FD+BRS 单帧。

代码落点如下：

- `src/communication/transport/SerialTransport.*`：VID/PID 枚举、USB CDC 打开关闭、原始字节读写；不认识页面和 Bootloader 命令。
- `src/communication/protocol/`：`AA55`、`AA58`、`AA59` 的增量解析、编码和 CRC；只处理值类型数据。
- `src/communication/service/BootloaderCommunicationService.*`：统一接收路由、串口生命周期、AA59 流控状态和超时。
- `src/communication/bootloader/BootloaderService.*`：把结构化 Bootloader 请求转换为 CAN payload，并选择 AA55 或 AA59。
- `src/communication/bootloader/BootloaderDownloadController.*`：固件文件、CRC32、下载阶段、窗口确认和失败恢复策略。

当前通信实现的边界是固件升级链路：Dashboard 的姿态、推进器和六自由度控制仍以页面契约/Preview 为主，尚未接入真实 ROV 状态服务；Jetson Nano/TCP Transport、8 节点自治升级、自动缺包修复和真实设备全流程联调仍属于后续工作。详细命令、限制和实机测试步骤见 [Bootloader 功能说明](docs/bootloader/README.md)。

## Bootloader

固件升级页的 Bootloader 功能、命令列表、Classic CAN/CAN FD 选择、真实下载流程、限制和测试步骤见 [Bootloader 功能说明](docs/bootloader/README.md)。

多人协作请先阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。所有开发者和 Agent 都必须先阅读 `agent/` 目录中的统一提示词及通信分层规范，从 `main` 创建功能分支，通过 Pull Request 合并；不要直接向 `main` 推送。

## 相关文档

- `docs/framework.md`：工程框架和分层边界；
- `docs/ui-contract.md`：页面输入快照与输出请求契约；
- `docs/toolchain.md`：精确工具链和构建记录；
- `agent/`：后续开发必须遵守的统一 Agent 提示词、通信规范和参考图。
