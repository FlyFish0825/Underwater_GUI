# ROV Qt 上位机集成版统一规范与 Agent 提示词

文档版本：2.1-integrated · 更新日期：2026-09-16 · 适用项目：水下机器人 ROV Qt 上位机

> 本文件将《Qt 上位机双人协作规范与统一 Agent 提示词》和《ROV Qt 上位机统一 Agent 提示词 v2.1》完整合入。两份原文保留在本文后部；任何 Agent 先执行本节生效规则，再阅读对应原文。

## 0. 生效规则与冲突裁决

1. 视觉、导航和页面功能范围以 v2.1 为优先参考；五张示意图决定信息架构、区域、控件和视觉关系，但不要求逐像素复制。
2. 双人协作、公共骨架、目录归属、契约、分支、验收和交接以双人协作规范为优先流程。先建立一个公共可编译起点，再按完整页面分工和小步合并。
3. 统一技术基线名称为 ROV-UI-2.1-integrated：Qt Widgets、C++17、Qt API 不低于 5.9；不得使用 QML、WebEngine、Qt Charts 或 Qt 6 专属 API。精确 Qt/编译器/CMake/Ninja 由集成人在 docs/toolchain.md 锁定，页面 Agent 不得自行选版本。
4. 两份原文的版本表存在差异：双人规范提供 Qt 5.15.19 + CMake 3.28.6 + Ninja 1.11.1 的可复现 SDK，v2.1 提供 Qt 5.15.2/Qt 5.9.5 与系统 CMake 的兼容部署方案。实际使用的精确 Kit 必须由集成人记录、验证并统一；不能混用。该裁决不删除原文信息。
5. 当前默认阶段只实现 UI、契约、Preview、可重复演示数据和请求日志；不实现 STM32/网关驱动、串口、CAN、TCP/UDP、Nano 转发、数据融合、重连、真实电机控制、运动学、固件烧录、视频解码或视觉算法，除非任务卡明确授权。图片中的 CAN、固件格式、数值和在线状态只是参考。
6. STM32/网关是未来逻辑数据来源，本地 Nano 接入/外部电脑网络接入是未来接入路径，两者不得混为一谈。页面不判断接入路径、不决定来源优先级。
7. 所有冲突须在交付报告写明采用的规则、理由和影响文件，不得静默改共同基线。

## 1. 统一总目标

一个 Qt Widgets 程序、一个 MainWindow、一个 QStackedWidget、一套白灰科研/工业主题、一套页面契约。导航固定为 Dashboard、Motor Debug、Firmware、Manipulator、Vision、Settings；Settings 只占位。源码分别面向 Nano 本机和外部电脑构建，不要求共享同一二进制。

开发顺序：公共骨架 → Dashboard → Motor Debug/Manipulator → Firmware/Vision → Settings。每页有独立 QWidget、契约、CMake 目标、资源前缀和 Preview；集成人维护主窗口、公共主题、依赖、版本文档、公共契约和最终集成。

## 2. 集成人的统一决策

- 先读取 AGENTS.md、docs/team-ui-guide.md、docs/toolchain.md、docs/ui-contract.md、当前工程和参考图，再创建/修改代码。
- 公共外壳、导航、底栏、QStackedWidget、基础 QSS、公共控件、CMake、预设、资源注册由集成人维护。
- 页面以 setSnapshot(const XxxSnapshot&) 接收值类型快照，以有类型的 *Requested 信号输出用户意图；不访问全局单例、不持有另一页面、不直接引用通信对象。
- 字段必须注明类型、单位、范围、有效性、新鲜度和稳定业务 ID；未知显示 --，不以 0 冒充缺失；在线不等于字段有效。
- 用户请求不等于设备反馈，不能点击后直接把反馈状态改成成功。控制默认无真实权限，预留 canControl 和不可控原因。
- 公共接口、依赖、目录归属和全局样式变更都由集成人协调；页面 Agent 只改授权目录。
- 预览状态必须可重复，至少覆盖正常、无数据、离线/过期、部分缺字段、有报警；明显位置标注“演示数据 / 无真实设备连接”。
- 所有交付都报告真实构建/验证结果和未验证平台，不虚构实机、通信或烧录成功。

# 第三部分：可直接复制的 Agent 提示词

## 3.1 所有 Agent 共用前缀

~~~text
你正在参与 ROV Qt 上位机项目。开始前必须阅读 AGENTS.md、docs/team-ui-guide.md、docs/toolchain.md、docs/ui-contract.md、当前工程、公共主题、对应参考图和本任务卡，并遵守《ROV Qt 上位机集成版统一规范与 Agent 提示词 v2.1-integrated》。

本项目只有一个 Qt Widgets MainWindow 和一个 QStackedWidget。参考图用于确定布局和信息层级，不逐像素复制，不自行增加未定义功能。当前默认只做 UI、页面契约、Preview、可重复演示数据和请求日志；不得实现真实设备通信、数据层、Nano 转发、STM32/网关驱动、重连、真实控制、烧录、视频或视觉算法，除非任务卡明确授权。

技术约束：Qt Widgets、C++17、Qt API >= 5.9；不使用 QML、WebEngine、Qt Charts、Qt 6 专属 API、std::filesystem、std::to_chars/from_chars。优先 QFile/QDir/QFileInfo、QPainter、Qt 资源系统和布局管理器。精确 Kit 由集成人锁定，不私自升级、降级或新增库。页面是独立 QWidget，命名空间 rov，构造函数为 explicit XxxPage(QWidget* parent = nullptr)，不重复创建主窗口、导航、顶栏或底栏。

输入使用 setSnapshot(const XxxSnapshot&)；契约值类型不得包含 QWidget、通信对象、包头、命令码或校验位。每字段写类型、单位、范围、有效性、新鲜度、业务对象和稳定 ID。目标值、请求、反馈分开；快照刷新不得误发信号，必要时使用 QSignalBlocker。控制只输出有类型请求，不做混控、PWM、QP、动力学、逆运动学、仲裁或看门狗；按住式控制覆盖释放、失焦、切页、禁用和销毁。

遵守目录归属、父子对象所有权、新式 connect、lambda 上下文、Allman 大括号、UTF-8/LF、.clang-format、资源路径和平台字体回退。不编辑 moc/uic/rcc，不用个人绝对路径，不把截图当 UI，不用 emoji 作为正式图标。所有 QWidget 更新在 GUI 线程。

Preview 数据必须固定可重复并标明演示/无真实设备连接，日志应可观察请求参数，波形和日志限制容量。先做最小可运行版本再完善。缺 Qt、缺设备或未能构建时如实报告，禁止虚构结果。交付必须列完成区域、文件、类/CMake/资源、契约、命令、真实验证、未验证平台、演示/占位内容、集成步骤、未决问题和冲突裁决。
~~~

## 3.2 集成人 Agent

~~~text
角色：公共骨架维护者 / 集成人。

目标：维护唯一公共起点和最终可集成程序。确认实际 Qt/编译器/CMake/Ninja、Nano 型号与系统、Windows 版本；按统一基线准备或记录 SDK，但不把整套 Qt、IDE 或个人路径提交仓库。

允许修改：根 CMake/CMakePresets.json、cmake、AGENTS.md、docs、公共格式/忽略配置、src/app、src/ui/common、src/ui/theme、contracts/common，以及页面最小占位和 Preview 注册。普通页面文件由页面 Agent 修改。

必须完成：建立唯一 main、MainWindow、顶栏、侧导航、底栏、QStackedWidget、统一主题和公共控件；固定六个导航业务 ID；注册页面类、CMake 目标、资源前缀和独立 Preview；维护 toolchain/ui-contract；逐页从干净目录构建并检查资源、切页和信号；公共接口变更同步头文件、文档、演示和所有受影响页面。

不做真实通信、数据仓库、Nano 转发、硬件控制、完整 Settings 或无关重构。验收必须证明两个页面 Agent 从同一基线开始，不能声称未验证设备已联调。
~~~

## 3.3 Dashboard Agent

~~~text
角色：页面开发者。页面 Dashboard；类 rov::DashboardPage；目标 rov_page_dashboard；业务 ID dashboard；资源前缀 :/pages/dashboard/；参考第一页 Dashboard。允许修改 src/pages/dashboard、src/contracts/dashboard、examples/dashboard_preview；公共文件只提建议。

实现六推进器 FL/FR/ML/MR/RL/RR 俯视示意（优先 QPainter），每个显示 RPM、Current、Temperature、Status；显示 Depth、Roll、Pitch、Yaw、Bus Voltage、Robot Mode、ARM、Leak Status、Internal Temperature、通信状态；提供 Surge/Sway/Heave/Roll/Pitch/Yaw、Manual Enable、Thruster Limit、Control Permission，以及 ARM/DISARM/HOLD POSITION/SURFACE 请求。

DashboardSnapshot 写明类型、单位、范围、坐标和待定物理语义；输出 armRequested、disarmRequested、holdPositionRequested、surfaceRequested、manualControlRequested 和使能/推力上限请求，预留 canControl。Preview 覆盖正常、无数据、离线/过期、缺失、有报警。不做推进器分配、PWM、STM32、数据层和通信。
~~~

## 3.4 Motor Debug Agent

~~~text
角色：页面开发者。页面 Motor Debug；类 rov::MotorDebugPage；目标 rov_page_motor_debug；业务 ID motor_debug；资源前缀 :/pages/motor_debug/；参考第二页电机调试。允许修改 src/pages/motor_debug、src/contracts/motor_debug、examples/motor_debug_preview。

实现电机选择、节点状态、RPM、电流、电压、温度、故障、控制目标、参数和实时波形。波形只用 QWidget + QPainter，自绘有界窗口，不引入 QCustomPlot/Qt Charts。契约区分反馈、目标、请求、有效性和新鲜度；演示数据固定可重复；请求写日志而不驱动电机；验证一次点击一次请求、切页不累积连接，缺少 CAN 时报告未联调。
~~~

## 3.5 Firmware Agent

~~~text
角色：页面开发者。页面 Firmware；类 rov::FirmwarePage；目标 rov_page_firmware；业务 ID firmware；资源前缀 :/pages/firmware/；参考第三页固件/Bootloader。允许修改 src/pages/firmware、src/contracts/firmware、examples/firmware_preview。

实现节点列表、在线状态、当前/目标版本、固件文件、大小、Upgrade、Verify、Progress、Result、Log 和 UI + Request Interface。即使图中写 CAN，也不得实现 CAN Bootloader、报文、擦写、烧录、校验或重启；按钮只发有类型 firmware 请求，反馈由外部快照回传；Preview 明确模拟，禁止伪造真实升级成功。
~~~

## 3.6 Manipulator Agent

~~~text
角色：页面开发者。页面 Manipulator；类 rov::ManipulatorPage；目标 rov_page_manipulator；业务 ID manipulator；资源前缀 :/pages/manipulator/；参考第四页机械臂。

实现 Shoulder Pitch、Elbow Pitch、Wrist Roll、Gripper 的示意、关节状态、目标值、手动操作、状态和报警请求；写明角度单位、范围、有效性和稳定 ID，目标与反馈分开。不得实现动力学、逆运动学、重心补偿、力控、水动力补偿或通信。Preview 覆盖正常、无数据、离线/过期、缺字段和报警。
~~~

## 3.7 Vision Agent

~~~text
角色：页面开发者。页面 Vision；类 rov::VisionPage；目标 rov_page_vision；业务 ID vision；资源前缀 :/pages/vision/；参考第五页视觉/双目。

实现 Camera Area、Camera/Vision Status、连接状态、检测信息、位置、姿态和置信度框架；显示 CAMERA NOT CONNECTED 或 VIDEO PLACEHOLDER，未确认算法只占位。严禁 OpenCV、GStreamer、CUDA、真实摄像头和视觉算法；页面不按本地/远程分支。Preview 使用明确未接入状态和请求日志。
~~~

## 3.8 Settings Agent

~~~text
角色：页面开发者。页面 Settings；当前只有导航占位，没有参考图。

只创建极简 SettingsPlaceholder，例如“Configuration interface reserved. / Not implemented yet.”，保持公共导航、主题、尺寸和底栏一致。不得设计复杂设置、连接配置、协议参数、设备管理或持久化系统。完成后交付运行结果和集成人装配说明。
~~~

## 3.9 Agent 固定交付模板

~~~text
1. 完成区域、交互和修改文件。
2. 页面类、命名空间、CMake 目标、导航业务 ID、资源前缀和构造方式。
3. 输入快照与输出请求的类型、单位、范围、有效性、新鲜度和未确认项。
4. 构建/运行命令，实际 Qt、编译器、CMake、Ninja、依赖版本。
5. 已执行验证的真实结果、未验证平台/设备及原因。
6. 演示数据、占位素材、模拟状态和未接入功能。
7. 集成人需要执行的注册、装配、公共接口或契约同步。
8. 两份原文冲突时的采用规则、理由和影响。
~~~

---

# 第四部分：完整纳入《Qt 上位机双人协作规范与统一 Agent 提示词》

# Qt 上位机双人协作规范与统一 Agent 提示词

文档版本：1.1 · 更新日期：2026-09-16 · 工具链基线：ROV-UI-1

适用范围：两人分工开发 ROV 上位机界面；同一套源码分别用于 Nano 本机与通过网线连接的外部电脑。当前先做 Dashboard 第一页，只实现 UI、页面契约和演示，不实现真实数据层、通信及转发。

用户已明确：当前为较老的 Nano，需兼容 Ubuntu 18.04，后续设备需兼容 Ubuntu 22.04；开发电脑为 Windows。“22.0”按 Ubuntu 22.04 LTS 处理。第 3 节已锁定工具链，不再由两个 Agent 分别选版本。具体设备镜像、驱动和实机运行结果仍需采集验证；版本选定不等于已经在设备上验证通过。

文档版本、工具链基线、应用版本、页面契约版本和设备固件版本是不同概念，不要混用。

## 1. 如何使用这份文档

1. 两个人统一使用第 3 节已锁定的技术基线，由其中一人担任集成人。集成人也可以负责第一页；按固定版本准备 SDK，不再重新选型。
2. 先完成一次“公共骨架任务”，提交同一个可编译的起点，再分别开发页面。不要各建一个完整 Qt 工程，最后再拼接。
3. 将本文保存进项目的 `docs/team-ui-guide.md`，根目录 `AGENTS.md` 写明：开始任务前读取该文档、技术基线和页面契约。路径是未来项目内的约定，并不表示现在已经存在这些文件。
4. 两个人给 Agent 使用第 12 节的相同提示词，只改第 13 节的任务卡。使用不自动读取 `AGENTS.md` 的工具时，手动附上文档。
5. 文档建议经团队采纳后成为共同基线。修改共同规则、接口或依赖时，要同步修改仓库文件，不能只留在某个人的聊天记录里。

核心原则：**一套公共骨架、一个版本基线、每页独立目录、输入数据快照、输出操作请求、小步合并。**

## 2. 当前范围与整体边界

附件包含总览、电机调试、固件升级、机械臂、视觉 5 张参考图，没有现成工程。导航中还有 Settings，但没有对应设计图，当前只预留导航位置，不擅自增加完整设置页。

### 2.1 当前做什么

- 主窗口公共外壳：顶部状态、侧边导航、页面容器、底部状态。
- 任务卡指定页面的显示、布局、局部交互。
- 有明确类型的页面输入与输出契约。
- 独立页面预览、可重复的演示数据、操作请求日志。
- 需要时创建少量真正复用的公共控件，例如卡片容器、数值卡、状态标签。

### 2.2 当前不做什么

- STM32 或网关的驱动、串口、CAN、TCP/UDP 接入、报文解析、重连。
- Nano 的网络转发服务、数据融合、真实数据仓库。
- 实际电机控制、运动学、固件烧录、视频解码、视觉算法。
- 动态插件系统、通用消息总线、复杂依赖注入框架。

图片中的英文说明、协议名称、固件格式和数值只是参考内容。例如固件页写有 CAN，并不代表用户已经确认底层使用 CAN；视觉页的开发路线也不自动变成本次实施需求。

### 2.3 同一源码，两种部署方式

```text
逻辑数据来源：STM32、网关
                 │
          Nano 本地接入（未来）
                 ├── 统一数据层／UI 绑定 ── 相同的 UI 源码
                 │
          网络转发服务（未来）
                 │
          外部电脑网络接入（未来）
                 └── 统一数据层／UI 绑定 ── 相同的 UI 源码
```

- “STM32、网关”描述来源；“本地接入、网络接入”描述接入路径，是两个不同维度。
- 两个逻辑来源均留在未来接入设计中，不把 Nano 固定等同 STM32、外部电脑固定等同网关。
- 本阶段在接口文档中各保留一个来源接入条目，记录预期输入、连接状态和待定项；不为了预留而编写未使用的空驱动类。
- 后续启动配置选择本地或远程接入，页面不读取接入模式、不决定来源优先级。
- 同一份源码分别编译部署，不要求跨平台共用一个可执行文件或同一套二进制库。

## 3. 版本、依赖与环境统一

### 3.1 选型结论与适用范围

**固定为 Qt 5.15.19 + Qt Widgets + C++17 + CMake 3.28.6 + Ninja 1.11.1。**

这是一套优先照顾旧 Nano / Ubuntu 18.04 的兼容性基线。Qt 5.15.19 已于 2026-05-19 公开开源，源码标签为 `v5.15.19-lts-lgpl`，可以从官方归档获取；不需要把早期商业发布公告误解为当前仍没有公开源码。[Qt 官方开源发布公告](https://lists.qt-project.org/pipermail/announce/2026-May/000626.html)

选 5.15.19 是为了使用 5.15 分支较完整的修复，减少在 GCC 11 下自行修补老版本源码的工作。本项目不使用 Qt 6，不回退到常见安装教程中的 Qt 5.15.2，也不混用 Ubuntu 系统仓库自带的另一版本 Qt。

必须区分“兼容性选择”和“持续维护”：Qt 5.15 常规 LTS 已于 2025-05-26 结束，后续扩展维护是另外的服务；本选择不表示它仍有免费长期维护。未来全部退出 Ubuntu 18.04 后，再统一评估迁移 Qt 6，页面开发期间不分叉升级。[Qt 官方维护说明](https://www.qt.io/blog/commercial-lts-qt-5.15.19-released)

Windows 本基线面向 Windows 10/11 x64；Nano 面向 ARM64/aarch64。Ubuntu 22.04 是未来目标设备环境，不要求现在给旧 Nano 刷成 22.04。现有 JetPack、内核和显卡驱动保持与设备匹配，不能为了 UI 构建替换系统 glibc 或强行升级设备系统。

### 3.2 锁定版本表

由集成人把下表原样写入 `docs/toolchain.md`，再补实际安装路径和环境记录。下面是选定版本，不是供两个 Agent 自选的候选列表。

| 项目 | 固定值 | 使用规则 |
|---|---|---|
| UI 技术 | Qt Widgets | 不引入 QML、WebEngine |
| C++ 标准 | C++17 | 使用第 3.6 节约束的共同子集，不使用 C++20/23 |
| Qt 源码及全部启用模块 | **5.15.19** | 三端相同源码版本，分别构建 SDK |
| 正式应用 Qt 模块 | Core、Gui、Widgets，均 **5.15.19** | 来源统一为 qtbase；测试可启用同版 Test |
| CMake | **3.28.6** | 三端相同，不自动使用 IDE 自带的其他版本 |
| 应用构建工具 | **Ninja 1.11.1** | 三端相同，单配置生成器 |
| Windows C/C++ 编译器 | **MinGW-w64 GCC/G++ 8.1.0 x64** | 固定 Qt 官方 x86_64-posix-seh-rt_v6-rev0 构建；不用 MSVC、MSYS2 UCRT64 替换 |
| Ubuntu 18.04 C/C++ 编译器 | **GCC/G++ 7.5.0** | ARM64 原生构建；gcc-7/g++-7 包版本固定 7.5.0-3ubuntu1~18.04 |
| Ubuntu 22.04 C/C++ 编译器 | **GCC/G++ 11.4.0** | ARM64 原生构建；gcc-11/g++-11 包版本固定 11.4.0-1ubuntu1~22.04.3 |
| Windows IDE | **Qt Creator 10.0.2** | 两位开发者统一使用；IDE 自身运行库不等于项目 Qt 版本 |
| 格式化工具 | **clang-format 14.0.6** | Windows 开发机统一执行，不要求部署到 Nano |
| Qt 链接方式 | 动态链接 | Qt、应用和插件不得跨平台/编译器混用 |
| 日常开发配置 | Debug | 每个平台独立构建目录 |
| 交付配置 | Release | 单独构建目录，部署匹配的 Qt 与运行库 |
| 第三方运行库 | **0 个** | 当前 UI 不引入 OpenCV、Boost、QCustomPlot、spdlog 等 |
| 波形显示方案 | QWidget + QPainter | 自绘有界数据窗口，使用同版 QtGui；不引入 Qt Charts 等附加库 |
| 图标和设备图 | Qt 资源中的 PNG / QPainter 自绘 | 当前不启用 QtSvg，避免新增模块 |
| 界面语言 | 截图对应英文，文案使用 tr() | 后续统一增加翻译 |
| UI 编写方式 | C++ 手写布局 | 不生成 Designer .ui，不混搭两套页面编写方式 |

Qt 官方对 Qt 5.15 的 MinGW 配套构建就是上述 8.1.0 组合。[Qt MinGW 配套表](https://wiki.qt.io/MinGW) Ubuntu 编译器包版本分别可核对 [Bionic 官方发布记录](https://lists.ubuntu.com/archives/bionic-changes/2020-March/025345.html) 和 [Jammy ARM64 更新包](https://packages.ubuntu.com/jammy-updates/arm64/gcc-11)。若设备的软件源缺包，应由集成人准备官方归档包或恢复已验证 SDK；不能临时换编译器版本或混装其他 Ubuntu 发行版的包。

同一平台的两位开发者使用同一 SDK 副本；三个目标环境分别编译 Qt 和应用。Ubuntu 22.04 编译出的程序不直接拿到 18.04 运行，Windows DLL 也不用于 Nano。Qt 官方平台表不是这套具体 Jetson 设备组合的实测认证，必须完成目标机验收后再标记“已支持”。[Qt 5.15 平台说明](https://doc.qt.io/qt-5/supported-platforms.html)

### 3.3 依赖管理规则

1. 同一种功能只选一种方案，尤其是图表、图标和日志。页面负责人不能自行引入另一套库。
2. 第三方依赖固定到精确版本或不可变提交；下载包记录校验值。禁止浮动分支、`latest` 或不受限下载。
3. 依赖在公共构建配置中集中声明；页面只链接已经定义的目标，不独立下载或另找版本。
4. 新增依赖时记录用途、版本、来源、所需模块、许可证、两个平台的可用性和部署文件。库越少，Nano 部署越容易保持可控。
5. 修改 Qt、编译器、依赖版本或模块，应作为独立的共同变更处理，先验证双方环境，再更新基线。不要夹在页面样式提交中。
6. 当前波形显示已定为 Qt 自绘，不安装额外图表库；只做第一页时无需预先开发波形控件。以后若真实数据性能需求证明需要替换，单独做共同变更，不由页面 Agent 擅自替换。
7. 参考文档网站显示的最新版本不是本项目选定版本。开发时查阅已锁定版本的 API，避免 Agent 使用更新版本才有的接口。

### 3.4 公共配置与本地配置

仓库统一维护 `.clang-format`、`.editorconfig`、`.gitattributes` 和 `.gitignore`。编码为 UTF-8，源文件换行统一为 LF，普通缩进为 4 空格；二进制资源不做文本换行转换。

公共 `CMakePresets.json` 纳入版本管理；个人 Qt 安装路径等放在不提交的 `CMakeUserPresets.json` 或本地环境中。预设文件 schema 固定为 5，生成器固定 Ninja；定义 windows-x64、ubuntu1804-arm64、ubuntu2204-arm64 三组配置，各自区分 Debug/Release。该公共/个人配置分工符合 [CMake 官方预设文档](https://cmake.org/cmake/help/v3.28/manual/cmake-presets.7.html)。

忽略构建目录、IDE 的 `.user` 文件、个人预设、日志、临时文件、编译产物。禁止提交本机绝对路径、直接复制系统 SDK 或整套 Qt 库到源码仓库。

### 3.5 SDK 来源、构建与交接

本方案采用公开源码构建 Qt SDK，不承诺开源在线安装器中有现成的 5.15.19 Windows 二进制 Kit。**准备 SDK 是集成人执行一次的公共工作，不让两个人各自编出一套不同的 Qt。** 当前只需要 qtbase，无需编译全部 Qt 模块。

固定下载来源：

| 工具 | 官方来源 | 取得方式 |
|---|---|---|
| Qt 5.15.19 qtbase | [源码及校验值](https://download.qt.io/archive/qt/5.15/5.15.19/submodules/qtbase-everywhere-opensource-src-5.15.19.tar.xz.mirrorlist) | 同一个源码包，分别为三个环境构建 |
| Windows MinGW 8.1.0 | [精确工具链包及校验值](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/tools_mingw/qt.tools.win64_mingw810/8.1.0-1-202004170606x86_64-8.1.0-release-posix-seh-rt_v6-rev0.7z.mirrorlist) | 固定该包，包含配套编译和运行库工具 |
| CMake 3.28.6 | [官方发布](https://github.com/Kitware/CMake/releases/tag/v3.28.6) | Windows x64 包；ARM64 若预编译包不能在本机运行，用同版源码构建 |
| Ninja 1.11.1 | [官方发布](https://github.com/ninja-build/ninja/releases/tag/v1.11.1) | Windows 使用 win 包；ARM64 从同版源码原生构建 |
| Qt Creator 10.0.2 | [官方归档](https://download.qt.io/archive/qtcreator/10.0/10.0.2/) | Windows x86_64 安装包；Nano 不要求安装 IDE |
| clang-format 14.0.6 | [LLVM 官方发布](https://github.com/llvm/llvm-project/releases/tag/llvmorg-14.0.6) | Windows 使用同版工具，不使用编辑器内置另一版本 |

已核对官方公布的 SHA-256（尚未在本任务下载或安装 SDK）：

```text
qtbase-everywhere-opensource-src-5.15.19.tar.xz
51e91c73abacab81e64efd01bf95794e79c0e605ad80947a024769f0dd620a32

8.1.0-1-202004170606x86_64-8.1.0-release-posix-seh-rt_v6-rev0.7z
5a7afdc889ffbb53302a3ff998dfeee84236adb8824e5da94ebf30c87a09930c
```

SDK 构建约定：

1. Qt 5 自身源码构建仍走其 configure/qmake/Make 流程；**上位机工程**使用 CMake/Ninja。两者不冲突，不把本项目改成 `.pro` 工程。Windows 使用固定 MinGW 包中的 mingw32-make，Ubuntu 使用本发行版 Make 并纳入 SDK 构建环境记录。
2. 初期构建 qtbase、动态库、所需工具和平台插件；正式应用只链接 Core/Gui/Widgets。Qt 源码构建时不构建其整套 examples/tests；项目测试如需 QtTest，使用同一 qtbase 产出的 Test 模块。
3. 当前采用普通 Widgets/QPainter 光栅绘制，不启用应用 OpenGL 控件。Linux 固定 X11/xcb 路径，Windows 使用 windows 平台插件；Linux 的 xcb 依赖仍需安装并验收，不意味着完全没有系统图形依赖。
4. 在独立前缀安装，例如按工具链命名的 SDK 目录；不覆盖 `/usr` 中系统 Qt，不替换系统驱动。保存 configure 参数和配置摘要，避免两端功能开关意外不同。
5. Windows 两位开发者共享同一个经验证的 SDK 压缩包及哈希；Ubuntu 分别保存对应系统和架构的 SDK。调试与发布配置不得混装对应库文件。
6. 保存 Qt/编译器/CMake/Ninja 版本输出、构建参数、系统镜像或 sysroot 标识、安装包清单与 SDK 哈希。系统的 glibc、binutils、X11 等来自对应平台镜像，在 SDK 验收时记录快照，不人为强制三个系统使用相同系统库版本。
7. SDK 分发给同伴放在团队制品目录，不放进源码 Git 仓库。日后重建使用同一构建记录，禁止自动追随系统“最新包”。系统安全修订需要由集成人验证并更新基线，不等于永久禁止修复更新。

### 3.6 兼容旧编译器的代码约束与版本检查

虽然标准定为 C++17，GCC 7.5 的标准库覆盖范围仍是共享源码的下限。**文件路径统一使用 QFile、QDir、QFileInfo，不使用 std::filesystem；数值转换统一使用 QString 等已有能力，不依赖 std::to_chars / std::from_chars。** 后两类库功能在 GCC 8 才新增；不用更新编译器才有的标准库接口可减少平台分支。[GCC 8 官方变更](https://gcc.gnu.org/gcc-8/changes.html)

可以使用 GCC 7.5 支持的 optional、variant 等 C++17 能力，但不承诺跨 GCC 7/8/11 的 C++ 二进制兼容；每个 SDK 和应用由对应固定工具链构建。[GCC C++ 支持说明](https://gcc.gnu.org/projects/cxx-status.html#cxx17)

集成人在根构建中使用精确的 Qt 查找，避免机器上另一版本被自动选中：

```cmake
cmake_minimum_required(VERSION 3.28.6)
project(RovGroundStation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.15.19 EXACT REQUIRED COMPONENTS Core Gui Widgets)
```

这是将来根构建应采用的版本约束片段，不是本次已经创建或编译的工程。`cmake_minimum_required` 只规定下限；集成人还应在公共版本检查中要求 CMake 恰为 3.28.6、Ninja 恰为 1.11.1，并按所选平台核对编译器完整版本、架构及 Qt 来源。页面 Agent 不得把 EXACT 删除来绕过环境差异。

所有安装检查都用明确选中的工具路径执行 `qmake -query QT_VERSION`、`g++ -dumpfullversion -dumpversion`、`cmake --version`、`ninja --version`；不能仅凭 PATH 上碰巧找到一个同名工具就认为 Kit 正确。Qt Creator 的 Kit 必须指向本项目 SDK、固定编译器、CMake 和 Ninja。

## 4. 分工、目录与页面注册

### 4.1 先有一个公共骨架

集成人建立可编译的主程序、导航、页面容器、基础主题、最小公共控件、公共构建配置和版本文档，然后提交公共起点。后续页面从同一基线分支开发。

分工交接前，集成人为本轮分配的页面建立最小占位模块和预览入口，并在共同构建中注册页面及预览目标。之后页面负责人修改本页模块的源文件列表即可独立构建预览，无需修改根 CMake。新增页面时先补一次同样的注册；目标尚未注册时报告具体缺项，不声称预览已可构建。

推荐页面容器采用 `QStackedWidget`。初期页面按需创建后保留；切回时保留筛选项等普通局部状态，不恢复正在按下的控制动作。视频等重资源页面将来再按需要增加显式启停机制。

公共骨架任务可以修改公共文件；普通页面任务不得越过自己的任务卡范围。你负责第一页时，可先承担骨架任务，再切换成 Dashboard 页面任务。

### 4.2 目录建议

```text
project/
  AGENTS.md
  CMakeLists.txt
  CMakePresets.json
  .clang-format
  .editorconfig
  .gitattributes
  .gitignore
  cmake/                       # 共同依赖与工具链支持
  docs/
    team-ui-guide.md            # 本文
    toolchain.md                # 精确版本、平台与构建方法
    ui-contract.md              # 共同类型、单位及接口变更记录
  src/
    app/                       # 唯一正式入口、MainWindow、页面装配
    ui/
      common/                  # 公共卡片、按钮、状态标签
      theme/                   # 主题、公共 QSS、公共资源
    contracts/
      common/                  # 有效性、连接状态、业务标识
      dashboard/               # 页面输入及操作请求类型
      motor_debug/
      firmware/
      manipulator/
      vision/
    pages/
      dashboard/               # 本页代码、私有控件、资源、CMake
      motor_debug/
      firmware/
      manipulator/
      vision/
  examples/
    dashboard_preview/         # 独立入口、演示数据、信号观察
    motor_debug_preview/
```

这是一份完整项目的目录规划。只创建当前确实使用的目录和文件，不要求一次建立全部页面。

### 4.3 固定入口约定

| 页面 | 页面类名 | CMake 目标 | 导航业务标识 | 资源前缀 |
|---|---|---|---|---|
| 总览 | DashboardPage | rov_page_dashboard | dashboard | :/pages/dashboard/ |
| 电机调试 | MotorDebugPage | rov_page_motor_debug | motor_debug | :/pages/motor_debug/ |
| 固件升级 | FirmwarePage | rov_page_firmware | firmware | :/pages/firmware/ |
| 机械臂 | ManipulatorPage | rov_page_manipulator | manipulator | :/pages/manipulator/ |
| 视觉 | VisionPage | rov_page_vision | vision | :/pages/vision/ |

以上为建议统一采用的名称。公共入口使用 `explicit XxxPage(QWidget* parent = nullptr)`，命名空间为 `rov`。导航业务标识用于主窗口注册，不充当无类型控制命令。

每页交付页面类、契约、模块 CMake 和独立预览。集成人负责添加模块、链接目标、实例化页面及连接外部绑定；页面不自己查找或修改主窗口。首次最小页面可运行时就接入主程序一次。

### 4.4 文件归属

| 文件或目录 | 默认修改人 |
|---|---|
| 根 CMake、cmake/、公共预设、依赖及格式配置 | 集成人 |
| src/app/、导航注册、全局状态栏 | 集成人 |
| src/ui/common/、src/ui/theme/ | 集成人；按双方需求小步扩展 |
| contracts/common/、共同契约文档 | 集成人，接口变更须同步双方 |
| pages/<本页>/、examples/<本页>_preview/ | 对应页面负责人 |
| contracts/<本页>/ | 页面负责人；公开接口变更须同步集成人 |

不建议按“一个人写 .h，另一个人写 .cpp”拆分，也不要两人同时修改同一个 `.ui` 文件。以完整页面为单位分工最清楚。

## 5. 页面与数据层解耦

### 5.1 页面只接收显示所需的数据

推荐统一入口为 `setSnapshot(const XxxSnapshot&)`，数据类型放在 `contracts/<本页>/`。当前快照由演示入口提供，未来由页面外部的绑定层从统一数据层读取或订阅后传入。

页面不访问数据层单例，不轮询通信对象，不直接获取 STM32 或网关接口，不持有另一页面指针。页面也不根据本机/网络模式写条件分支。

依赖保持单向：`app → pages → ui/common + contracts`；公共控件和契约不能反向依赖具体页面或主程序。不强制建立通用 PageBase，有实际重复需求后再提取。

### 5.2 类型和字段规则

- 使用结构体和 `enum class` 表达业务含义，不把所有东西塞入 `QVariantMap` 或 JSON 字符串。
- 契约是可复制的数据值，不包含 QWidget 指针、通信对象或底层包头、命令码、校验位。
- 每个字段记录类型、单位、范围、有效性、更新时间含义和所属业务对象。
- 缺失值显示 `--`，不要用 0 代表未知；连接状态至少能区分未知、在线、离线。
- 有效性与连接状态分开。在线不表示每个字段有效；断线后保留的值应带过期标识。过期判断策略由未来外部层负责，页面按输入状态呈现。
- STM32 与网关的数据可能不同步，不能只靠一个全局时间戳暗示所有字段都刚刷新。按逻辑分组提供新鲜度，必要时细化到字段。
- 电机、节点、关节使用稳定业务 ID，不把表格行号作为设备 ID。六推进器 FL/FR/ML/MR/RL/RR 是本次参考图的布局，不自动等于未来协议地址。
- 报警未知与“已确认零条报警”要区分；缺失报警信息不能显示“一切正常”。
- 汇总数据应有统一语义，例如平均转速包含哪些有效电机。推荐由输入快照提供汇总，页面只格式化显示，避免不同页面各算一套。
- 日期时间由输入或演示入口提供；时间单位、时区及显示格式在契约中注明，不把截图日期写死在正式页面。

推荐显示单位为 m、°、V、A、℃、rpm、ms；字段名或文档必须体现单位。角度用度还是弧度、控制值用归一化值还是百分比，都必须先约定。UI 可以显示百分比，接口取值范围不能靠猜。

### 5.3 请求不等于反馈

用户操作通过类型明确的信号输出，例如 `armRequested()`、`disarmRequested()`、`holdPositionRequested()`、`surfaceRequested()`、`manualControlRequested(...)`。用 Qt 信号槽可以让页面不依赖请求接收方。[Qt 5 官方信号槽文档](https://doc.qt.io/qt-5/signalsandslots.html)

点击 Arm 只代表用户提出请求，不能直接把设备反馈状态改成 Armed。将来由外部层传回处理中、成功、失败或超时；当前若需要展示这种过程，只在演示程序中模拟并明确标识。

用 `setSnapshot` 更新滑块、复选框等时，应阻止误发操作请求，例如在适当位置使用 `QSignalBlocker`。用户正在编辑的目标值与设备当前反馈值分开保存，避免快照刷新把编辑内容覆盖掉。

### 5.4 手动控制与双端控制入口

六自由度命令使用 Surge、Sway、Heave、Roll、Pitch、Yaw 等业务轴，页面不计算推进器混控或 PWM。坐标系、正负方向、范围和推力上限含义必须写进契约；物理方向尚未确定时标明待定，不能宣称与设备一致。

按住式控制要验证按下、松开、鼠标移出、窗口失焦、页面切换、禁用及销毁流程。失去操作上下文时取消活动输入，发出中性请求；键盘重复事件不能导致动作粘住。初期页面常驻便于保证切页时清理明确可见。

预留由外部传入的 `canControl` 及不可控原因。默认无真实控制权限；演示程序可明确开启模拟权限。两个上位机同时运行时，谁能实际控制由未来业务/接入层仲裁，页面不自行争抢控制权。UI 的中性请求也不能替代底层超时、看门狗或实际控制策略；本阶段不实现这些底层逻辑。

### 5.5 生命周期与线程

- 页面构造时建立连接，不在每次显示时重复连接，避免一次点击发出多次请求。
- QObject 子对象优先采用父子关系管理；不要对同一个 QObject 同时用父子关系与智能指针重复拥有。
- 页面仅保留普通 UI 状态；隐藏页面停止非必要动画和演示刷新，不继续发控制请求。
- 所有 QWidget 更新必须在 GUI 线程进行。未来跨线程数据交付由外部绑定层负责；选择 queued connection 时一并核实自定义类型的元类型声明/注册与值传递。[Qt 5 官方线程与 QObject 文档](https://doc.qt.io/qt-5/threads-qobject.html)
- 不在 UI 回调里阻塞等待、sleep、同步读大文件或执行通信。当前不为了演示引入后台线程。

## 6. 统一视觉、代码与性能约定

### 6.1 视觉规则

公共主题统一提供颜色、字号、圆角、间距和状态语义。参考截图为浅色背景、蓝色强调、深色正文；绿色、橙色、红色分别用于明确的正常、警告、异常状态，未知用中性色。

页面复用公共卡片和状态标签，不复制整套全局 QSS。页面局部样式用特定对象名或属性限定作用域，禁止无范围的样式选择器影响其他页面。

普通布局使用布局管理器；设备示意图可以自绘并按可用区域缩放。小窗口需要明确重排或滚动方案，不靠大量绝对坐标固定整页。[Qt 5 官方布局文档](https://doc.qt.io/qt-5/layout.html)

初期建议以 1280×720 和 1920×1080、100% 和 150% 缩放检查布局，作为建议验收组合；Nano 的实际分辨率确认后加入。固定的是测试条件，不是强制窗口尺寸。

图标与图片打包进资源，页面使用独立前缀；禁止硬编码本机文件路径。文件名大小写必须与引用一致，避免 Windows 可用而 Linux 找不到。Qt 支持通过资源系统随程序打包资源。[Qt 5 官方资源文档](https://doc.qt.io/qt-5/resources.html)

没有独立设备素材时使用可替换的简洁示意图，并标明占位；不把整张界面截图当作可交互 UI。不要用 emoji 作为正式图标。若引入 SVG 支持、额外字体或图标库，按共同依赖流程处理，并记录资源来源。

初始文案按截图英文，使用 `tr()`；后续统一切换语言。字体选择两平台可用的回退方案，不直接依赖某台 Windows 电脑上的字体。

### 6.2 代码规则

| 项目 | 共同约定 |
|---|---|
| 命名空间 | rov |
| 类名、类文件名 | PascalCase，如 DashboardPage.h/.cpp |
| 目录、CMake 目标 | snake_case，如 dashboard、rov_page_dashboard |
| 函数、局部变量、普通结构体字段 | camelCase |
| 类私有成员 | m_ 前缀 |
| 枚举 | enum class；类型和值采用 PascalCase |
| 常量 | kPascalCase |
| 大括号 | Allman；仓库 .clang-format 为最终格式依据 |
| connect | 新式函数指针语法；lambda 连接提供上下文对象 |
| 注释 | 解释意图、单位和边界，不逐行复述代码 |
| 平台差异 | 放在构建或外部适配位置，页面中避免散落条件编译 |

不编辑自动生成的 moc、uic、rcc 文件。不在页面里调用 `qApp->setStyleSheet()` 改全局主题。不为当前任务重新格式化其他人的目录。

### 6.3 刷新与资源占用

业务采样频率与 UI 刷新频率分开。后续高频数据应由外部层合并或降采样后送给页面，不把每一帧原始采样都排进 GUI 队列。

静态卡片收到快照才更新；不重建整页控件。波形和日志要约定显示窗口、最大点数和最大行数，避免无限增长。图表实现已定为 Qt 自绘，具体刷新率、曲线容量和性能指标在目标 Nano 上测量后记录，不把未经测量的数值当作已达标性能。

## 7. 演示数据和独立预览

每页预览拥有自己的 `main()`，放在 `examples/<page>_preview/`；正式应用只有一个 `main()`。页面源码列表不能把预览入口带进正式目标。

演示数据只放在预览或公共演示装配层，用正式的 `setSnapshot` 接口注入。正式页面不写死“Connected”、截图中的遥测值或随机变化定时器。

至少提供可重复选择的状态：正常、尚无数据、离线/过期、部分字段缺失、有报警。按钮接到预览日志，可观察请求参数；日志中的操作成功只能是明确标识的模拟结果。

波形需要演示时使用固定数据或固定种子；同一场景可重复，方便两人对比截图。摄像头和固件操作在当前阶段显示占位或未接入状态，不虚构已经读取视频或烧录成功。

预览底部等明显位置显示“演示数据 / 无真实设备连接”。截图中的 CPU、磁盘和运行时间等也不能硬编码为真实测量值。

## 8. 接口变更与日常合并流程

### 8.1 接口变更流程

1. 先列出变更原因、受影响字段/信号、影响页面及演示数据。
2. 新增可选字段可默认“无数据”，保持原调用方可用；新增字段也要更新文档和演示。
3. 重命名、删除字段，修改单位、坐标方向、枚举含义或信号签名，属于破坏性变更；由集成人协调相关调用方一起修改。
4. 公共接口头文件是实际约束，`docs/ui-contract.md` 记录语义和版本变更；两者必须在同一次合入中更新。页面负责人提供所需文档修改内容，集成人合入契约时同步更新公共文档；普通页面任务不擅改公共文档。
5. 变更契约后编译所有受影响页面及其预览，不能只编译修改者自己的页。

早期可以记录简单的契约版本和变更列表，不必建立复杂的运行时版本协商。页面契约版本不是通信协议版本。

### 8.2 分支与集成

1. 公共骨架先进入共享主分支，两个人从同一个基线提交建立各自页面分支。
2. 一次提交围绕一个明确结果，不混入无关重构、公共依赖升级或大量格式变更。
3. 每个可运行小阶段同步主分支并做一次集成。至少在“页面最小骨架”和“页面验收完成”各集成一次。
4. 集成人先合入一个页面并验证，再合入另一个；不把覆盖对方文件作为解决冲突的方法。
5. 冲突涉及契约、依赖和公共样式时，对照最新共同约定处理。未沟通不要重写共享分支历史或强推覆盖对方提交。
6. 每次合并后从干净构建目录验证完整应用，并检查切页、公共样式和请求连接。

统一代码风格能减少噪声，但不能保证完全没有 Git 冲突。接口、目录归属和持续集成才是减少最终返工的主要手段。

## 9. 最小验收清单

下列项目按本次授权范围检查。当前只做文档，不代表以下工程验收已经执行。

- [ ] 实际环境与技术基线一致；没有页面私自添加的库或版本。
- [ ] 本页预览可配置、编译、运行；相关公共改动后完整应用也可编译。
- [ ] 页面可被主程序装配，导航标识、构建目标、资源路径无冲突。
- [ ] 页面不依赖真实通信、数据单例、其他页面或主窗口内部控件。
- [ ] 正常、无数据、离线、过期、缺字段及报警场景显示正确。
- [ ] 用户请求与设备反馈分离；输入快照刷新不会误发控制请求。
- [ ] 点击一次只发送一次预期请求；切页再返回不会累积连接。
- [ ] 按住式控制释放、失焦、切页及禁用后停止活动输入。
- [ ] 修改目标值时不会被遥测刷新覆盖；目标与当前反馈可辨认。
- [ ] 在约定尺寸和缩放下没有关键控件遮挡、截断或无法操作。
- [ ] 演示状态明确，设备图占位及未接入功能已说明。
- [ ] 仓库没有个人路径、构建产物、Qt Creator .user 文件。

关键行为可以用已有测试设施验证，或用独立预览的信号日志/计数验证；无需为普通静态布局搭建庞大的测试框架。Qt Test 5.15.19 已允许用于测试目标，不链接到正式应用；其他新测试依赖仍须走共同变更流程。

### 双平台验证记录模板

| 环境 | 配置/编译 | 启动及资源 | 布局与交互 | 记录要求 |
|---|---|---|---|---|
| 当前开发机 | 未执行 | 未执行 | 未执行 | 写实际系统、Qt、编译器版本与结果 |
| Nano 目标环境 | 未执行 | 未执行 | 未执行 | 无设备时明确未验证 |
| 外部电脑目标环境 | 未执行 | 未执行 | 未执行 | 不用开发机通过替代目标机结果 |

一个环境可能同时是开发机和目标机，说明即可。不因无法访问某台设备而停止其他可做的工作，但也不能声称双端联调通过。当前没有通信层，验收只覆盖 UI 和请求接口。

## 10. 每次页面交付的固定格式

页面负责人或 Agent 交付时应列出：

1. 本次完成的显示区域和交互，以及修改文件。
2. 页面类、CMake 目标、资源前缀、构造方式。
3. 输入快照与输出信号的类型、单位及未确认项。
4. 本页构建和运行方法，实际使用的 Qt、工具链、依赖版本。
5. 已执行的验证、具体结果及未验证的平台。
6. 演示数据、占位素材及当前未接入功能。
7. 集成人需要执行的模块注册、页面装配或公共接口修改。

已经授权的构建、代码修正和页面内工作应持续完成，不要每一步都要求确认。只有共同基线变更、跨文件归属边界或确实缺少决定性信息时才提出具体问题，并继续完成独立工作。

## 11. 你们开始前需要落实的事项

1. 集成人是谁，哪一份仓库及哪个基线提交作为共同起点。
2. 记录当前 Nano 的具体型号、18.04 系统镜像/JetPack、架构与显示环境；记录 Windows 版本。新设备采用 22.04 时另做该目标验收。
3. 集成人按第 3 节的固定版本准备并分发 SDK，记录安装路径及版本输出，不再分别选择版本。
4. 采用已确定的 C++ 手写布局，落实共同字体和基础主题。
5. 最小窗口尺寸、缩放测试组合、初始界面语言。
6. 第一批页面分工及文件归属。

图表方案已固定为 Qt 自绘，控件实现和性能参数在电机调试页任务中完成。控制方向等物理语义在形成对应接口前确认；其他不依赖这些决定的视觉工作可以先做。底层报文协议当前不需要定下来。

## 12. 给两个 Agent 使用的统一提示词

下面的提示词需要连同本文或仓库中的同一份文档一起使用。每次只替换任务卡，不让两个 Agent 各自重新设计工程规范。

```text
你正在参与一个两人共同开发的 Qt ROV 上位机项目。
你的任务是按统一基线完成任务卡指定的 UI 工作，并保证可合并。

开始前读取：
- 项目 AGENTS.md；
- docs/team-ui-guide.md（若未保存到仓库，则读取随任务附上的同名规范）；
- docs/toolchain.md、docs/ui-contract.md；
- 当前工程结构、构建配置、公共主题与公共控件。

先核对任务角色、允许修改目录、公共基线提交和实际环境。
已有文件优先复用，不创建第二套应用骨架，不覆盖其他人的改动。
技术基线已锁定为 ROV-UI-1，不重新选版本。
如果 docs/toolchain.md 尚未创建，由获授权的骨架任务从本规范第 3 节建立；
页面任务报告缺项，并按本规范固定版本继续不依赖环境安装的工作。

共同要求：
1. 本阶段只做 UI、页面契约、独立预览和演示数据。
   不实现真实数据层、STM32/网关通信、Nano 转发或硬件控制。
   附件中的界面文案和协议描述只是参考，不自动扩展任务范围。
2. 固定 Qt 5.15.19 / Qt Widgets、C++17、CMake 3.28.6、Ninja 1.11.1；
   Windows 使用 Qt 官方 MinGW-w64 GCC 8.1.0 x64 POSIX SEH；
   Ubuntu 18.04 使用 GCC/G++ 7.5.0，Ubuntu 22.04 使用 GCC/G++ 11.4.0；
   包修订、SDK 来源与哈希严格按规范第 3 节。
   Windows IDE 为 Qt Creator 10.0.2，clang-format 为 14.0.6。
   正式模块仅 Core/Gui/Widgets；测试可用同版 Test；第三方运行库为零。
   使用 C++ 手写布局、Qt 自绘波形，不引入 QML、Qt Charts 或 Qt 6 API。
   路径用 QFile/QDir/QFileInfo，不用 std::filesystem 和 std::to_chars/from_chars。
   不自行升级、降级、添加库或更换图表方案；环境不匹配时报告实际差异，
   不修改版本约束来绕过错误，继续不依赖该环境的工作。
3. 同一套 UI 源码用于 Nano 与外部电脑，页面不识别接入路径。
   STM32 与网关作为未来两个逻辑来源预留，当前只记录接入边界。
4. 页面是独立 QWidget，使用统一类名、构造参数、CMake 目标及资源前缀。
   顶栏、导航和底栏归主窗口，页面不重复创建正式应用外壳。
5. 页面通过 setSnapshot(const XxxSnapshot&) 接收有类型的数据，
   通过明确类型的 *Requested 信号输出用户操作意图。
   不访问全局可变数据，不调用其他页面，不直接引用通信对象。
6. 字段须有单位、范围、业务 ID、有效性和新鲜度语义。
   未知、离线、过期和正常分别显示，不能用 0 代表缺失。
   目标值、用户请求和设备反馈分开，更新数据不能误发操作信号。
7. 手动控制定义按下、释放、失焦、切页及禁用行为；
   外部传入 canControl，默认不具备真实控制权限。
   不实现推力分配、控制权仲裁或底层设备策略。
8. 复用共同主题、公共控件和布局管理器，资源放入 Qt 资源系统。
   禁止页面全局 QSS 污染、硬编码个人路径、依赖平台特定字体。
   遵循同一 .clang-format、命名、编码、换行和所有权约定。
9. 演示数据在 examples 或明确标识的公共演示装配层中注入，
   公共装配层由集成人维护；正式页面不内置假遥测定时器。
   预览明确显示“演示数据 / 无真实设备连接”，并能观察操作请求。
10. 按任务角色修改文件：骨架任务可修改明确授权的公共文件；
    页面任务只改本页、授权页面契约与预览目录。
    涉及公共依赖、共享契约或他人目录时，列出具体变更建议，
    交由集成人统一处理，继续其他可独立完成的工作。
11. 先实现最小可运行版本并便于集成，再完善细节。
    不擅自重构无关目录，不添加过度抽象、空驱动或第二套框架。
12. 执行本次范围内的构建与关键行为验证，报告真实结果。
    缺少 Qt 或无法访问 Nano 时明确未验证，禁止虚构编译或联调成功。
    最终按规范第 10 节交付文件、接口、运行方式、验证和接入步骤。

如任务卡与当前共同规范冲突，指出具体冲突，不静默修改团队基线。
明确授权且不冲突的工作直接完成，不逐步反复请求批准。

本次任务卡：
（粘贴第 13 节的一张任务卡，并填写实际内容。）
```

## 13. 可直接填写的任务卡

### 13.1 先执行一次：公共骨架任务

```text
任务角色：公共骨架维护者 / 集成人
目标：建立两人共用、可编译的最小 Qt Widgets 工程。
仓库及基线提交：待填写
已确认技术基线：ROV-UI-1，严格使用本规范第 3 节固定版本
授权目录：src/app、src/ui/common、src/ui/theme、cmake、docs；
          根 AGENTS.md、根 CMake、公共预设和共同格式/忽略配置；
          当前实际需要的 contracts/common 文件；
          本轮分配页面的最小占位模块及 examples 预览入口。
需完成：先核对并准备固定版本 SDK；如尚未安装，明确环境准备缺项；
        一个正式主程序、顶栏、左侧导航、页面容器、底栏、
        基础主题和确实需要的公共控件、构建配置及精确版本文档。
页面接入：确定 DashboardPage 等的入口清单与模块注册方法；
          注册本轮分配页面及其预览目标，记录实际构建命令；
          先用清楚标识的占位内容验证切页，不实现其他完整页面。
不做：通信、数据层、网络转发、真实硬件控制、完整设置页。
验收：可编译运行，资源加载正确，导航可切换，公共基线可供两人使用。
```

### 13.2 你当前执行：Dashboard 第一页

```text
任务角色：页面开发者
页面名称 / 类名：Dashboard / rov::DashboardPage
参考图：附件中第一页 Dashboard（总览）
CMake 目标：rov_page_dashboard
导航业务标识：dashboard
资源前缀：:/pages/dashboard/
仓库及基线提交：待填写
Qt、构建工具和平台：ROV-UI-1，引用 docs/toolchain.md 及本规范第 3 节
允许修改：src/pages/dashboard、src/contracts/dashboard、
          examples/dashboard_preview。
公共文件需要变更时：提供具体建议，由集成人处理。

显示区域：
- 六推进器俯视示意及各推进器转速、电流、温度、状态；
- 深度、姿态、母线电压、模式、解锁状态、漏水状态、内部温度；
- 六自由度手动控制、使能状态、推力上限；
- 电机汇总、报警列表、快捷操作。

接口：
- 输入为 DashboardSnapshot；字段及单位写入页面契约。
- 输出包含 armRequested、disarmRequested、holdPositionRequested、
  surfaceRequested、manualControlRequested，及使能/推力上限请求。
- 六轴方向、范围及请求参数应明确；未确认物理语义不得自行认定。
- 预留 canControl 及不可控原因，正式默认不可控制。

演示场景：正常、无数据、离线/过期、部分推进器缺失、有报警。
布局检查：先使用规范建议尺寸；实际屏幕与缩放确认后加入验收。
不做：重新实现公共顶栏/导航/底栏；其他完整页面；通信与数据层。
交付：可独立运行的第一页、契约、演示数据、请求日志和集成说明。
```

### 13.3 另一位同学通用页面任务

```text
任务角色：页面开发者
页面名称：
参考截图：
页面类名 / CMake 目标 / 导航业务标识 / 资源前缀：
仓库及公共基线提交：
技术基线：ROV-UI-1，引用 docs/toolchain.md 及本规范第 3 节
允许修改目录：
输入快照与输出请求：
需要实现的显示区域与局部交互：
可复用的公共控件：
演示场景：
尺寸与缩放要求：
本次明确不做：
验收与交付：按共同规范第 9、10 节执行。
```

本文已锁定工具链和 UI 依赖版本，并核对官方来源；没有安装 SDK、编译 UI 或完成实机验收。落地顺序是：**按固定版本准备 SDK → 提交公共骨架 → 各自开发页面 → 及早集成 → Windows、Ubuntu 18.04、Ubuntu 22.04 分别验证。**



---

# 第五部分：完整纳入《ROV Qt 上位机统一 Agent 提示词 v2.1》

# ROV Qt 上位机统一 Agent 提示词

你正在参与一个水下机器人 ROV 上位机 GUI 项目。

本项目不是自由设计 UI。

**任务提供的界面示意图 / 原型图是主要视觉与功能参考。**

在开始开发页面之前，必须先查看对应页面的参考示意图，再阅读现有工程。

参考图用于确定：

- 页面整体布局；
- 信息层级；
- 卡片数量与位置；
- 主要功能区域；
- 控件种类；
- 导航结构；
- 页面之间的视觉一致性。

参考图不是要求逐像素复刻。

可以根据实际窗口尺寸、Qt Widgets 布局方式和工程需求进行合理优化，但是：

**不得在没有需求依据的情况下，自行大幅改变页面的信息架构。**

如果参考图和文字任务存在冲突：

优先满足明确的文字需求，同时尽量保持参考图的布局和视觉关系，并在最终报告中说明调整内容。

---

# 一、整体视觉方向

参考现有示意图进行设计，但统一调整为：

**简约科研风格 + 白灰色调。**

整体视觉关键词：

- Scientific
- Engineering
- Minimal
- Industrial
- Clean
- Light Theme

主要颜色：

- 白色；
- 浅灰；
- 中性灰；
- 深灰文字；
- 少量冷蓝色强调。

状态颜色：

- 绿色：正常；
- 橙色：警告；
- 红色：故障；
- 灰色：未知 / 离线。

避免：

- 游戏 HUD；
- 赛博朋克；
- 大面积黑色；
- 霓虹发光；
- 复杂渐变；
- 过量阴影；
- 装饰性仪表盘；
- 与科研设备无关的视觉效果。

目标感觉应该接近：

**现代实验室仪器软件 / 工业控制软件 / 科研设备 GUI。**

---

# 二、整个软件分成 6 个导航入口

整个正式上位机的左侧导航固定规划为：

```text
Dashboard
Motor Debug
Firmware
Manipulator
Vision
Settings
```

其中当前真正规划的主要功能页面为 **5 个**。

Settings 当前只有导航预留，不设计完整功能。

主程序采用：

```text
MainWindow
   │
   ├── DashboardPage
   ├── MotorDebugPage
   ├── FirmwarePage
   ├── ManipulatorPage
   ├── VisionPage
   └── SettingsPlaceholder
```

推荐使用：

```cpp
QStackedWidget
```

切换页面。

所有页面共用：

- 顶部状态栏；
- 左侧导航栏；
- 底部状态栏；
- 全局颜色；
- 字体；
- Card 样式；
- Status 样式。

页面内部不要重复建立 MainWindow。

---

# 三、页面 1：Dashboard

类名：

```cpp
rov::DashboardPage
```

业务 ID：

```text
dashboard
```

这是整个软件启动后的默认主页。

必须优先参考：

**Dashboard 示意图。**

该页面用于：

**查看整台水下机器人的综合状态，并完成基础人工控制。**

主要区域规划：

```text
┌────────────────────────────────────────────┐
│               Robot Overview               │
├──────────────────────┬─────────────────────┤
│                      │ Status / Telemetry  │
│   ROV Top View       │                     │
│   Thruster Layout    │ Depth               │
│                      │ Attitude            │
│   FL   FR            │ Bus Voltage         │
│   ML   MR            │ Temperature         │
│   RL   RR            │ Leak Status         │
│                      │ Mode / Armed        │
├──────────────────────┼─────────────────────┤
│ Manual Control       │ Motor Summary       │
│                      │                     │
│ Surge / Sway         │ Alarm              │
│ Heave                │                     │
│ Roll / Pitch / Yaw   │ Quick Actions       │
└──────────────────────┴─────────────────────┘
```

至少显示：

机器人：

- 深度；
- Roll；
- Pitch；
- Yaw；
- 工作模式；
- Arm 状态；
- 母线电压；
- 漏水状态；
- 内部温度；
- 通信状态。

推进器：

六个推进器：

```text
FL
FR
ML
MR
RL
RR
```

每个显示：

- RPM；
- Current；
- Temperature；
- Status。

机器人俯视图优先使用：

```cpp
QPainter
```

自绘。

不要直接把参考截图裁下来作为页面内容。

---

# 四、Dashboard 控制区域

基础人工控制为六自由度：

```text
Surge
Sway
Heave
Roll
Pitch
Yaw
```

需要提供直观操作控件。

页面只输出控制意图。

不要进行：

- 推力分配；
- 八推进器解算；
- QP；
- PWM；
- 电机控制。

控制链路：

```text
UI
 ↓
6-DOF command request
 ↓
未来控制层
 ↓
Thruster Allocation
 ↓
STM32
```

快捷控制包括：

```text
ARM
DISARM
HOLD POSITION
SURFACE
```

还应提供：

- Manual Control Enable；
- Thruster Limit；
- Control Permission 状态。

---

# 五、页面 2：Motor Debug

类名：

```cpp
rov::MotorDebugPage
```

业务 ID：

```text
motor_debug
```

必须优先参考：

**电机调试页面示意图。**

用途：

对单个推进器 / 电机节点进行状态查看和调试。

主要区域：

```text
┌───────────────────────────────────────────┐
│ Motor Selection                           │
├─────────────────┬─────────────────────────┤
│ Motor Status    │ Real-time Plot          │
│                 │                         │
│ RPM             │ RPM                     │
│ Current         │ Current                 │
│ Voltage         │ Temperature             │
│ Temperature     │                         │
├─────────────────┼─────────────────────────┤
│ Control         │ Parameters / Status     │
└─────────────────┴─────────────────────────┘
```

页面可规划：

- 电机选择；
- 节点状态；
- RPM；
- 电流；
- 电压；
- 温度；
- 故障状态；
- 控制目标；
- 实时曲线区域；
- 调试参数区域。

实时曲线：

优先使用：

```cpp
QWidget + QPainter
```

暂时不引入 QCustomPlot、Qt Charts。

当前阶段没有真实 CAN 数据。

使用 Preview 演示。

---

# 六、页面 3：Firmware

类名：

```cpp
rov::FirmwarePage
```

业务 ID：

```text
firmware
```

必须优先参考：

**固件升级页面示意图。**

用途：

未来用于机器人各节点固件升级。

当前页面只实现：

UI。

建议区域：

```text
┌──────────────────────────────────────────┐
│ Device / Node List                       │
├─────────────────────┬────────────────────┤
│ Firmware File       │ Update Status      │
│                     │                    │
│ File information    │ Progress           │
│ Version             │ Node state         │
│ Target              │ Log                │
│                     │                    │
└─────────────────────┴────────────────────┘
```

显示：

- 节点列表；
- 在线状态；
- 当前版本；
- 目标版本；
- 固件文件；
- 固件大小；
- Upgrade；
- Verify；
- Progress；
- Result；
- Log。

注意：

即使参考图写了 CAN，

当前也不能因此直接实现 CAN Bootloader。

这里只做：

```text
UI + Request Interface
```

---

# 七、页面 4：Manipulator

类名：

```cpp
rov::ManipulatorPage
```

业务 ID：

```text
manipulator
```

必须优先参考：

**机械臂页面示意图。**

用途：

未来控制水下机械臂。

当前机械臂结构按项目规划主要考虑：

- Shoulder Pitch；
- Elbow Pitch；
- Wrist Roll；
- Gripper。

主要区域：

```text
┌──────────────────────────────────────────┐
│ Manipulator Overview                     │
├─────────────────────┬────────────────────┤
│ Arm Visualization   │ Joint Status       │
│                     │                    │
│                     │ Shoulder           │
│                     │ Elbow              │
│                     │ Wrist              │
│                     │ Gripper            │
├─────────────────────┼────────────────────┤
│ Manual Control      │ Status / Alarm     │
└─────────────────────┴────────────────────┘
```

当前只负责：

- 关节显示；
- 目标值输入；
- 手动操作 UI；
- 状态显示；
- 请求信号。

不要实现：

- 机械臂动力学；
- 逆运动学；
- 重心补偿；
- 力控；
- 水动力补偿。

这些属于后续控制层。

---

# 八、页面 5：Vision

类名：

```cpp
rov::VisionPage
```

业务 ID：

```text
vision
```

必须优先参考：

**视觉页面示意图。**

摄像头连接关系：

```text
Camera
   ↓
Nano
```

摄像机不经过 STM32。

但是当前阶段：

**不实现真实摄像机。**

只建立页面框架。

页面结构：

```text
┌──────────────────────────────────────────┐
│ Camera / Vision Status                   │
├─────────────────────────┬────────────────┤
│                         │ Detection      │
│                         │ Information    │
│      Camera Area        │                │
│                         │ Position       │
│                         │ Orientation    │
│                         │ Confidence     │
├─────────────────────────┴────────────────┤
│ Vision / Camera Status                   │
└──────────────────────────────────────────┘
```

Camera Area 当前显示明确占位：

```text
CAMERA NOT CONNECTED
```

或者：

```text
VIDEO PLACEHOLDER
```

不要：

- 调 OpenCV；
- 打开摄像头；
- 编写 GStreamer；
- 写 CUDA；
- 接入视觉算法。

后续再做。

---

# 九、页面 6：Settings

导航中保留：

```text
Settings
```

但是当前没有完整 Settings 参考图。

因此：

**不得自行设计复杂 Settings 页面。**

当前只建立极简占位页，例如：

```text
Settings

Configuration interface reserved.

Not implemented yet.
```

未来需求明确后再开发。

---

# 十、参考图使用规则

每开发一个页面，都执行：

```text
查看对应参考图
       ↓
识别主要区域
       ↓
识别信息层级
       ↓
识别主要控件
       ↓
结合统一科研风格重新实现
       ↓
Qt Widgets 页面
```

不要执行：

```text
看一眼参考图
       ↓
完全自由发挥
```

也不要执行：

```text
直接复制整张截图
```

参考图的优先级：

### 高优先级保留

- 页面功能区域；
- 页面信息层级；
- 关键数据显示位置；
- 控制区域位置；
- 页面基本结构。

### 可以优化

- 间距；
- 卡片大小；
- 圆角；
- 字号；
- 配色；
- 图标；
- 对齐；
- 局部布局比例。

### 不要求复刻

- 截图中的具体演示数字；
- 日期；
- CPU 占用率；
- 虚构连接状态；
- 协议名称；
- 临时文案。

---

# 十一、不同页面必须保持统一

所有页面必须共享：

```text
Navigation Width
Top Bar Height
Bottom Bar Height

Card Radius
Card Border
Card Background

Title Font
Body Font
Numeric Font

Spacing
Margins

Status Colors
Button Height
```

不能出现：

```text
Dashboard 是白色科研风

Motor Debug 是黑色赛博风

Firmware 又是 Windows 98 风格
```

如果参考图本身风格不统一：

不要逐图复刻颜色。

统一转换成：

**ROV GUI 白灰科研主题。**

---

# 十二、页面导航顺序固定

左侧导航顺序：

```text
01  Dashboard

02  Motor Debug

03  Firmware

04  Manipulator

05  Vision

06  Settings
```

启动默认：

```text
Dashboard
```

点击导航只切换：

```cpp
QStackedWidget
```

不要创建多个独立主窗口。

---

# 十三、当前页面优先级

开发顺序：

```text
第一阶段
Dashboard
        ↓
公共 MainWindow / Theme / Navigation

第二阶段
Motor Debug
Manipulator

第三阶段
Firmware
Vision

第四阶段
Settings
```

当前最优先：

**Dashboard。**

先让整个 GUI 的：

- 主题；
- 导航；
- 页面框架；
- Card 风格；
- Status 风格；
- 字体；
- 间距；

在 Dashboard 上确定。

后续页面直接复用。

---

# 十四、Agent 开始任务时必须检查

任务开始后首先确认：

1. 当前要求开发哪个页面；
2. 是否提供该页面参考示意图；
3. 现有工程中是否已经存在页面骨架；
4. 是否已经存在统一 Theme；
5. 是否存在公共控件；
6. 是否已经建立 MainWindow 和 QStackedWidget。

如果有对应参考图：

必须以参考图为主要视觉依据。

如果没有：

优先保持已有页面形成的设计语言。

不能因为缺少一张图就重新设计另一套 GUI 风格。

---

# 十五、技术基线

继续使用：

```text
ROV-UI-2 Stable
```

Windows：

```text
Qt 5.15.2
MinGW 8.1
VSCode
CMake
```

无需安装：

```text
Qt Creator
Qt Online Installer GUI
```

Ubuntu 18.04：

```text
Qt 5.9.5
GCC 7.5
系统 CMake
```

Ubuntu 22.04：

```text
Qt 5.15.x
GCC 11.x
系统 CMake
```

CMake：

```text
>= 3.10.2
```

Qt API：

```text
>= Qt 5.9
```

不要使用：

```cmake
EXACT
```

锁死 Qt Patch Version。

不要为了工程重新安装已经能够工作的：

- CMake；
- Ninja；
- GCC；
- Qt Creator。

---

# 十六、最终目标

软件最终结构应保持：

```text
ROV Ground Station
│
├── Dashboard
│
├── Motor Debug
│
├── Firmware
│
├── Manipulator
│
├── Vision
│
└── Settings
```

其中：

```text
Dashboard
```

负责整机总览与基础控制；

```text
Motor Debug
```

负责推进器 / 电机调试；

```text
Firmware
```

负责未来节点升级入口；

```text
Manipulator
```

负责机械臂；

```text
Vision
```

负责 Nano 摄像机与视觉信息；

```text
Settings
```

暂时预留。

所有页面属于：

**同一个程序、同一个 MainWindow、同一套源码、同一个设计系统。**

本次任务卡：

【在这里填写具体要开发的页面和允许修改的文件范围。】


---

# 第六部分：集成完成检查

- [ ] 精确 Qt / 编译器 / CMake / Ninja 已由集成人锁定并写入 docs/toolchain.md。
- [ ] 只有一个 MainWindow、一个 QStackedWidget 和一个正式 main。
- [ ] 五个正式页面及 Settings 占位统一导航、主题和目录归属。
- [ ] 每页都有契约、Preview、可重复演示数据和请求日志。
- [ ] 没有真实通信、数据单例、硬件控制或截图贴图。
- [ ] 公共接口变更已同步头文件、文档、演示和受影响页面。
- [ ] 已记录实际构建、布局、交互和设备验证；未访问平台明确标记未验证。
- [ ] 交付报告使用第 3.9 节模板。

