# IMU AA5B GUI 接入与验证（2026-10-06）

## 1. 依据与范围

依据本轮用户附件 `imu-aa5b-host-protocol.md`，原样副本保存在同目录。
附件 SHA256：`90eef8105d4e4ef53916a2fd38cb8ae86419a9ac83235abea64e955d1b5cbcd0`。
该副本与 MCU 主工程 docs 中的同名文件逐字节一致；仅复制文档，不修改下位机源码。

依然只有原六个导航页面；IMU 详情位于总览原 `SensorPanel` 的 IMU 标签。
MainWindow 装配、总览布局和红框诊断条隐藏策略、深度计启动 02BA/缺失零点初始化、
OSR/频率联动及回读顺序、手动水面校准、CAN、Bootloader、记录链不做更改。
本轮没有新增依赖、CMake 目标、串口、线程或应用级服务。

```text
现有 USB CDC → BootloaderCommunicationService 家族路由 → SensorProtocol
           → SensorDataService → SensorSnapshot / SensorParameterFeedback → 原总览/IMU 标签
原参数和上传按钮 → SensorRequest → 同一服务与协议链
```

## 2. UI / 命令映射

| 原有位置 | 当前行为 |
| --- | --- |
| 总览横滚/俯仰/航向 | 保留有效整机值优先，否则显示 IMU 欧拉角；不解锁电机权限 |
| IMU 设备信息 | 设备返回名称/版本/能力；model 必须为 0，不从模式或数据猜硬件型号 |
| 原始数据表 | 0x80：加速度 g、角速度 rad/s、磁场仅标“协议单位” |
| 姿态数据表 | 0x81：WXYZ 四元数、RPY 从弧度转换为度；分别按有效位显示 |
| 参数反馈列 | 明确区分“UART 已下发 / 未确认生效”和“最后下发缓存 / 非设备读回” |
| 原参数卡片下方 | 新增轻量文字：原始实收帧率、合并姿态实收帧率、最近一次频率观测结果 |
| 原命令结果悬停 | 最近请求编码与匹配回复 HEX；不逐条显示高频遥测日志 |

原来的 IMU 硬件校准、清除校准、保存和恢复控件已移除。
即使 capability bit7/bit8 或其他能力位被置位，也不会重新出现这些入口。
`makeSensorRequestPayload` 对 IMU 的 CALIBRATE/SAVE_CONFIG/RESTORE_DEFAULTS/
SELF_TEST/REBOOT 拒绝构造请求；硬件校准的 type/action 组合均不可通过业务服务发送。
保留协议枚举用于说明/解码，并不代表 GUI 支持执行。

IMU 频率仍为 10–100 Hz；算法只可显式选择 6 或 9，初始显示“未选择（不下发）”。
编辑、切标签、展开和收起不发送命令；缓存读取不会覆盖用户输入。
PIN_BLOCKED 按当前状态禁止配置 SET，页面和服务均保护，但允许 GET 和上传控制，
已带有效标志的接收数据不受配置发送保护影响。

## 3. 握手和参数语义

每次连接保持现有 GET_INFO 握手；成功后 IMU 自动执行一次：

```text
GET_STATUS → GET_PARAMETER 0001 → GET_PARAMETER 0003 → 结束
```

只读查询复用已有 50 ms 服务定时器与每 TARGET 一个在途请求，不持续轮询，
不自动设置频率/算法、不切换上传、不操作校准。NOT_READY/UNCONFIRMED 均为可继续的
预期结果；发送错误、超时或协议错误停止剩余查询，断线清空旧会话的只读队列。
用户显式 IMU 操作优先于尚未发送的 IMU 发现查询，不取消深度计的独立工作。

GET_INFO：31 B 负载（RESULT + 30 B 描述），type=1、model=0、capabilities/name/firmware
原样解释。版本不是 GUI 写死的 1.0.0，未知时仍显示对端实际返回内容。
GET_STATUS：21 B 负载（RESULT + 20 B 统计）；0x83 只有 20 B，不含 RESULT。
附件 §6 首句“20 字节统计体（RESULT + ...）”有表述歧义，本实现按其命令表中的
21 B 回复，以及被引用的深度协议与真机回复区分 RESULT 和统计体，不多读/少读一个字节。

IMU 参数 GET/SET 必须返回 RESULT=7 + 精确参数元组；NOT_READY 表示尚无下发缓存，
显示“通信正常”，不填入伪造的 0。结果 7 不设置 confirmed，即使观测频率吻合也不改变。
成功/错误回复仍严格匹配 TARGET/SEQ/CMD/参数 ID/类型/长度；SET 的回显值必须与请求一致。

FLAGS 兼容说明：现有通用 GUI 文档曾写 UNCONFIRMED 使用 06，但实际固件和本轮真机
使用 02。仅对 RESULT=7 兼容 02/06 两种标志；RESULT=0 仍须 02，其他错误仍须 06。
任何 IMU 参数 OK 回复都不会冒充原生读回：当前文件明确无原生 ACK，业务层拒绝该不符格式。

## 4. 修改频率后的真实观测

SET 0001 得到匹配的 UNCONFIRMED 后，立即清空旧原始流计数，观察约 2000 ms。
只计入有效、去重的 0x80，不使用 0x81，也不使用 TARGET 共用的 SEQ 差或 sample_seq
差来推算原始帧数。0x81 的频率单独显示，两个姿态来源独立到达时可能约为原始流两倍。

用收到的样本源时间戳计算 `(N-1) × 1e6 / (last_us-first_us)`，无符号差处理 u32 回绕。
PC 单调时钟仅确定观察窗口及到达间隔，不冒充设备时间戳；只接纳本次 SET 回复时刻之后
的样本。两个滚动窗口各最多 512 条，覆盖当前 100 Hz raw / 约 200 Hz attitude 的 2 秒窗口。
相同 SEQ 的重发不增加计数；同毫秒独立合并姿态帧可分别计数，不通过数值变化判断新样本。

以下是本次 GUI 采用的**观测判据，不是器件精度或协议新增字段**：

- 足够证据：观察窗口内至少 2 帧，源时间跨度及主机到达跨度均不少于 1500 ms，末值仍新鲜。
- 一致阈值：观测与请求的差不超过 `max(1 Hz, 请求值的10%)`。
- 一致：显示“观测一致”，同时注明“仅流量验证，参数仍无原生 ACK”。
- 不一致：显示实际速率及“未确认生效，可能未生效或链路丢帧”；不把丢帧直接断言为配置失败。
- 无流/样本不足：显示未确认，不自动 START_STREAM、不重发设置。

新频率请求取代旧观测；STOP TARGET=1 成功或断线取消观测，STOP TARGET=2 不影响它。
观测不是第二个设备回复，不额外发一次虚假的 commandFinished(OK)。GET 缓存也不会启动观测。
最近一次结果是历史诊断；实时帧率在失去新鲜数据时显示 --，不能拿历史吻合当作当前数据仍在线。

## 5. 有效性和已知边界

三组测量分别使用 RAW_VALID / QUAT_VALID / EULER_VALID；CONFIG_UNKNOWN 仅表示
速率/模式缓存未知，MODEL_CONFIRMED 不用于 IMU 门控，不能套用深度计的型号规则。
HOST 对三个测量组使用 500 ms 看门狗（参照附件 raw 新鲜度），各自加上估计的 USB 交付年龄；
状态查询和另一组数据不能延长旧测量寿命。深度计原 2500 ms 阈值保持不变。
设备在线状态仍使用收到的 ONLINE 位及原链路状态去陈旧机制，与本地是否暂停转发分开。

0x81 只有一个合并帧头时间戳，没有分别标注“本次更新的是哪一组”和两组原生采样时刻。
GUI 按有效位分别使用字段，并保存最近携带有效字段的帧龄；不凭浮点值是否变化推断新旧，
也不声称能从此帧恢复四元数/欧拉角各自精确的原生采样时间。原始流不会刷新姿态帧龄。
非有限数、畸形长度、非法四元数、重复/倒序同类帧继续按原协议/服务校验拒绝。

本协议声明校准应在上位机软件层面进行，但没有给出软件校准算法、参数或验收流程。
**本轮没有擅自增加软件零偏/旋转补偿算法，也没有伪装成已经完成 IMU 校准**；
现有显示仍是 IMU 数据及明确单位转换。软件校准是另一个需要定义输入/输出的任务。
IMU 气压计字段不会被当成水下深度；附件未定义新的气压计 AA5B 遥测负载，本轮不扩展它。

## 6. 验证入口与结果

```powershell
. '.\toolchain\env\activate.ps1'
cmake --build build/gui --target rov_ui rov_sensor_protocol_test rov_sensor_page_test rov_sensor_hardware_probe --parallel 4
ctest --test-dir build/gui --output-on-failure
.\build\gui\rov_sensor_hardware_probe.exe --port COM11 --output build/gui/imu-readonly.json --imu-read-only
```

只读模式只发 GET_INFO/GET_STATUS/GET_PARAMETER；深度启动策略在探针中默认关闭。
`--imu-rate-test` 是显式写入测试：先取得与实收速率一致的原始频率缓存，并确认无 PIN_BLOCKED，
才能设置测试频率、验证约 2 秒，然后恢复原频率并重新观测。不满足前提就记录跳过，不冒险改写。
未加入 CTest 或默认启动路径。

本轮实测数据与源文档中的历史数据分开记录：

| 项目 | 本轮结果 |
| --- | --- |
| COM11 只读测试 | 13 项检查 / 0 失败 |
| COM11 频率写入、观测与恢复 | 15 项检查 / 0 失败 |
| 实际版本、型号、状态 | 1.0.0 / model=0 / 0x43F |
| 初始原始 / 合并姿态实收速率 | 24.860 Hz / 50.177 Hz |
| 设置 50 Hz 后观测 | 49.744 Hz，UNCONFIRMED 不变 |
| 恢复原 25 Hz 后观测 | 24.873 Hz，已恢复 |
| 错误累计 | 0（当前只读观测值） |

本轮真实的频率命令（不是按截图重建）：

```text
SET 50 Hz TX: AA 5B 01 04 01 01 3A 75 A9 CA 06 00 00 00 00 00 01 00 04 02 32 00 D4 DB 5B AA
SET 50 Hz RX: AA 5B 01 44 02 01 3A 75 A9 CA 07 00 80 D5 37 81 07 01 00 04 02 32 00 C9 7E 5B AA
RESTORE 25 Hz TX: AA 5B 01 04 01 01 3B 75 A9 CA 06 00 00 00 00 00 01 00 04 02 19 00 EC A9 5B AA
RESTORE 25 Hz RX: AA 5B 01 44 02 01 3B 75 A9 CA 07 00 20 25 57 81 07 01 00 04 02 19 00 77 54 5B AA
```

数据、完整命令 trace、限定的遥测 trace 与恢复证据保存在
`build/gui/imu-host-hardware-readonly.json/.log`、`imu-host-hardware-rate.json/.log`。
本轮不发送 IMU 模式设置、硬件校准/清除、复位/保存，不修改深度配置或 MCU 固件。
这不是 IMU 姿态精度或软件标定验收。

主程序、全部既有测试目标和探针使用锁定 Qt 5.15.19 / MinGW 8.1.0 构建通过。
完整 CTest **10/10**，协议/服务/路由 **7091 checks / 0 failures**，
页面 **165 checks / 0 failures**。Windows 模式页面测试同样运行通过。
初次链接测试程序遇到一次输出文件 Permission denied；确认无对应运行进程后重试成功，
保留初次失败日志，不将其误记为测试断言失败。

测试覆盖参数无缓存/未确认、禁止硬件操作、PIN_BLOCKED 与被动 RX、型号不推断、单位、
40/32/20/21 B 区分、独立有效位、500 ms 组龄、交付延迟、时间戳/SEQ 回绕、重复帧、
两倍姿态流不冒充原始频率、真实 2 秒吻合/不符/无流窗口、停止/重连、深度启动与采样组合回归。
自动化使用合成帧；fixture 图片不是实机测量证据。

## 7. 修改文件

运行时：`SensorContract.h`、`SensorDataService.h/.cpp`、`SensorProtocol.cpp`、`SensorPanel.h/.cpp`。
测试：`SensorProtocolTest.cpp`、`SensorPageTest.cpp`、`SensorHardwareProbe.cpp`。
文档：本说明、原样 IMU 协议副本、README、通用 AA5B 说明。
MainWindow、DashboardPage、CMake、串口传输、CAN/Bootloader/记录器在本轮相对备份无改动。
保留当前 `feature/sensor-console-20261006` 分支和原有未提交工作，不合并、不推送。


## 8. 实窗检查与最终状态

实际启动新版 `build/gui/rov_ui.exe`，窗口正常响应；在原总览展开 IMU 详情，确认
原始九项、四元数、欧拉角和两个实收帧率实时刷新，硬件校准/清除/保存/恢复控件不存在，
深度计标签保留。总览原说明块仍隐藏，没有新增导航页。

实窗截图保存在 `build/gui/sensor-test-artifacts/imu-host-live-overview.png` 和
`imu-host-live-panel.png`；`imu-host-windows-fixture.png` 是合成快照测试图，不能当成实机数据。
实际 IMU 面板当时显示原始约 24.8 Hz、合并姿态约 50.2 Hz。

注意时间区别：前面的硬件探针测试读到固件版本 1.0.0、频率缓存25，50→25 Hz 写入观测通过；
之后实际 GUI 检查时，对端报告 PIN_BLOCKED、版本 unknown、参数缓存 NOT_READY，但有效的
原始/姿态接收仍在刷新。GUI 因此只禁用参数写入，不编造版本或缓存值；该状态不等于 CDC
收包失败。本轮没有为解除保护而改管脚、复位、烧录或启动其他设备配置。需以当前设备实际
状态判断是否允许发送，不能把先前测试时的状态当作所有时刻都成立。

完整最终回归：CTest 10/10；协议/服务/路由 7091 checks / 0 failures；页面 165 checks /
0 failures，Windows 模式页面同样165/0。集成测试中一处重名函数已拆为不同边界测试入口，
保留两组覆盖后重新编译并完成全量回归。最终 `git diff --check` 通过；程序和文档均保留在
当前功能分支工作区，没有提交、合并或推送。


## 9. 收尾复核时间线（同日，保留成功和未通过记录）

以下时间为现场 Windows 本地时间 UTC+08:00，不用较早的通过结果替代后来的现场状态：

| 时间 | 记录 | 结果与含义 |
| --- | --- | --- |
| 22:30:17 | `imu-host-hardware-rate.json` | 阶段实机验证 15/15；设置 50 Hz 实收 49.744 Hz，恢复 25 Hz 实收 24.873 Hz，`restored=true`。该记录的 TX/RX 见第 6 节。 |
| 22:36:42 | `imu-host-readonly-final.json` | 最终程序只读复查 13/13；版本 1.0.0，状态 0x43F，原始 24.848 Hz、合并姿态 50.152 Hz，错误计数 0；频率仅缓存 25，算法缓存 NOT_READY。 |
| 22:38:07 | `imu-host-rate-final.json` | 这一次复测 13 项中 4 项未通过：收到的 IMU 状态为 0x400，raw/attitude 帧计数均未增加，设备样本龄已约 41 秒并继续增大；深度和 AA58 心跳仍有。由于没有可验证的当前频率基线，探针没有发送任何 SET。不能把本轮这一尝试写成频率设置成功，也没有仅凭此推断停报的根本原因。 |
| 22:41:32 | `sensor-test-artifacts/imu-host-live-panel.png` | 实窗已再次显示有效原始/四元数/欧拉角，实收约 24.8 / 50.2 Hz；同时对端报告 PIN_BLOCKED、版本 unknown、参数无缓存。页面准确禁用写入而继续显示接收数据。 |
| 22:42:13 | `imu-host-last-readonly.json` | 再开独立探针时 COM11 返回 Win32 错误 5，独占打开失败，未进入收发；不强制关闭其他程序或抢占串口。 |

最后完整软件回归以 `build/gui/imu-host-final-ctest.log` 为准：10/10，
协议/服务/路由 7091 项、页面 165 项，均 0 失败；
Windows 窗口模式以 `imu-host-page-windows-final.log` 为准：165/0。
`imu-host-before-tests.log` 保留迁移前的旧测试预期失败（旧测试仍要求硬件校准/保存入口）；
`imu-host-completion-build.log` 保留追加边界测试时的一次函数重名编译失败。
这些问题已修正，并非用删除失败记录或跳过原有深度/CAN 回归获得通过。

当前现场是否有可写的 IMU 通路仍须看最新 PIN_BLOCKED 和有效位；GUI 不解除硬件保护，
不自动设置 IMU 频率/算法，更不发送硬件校准。测量原始值、参数缓存和主机帧率观测始终分开。
