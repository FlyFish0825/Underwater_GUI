# ROV Qt 上位机 Agent 统一规范

本文件是本项目唯一的 Agent 开发规范。通信协议细节以同目录的
`ROV上位机通信协议与分层架构规范_v1.0.md` 和源码为准；工具链版本以
`docs/开发说明.md` 和 `.vscode/` 为准。旧的集成版提示词已经合并到本文，
不再维护第二份重复规范。

## 1. 当前工程事实

这是一个 Qt Widgets/C++17 的 Windows ROV 上位机，当前不是纯 UI 原型，
已经包含 USB CDC、CAN 网关、Observer Motor 数据链路和 Bootloader 下载链路。

当前数据流：

```text
USB CDC 虚拟串口
    ↓
SerialTransport
    ↓
CanGatewayProtocol / BootloaderCommunicationService
    ├── ObserverMotorProtocol
    │       ↓
    │   ObserverMotorDataService
    │       ↓
    │   MotorDebugPage 快照
    └── BootloaderService / BootloaderDownloadController
            ↓
        FirmwarePage 日志和升级状态
```

页面通过 `MainWindow` 接收快照或输出业务请求，不得直接解析 USB、AA55、
CAN ID、DLC、字节偏移或 CRC。

已实现的 Observer Motor 关键帧：

- `0x100`：上位机到电机，24 字节 CAN FD 控制帧，当前 `FLAGS=0x02`，BRS 关闭；
- `0x201`~`0x208`：电机到上位机，12 字节普通反馈；
- `0x301`~`0x308`：电机到上位机，64 字节调试反馈；
- `0x281`~`0x288`：每秒心跳；`0x381`~`0x388`：上电 HELLO。

`0x100` 的字节布局、保留字节清零、节点范围和命令含义必须通过
`ObserverMotorProtocol::ControlFrame`、编码函数和协议测试维护，禁止在页面中
重新拼包。

固件页已接入匹配 `VID_0483/PID_5740` 的 USB CDC 设备发现、串口接管、心跳
监视、Bootloader 命令中心以及正式下载流程。Classic CAN 下载和协议限制见
`docs/bootloader/README.md`，不要把固件页的高级命令误认为电机协议命令。

## 2. 开始任务前必须读取

1. 本文件；
2. `README.md`、`docs/开发说明.md`；
3. 与任务相关的 `docs/bootloader/README.md` 或页面 README；
5. `agent/ROV上位机通信协议与分层架构规范_v1.0.md`；
6. 当前源码、CMake 文件、公共主题、参考图和任务卡。

架构阅读入口是 `docs/architecture.md`：先看系统总览，再看通信/UI 局部，最后看
`0x100`、高频反馈和 Bootloader 细节。曲线组件选择和默认预设见
`docs/plotting.md`。

如果规范、任务卡和源码不一致，先在阶段报告中说明冲突、采用规则和影响文件，
再修改，不得静默恢复历史行为。

## 3. 分层和目录边界

正式依赖方向为：

```text
UI/Page → MainWindow/Contract → Service/Data → Protocol → Transport
```

- `src/communication/transport`：USB CDC/串口字节传输；
- `src/communication/protocol`：AA55 网关、Observer Motor、网关配置等纯协议；
- `src/communication/bootloader` 和 `src/communication/service`：升级状态机、
  发送队列、节点命令和通信服务；
- `src/data/services`：解析后的电机状态、批量发布和 freshness；
- `src/contracts`：页面快照和请求等可复制值类型；
- `src/pages`：独立 QWidget，只负责显示和发出类型化请求；
- `src/app`：唯一 `MainWindow`、导航、全局连接和页面装配；
- `src/ui/common`、`src/ui/theme`：公共控件和主题。

禁止出现以下依赖：

```text
Page → SerialTransport / Protocol / CRC / 原始 CAN 字节
Protocol → QWidget
Transport → 直接更新页面
```

契约字段要写清类型、单位、范围、有效性、新鲜度和稳定业务 ID。未知值显示
`--`，不能用 0 伪装缺失；目标值、设备反馈和用户请求必须分开。所有 QWidget
更新在 GUI 线程，跨线程只传递 Qt 信号和可复制快照。

## 4. 高频数据和日志规则

电机反馈可以达到 100/1000 Hz，不能让每一帧都触发 HTML 渲染、历史保存或日志
窗口刷新。

- 数据服务持续接收电机普通反馈（当前约 100 Hz），按已有批量/定时机制发布约 50 ms
  快照；总览和 Motor Debug 共用快照，页面不可见时只跳过曲线重绘。
- 不需要绘图时，不把高频反馈转成文本日志；
- 固件页只保留 Bootloader 命令收发、下载阶段、进度、错误和必要状态日志；
- 高频 `解析到 CAN` 诊断行不得进入实时日志、录制文件、历史记录或高级命令监视；
- 新增日志时先判断频率和用途，优先使用计数器/汇总，不逐样本打印。

## 5. UI 和控件约定

工程使用一个 `rov::MainWindow`、一个 `QStackedWidget` 和固定导航 ID：
`dashboard`、`motor_debug`、`firmware`、`manipulator`、`vision`、`settings`。
页面是 `rov` 命名空间下的独立 QWidget，构造函数使用
`explicit XxxPage(QWidget *parent = nullptr)`。

页面输入使用 `setSnapshot(const XxxSnapshot&)`，输出使用有类型的 `...Requested`
信号。刷新快照不得误发用户操作信号，必要时使用 `QSignalBlocker`。
按住式控制必须覆盖释放、失焦、切页、禁用和销毁。

公共输入控件必须在普通窗口和 `QGraphicsProxyWidget` 场景中可点击、可聚焦、可编辑。
若 Fluent 包装控件导致弹出框或文本框失焦，优先修复公共控件层，不在页面中添加
临时鼠标拦截。

视觉上保持白灰背景、深蓝文字、蓝色主操作和明确的绿/橙/红状态；不把截图当作
UI，不使用 emoji 代替正式图标，不引入 QML、WebEngine、Qt Charts 或 Qt 6 专属 API。

## 6. Bootloader 高级命令

Bootloader 命令的唯一枚举来源是 `src/communication/bootloader/BootloaderTypes.h`。
高级命令窗口的列表、参数提示、发送编码和 `FirmwarePage` 的中文日志映射必须
保持同步。新增或修改命令时至少同步：

1. `BootCommand` 枚举及编码/解码；
2. `BootloaderCommandDialog.cpp` 的可选项、参数校验和说明；
3. `FirmwarePage.cpp`/`FirmwareLogFormatter.cpp` 的日志名称；
4. 对应协议测试和 `docs/bootloader/README.md`。

不要把 `WINDOW_STATUS`、Guard、Rollback、Full Stream、Commit 等 Bootloader
命令删减成“常用命令”后声称协议完整；当前列表应覆盖源码中的全部命令。

## 7. 固定工具链和 VSCode/clangd

必须使用项目锁定工具链，不依赖机器上其他版本：

| 工具 | 版本/位置 |
| --- | --- |
| Qt Widgets | `toolchain/qt/5.15.19` |
| MinGW GCC/G++ | `toolchain/mingw/8.1.0/Tools/mingw810_64` |
| CMake | `toolchain/cmake/3.28.6/.../bin/cmake.exe` |
| Ninja | `toolchain/ninja/1.11.1/ninja.exe` |
| clangd/clang-format | `toolchain/llvm/14.0.6` |

先执行：

```powershell
. 'toolchain/env/activate.ps1'
```

构建目录固定为 `build/gui`。VSCode 使用 `.vscode/settings.json`、
`.vscode/tasks.json`、`.vscode/launch.json` 和根目录 `.clangd`；clangd 必须读取
`build/gui/compile_commands.json`，并使用项目 MinGW 查询驱动，不能误用 MSVC STL。

## 8. 验证和交付

每次修改至少运行：

```powershell
. 'toolchain/env/activate.ps1'
& 'toolchain/cmake/3.28.6/cmake-3.28.6-windows-x86_64/bin/cmake.exe' --build build/gui --parallel 4
& 'toolchain/cmake/3.28.6/cmake-3.28.6-windows-x86_64/bin/cmake.exe' -E chdir build/gui ctest --output-on-failure
git diff --check
```

涉及 C++ 跳转时，再执行：

```powershell
& 'toolchain/llvm/14.0.6/bin/clangd.exe' --check=src/app/MainWindow.cpp `
  --compile-commands-dir=build/gui `
  --query-driver='toolchain/mingw/8.1.0/Tools/mingw810_64/bin/*' --log=error
```

交付报告必须列出：完成区域、修改文件、契约/协议变化、真实构建和测试结果、
是否做过 GUI 启动或硬件验证、未验证部分、演示/占位内容以及后续集成步骤。
不得把“配置完成”写成“真实设备联调完成”。

## 9. 本会话阶段报告格式

用户要求执行过程中保持可见。每完成一个阶段，在 commentary 中输出：

1. 当前目标；
2. 已读取文件；
3. 使用的工具；
4. 修改计划；
5. 下一步动作。

修改遵循最小范围原则，优先使用 `apply_patch`；不得编辑生成的 moc/uic/rcc 文件，
不得使用个人绝对路径写入源码或配置，不得用破坏性 Git 命令覆盖用户已有修改。
