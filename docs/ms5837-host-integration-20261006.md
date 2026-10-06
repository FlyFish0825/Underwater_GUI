# MS5837 深度计上位机接入与验收（2026-10-06）

> 当前 GUI 交互以第 11 节为准：总览隐藏诊断说明，启动无零点时自动把当前有效压力视为水面。前面章节保留历史验收，不代表仍需每次手动配置。

> 当前使用规则以第 10 节为准：固定 02BA，OSR/频率联动限幅并按序应用。第 9 节的 50 Hz 服务层拦截是历史诊断；新版设备协议已明确该拦截解除。

## 1. 协议依据与范围

协议依据已按用户最新指定路径更新为
`CAN_To_Uart/CAN_To_Uart/docs/ms5837-aa5b-host-protocol.md`（相对于 Project 根目录）。
第一轮使用其 `worktrees/depth-ms5837` 副本；第二轮复核主工程副本，协议文档提交仍为
`096cc5a`。采用已经实测的 AA5B v1、TARGET=2、0x82/0x83 布局。原文第 14 节的
`0x0D/0x4D` 单次采样、默认不推流是**待实现提案**，本次没有实现、发送或依赖它。

深度计数据不是 IMU 气压高度，也不是整机融合状态。原有六个导航页面、两种传感器标签、
七行深度数据表都保留；没有新增页面、导航入口、窗口、独立串口或新的应用层服务。

## 2. 原架构内的接入位置

```text
SerialTransport（现有 USB CDC 字节链路）
  → BootloaderCommunicationService（AA55/AA58/AA59/AA5B 家族路由）
  → SensorProtocol（AA5B 编解码、CRC、参数元组校验）
  → SensorDataService（TARGET、请求匹配、有效性、新鲜度、快照）
  → MainWindow（原有信号连接，不改装配）
  → DashboardPage / SensorPanel（只消费业务快照、发出类型化请求）
```

页面不解析偏移、CRC 或串口字节；没有改动电机控制、固件下载和科研记录链路。
传感器刷新仍由原服务按 50 ms 周期发布，不给每条遥测打印日志。

### 显示映射

| 原有位置 | 本次使用的数据 |
| --- | --- |
| 总览“深度” | 有效整机深度优先，否则显示 MS5837 `depth_filtered_m`，单位 m |
| 总览原数据来源文字 | 显示深度计来源，并补入有效绝对压力 Pa、温度 °C、数据产生时刻 µs |
| 深度计详情原七行数据表 | 绝对压力、传感器温度、原始水深、滤波水深、P0、D1、D2 |
| 原设备信息文字 | MS5837 名称、固件、能力位与设备回读/确认后的型号 |
| 原状态文字 | PROM、型号/零点状态、数据样本龄、完成采样数、良好样本数、错误数、设备样本龄、数据时刻、流序号 |
| 原参数反馈列 | 确认值、未确认值或明确的读写错误；不覆盖用户正在编辑的待写入值 |

传感器数据不被电机快照刷新清空；只用于显示，不解锁控制、不冒充融合状态、不写入
旧科研记录器的整机列。AA5B 逐帧科研记录仍未接入，不属于本次新增内容。

## 3. 帧、数值与状态约束

### AA5B 校验

保持原公共协议：版本 1、LEN≤58、总长 20+LEN，CRC-16/CCITT-FALSE 覆盖偏移
1..15+LEN，CRC 小端，帧尾 5B AA。生产路径先拆家族再解码，AA5B 负载里的 AA55
不会逃逸成电机帧。回复按 TARGET、SEQ、CMD=request+0x40 同时匹配。

0x82 精确 32 字节，0x83 精确 20 字节，均不含 RESULT；GET_STATUS 回复精确
21 字节，首字节 RESULT，其后才是同样的统计体。两个 TARGET 的 0x83 分别更新，
不会让离线 IMU 状态覆盖深度计。错误回复的 RESULT 和长度也必须合法。

### 有效性

压力/温度需要 ONLINE、PROM_VALID、MODEL_CONFIRMED，以及各自的 VALID 位。
CONFIG_UNKNOWN 会覆盖相互矛盾的“已确认”位，禁止显示物理量。
水深还需要 ZERO_VALID、PRESSURE_VALID 和 DEPTH_VALID。

线上占位 0 不是有效测量；只有有效位成立才显示。有效的真实 0 m 可以正常显示。
D1/D2 独立使用深度 RAW_VALID，不再仅凭“在线、PROM 通过”显示旧 ADC。
P0 同样要求在线、零点有效且已有新鲜的 0x82，断线或过期显示 `--`。

### 采样时间和新鲜度

`depthTimestampUs` 原样保存 0x82 帧头里的 D2 读完时刻。它是 H750 设备启动时钟的
32 位微秒值，当前固件分辨率 1 ms，约 71.6 分钟回绕；不是 PC 墙上时间，也不是
USB 接收时刻。界面明确标注单位，并在状态提示中说明回绕。

服务沿用“匹配回复建立设备时钟基准”的机制，以无符号差值处理回绕。深度
`depthAgeMs` 现在包含接收时估算的已有传输年龄和接收后的单调时间，不再把一个
已排队 2.4 秒的样本重新当作有完整 2.5 秒寿命的新数据。这是基于请求回复的时间
估计，不宣称已实现精密时钟同步或精确单向链路延迟测量。

超过 2500 ms 的深度测量失效。0x83 / GET_STATUS 只更新状态和计数，不能延长
0x82 数值的寿命；因此 STOP_STREAM 后即使还有状态/查询回复，旧数值也会过期。
重复或倒序遥测不回写快照；深度 0x82/0x83 共用 TARGET 流序号检查，IMU 保留原分组策略。流序号和时间戳回绕都有测试。

### 采样计数不是流序号

契约中的 `SensorDeviceState::sequence` 仅表示最近遥测流 SEQ；新增 `sampleSequence`
表示设备完成采样计数，`statusKnown` 表示是否收到统计体，`sampleAgeMs` 表示设备
报告并由主机单调时间推进的样本龄。线上 `0xFFFFFFFF` 映射为 -1，显示“尚无样本”，
不显示成几十天延迟。

0x82 与 0x83 共用 TARGET 自己的流序号，背压还可能主动丢弃传感器帧，因此不把
相邻 0x82 跳号直接当成同样数量的丢样；GET_STATUS 的请求 SEQ 不写入流计数。

## 4. 参数和交互

保持现有读写与确认操作，参数类型严格对齐原协议。输出率范围修正为 **1–100 Hz**；
超过型号/OSR 的转换预算仍由设备返回 BAD_VALUE，不在上位机猜一个可用采样率，
也不把被拒绝的配置显示为已经生效。深度频率控件使用深度配置能力 bit11。

原文的深度能力 bit5 为原始深度能力，不应套用旧通用表里 IMU 的 bit5 频率说明。
实测深度 capabilities=0x00000C38，参数配置能力为 bit11。

密度和 K 编辑初值分别对齐协议默认的 1029 kg/m³、0；编辑值不是设备回读值，
不会自动下发。型号允许 0/2/30，OSR、密度、P0、K 保持原协议范围。
成功的型号 GET/SET 会同步设备型号描述；成功写参数或归零后先清除旧数值，等待
产生时间不早于应答时刻的新样本，避免排队旧帧恢复旧型号/旧零点测量。

`GET_PARAMETER 0x0103 → NOT_READY` 在对应反馈列显示
“未采集水面零点（NOT_READY，通信正常）”，不伪造 P0=0，也不标成通信故障。
其他失败也通过原 `SensorParameterFeedback` 的无效 value 和 message 回到反馈列。
失败写入不会被标为“设备确认”，也不会据此改设备型号。

连接时仍沿用原有 GET_INFO 握手与设备默认推流。不会自动选型号、自动归零、重试
写操作、保存 Flash，或在切标签/展开/收起时发送上传开关。当前深度计不支持的
保存、恢复默认、校准、自检、重启没有新增为可用按钮。

现场操作顺序：在设置中连接 USB CDC；在总览展开“IMU / 深度计配置”，切至深度计；
按实物确认 02BA/30BA 并应用型号；等压力和温度有效后，将探头放在水面参考位置，
明确点击“采集水面零点”。确认对话框提醒水下归零会改变基准，定深控制中不应执行。
掉电/复位后参数只保存在 RAM，需重新确认型号和水面零点。尚未完成这些步骤时，
界面保留 `--` 是正确行为，不用软件默认值掩盖未标定状态。

## 5. 第一轮软件验收（历史结果，第二轮见第 8 节）

使用仓库锁定的 Qt 5.15.19、MinGW 8.1.0、CMake 3.28.6、Ninja，构建目录仍为
`build/gui`。

- 主程序 `rov_ui`、全部十项测试及只读硬件探针编译通过。
- CTest **10/10 通过**，含原八项 Bootloader/CAN/电机/记录回归。
- `rov_sensor_protocol_test`：**419 checks / 0 failures**。
- `rov_sensor_page_test`：**69 checks / 0 failures**。
- 最终 `git diff --check` 通过（退出码 0）；MainWindow、导航、Transport、CAN/Bootloader 运行时代码无 diff。

协议测试直接固定了用户协议中的 GET_INFO/GET_STATUS 请求与回复、实测 0x82
整帧，独立校验 CRC、52 字节拆包、101219 Pa、24.53 °C、D1=6308863、D2=7871683。
另覆盖 1/50/51/100 Hz、非法参数、NOT_READY、TARGET 隔离、流/样本序号区别、
真实零值、CONFIG_UNKNOWN、无效 ADC、过期但在线、配置后的旧帧、断线和双回绕。
这些固定字节来自协议文档，**不是本次新抓取的硬件数据**。

页面测试仍构造原 Dashboard/SensorPanel，检查两标签/七行表、默认折叠、请求不误发、
编辑值不被回读覆盖、状态/压力温度/采样时刻显示、超时全部置 `--` 和原控制权限。
初次协议运行有一个依赖定时器精确唤醒时刻的断言，后改为检验实际 2500 ms 阈值，
完整复跑通过；没有放宽产品的新鲜度阈值。

日志：`build/gui/ms5837-host-build.log`、`ms5837-host-full-build.log`、
`ms5837-host-final-build.log`、`ms5837-host-ctest.log`。
`build/gui/sensor-test-artifacts/*fixture.png` 是合成快照截图，不能作硬件数据证据；
offscreen 平台存在中文字体渲染缺失，数值和控件测试结果与实窗截图应分别看待。

## 6. 只读硬件验收入口与第一轮边界（第二轮已恢复通信，见第 8 节）

原探针新增一个明确的只读模式，没有增加默认构建/CTest 硬件副作用：

```powershell
. '.\toolchain\env\activate.ps1'
cmake --build build/gui --target rov_sensor_hardware_probe --parallel 4
.\build\gui\rov_sensor_hardware_probe.exe --port COM11 `
  --output build/gui/ms5837-host-hardware-readonly.json --depth-read-only
```

该模式走正式 SerialTransport → 家族路由 → SensorDataService，只对 TARGET=2
发送 GET_INFO、GET_STATUS 和六个 GET_PARAMETER，不设置型号、归零、开关上传、
写 RAM、写 Flash、操作 IMU 或发送 CAN 控制。未归零的 NOT_READY 是允许结果；
压力/温度是否应有效，按当前设备状态判断，不假设现场型号已设置。

首次只读尝试发现目标 VID_0483/PID_5740、COM11，但在配置串口时失败，未进入
AA5B 收发；该轮 JSON 中没有传感器测量，不能写成实机通过。进一步串口检查出现
打开错误码 5；同时桌面显示固件侧在调试/重新烧录。最后一次复测时，COM11 已未在
目标设备枚举中出现，探针退出码 1。因此本次**没有通过实时硬件收发或水深精度验收**。
没有强制关闭其他进程、重启设备、修改 MCU 工程，或绕开串口失败显示默认值。

硬件证据分别保留：`build/gui/ms5837-host-hardware-readonly.json/.log`、
`ms5837-com11-diagnostic.json`、`ms5837-host-hardware-recheck.json/.log`。
复测需先恢复 USB CDC 枚举并确保串口可用，再运行上面的只读命令。实际水深还需要
用户按实物设置型号、在水面采集零点并进行真实入水对照，不能用协议向量替代。

实际启动本轮 `rov_ui.exe`，窗口正常响应；将目标窗口置前并截屏，确认总览原布局、
六个导航入口、中文显示及未连接时的 `--`，截图为
`build/gui/sensor-test-artifacts/ms5837-live-overview.png`。由于桌面 RPC 接口不可用，
使用只针对该 GUI 进程的 Windows 截屏；没有把后台其他程序截图当作 GUI 验收。

此外单独执行 `rov_sensor_page_test.exe -platform windows`，**69 checks / 0 failures**，
日志为 `build/gui/ms5837-host-page-windows.log`。Windows 模式截图
`ms5837-windows-depth-fixture.png`、`ms5837-windows-dashboard-collapsed-fixture.png`、
`ms5837-windows-dashboard-expanded-fixture.png` 中，原详情区的中文、数值、单位和反馈
正常显示。它们仍使用明确的合成快照、不打开串口，并非本次硬件数据。

## 7. 修改文件与集成

运行时修改仅涉及 `SensorContract.h`、`SensorProtocol.cpp`、`SensorDataService.*`、
`DashboardPage.cpp`、`SensorPanel.cpp`。原 MainWindow、导航、Transport、CAN 和
Bootloader 运行时代码不改。CMake 仅将原硬件探针链接到现有 `rov_sensor_data`，
以便只读模式验证正式服务链；未添加依赖或新的应用目标。

测试为 `SensorProtocolTest.cpp`、`SensorPageTest.cpp`、`SensorHardwareProbe.cpp`。
本说明及 `AA5B_Sensor_Protocol.md`、README 同步维护。保留原功能分支
`feature/sensor-console-20261006`，不切换主线、不覆盖其他工作树。代码备份和构建
产物都在被忽略的 `build/` 下。


## 8. 主工程路径复核与第二轮验收（2026-10-06）

### 8.1 协议版本边界

用户指定的主工程协议副本 SHA-256：
`007A12D536A4720193D04EF2CF9B04214C2E64FAECB8B4CFC47C6B57EF32A0B1`。
该文件没有把第 14 节提案升级为已实现协议，本轮也不实现/发送 0x0D/0x4D，不删除
0x82/0x83、不改变开流行为。`ms5837.md` 第 8 节仍属于集成改动规范，不是固件已经
提供该命令的证据。后续按需模式必须在设备端完成后单独对齐。

### 8.2 在原服务内补齐只读刷新

原 MainWindow 在连接时发送 GET_INFO 的入口保持不变。深度 GET_INFO 成功后，
SensorDataService 用现有 50 ms 定时器逐项发送一次 GET_STATUS 和六个 GET_PARAMETER
（0001、0101、0102、0103、0104、0105），结果回到原状态文字和参数反馈列。
原“读取信息”按钮同样触发这组只读刷新，其 tooltip 已说明。没有增加按钮或页面。

最多保留七个待读取请求、每个 TARGET 仍只允许一个在途请求。读取结束不继续轮询；
发送失败、协议错误、超时等中止剩余只读请求，NOT_READY（未采零点）可正常继续。
断线清理待发读取；接受到明确用户请求时，取消尚未发送的后台读取，优先用户操作。
不存在自动重试写入、自动配置型号、自动归零、自动保存或自动开关流。

显式 ZERO_DEPTH 或成功写入型号后，自动只读查询状态与 P0，防止反馈列仍挂着归零前
的 NOT_READY/旧参数。读取结果不覆盖用户编辑框，不把 GET_PARAMETER 的数值当成
新的遥测样本，不改变原电机控制权限。

### 8.3 两处数据一致性修正

`SensorSnapshot::zeroValid` 表示设备零点配置存在；新增 `surfacePressureValid` 专门
表示已经收到具有 ZERO_VALID 的新鲜 0x82 中的 P0 数值。仅收到 0x83 / GET_STATUS
的 ZERO_VALID，不能把上一帧尚未归零时的 P0=0 占位符变成“有效参考压力”。
原七行表中的 P0 行按新有效位显示，直到真的有对应样本才出现数值。

深度 0x82 和 0x83 使用同一个 TARGET 流序号排序，防止先收到新深度帧后，又被较早的
状态帧回写成旧状态。仍允许背压导致跳号，处理 u32 回绕，不把请求 SEQ、采样计数
和流 SEQ 混用；IMU 分组处理未改。

### 8.4 本轮实际软件结果

- 主程序、两个传感器测试和只读探针构建成功；原八项回归重新链接成功。
- 完整 CTest：**10/10 通过**，0 失败。
- 协议/服务/路由：**492 checks / 0 failures**（第一轮为 419）。
- 原 Dashboard / SensorPanel 页面：**74 checks / 0 failures**（第一轮为 69）。

新增回归覆盖：完整自动读回、未归零后继续读余项、无自动写入、只读队列停止/取消/
断线清理、发送失败、零点配置与 P0 数值分离、0x82/0x83 跨类型旧帧、跳号和回绕。
页面仍为六导航、两传感器标签、七行深度数据表。测试不打开物理串口。

日志：`build/gui/ms5837-revision2-build.log`、`ms5837-revision2-regression-build.log`、
`ms5837-revision2-ctest.log`。

### 8.5 本轮真实 COM11 只读验证（已通过）

与第一轮不同，本次 COM11 已恢复枚举并可打开。修改前、修改后均执行了原探针的
`--depth-read-only`，两次均 **17 checks / 0 failures**。修改后记录 UTC
`2026-10-06T07:51:17Z`，收到 62 个深度帧与 3 个 AA58 心跳。

该次末帧（不是合成向量）：型号 2（02BA），status=0x00000933，压力 101163 Pa，
温度约 23.54 °C，D1=6311791，D2=7841231，样本产生时刻 4236016000 µs，
估计样本龄 16 ms。压力、温度、原始 ADC 有效；ZERO_VALID/DEPTH_VALID 均未置位。
设备累计 errors 为 **2**，修改前后两次读回均为 2，不能写成“设备累计错误为 0”。
探针检查失败数 0 和设备累计错误数 2 是不同计数。

原探针在该模式中显式逐项查询，因此会优先于尚未发送的自动读回。自动读回本身还通过
真实主程序验证：启动新版 rov_ui，展开总览原面板、切换深度计，未点参数“读取”，
即见型号 2、25 Hz、OSR4096、密度 1029、K=0 的设备确认值及 P0 的 NOT_READY。
真实压力/温度/D1/D2 持续刷新，水深与 P0 测量行保持 `--`。编辑框和确认列保持分离。

实际窗口截图（不含 fixture 的以下文件均是本轮真机窗口）：
`build/gui/sensor-test-artifacts/ms5837-revision2-overview.png`、
`ms5837-revision2-expanded.png`、`ms5837-revision2-live-depth.png`、
`ms5837-revision2-live-parameters.png`。桌面 RPC 截图接口失败后，使用限定到本 GUI
进程的 Windows 截屏/输入进行检查；未把其他程序或合成快照当作实机结果。

原始证据：`build/gui/ms5837-revision2-hardware-before.json/.log` 和
`ms5837-revision2-hardware-after.json/.log`。本轮没有写型号、归零、改 RAM 参数、
操作电机、写 Flash 或烧录 MCU。未做实际入水深度对照，仍不能宣称深度精度验收完成。

### 8.6 本轮改动边界与交接

相对第一轮，运行时代码仅改 `SensorContract.h`、`SensorDataService.h/.cpp`、
`SensorPanel.cpp`；增加两个现有测试文件的用例，更新本说明、AA5B 文档和 README。
MainWindow、Dashboard 布局、Transport、CAN/Bootloader、CMake 均未追加改动。
保留第一轮未提交修改、原分支和全部备份，不自动合并/推送。

只读核对时，所给 MCU 主工程目录尚无 `sensor_protocol.*`、`sensor_service.*`、
`sensor_board.c`，因此本次板上 AA5B 收发通过，不能证明该目录重新编译烧录后仍会
提供传感器协议。固件集成人应确认驱动调用与 AA5B 路由；本任务没有代改 MCU 工程。


## 9. 固定 02BA 与 100 Hz 拒绝问题定位（2026-10-06，第三轮）

### 9.1 本轮用户要求与界面改动

用户已明确实物只有 MS5837-02BA。原型号参数行不删除，原下拉框替换为
`MS5837-02BA（固定）` 文本，不再提供 unknown/30BA 选项。原“读取”仍读设备反馈；
原“应用”经确认后仅发 `SET_PARAMETER 0x0105 = 2`，负载 `05 01 02 01 02`。
不自动写型号、不自动归零，不将本机固定型号标签冒充设备确认值；若设备实际读回 30，
设备信息仍如实显示 30BA，以便用户发现配置不一致。原六页、两标签、七行数据表不变。

在原深度命令结果 QLabel 的悬停提示中显示最近请求编码与匹配回复 HEX。
`SensorDeviceState` 增加 `lastRequestHex/lastReplyHex`，由数据服务调用现有编码器生成；
页面只显示字符串，不拼字节或计算 CRC。每个 TARGET 只保留最近一组报文，遥测不覆盖，
不逐帧写日志、不新增窗口。请求 HEX 表示提交给 sender 的编码，发送失败时不能据此
声称串口已经发送成功；RX 只来自匹配 TARGET/CMD/SEQ 的回复。断线清空。

### 9.2 为什么 OSR=256 时 100 Hz 仍被拒绝

本轮只读检查了用于联调的 `CAN_To_Uart/worktrees/sensor-integration` 工作树
（HEAD `b19a70e`，检查时工作区干净），发现服务层和驱动层范围不一致。

`Core/Src/sensor_service.c:104-107`：

```c
if(id==MS5837_PARAM_OUTPUT_RATE_HZ &&
   (f->payload[2]!=4U || f->payload[3]!=2U || Sensor_Read16(f->payload+4)>50U)) {
    complete(token,SENSOR_BAD_VALUE,NULL,0U); return;
}
result=Ms5837_SetParam(id,f->payload[2],f->payload+4,f->payload[3]);
```

也就是说，频率大于 50 Hz 时直接返回 `RESULT=2`，尚未进入 `Ms5837_SetParam`。
改 OSR 无法绕过这个前置上限。与此同时，同工作树 `Core/Inc/ms5837.h:48` 定义
`MS5837_OUTPUT_RATE_HZ_MAX = 100U`，`ms5837.c:1071` 的 `Ms5837_SetOutputRateHz`
按 1..100 Hz 和 `ms5837_schedule_fits` 判断；协议文档也写 1..100 Hz。

用户截图中的 OSR=256 已回读确认，不能再把本次失败泛称为“高 OSR 导致转换预算不足”。
上位机设置 100 Hz 的类型、长度、小端值均符合文档，截图的 BAD_VALUE 与上述 50 Hz
前置拦截一致。前两轮软件测试和只读硬件测试没有覆盖真实 100 Hz 写入，这一遗漏在本轮
明确补充；不能把此前 17 项只读通过解释为 100 Hz 写入已通过。

给下位机集成人的最小修正：将该处 `>50U` 改为 `>MS5837_OUTPUT_RATE_HZ_MAX`，
保留类型/长度校验，实际可用采样周期继续由 `Ms5837_SetParam` / 驱动检查。
不要删掉 OSR 转换预算校验，也不要只改 GUI 范围并声称设备已支持。修正后至少验证
OSR256 下 50/51/100 Hz 成功回显，0/101 Hz 被拒，以及高 OSR 超预算仍被拒。
这属于待集成建议：**本轮没有修改 MCU 源码，也没有烧录或试写设备频率**。
本轮未读取板上固件的二进制版本号，不把源代码定位称为对板上固件的二进制认证。

### 9.3 100 Hz 的完整请求报文

按用户截图 `SEQ=79763011`（十六进制 `0x04C11643`）和当前正式编码器重建：

```text
AA 5B 01 04 01 02 43 16 C1 04 06 00 00 00 00 00
01 00 04 02 64 00 1E 7C 5B AA
```

总长 26 字节：CMD=04 SET_PARAMETER、FLAGS=01、TARGET=02；SEQ 小端 `43 16 C1 04`；
PAYLOAD_LEN=6 (`06 00`)；请求 TIMESTAMP_US=0；CRC=0x7C1E，小端 `1E 7C`，覆盖
偏移 1..21（从 5B 到 VALUE 最后一个字节）。不是 AA55/CAN 封装，直接走 AA5B。

负载逐字段为：

```text
01 00 | 04 | 02 | 64 00
ID=0001  u16  2B   100 Hz
```

这份 HEX 是**按截图序号和当前实现重建，并由正式 Qt 编码器测试通过的报文**，不是
当时串口历史抓包。旧版未保存该次完整原始 TX/RX，不伪造下位机响应时间戳与 CRC。
截图错误文字在 `sensorResultText` 中对应 BAD_VALUE；匹配的回复应是 CMD=44、
FLAGS=06、同 TARGET/SEQ、RESULT=02。新版悬停提示可供后续实际操作核对原始匹配回复。

### 9.4 本轮软件验证

- CTest 10/10 通过；协议/服务/路由 509 checks、页面 84 checks，均 0 failures。
- 新增固定型号控件/申请值测试、设备回读不被伪造测试、独立常量 100 Hz 整帧向量测试，
  以及最近请求/匹配回复、错误序号忽略、遥测不覆盖命令报文、断线清理测试。
- 日志：`build/gui/ms5837-fixed02ba-test-build.log`、
  `ms5837-fixed02ba-regression-build.log`、`ms5837-fixed02ba-ctest.log`。
- 这些测试不打开 COM 口，不证明真实设备已接受 100 Hz；下位机 50 Hz 前置限制仍待修改。
- 主程序 `rov_ui` 与硬件探针重新编译通过，日志 `build/gui/ms5837-fixed02ba-build.log`；
  正常关闭旧上位机后启动新版，窗口标题正确且 Responding=True，没有强制终止其他进程。
- Windows 模式页面测试另跑 84 checks / 0 failures，日志 `ms5837-fixed02ba-page-windows.log`；
  `sensor-test-artifacts/ms5837-fixed02ba-windows-fixture.png` 已检查固定型号文本与原布局，
  截图明确是合成快照，不是本轮实机采集。
- `git diff --check` 退出码 0；MainWindow、导航、Transport、CAN/Bootloader 运行时代码无 diff，
  下位机集成工作树仍保持干净。本轮无参数试写或烧录，原硬件配置和零点未主动更改。


## 10. OSR / 频率联动硬约束（2026-10-06）

### 10.1 本轮依据与最终界面行为

根据用户更新的主工程 `CAN_To_Uart/CAN_To_Uart/docs/ms5837-aa5b-host-protocol.md`
第 5.1 节，02BA 每样本 D1+D2 转换预算在驱动中先把最大转换时间向上取整为 ms，
再检查 `2 × (ceil(Tmax_ms) + 1) + 2 ≤ floor(1000/rate_hz)`。
本轮没有改 MCU 或放松其校验。旧服务层 50 Hz 前置拦截已由固件侧解除，不能再把它
作为当前高 OSR 无法使用 100 Hz 的解释。

| 选定 OSR | 频率编辑框允许范围（整数） |
| --- | --- |
| 256 | 1–100 Hz |
| 512 | 1–100 Hz |
| 1024 | 1–100 Hz |
| 2048 | 1–71 Hz |
| 4096 | 1–45 Hz |
| 8192 | 1–25 Hz |

上限唯一来源是业务契约 `depth02baMaxRateHz()`，页面与服务复用；未知 OSR 返回 0，
不视为合法设置。型号仍固定 MS5837-02BA。没有增加或删除页面、导航入口、表格行或按钮。

OSR 下拉项直接标明对应频率范围。选中 OSR 后，原频率 QSpinBox 立即更新范围，
约束按钮、滚轮和键盘输入；粘贴后解释的值也不能超界。选 512/100 Hz 再改为 8192，
待应用频率自动收至 25 Hz，原说明区域提示自动下调。降低 OSR 时不会擅自把频率拉到最大。
编辑行为只改待应用值，绝不直接发串口命令或伪造设备反馈；回读也不覆盖用户的草稿值。

原 OSR 与频率行的“应用”按钮改为“应用组合”，两者都提交当前两个编辑值，免去
操作员判断设置顺序。原“读取”仍为单参数只读。组合在执行期间，深度计相关操作
持续处于 pending，不能在步骤间隙插入另一条设置或重复启动组合；IMU 不被该保留阻塞。

### 10.2 仍走原分层和原 AA5B 命令

`SensorRequest` 增加主机内部字段 `samplingOsr`、`samplingRateHz`；都为 0 时保留
原单参数语义。OSR/频率行发出的是带两个值的类型化组合意图，不是新的协议命令。
MainWindow 路由不变。SensorDataService 独立验证目标组合，即使调用方绕过界面，
4096/100、2048/72、8192/26 等非法组合也不会发出任何报文。

服务收到显式“应用组合”后执行一次有界流程：

1. 用原 `GET_PARAMETER` 依次读取型号、OSR、频率；必须确认型号=2，并得到合法当前组合。
   不猜设备当前值，也不偷偷设置型号或水面零点。
2. 若目标频率适用于当前 OSR，先改频率再改 OSR；否则先改 OSR 再改频率。
   相同值跳过写入，每一步使用原 `SET_PARAMETER`，等待 TARGET/CMD/SEQ 匹配、RESULT=OK
   且参数类型/长度/回显值精确一致后，才进行下一步。
3. 最后再读 OSR 和频率，均与目标一致才显示“组合已回读确认”。每次最多 3 个预读、
   2 个写入、2 个回读，共 7 条；不另开串口、不停止遥测、不增加高频日志。

安全顺序实例：

```text
4096 / 25 Hz → 512 / 100 Hz：SET OSR=512 → 确认 → SET rate=100 → 确认 → 回读两项
512 / 100 Hz → 8192 / 25 Hz：SET rate=25 → 确认 → SET OSR=8192 → 确认 → 回读两项
```

任一步拒绝、错误回显、发送失败或超时，后续步骤立即取消，不自动重试或反向回滚；
断线清空整个组合，重连不会恢复未完成写入。**这不是固件端原子事务**：如果第一步已确认、
第二步失败，第一步仍可能已生效；界面明确显示组合中断，不能声称整个组合未变化或整体成功。
下一次应用仍先重新读取设备当前值。只有完整回读成功才确认整个组合完成。

线上帧保持原 GET=0x03 / SET=0x04、TARGET=2、原长度和 CRC，无新 CMD 或负载扩展。
纯协议编码器仍只处理单参数，组合编排留在服务层；页面不导入协议或传输层。

### 10.3 本轮实际验证

使用锁定 Qt 5.15.19 / MinGW 8.1.0 / CMake 3.28.6：

- GUI、全部测试和硬件探针构建通过。
- 完整 CTest **10/10 通过**。
- 传感器协议/服务/路由测试：**6150 checks / 0 failures**。
- 页面测试：**123 checks / 0 failures**；Windows 窗口模式同样 **123 / 0**。
- 新增 144 组 OSR 切换组合（6×6×当前最低/最高速率×目标最低/最高速率），模拟设备用
  独立上限表检查每一个中间状态。另覆盖超限不发包、错误型号、第一/二步拒绝、错误回显、
  最终回读不一致、发送失败、超时、重复提交和断线后不继续写入。
- 页面检查六档上限、超限钳制、键盘文本、无隐式请求、组合请求字段、执行中禁用和草稿/回读分离。

Windows 组件截图 `build/gui/sensor-test-artifacts/depth-sampling-windows-fixture.png`
已检查：原深度面板、固定型号、OSR 范围标注、两个应用组合按钮、动态说明正常显示。
该截图是合成快照，**不是本次硬件数据**。

### 10.4 COM11 实机组合设置与恢复

本轮通过现有硬件探针新增的显式 `--depth-sampling-test` 模式，实际运行正式
SensorDataService → BootloaderCommunicationService → SerialTransport 链路。
所有 **7 项硬件检查通过**，没有借用旧日志或仅从合成测试推断实机结果。

测试前回读并保存：型号 2、OSR=4096、输出率=25 Hz。实际执行并收到完整回读确认：

| 操作 | 结果 |
| --- | --- |
| 原配置 → 512 / 100 Hz | 成功，先 OSR 后速率 |
| 512 / 100 Hz → 8192 / 25 Hz | 成功，先速率后 OSR |
| 恢复 4096 / 25 Hz | 成功 |
| 独立 GET 再核对原 OSR/频率 | 完全一致 |
| 水面 P0 配置前后核对 | 完全一致 |

真实发送的 SET 负载顺序如下（完整 TX/RX 和 SEQ 存在 JSON trace 中）：

```text
01 01 04 02 00 02   SET OSR=512
01 00 04 02 64 00   SET rate=100
01 00 04 02 19 00   SET rate=25
01 01 04 02 00 20   SET OSR=8192
01 01 04 02 00 10   SET OSR=4096（恢复，rate 已是原 25）
```

只改变并恢复 OSR/速率 RAM 配置，未改型号、零点、密度、滤波、上传开关、IMU、CAN
或 Flash，未烧录 MCU。测试验证的是组合应用/回读链路，不是重新测定全部档位的实际帧率，
也不是水深精度标定。前述所有档位上限依据用户协议，当前硬件实测范围以上表为准。

```powershell
. '.\toolchain\env\activate.ps1'
.\build\gui\rov_sensor_hardware_probe.exe --port COM11 `
  --output build/gui/depth-sampling-hardware.json --depth-sampling-test
```

该模式有显式 RAM 写入，仅在端口空闲且允许测试时运行；它不属于默认构建或 CTest。
日志：`depth-sampling-build.log`、`depth-sampling-full-build.log`、`depth-sampling-ctest.log`、
`depth-sampling-page-windows.log`、`depth-sampling-hardware.json/.log`，均在 `build/gui/`。

### 10.5 修改边界

运行时仅增量改 `SensorContract.h`、`SensorDataService.*`、`SensorPanel.*`；测试为
`SensorProtocolTest.cpp`、`SensorPageTest.cpp`、原 `SensorHardwareProbe.cpp`。
README 和 AA5B 说明同步维护。没有改变 MainWindow、导航、DashboardPage、Transport、
CAN/Bootloader 运行时代码或 MCU 工程；不引入依赖、不新增构建目标，保留本轮前的用户修改。
源文件备份保存在忽略目录 `build/depth-sampling-backup-*`。未提交、未推送、未合主线。


## 11. 隐藏总览诊断说明与启动水面参考（2026-10-06）

### 11.1 用户要求与显示

用户要求不再在总览显示来源/压力/时间戳/IMU 引脚保护这段说明；默认启动时的压强
就是水面参考，需要校准时再手动操作。本轮隐藏原 dashboardSensorSources 标签，
其不再占据布局空间；数值卡片、六个导航、传感器详情及七行测量表全部保留。
源信息仍可通过深度/姿态卡片的悬停提示和原详情查看。

顶部“设备离线”此前只依赖电机在线数：电机全未接时，即使深度计和网关已在线，
也会显示笼统的离线。本轮改为从现有服务快照显示“电机节点在线”“深度计在线”
“IMU 在线”；仅串口已打开但没有在线节点时显示“网关已连接”，真正断开时显示
“设备离线”。该变动只影响展示；dashboardFromMotorFleet 的连接及控制权限不改，
传感器在线不会解锁电机，电机/IMU 自身离线也没有被改为在线。

### 11.2 启动次序与安全边界

MainWindow 装配时启用 SensorDataService::setDepthStartupEnabled(true)。服务本身默认
关闭该策略，原独立只读硬件探针/采样配置测试不因此产生额外写入；不是新 UI 开关。

1. 新连接先执行原 GET_INFO、GET_STATUS 与参数读取；未知描述之前不写设备。
2. 本机探头已经由用户确定为 02BA。读回型号不是 2 时，使用现有 SET_PARAMETER
   0105=2 配置一次，等待确认；不修改 OSR、输出率、水密度和滤波系数。
3. 若同一设备已经报告在线且 ZERO_VALID，保留已有参考，不再次归零。这保证
   在水下重新打开 GUI 或 USB 重连时，不把当前位置重新当成水面。
4. 若没有零点，等待 ONLINE / PROM_VALID / MODEL_CONFIRMED / PRESSURE_VALID，
   且没有 CONFIG_UNKNOWN 的真实 0x82。压力必须新鲜（沿用 2500 ms 阈值）、有限，
   在协议水面范围 10000～200000 Pa 内，然后用现有 ZERO_DEPTH 捕获当前压力。
5. ZERO_DEPTH 每连接最多自动发一次；ACK 不是测量，不据此填 0 m。旧数据先失效，
   直到归零后产生的新鲜深度帧到达，才显示有效的设备深度，并按原流程回读状态与 P0。
6. 用户显式应用型号、P0、手动归零或停止上传会取消尚未执行的启动归零。
   OSR/频率组合优先执行，初始化不会插入组合事务中；单纯编辑和切页不发命令。
7. 启动等待窗口为连接后 10 秒。始终没有有效数据时停止自动初始化，详情保留说明，
   用户可手动处理；命令被拒/发送失败/超时均不循环重试、不自动回滚、不冒充成功。
8. 电源复位后零点丢失，下一次连接重新执行缺失参考的初始化；已经建立的手动参考
   或当前运行中的有效参考不被反复重设。

这是**GUI 连接时的应用策略**，未修改或烧录 MCU。以当前压力为水面需要探头在水面
启动；首次在水下启动而没有历史零点，会把那个位置作为 0 m，之后应在真实水面使用
原“采集水面零点”重新校准。界面不夹紧负水深，不使用固定 101325 Pa 冒充现场压力。

### 11.3 软件验证

主程序与原测试目标使用锁定工具链构建通过；CTest **10/10**，传感器协议/服务/路由
**6830 checks / 0 failures**，页面 **125 checks / 0 failures**。Windows 模式页面测试
同样 125/0；`git diff --check` 退出码 0。日志：`build/gui/depth-startup-app-build.log`、
`depth-startup-ctest.log`、`depth-startup-page-windows.log`。

COM11 本轮只读实机检查 **16/16 通过**，例：model=2、status=0x9F3、errors=0、
pressure=101202 Pa、temperature≈22.59 °C、P0=101325 Pa、depth≈-0.012189 m。
设备已有有效零点，因此没有清掉它来强行触发冷启动归零；本轮未做真实断电/缺失零点
自动归零的硬件试验，该分支由下面的模拟测试覆盖。真实数据与既有参考保留已验证。
原始记录：`build/gui/depth-startup-hardware-connection.json/.log`。

已实际启动新版 GUI，窗口正常响应；实窗确认红框诊断文字消失、六个导航保留、
深度显示真实测量、顶栏为绿色“深度计在线”，电机 8 个仍离线且控制权限不可用。
截图：`build/gui/sensor-test-artifacts/depth-startup-live-overview.png`。
桌面 Snapshot RPC 接口失败，实窗截屏通过已核对 PID/路径的 Windows 截屏脚本取得。
带 `fixture` 的截图仍为合成快照，不当作硬件测量。

测试包含：先等握手；固定型号初始化；旧帧、
离线和压力越界不归零；零点 ACK 不伪造数值；新深度帧恢复显示；只执行一次；
既有零点/水下重连不覆盖；实际电源状态复位后重新建立；手动校准优先；拒绝/超时
不重试；只读探针默认不启用。页面检查数据刷新前后诊断条始终隐藏。

### 11.4 修改范围

`SensorDataService.h/.cpp` 新增明确启用的启动策略和有限状态；`MainWindow.cpp`
启用该策略并仅修正顶栏状态汇总；`DashboardPage.cpp` 隐藏指定说明；
`SensorPanel.cpp` 更新原参数说明。更新两项既有传感器测试、README 和本文档。
未更改通信协议字节、SerialTransport、Bootloader、电机控制/权限、导航或 CMake 目标。


## 12. 分批交付状态（2026-10-06）

深度计运行时代码、对应测试及本接入说明已在 `976bc71` 提交并推送到
`origin/feature/sensor-console-20261006`。上文未提交等状态为当时的阶段记录。
临时补丁脚本、阶段备份和编译中间产物已清理；主程序、运行库、正式测试入口、实机采集
及成功/失败验收记录保留。清理后最终版本完整 CTest 10/10 通过。
完整批次、清理边界和保留产物见 [`分批交付与清理说明`](sensor-console-release-20261006.md)。
