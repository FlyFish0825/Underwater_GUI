# ROV Qt 上位机

这是水下机器人 ROV 的 Qt Widgets 上位机工程。当前已在固件升级页接入 **Windows USB CDC 虚拟串口发现、VID/PID 筛选、串口接管、心跳监视、CAN 网关 AA55 增量拆包、Bootloader 命令中心和固定使用 Classic CAN 的 Legacy 单节点正式下载**；电机调试页已接入 Observer Motor 控制帧、Qwt 多窗口曲线和预览变量目录；视觉页已接入 Windows DirectShow 相机枚举、实时预览、帧率状态和当前帧保存；其他设备驱动仍按任务卡逐步实现。

## 开始开发前必须阅读

**Nano 局域网连接已接入：** 在“设置”页选择 `Nano TCP`，填写服务器地址和端口后连接。
默认只读，支持显式双向发送和意外断线自动重连；原 USB CDC 方式继续保留。
使用、运行库部署、验证及修改前恢复点见 [Nano TCP 连接教程](docs/nano-tcp.md)。

**机器人双目相机已接入原“视觉”页：** 默认连接 `192.168.20.70:9001`，解码 UWSC v1 JPEG，
左右目并排显示，保留本机相机与当前帧保存。图像连接与 MCU 的 `9000` 独立；
操作、运行库和本轮离线验证边界见 [双目相机接入说明](docs/stereo-camera.md)。

后续所有开发者、Agent 和协作者，开始任务前必须先阅读 `agent` 目录中的统一规范：

1. `agent/agent.md`：唯一的当前任务执行规则、分层边界、日志策略和验收要求；
2. `agent/ROV上位机通信协议与分层架构规范_v1.0.md`：Transport、Protocol、Service/Data、UI 的协议分层细节；
3. `docs/开发说明.md`：工程边界、页面约定、依赖、工具链和验证方法；
4. `agent/示意图/`：Dashboard、电机调试、Bootloader、机械臂和双目视觉参考图。

不得跳过上述文档自行建立另一套工程结构、主题、页面接口或通信方式。任务卡与规范冲突时，必须先报告冲突和影响范围。

## 工程结构

```text
src/app/             主窗口和应用入口
src/pages/           Dashboard、Motor Debug、Firmware、Manipulator、Vision、Settings
src/contracts/       页面快照和请求契约
src/communication/   USB CDC 传输与 CAN 网关协议解析
src/data/            Service/Data 层、电机快照与本机相机采集
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
- 高频 `解析到 CAN` 文本不进入固件页实时日志、历史记录或保存文件；电机数据服务持续
  接收 100 Hz 帧并按约 50 ms 发布快照，总览页和 Motor Debug 共用这份快照，页面不可见
  时只跳过曲线重绘。

参数辨识页与 Observer_Motor 的 `Command=0x40` 保持同步：使用 `0x100` 的 24 字节
CAN FD 控制帧发送 Action `0x01～0x08`，从 `0x340 + NodeID` 接收 64 字节事件/结果帧。
页面按 `NodeID + Sequence` 管理活动任务，分别显示 Rs、三组 Ls、线间/相电阻、有效位、
阶段和错误原因；`0x08` 只有 Rs 与三组 Ls 的 Valid 位齐全时才显示完整成功。结果只在
节点 RAM 中有效，停止确认和 `0x07` 快照不会被误判为辨识完成。具体字节布局以固件仓库
的 `docs/电机协议.md` 为准。

当前 Qwt 曲线只显示电机节点的真实反馈，并使用约 50 ms 的数据服务快照；节点离线时
曲线保持空白。真实设备原始 1000 Hz 样本环、窗口布局持久化和压力测试仍需单独完成。
详细边界见
[`docs/plotting.md`](docs/plotting.md)。

## 总览中的 IMU 与深度计

总览的“机器人状态”直接显示有效的 IMU 欧拉角与深度计滤波水深；数据来源保留在悬停提示和详情中，不再显示大段诊断说明。
这些测量值不是整机融合结果，不会改变电机控制权限；电机快照刷新也不会清除传感器数据。
点击卡片右上的“IMU / 深度计配置”，在总览下方展开实时数据、参数读写与校准；
面板内可切换 IMU / 深度计并原地收起。导航仍是原有六个页面，不增加传感器页。

传感器共用 USB CDC 的 AA5B 双向协议，命令匹配、超时、能力限制和数据新鲜度由
`SensorDataService` 负责，页面不碰串口。配置请求值和返回值分开显示；IMU 无 ACK
的设置显示“未确认”，发送引脚受保护不影响有效接收。深度计型号未确认或没有显式
水面零点时不显示有效水深；当前深度计配置只在 H750 RAM 生效，不冒充已持久保存。
协议、参数和硬件验证边界见 `docs/AA5B_Sensor_Protocol.md`。

MS5837 深度计已按 TARGET=2 实测协议补齐压力、温度、水深、采样时间和统计显示，
输出率范围为 1–100 Hz（仍受设备型号/OSR 转换预算限制）。未归零、无效和过期值
显示 `--`；设备 `NOT_READY` 不冒充通信故障。详细字段映射、接入边界、测试结果与
只读联调命令见 [`MS5837 接入与验收说明`](docs/ms5837-host-integration-20261006.md)。
连接后的深度信息握手会查询状态与六个参数，结果填入原反馈列，不覆盖编辑值。
按本机“上电位置视为水面”的使用约定，GUI 启动时自动配置固定的 02BA；若设备还没有
零点，等待新鲜有效压力后发送一次 ZERO_DEPTH，收到后续有效测量才显示水深。
已有零点会保留，断线重连不会覆盖手动校准；需要调整时使用原“采集水面零点”按钮。
无数据、无效压力或超时不会伪造 0 m，也不会反复自动归零。顶栏区分网关已连接、
深度计在线和电机节点在线，不因电机未接就把已在线的深度计称为整机离线。
本策略只在 GUI 连接时执行，不改 MCU 上电逻辑；独立只读探针仍无自动写入。
仍使用已实现 0x82/0x83，0x0D/0x4D 未启用；当前规则与验收见说明第 11 节。
本机探头型号现固定为 MS5837-02BA，不再提供型号选择；原应用按钮仅写入型号 2。
命令结果的悬停提示可查看最近请求编码与匹配回复 HEX。旧服务层 50 Hz 拦截的历史诊断
见上述说明第 9 节；更新协议已明确固件侧解除该拦截。
OSR/频率现为联动组合：256/512/1024 对应 1–100 Hz，2048 对应 1–71 Hz，4096 对应
1–45 Hz，8192 对应 1–25 Hz。选 OSR 自动限制频率范围，超界草稿值自动下调；任一
“应用组合”都由服务先读当前值、按安全顺序写入并逐步确认，最后回读两项。组合设置不改变水面零点、
不在编辑时发送命令。COM11 实测两个方向切换成功，并恢复原 4096/25 Hz；本轮记录见
上述说明第 10 节。

## IMU 协议补齐（2026-10-06）

IMU 沿用总览原标签与共享 AA5B 链路。连接后读取真实版本/状态及两个参数缓存，
不自动选择六/九轴算法、不自动发 IMU 配置。参数“已下发/缓存”明确为 UNCONFIRMED，
设置频率后自动观察约 2 秒原始流并显示实际速率；合并姿态流另计，不能当成原始输出率。
硬件校准/清除、保存、恢复、自检与复位入口均不开放，PIN_BLOCKED 不抹掉有效 RX 数据。
本轮实测 COM11 原始流约 25 Hz、合并姿态约 50 Hz；改 50 Hz 后观测约 49.7 Hz，
恢复 25 Hz 后观测约 24.9 Hz。软件校准算法不在该附件定义范围，本轮未新增。
协议原件见 `docs/imu-aa5b-host-protocol.md`；实现、约束、原始 TX/RX 和验收结果见
[`IMU 接入说明`](docs/imu-host-integration-20261006.md)。

IMU 与深度计接口已继续对齐当前下位机：两个目标分别显示上传确认状态，停止后清除
对应旧测量，恢复后等待新样本；状态消息仍可保持设备在线。共享 AA5B 路由补齐坏包
恢复，并严格区分 IMU 参数 UNCONFIRMED 与动作/深度参数 OK。
本轮变化和实机边界见 [`传感器接口对齐说明`](docs/sensor-interface-alignment-20261007.md)。

传感器控制台已按深度计、IMU、文档三批整理交付；中间文件清理范围、保留产物及本次回归结果见
[`分批交付与清理说明`](docs/sensor-console-release-20261006.md)。历史接入说明中的未提交状态为当时记录。

## 科研数据记录

总览页可启动独立的后台记录会话。记录链在页面可见性判断之前接收 CAN 收发帧，保存
原始 payload、Observer Motor 转速/电流/IABC/电压/温度、控制帧、六自由度输入、用户
事件以及有效的深度/姿态快照；它不把逐点记录送到界面或日志。每次会话生成 JSONL、
CSV 和元数据文件，队列满时不阻塞通信线程，而是累计明确的丢弃计数。

AA5B 传感器已接入总览显示，但本记录器尚未记录逐帧 AA5B 数据。
总览的传感器显示回退值不会冒充整机融合快照写入原有记录列。
使用方法、字段和完整性检查见 [`docs/research-recording.md`](docs/research-recording.md)。

## 编译与运行

本工程使用 Windows、Qt Widgets、C++17 和仓库内固定的工具链。请从仓库根目录打开 PowerShell，所有下面的相对路径都以该目录为起点。

| 工具 | 锁定版本 | 仓库内位置 |
| --- | --- | --- |
| Qt | 5.15.19 | `toolchain/qt/5.15.19` |
| MinGW GCC/G++ | 8.1.0 | `toolchain/mingw/8.1.0/Tools/mingw810_64` |
| CMake | 3.28.6 | `toolchain/cmake/3.28.6/cmake-3.28.6-windows-x86_64` |
| Ninja | 1.11.1 | `toolchain/ninja/1.11.1` |
| clangd / clang-format | 14.0.6 | `toolchain/llvm/14.0.6` |

### 编译过程

1. `activate.ps1` 设置仓库和工具链目录变量，并把 Qt、MinGW、CMake、Ninja 加入当前 PowerShell 的 `PATH`。
2. CMake 读取根目录 `CMakeLists.txt`，使用 Ninja 生成器，将 Debug 构建配置写入 `build/gui`。配置中指定项目内 Qt、`gcc.exe` 和 `g++.exe`。VSCode/clangd 所需的 `compile_commands.json` 由 VSCode 配置生成；手工配置时可添加 `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`。
3. Ninja 编译 C++ 源码、Qt MOC/UIC/RCC 生成步骤和静态库，最后链接 `rov_ui.exe`。并行度为 2；标记为 `EXCLUDE_FROM_ALL` 的测试程序不会随主程序一起编译。
4. `deploy-qt.ps1` 将运行时依赖复制到程序目录：Qt 与 MinGW DLL 放在 `build/gui`，Qt 平台插件放在 `build/gui/platforms`。
5. 从 `build/gui/rov_ui.exe` 启动程序。

### 编译目录和主要产物

主程序构建统一使用 `build/gui`，不要删除它来做普通增量编译。测试使用与 `build/` 同级的独立目录 `build-test/`，不得将测试产物写入 `build/`。这两个目录均已由 `.gitignore` 忽略，只保存在本机。

| 路径 | 用途 |
| --- | --- |
| `build/gui/CMakeCache.txt` | 当前 Debug 配置、Qt 与编译器路径等 CMake 缓存 |
| `build/gui/build.ninja` | CMake 生成的 Ninja 构建规则 |
| `build/gui/CMakeFiles/` | C++ 对象文件、AUTOMOC/AUTOUIC/AUTORCC 生成文件及中间产物 |
| `build/gui/compile_commands.json` | 每个 C/C++ 文件的编译命令，供 clangd/VSCode 跳转与诊断使用 |
| `build/gui/rov_ui.exe` | GUI 主程序 |
| `build/gui/Qt5Core.dll`、`Qt5Gui.dll`、`Qt5Widgets.dll`，以及 `libgcc_s_seh-1.dll`、`libstdc++-6.dll`、`libwinpthread-1.dll` | Qt 与 MinGW 运行库 |
| `build/gui/platforms/` | `qwindows.dll`、`qoffscreen.dll`、`qminimal.dll` 平台插件 |

### 一键构建

首次配置、完整构建或重新部署运行库时，在仓库根目录运行：

```powershell
& '.\toolchain\env\build-gui.ps1'
```

脚本依次执行工具链激活、CMake 配置、`ninja -C build/gui -j2` 和 Qt 运行库部署。再次运行会复用 `build/gui` 缓存，只重编发生变化的文件，然后重新部署运行库。

### 分步执行命令

需要检查或单独重做某一步时，可按以下顺序执行。配置命令适用于首次构建或需要重新生成 CMake 配置时：

```powershell
. '.\toolchain\env\activate.ps1'
$root = $env:ROV_UI_ROOT
$build = Join-Path $root 'build\gui'
$cmake = Join-Path $env:ROV_UI_CMAKE_DIR 'bin\cmake.exe'
$qt = $env:ROV_UI_QT_DIR.Replace('\', '/')
$gcc = (Join-Path $env:ROV_UI_MINGW_DIR 'bin\gcc.exe').Replace('\', '/')
$gxx = (Join-Path $env:ROV_UI_MINGW_DIR 'bin\g++.exe').Replace('\', '/')

& $cmake -S $root -B $build -G Ninja `
    '-DCMAKE_BUILD_TYPE=Debug' `
    "-DCMAKE_PREFIX_PATH=$qt" `
    "-DCMAKE_C_COMPILER=$gcc" `
    "-DCMAKE_CXX_COMPILER=$gxx" `
    '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
```

配置成功后，编译全部默认目标：

```powershell
$ninja = Join-Path $env:ROV_UI_NINJA_DIR 'ninja.exe'
& $ninja -C $build -j2
```

或者只编译 GUI 主程序及其依赖：

```powershell
cmake --build $build --target rov_ui --parallel 2
```

CMake/Ninja 编译完成后，单独部署运行库：

```powershell
& '.\toolchain\env\deploy-qt.ps1' -Executable (Join-Path $build 'rov_ui.exe')
```

启动程序：

```powershell
& '.\build\gui\rov_ui.exe'
```

若 PowerShell 因执行策略阻止脚本，可在仓库根目录运行以下命令，在该子进程中临时绕过执行策略：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File '.\toolchain\env\build-gui.ps1'
```

若链接时报 `rov_ui.exe: Permission denied`，或部署 Qt DLL 时出现 `Copy-Item` 文件占用错误，通常是程序仍在运行并加载了这些文件。先关闭 `rov_ui.exe`，再重新运行一键构建脚本；确认输出没有 `Copy-Item` 错误，且显示 `Qt runtime deployed beside` 后，运行库部署才完成。构建脚本和 VSCode 配置都会使用 `build/gui`，共享同一份 Ninja/CMake 缓存。

测试目标默认不参与主程序构建，需要验证时按下方“测试目录隔离”说明，在独立目录中显式构建对应测试目标。

VSCode/clangd 使用 `build/gui/compile_commands.json`。需要手工确认跳转时，可执行：

```powershell
& 'toolchain/llvm/14.0.6/bin/clangd.exe' `
  --check=src/pages/motor_debug/MotorDebugPage.cpp `
  --compile-commands-dir=build/gui `
  --query-driver='toolchain/mingw/8.1.0/Tools/mingw810_64/bin/*' --log=error
```

### 测试目录隔离

执行测试时，必须在项目根目录创建与 `build/` 同级的 `build-test/`，不能使用 `build/test/`、`build/gui/` 或 `build/test-evidence/` 存放测试文件。

- `build/`：仅用于主程序构建和运行所需产物。
- `build-test/`：统一存放测试构建缓存、中间文件、测试程序、测试运行库、临时脚本、日志、截图、录制数据、测试报告及证据归档；可按任务建立子目录。
- `tests/`：继续存放需要纳入版本管理的测试源码，不存放生成物。

测试必须单独配置 CMake，构建目录、测试工作目录和输出路径均指向 `build-test/` 内部，不得复用或修改 `build/gui` 的缓存，也不得把测试文件复制回 `build/`。例如，在仓库根目录执行以下命令，仅构建并运行电机协议测试：

```powershell
. '.\toolchain\env\activate.ps1'
$root = $env:ROV_UI_ROOT
$testBuild = Join-Path $root 'build-test\gui'
$qt = $env:ROV_UI_QT_DIR.Replace('\', '/')
$gxx = (Join-Path $env:ROV_UI_MINGW_DIR 'bin\g++.exe').Replace('\', '/')

cmake -S $root -B $testBuild -G Ninja `
    '-DCMAKE_BUILD_TYPE=Debug' '-DBUILD_TESTING=ON' `
    "-DCMAKE_PREFIX_PATH=$qt" "-DCMAKE_CXX_COMPILER=$gxx"
if ($LASTEXITCODE -ne 0) { throw 'Test configuration failed.' }
cmake --build $testBuild --target rov_observer_motor_protocol_test --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Test build failed.' }
ctest --test-dir $testBuild -R '^rov_observer_motor_protocol_test$' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Test failed.' }
```

其他测试按需替换目标名；如需部署测试运行库，应将 `deploy-qt.ps1` 的 `-Executable` 指向 `build-test/` 中的程序。不要使用 `build-gui.ps1 -BuildName test` 代替上述独立配置，该参数仍会把产物写入 `build/test/`。测试结束后的清理仅针对对应的 `build-test/` 子目录，不影响主程序目录。

## Git 约定

`build/`、`build-test/`、`toolchain/`、日志、缓存和编译产物只保留在本机，不提交到 Git。源码、文档、协议规范、资源和参考图可以提交。完整规则见根目录 `.gitignore`。

## USB CDC 通信

AA55 上行现兼容旧 `BODY_LEN=8+N` 与新 `12+N`（最大 82 字节）；CAN 配置回复兼容
23/27 字节。新增设备时间元数据与连接内 64 位扩展，DATA 和所有下行编码保持不变。
科研记录 JSONL/CSV 在旧列后追加网关时间，不用主机时间冒充。适配范围、历史固定包体
歧义及实机未验证边界见 [`AA55 上行适配说明`](docs/aa55-host-adaptation-20261007.md)。


固件页按底层 `CAN_To_Uart` 工程筛选 `VID_0483`、`PID_5740`（十六进制）。启动或点击“刷新设备”后，若发现匹配设备会自动连接；找不到设备时仍可手动刷新和接管。USB CDC 不使用波特率；界面解析 `AA 55` CAN 网关帧并显示 CAN 数据，`AA 58` 心跳帧只在后台用于在线判断，不在日志中显示。心跳连续约 2.5 秒未收到时标记为疑似离线。点击“下载到选中节点”会按 `ENTER_BOOT → ERASE → WRITE → DATA → WRITE_END → VERIFY → JUMP_APP` 执行真实下载。

## Bootloader

固件升级页的 Bootloader 功能、命令列表、Classic CAN 真实下载流程、限制和测试步骤见 [Bootloader 功能说明](docs/bootloader/README.md)。

多人协作请先阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。所有开发者和 Agent 都必须先阅读 `agent/` 目录中的统一提示词及通信分层规范，从 `main` 创建功能分支，通过 Pull Request 合并；不要直接向 `main` 推送。

## 专题文档

- `docs/architecture.md`：按总览、局部、细节组织的项目架构图；
- `docs/plotting.md`：高速多窗口曲线组件调研、许可证和接入方案；
- `docs/research-recording.md`：科研数据记录、导出格式、性能策略和传感器接入边界；
- `docs/开发说明.md`：日常开发、依赖、工具链和验证的统一入口；
- `agent/`：后续开发必须遵守的统一 Agent 提示词、通信规范和参考图。
