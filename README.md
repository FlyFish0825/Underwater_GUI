# ROV Qt 上位机

这是水下机器人 ROV 的 Qt Widgets 上位机工程。当前已在固件升级页接入 **Windows USB CDC 虚拟串口发现、VID/PID 筛选、串口接管、心跳监视、CAN 网关 AA55 增量拆包、Bootloader 命令中心和固定使用 Classic CAN 的 Legacy 单节点正式下载**；电机调试页已接入 Observer Motor 控制帧、Qwt 多窗口曲线和预览变量目录；其他设备驱动仍按任务卡逐步实现。

## 开始开发前必须阅读

后续所有开发者、Agent 和协作者，开始任务前必须先阅读 `agent` 目录中的统一规范：

1. `agent/agent.md`：唯一的当前任务执行规则、分层边界、日志策略和验收要求；
2. `agent/ROV上位机通信协议与分层架构规范_v1.0.md`：Transport、Protocol、Service/Data、UI 的协议分层细节；
3. `agent/示意图/`：Dashboard、电机调试、Bootloader、机械臂和双目视觉参考图。

不得跳过上述文档自行建立另一套工程结构、主题、页面接口或通信方式。任务卡与规范冲突时，必须先报告冲突和影响范围。

## 工程结构

```text
src/app/             主窗口和应用入口
src/pages/           Dashboard、Motor Debug、Firmware、Manipulator、Vision、Settings
src/contracts/       页面快照和请求契约
src/communication/   USB CDC 传输与 CAN 网关协议解析
src/data/            Service/Data 层与电机快照
src/ui/              公共控件和主题
src/widgets/plot/    Qwt 曲线控件适配层
third_party/qwt-6.3/ 可追溯的 Qwt 6.3.1 上游源码和许可证
resources/           Qt 资源和统一 QSS
docs/                框架、工具链、契约文档
agent/               Agent 提示词、通信规范和参考图
```

依赖方向必须保持：

```text
UI → Service/Data → Protocol → Transport
```

Observer_Motor 的实际链路为：USB CDC 虚拟串口 → AA55 CAN 网关帧 →
ObserverMotorProtocol → ObserverMotorDataService → UI 快照。UI 不直接解析
CAN ID、DLC、字节偏移或心跳 CRC8。

页面不得自行实现串口或协议细节；固件页仅通过通信层提供的设备枚举、连接状态和解析结果接收数据。

## 当前已实现的电机调试能力

- 通过 `MotorDebugPage → MainWindow → BootloaderCommunicationService` 发送
  `0x100`、24 字节 CAN FD 控制帧，当前网关标志为 `FLAGS=0x02`（BRS 关闭）；
- 支持 Node1~Node8 目标选择、目标转速下发和启动/停止请求；
- 使用 Qwt 显示多个独立曲线窗口，每个窗口可叠加变量并单独自动适配、框选缩放、
  中键平移和查看图例；
- 默认曲线组合为 `IABC`（U/V/W 三相电流）和 `Speed`（转速/PLL 电速度），也可
  新建自定义窗口从变量目录叠加曲线；
- 高频 `解析到 CAN` 文本不进入固件页实时日志、历史记录或保存文件；Motor Debug
  页面不可见时，绘图入口直接丢弃高频电机帧。

当前 Qwt 曲线使用预览样本和约 50 ms 的数据服务快照；真实设备原始 1000 Hz 样本环、
窗口布局持久化和压力测试仍需单独完成，不能把预览截图视为实机联调结果。详细边界见
[`docs/plotting.md`](docs/plotting.md)。

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

构建后可运行协议测试：

```powershell
ctest --test-dir build/gui --output-on-failure
```

VSCode/clangd 使用 `build/gui/compile_commands.json`。需要手工确认跳转时，可执行：

```powershell
& 'toolchain/llvm/14.0.6/bin/clangd.exe' `
  --check=src/pages/motor_debug/MotorDebugPage.cpp `
  --compile-commands-dir=build/gui `
  --query-driver='toolchain/mingw/8.1.0/Tools/mingw810_64/bin/*' --log=error
```

## Git 约定

`build/`、`toolchain/`、日志、缓存和编译产物只保留在本机，不提交到 Git。源码、文档、协议规范、资源和参考图可以提交。完整规则见根目录 `.gitignore`。

## USB CDC 通信

固件页按底层 `CAN_To_Uart` 工程筛选 `VID_0483`、`PID_5740`（十六进制）。启动或点击“刷新设备”后，若发现匹配设备会自动连接；找不到设备时仍可手动刷新和接管。USB CDC 不使用波特率；界面解析 `AA 55` CAN 网关帧并显示 CAN 数据，`AA 58` 心跳帧只在后台用于在线判断，不在日志中显示。心跳连续约 2.5 秒未收到时标记为疑似离线。点击“下载到选中节点”会按 `ENTER_BOOT → ERASE → WRITE → DATA → WRITE_END → VERIFY → JUMP_APP` 执行真实下载。

## Bootloader

固件升级页的 Bootloader 功能、命令列表、Classic CAN 真实下载流程、限制和测试步骤见 [Bootloader 功能说明](docs/bootloader/README.md)。

多人协作请先阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。所有开发者和 Agent 都必须先阅读 `agent/` 目录中的统一提示词及通信分层规范，从 `main` 创建功能分支，通过 Pull Request 合并；不要直接向 `main` 推送。

## 相关文档

- `docs/framework.md`：工程框架和分层边界；
- `docs/architecture.md`：按总览、局部、细节组织的项目架构图；
- `docs/plotting.md`：高速多窗口曲线组件调研、许可证和接入方案；
- `docs/dependencies.md`：实际使用的第三方库、版本、许可证和构建范围；
- `docs/ui-contract.md`：页面输入快照与输出请求契约；
- `docs/toolchain.md`：精确工具链和构建记录；
- `agent/`：后续开发必须遵守的统一 Agent 提示词、通信规范和参考图。
