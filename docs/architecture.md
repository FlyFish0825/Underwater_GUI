# 项目架构图

这份文档按三层阅读：先看系统总览，再看局部职责，最后看关键数据流细节。
每张图只回答一个问题，不用一张图塞下整个工程。

## 第一层：系统总览

问题：数据从哪里来，经过哪些边界，最后到达哪个页面？

```mermaid
flowchart LR
    device[电机 / 网关 / Bootloader<br/>真实设备与 CAN 总线]
    transport[Transport<br/>USB CDC 字节收发]
    protocol[Protocol<br/>AA55、Observer Motor、Bootloader]
    service[Service / Data<br/>会话、解析、状态、缓存、记录]
    app[MainWindow<br/>页面装配与请求路由]
    ui[Qt Widgets UI<br/>Dashboard / Motor Debug / Firmware / ...]

    device -->|USB CDC| transport --> protocol --> service --> app --> ui
    ui -->|类型化 Request| app --> service --> protocol --> transport
```

| 结构 | 作用 | 不负责什么 |
| --- | --- | --- |
| 设备与总线 | 产生反馈、接受控制或升级命令 | 不参与 Qt 页面布局 |
| Transport | 把 USB CDC 字节可靠交给上层 | 不解释 CAN ID 或页面状态 |
| Protocol | 校验、拆包、编码协议对象 | 不持有 QWidget |
| Service / Data | 把协议对象变成快照、状态和升级进度 | 不绘制曲线 |
| MainWindow | 装配服务与页面、路由请求、控制高频数据入口 | 不重新拼协议字节 |
| UI | 显示快照、产生用户请求、管理曲线窗口 | 不直接访问串口或 CRC |

## 第二层：局部结构

### 2.1 通信与状态局部

问题：USB 收到一段字节后，谁处理普通电机反馈，谁处理升级？

```mermaid
flowchart TB
    serial[SerialTransport<br/>USB CDC 打开、读写、设备发现]
    gateway[BootloaderCommunicationService<br/>AA55 网关拆包、发送队列、心跳]
    motor[ObserverMotorProtocol<br/>0x100 控制 / 0x201 反馈 / 0x301 调试]
    motorData[ObserverMotorDataService<br/>Node1~8 状态、freshness、批量发布]
    boot[BootloaderService<br/>命令、回复、会话状态]
    download[BootloaderDownloadController<br/>ENTER_BOOT→WRITE→VERIFY→JUMP_APP]
    motorPage[MotorDebugPage<br/>快照与控制请求]
    main[MainWindow<br/>请求校验与服务路由]
    firmware[FirmwarePage<br/>节点、进度、日志、高级命令]

    serial --> gateway
    gateway -->|电机 CAN 帧| motor --> motorData --> main --> motorPage
    gateway -->|Bootloader 回复| boot --> download --> main --> firmware
    firmware -->|升级请求| download --> boot --> gateway
    motorPage -->|MotorSpeedControlRequest| main --> gateway
```

| 结构 | 作用 |
| --- | --- |
| `SerialTransport` | 处理 USB CDC 的设备枚举、打开、关闭和字节流 |
| `BootloaderCommunicationService` | 统一发送 AA55、分发接收帧、维护心跳和传输错误 |
| `ObserverMotorProtocol` | 只做电机帧编码/解码和字段合法性检查 |
| `ObserverMotorDataService` | 聚合 Node1~8、维护有效性/过期状态，按 UI 频率发布 |
| `BootloaderService` | 把高级命令转换成 Bootloader 业务结果 |
| `BootloaderDownloadController` | 编排正式下载阶段和进度 |

### 2.2 科研记录局部

问题：如何在不触发曲线和逐帧 UI 更新的前提下，保存完整实验数据？

```mermaid
flowchart LR
    gateway[BootloaderCommunicationService<br/>CAN 收帧 / 发帧]
    dashboard[DashboardPage<br/>输入请求 / 非演示快照]
    main[MainWindow<br/>旁路路由]
    queue[ResearchDataRecorder<br/>20,000 条有界队列]
    worker[后台线程<br/>批量序列化与写盘]
    files[JSONL + CSV + Meta<br/>科研分析文件]
    plotGate{Motor Debug 可见？}
    plot[ObserverMotorDataService<br/>曲线快照]

    gateway --> main --> queue --> worker --> files
    dashboard --> main
    main --> plotGate
    plotGate -->|是| plot
    plotGate -->|否| discard[仅丢弃绘图路径]
```

记录链位于绘图可见性门控之前，因此切换页面不会中断会话；绘图链仍按原策略在页面
不可见时丢弃。记录器只向总览页低频发布接收/丢弃计数，不发布逐点数据。

### 2.3 UI 与曲线局部

问题：多个 Qwt 曲线窗口如何共享变量目录，却互不影响显示配置？

```mermaid
flowchart LR
    snapshot[MotorDebugSnapshot<br/>当前已有反馈、调试变量、时间戳]
    catalog[DebugSeries Catalog<br/>变量 ID、名称、单位、样本]
    workspace[MotorDebugPage<br/>新增/删除/二列布局/预设]
    windowA[CurveWindow A<br/>IABC 预设]
    windowB[CurveWindow B<br/>速度预设]
    windowN[CurveWindow N<br/>自定义变量组合]
    renderer[QwtCurvePlotWidget<br/>图例/缩放/平移/自动适配]

    snapshot --> catalog --> workspace
    workspace --> windowA
    workspace --> windowB
    workspace --> windowN
    windowA --> renderer
    windowB --> renderer
    windowN --> renderer
```

| 结构 | 作用 |
| --- | --- |
| `MotorDebugSnapshot` | 提供已解析的变量和采样时间，不关心画布 |
| `DebugSeries Catalog` | 为 UI 提供稳定变量 ID、显示名、单位、采样率和样本 |
| `MotorDebugPage` | 管理多窗口、IABC/速度预设、自定义窗口和二列布局 |
| `CurveWindow` | 保存本窗口的曲线选择和独立缩放状态 |
| `QwtCurvePlotWidget` | 负责多曲线、图例、缩放、平移和重绘，不决定协议 |

当前 Motor Debug 已使用 Qwt 显示多变量和多窗口。窗口布局持久化、
自定义颜色/左右 Y 轴以及高频原始样本环仍是后续边界。

## 第三层：关键数据流细节

### 3.1 `0x100` 电机控制

```mermaid
sequenceDiagram
    participant U as MotorDebugPage
    participant M as MainWindow
    participant S as BootloaderCommunicationService
    participant P as ObserverMotorProtocol
    participant G as AA55 Gateway
    participant E as 电机节点

    U->>M: MotorSpeedControlRequest(nodeId, rpm, runCommand)
    M->>M: 校验节点、转速和当前页面
    M->>S: sendObserverMotorControl(ControlFrame)
    S->>P: ObserverMotorProtocol::encodeControl()
    P-->>S: 24 字节 payload（保留字节为 0）
    S->>G: CAN ID 0x100, DLC 24, FLAGS 0x02
    G->>E: 发送 CAN FD 控制帧
```

### 3.2 高频反馈到曲线

```mermaid
flowchart LR
    rx[0x201 / 0x301<br/>普通反馈约 100 Hz]
    decode[Protocol decode<br/>只生成结构化帧]
    cache[DataService cache<br/>按节点覆盖/批量缓存]
    publish[定时发布快照<br/>当前服务约 50 ms 发布]
    gate{Motor Debug<br/>页面可见？}
    drop[跳过曲线重绘<br/>不打印、不保存高频文本]
    ring[Curve ring buffer<br/>规划：每条曲线有界容量]
    plot[QwtCurvePlotWidget<br/>已接入多曲线与交互]

    rx --> decode --> cache --> publish
    publish --> dashboard[Dashboard<br/>8 节点实时状态]
    publish --> gate
    gate -->|否| drop
    gate -->|是| ring --> plot
```

当前普通反馈约 100 Hz，数据服务按约 50 ms 发布快照，因此总览和第二页不会执行 100 次
每秒的 QWidget 更新。Qwt 曲线使用服务快照构建有界显示历史；记录器旁路保留每一帧原始
数据。关键约束是“采样频率”
和“绘图频率”分离：1000 Hz 的数据不能造成 1000 次 GUI repaint，也不能造成
1000 条日志。

### 3.3 后台科研记录

```mermaid
sequenceDiagram
    participant C as CAN / 用户输入
    participant M as MainWindow
    participant Q as 有界记录队列
    participant W as 写盘线程
    participant F as JSONL / CSV / Meta

    C->>M: 原始帧或类型化请求
    M->>Q: 复制后立即入队
    alt 队列有空间
        Q-->>M: accepted++
        W->>Q: 最多取 256 条
        W->>F: 批量追加，约 250 ms 刷新
    else 队列已满
        Q-->>M: dropped++，不阻塞通信
    end
```

详细字段、文件命名和 IMU/深度接入边界见 `docs/research-recording.md`。

### 3.4 Bootloader 下载

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> EnterBoot: 用户开始下载
    EnterBoot --> Erase: 节点确认进入 Bootloader
    Erase --> Write: 擦除成功
    Write --> Verify: WRITE_END 完成
    Verify --> JumpApp: 校验通过
    JumpApp --> Success: APP 启动
    EnterBoot --> Failed
    Erase --> Failed
    Write --> Failed
    Verify --> Failed
    Failed --> Idle: 记录错误并允许重试
    Success --> Idle
```

Bootloader 的收发和下载阶段日志保留；普通高频电机反馈不进入固件页日志。

## 阅读入口

- 协议字段和 CRC：`agent/ROV上位机通信协议与分层架构规范_v1.0.md`；
- 当前工具链和构建：`docs/toolchain.md`；
- 曲线组件选择和窗口设计：`docs/plotting.md`；
- 科研记录格式和完整性：`docs/research-recording.md`；
- Agent 开发边界：`agent/agent.md`。
