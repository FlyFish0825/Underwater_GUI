# Bootloader 功能说明

本文档说明 ROV Qt 上位机中固件升级页与 STM32G431 Bootloader 的关系、通信链路和当前实现状态。协议编号、字段含义和 CRC 以底层工程 `CAN_FD_IAP\CAN_FD_IAP` 的 V1.3 文档为准，GUI 不重新定义协议。

## 1. 通信链路

### V1.3.1 单节点窗口流控与 AA59 网关流控

WRITE 回复 Byte6 非零时启用单窗口发送，Byte4~5 总包数必须与固件匹配。
按回复的窗口大小（当前为 64 个逻辑包）发送后停止 DATA，等待 `0x32 WINDOW_STATUS`。
每包仍为 56 字节固件数据，编码为固定 64 字节 Bootloader DATA 包，尾包用
FF 补齐。连续 APP 数据不再通过 AA55 加固定延时发送，而是使用 AA59：先发
BEGIN，获得固定初始 Credit=16，再按 Credit 发送 DATA_BLOCK；收到累计
FLOW_ACK 后补充 Credit，最后一块确认完成后才发送 END。Classic CAN 的每个
64 字节逻辑块由 H750 自动拆成 `0x100~0x107` 八帧；CAN FD 每块对应一帧。
界面进度按已确认写入并回读的包数更新。

500 ms 未收到确认时先查询 0x32；查询显示窗口未完整提交则重发当前窗口。
可用窗口为零时继续等待/查询。连续五次查询/重发无进展后停止并提示重新下载，
不会自动擦除。只有 NextSequence 等于总包数才发送 WRITE_END，缺包为零后才校验。
WRITE_END 返回 WRITE 忙状态时重新等待/查询窗口。Byte6=0 保持旧协议兼容。

```text
FirmwarePage
    ↓
BootloaderService
    ↓ 生成 CAN payload
BootloaderCommunicationService
    ↓ 控制帧 AA55 / 连续 DATA 使用 AA59 Credit/ACK
USB CDC 虚拟串口
    ↓
STM32H750 CAN_To_Uart
    ↓ CAN / CAN FD
STM32G431 Bootloader
```

Bootloader 协议层只处理 CAN payload，不知道 COM 口、USB CDC、AA55、AA59
或 Qt 控件。USB CDC 接收入口先按协议族和长度统一拆包，再分别交给 AA55、
AA58 和 AA59 解码器，避免固件数据内部出现 `AA55` 时被误认为新帧。

## 2. USB CDC 与心跳

- VID：`0x0483`
- PID：`0x5740`
- 产品：`Lamost UnderWater Robot ComPort`
- 启动后每秒扫描一次匹配设备。
- 找到设备后自动连接第一个匹配串口。
- 设备拔出、串口错误或心跳超时后自动重新搜索。
- `AA 58` System PING 独立解析，当前为 25 字节（携带 5 个缓冲区占用率），
  同时兼容旧版 20 字节格式，使用 CRC16-CCITT。
- 心跳不显示原始数据，只更新在线状态和心跳计数。
- 心跳约 2.5 秒未到时显示红色离线状态。

## 3. CAN Bootloader 帧

### Host CONTROL

- CAN ID：`0x000`
- 固定 8 字节：`Target, Command, Byte2, Param0..Param3, CRC8`
- CRC8：多项式 `0x07`，初值 `0x00`，计算 Byte0~Byte6。

### Node RESPONSE

- CAN ID：`0x500 + Node_ID`
- 固定 8 字节：`Node, Command, Status, Data0..Data3, CRC8`

### Peer Control

- CAN ID：`0x600 + Source`
- 固定 8 字节：`Target, Command, Source, SessionLo, SessionHi, ValueLo, ValueHi, CRC8`
- 默认只监视；协议开发模式下才允许手工注入。

## 4. 命令中心

固件页左侧“节点控制”提供常用 Host 命令：

```text
GET_VERSION    0x01    GET_DEVICE_ID  0x02
GET_INFO       0x03    ENTER_BOOT     0x04
GET_STATUS     0x30    JUMP_APP       0x20
RESET          0x21    ABORT          0x18
```

“高级命令…”包含全部 Host 命令入口：

```text
SET_GUARD、RELEASE_GUARD、SESSION_BEGIN、SESSION_CRC32
ERASE、WRITE、READ、VERIFY、WRITE_END
MISSING_COUNT、MISSING_ITEM、PROVIDER_GRANT
```

命令中心还列出 Peer/Autonomous 命令。Peer 报文默认只解析、显示和记录；打开“协议开发模式”后，才可以选择 Source、Target、Session 和 Value 发送 Peer Control。

固件页顶部的“读取并保存 BIN”在单节点、多节点顺序升级和协议调试三种模式中共用。
选择节点与本地保存路径后，`BootloaderFirmwareReader` 自动发送 `ENTER_BOOT`、
探测 Bootloader，再用现有 `READ` 命令读取配置页中的 APP 长度和 CRC32，随后顺序读取
`0x08005000` 起的 APP 镜像。配置和整份镜像均通过 CRC32 校验后才保存为 `.bin`；
取消、通信超时或校验失败不会留下导出文件。读取期间与升级、调试命令互斥。
设备读取后停留在 Bootloader，可从节点信息卡执行复位以返回 APP。

## 5. APP 与 Bootloader 返回流程

APP 和 Bootloader 使用同一套 Host CONTROL 协议。APP 阶段接收 `ENTER_BOOT`
（命令 `0x04`）时，不发送 ACK，而是写入 `TAMP->BKP0R = 0x544F4F42`，执行
数据同步屏障后调用 `NVIC_SystemReset()`。Bootloader 复位后检测到该标志，
会停留在 Bootloader 等待上位机命令。

Node1 的普通返回示例：

```text
CAN ID: 0x000
DATA:   01 04 00 00 00 00 00 7B
```

该帧仍然要求标准数据帧、Classic CAN、DLC 8、1 Mbit/s；上位机不切换
CAN 速率，也不能等待 APP 的 ACK。GUI 发送后应记录“已发送，等待复位”，
而不是把没有 ACK 当成发送失败。

### Trial Jump

Trial 使用同一个 `JUMP_APP` 命令（`0x20`）的 Byte2，不新增命令号：

```text
1. 发送 Trial Jump：01 20 01 00 00 00 00 86
2. APP 完成 CAN 初始化后，上位机发送：01 04 00 00 00 00 00 7B
```

Bootloader 识别第二步为 Trial 成功，恢复 `app_valid=1`，但仍停留在
Bootloader 等待后续命令。上位机不提供 Byte2=0x00 的直接跳转：任何从
Bootloader 启动 APP 的操作都固定使用 Trial（Byte2=0x01）。Bootloader 会在
Trial 前开启自身 IWDG；上位机在 APP 完成 CAN 初始化后连续发送 ENTER_BOOT，
只有 APP 主动复位回 Bootloader 且 `app_valid` 恢复为 1 才算成功。成功后本次仍
停留 Bootloader；下一次复位或上电由 Bootloader 自动启动已验证的 APP。如果 APP
无法返回，IWDG 会把 MCU 拉回 Bootloader，APP 保持未验证，避免软变砖。

### 固件前置条件

APP 必须使用 Boot-Release 构建，并链接到 `0x08005000`。当前参考镜像为：

```text
F:\file\BaiduSyncdisk\Project\Observer_Motor\build\Boot-Release\Observer_boot.bin
```

这两个流程与 USB CDC 心跳不同：心跳只证明 H750 与上位机链路在线，不能
替代 G431 的 `ENTER_BOOT`/Trial 状态确认。

## 6. 节点状态解析

收到有效响应后，页面会按当前目标节点更新：

- Bootloader 版本：由 `GET_VERSION` 的 Data0~Data2 组成；
- APP / Config：由 `GET_INFO` 的有效标志更新；
- Status / Last Error / Progress：由 `GET_STATUS` 更新；
- 右侧节点表在线状态与左侧目标节点保持同步。

错误回复会同时显示中文含义和原始错误码，当前映射如下：

```text
0x01 帧校验失败        0x02 帧长度错误        0x03 地址非法
0x04 状态不允许        0x05 Flash 擦除失败    0x06 Flash 写入失败
0x07 配置数据错误      0x08 APP 镜像无效      0x09 数据大小错误
0x0A 升级保护锁定      0x0B 数据顺序错误      0x0C 镜像 CRC 不匹配
0x0D 数据提供者错误    0x0E 设备忙            0x0F 受保护区域
0x10 操作已中止        0x11 接收缓冲区溢出    0x12 升级会话错误
0x13 协调器错误        0x14 协同恢复失败      0x15 提交失败
0x16 APP 试运行超时
```

日志先显示中文结果，再显示原始 `RX/TX` 文本和 CAN 数据；历史记录保存原始事件，打开时重新解析，因此协议字段调整后历史内容也能获得最新中文解释。

原始 CAN 数据会紧跟在中文结构化结果下方显示，便于现场排查，同时避免把二进制内容混入中文摘要。

实时日志区域是只读滚动文本框，默认保留最近 100 条事件；点击“清屏”只
清理当前显示缓冲，不会删除历史。点击同一卡片中的“历史记录”可打开独立
窗口查看全部已保存事件。

历史文件由 `FirmwareHistoryStore` 写入 Qt `QStandardPaths::AppDataLocation`
目录下的 `firmware_history.json`，不放入 `build/`，也不会提交到 Git。

## 7. 当前实现与后续工作

### 已实现

- USB CDC 虚拟串口 VID/PID 搜索和自动重连；
- AA55 CAN 网关帧增量拆包和 CRC8；
- AA59 连续数据 BEGIN/DATA_BLOCK/END、CRC16、初始 Credit=16、累计 ACK、
  重复 ACK 去重、状态码与超时处理；
- AA58 心跳校验和在线状态；
- Host CONTROL / Node RESPONSE / Peer Control 编解码；
- 常用命令发送和响应解析；
- Bootloader 命令中心与 Peer 监视；
- 固件页节点选择和结构化节点状态。
- Legacy 单节点正式下载：`ENTER_BOOT → ERASE → WRITE → DATA → WRITE_END → VERIFY → Trial JUMP_APP → ENTER_BOOT 返回验证`；
- 64 字节逻辑 DATA 包、56 字节有效载荷；正式下载固定由 H750 拆成 Classic CAN `0x100~0x107` 八片分片；
- APP DATA 通过 AA59 Credit/ACK 发送，不再依赖固定毫秒延时；
- CRC-32/MPEG-2 计算、下载阶段、数据包进度和设备错误码显示。

### 当前限制

- 自动缺包修复（设备在 `WRITE_END` 报告缺包时，当前版本会停止并保留错误信息，避免盲目跳转）；
- 多节点可按 Canary 优先、Guard 最后串行复用单节点安全下载；设备侧自治升级、Provider/Coordinator 修复轮次和 Guard/Rollback 尚未接入；
- Trial 启动状态的真实回读；
- Jetson Nano/TCP Transport。

“编程”和“下载到选中节点”按钮现在执行真实的 Legacy 单节点下载。固件页上方的“升级总线”
固定显示 `Classic CAN（1M，8 分片）`：控制帧和 DATA 数据面均使用标准 Classic CAN 8 字节，
不启用 CAN FD 数据阶段。正式下载只接受 `.bin` 镜像；`.hex`、`.uf2`
仍可用于查看文件信息，但需要先转换成 BIN。目标节点必须已经进入 Bootloader，或由下载流程先
发送 `ENTER_BOOT` 后等待复位。

## 8. 实机测试建议

1. 连接 STM32H750 USB CDC 设备。
2. 启动 `build/gui/rov_ui.exe`，进入“固件升级”。
3. 确认状态显示绿色“下位机在线”和递增心跳计数。
4. 选择 Node，点击“读取版本”“设备信息”“运行状态”。
5. 拖入 Boot-Release 的 `.bin` 文件，确认文件大小、SHA-256 和 CRC32 日志。
6. 确认页面上方“升级总线”为 Classic CAN，再点击“下载到选中节点”（或“编程”），
   观察擦除、写入进度、校验和启动 APP。
7. 对照底层 `COMMAND_TEST_GUIDE.md` 检查 CAN ID、payload 和响应状态；如果设备报告缺包或错误码，
   页面会停止流程并保留中文错误与原始帧，确认总线和供电后再重试。

## 9. 参考资料

- `F:\file\BaiduSyncdisk\Project\CAN_FD_IAP\CAN_FD_IAP\COMMAND_TEST_GUIDE.md`
- `F:\file\BaiduSyncdisk\Project\CAN_To_Uart\CAN_To_Uart\README.md`
- [项目通信层说明](../../src/communication/README.md)
- [统一 Agent 提示词](../../agent/agent.md)
