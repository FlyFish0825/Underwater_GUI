# AA55 上行时间戳：上位机适配与验证（2026-10-07）

## 1. 依据、范围和交付状态

依据用户附件 `aa55-uplink-timestamp-changes.md`；原件 SHA-256：
`187ee7ac08436caac8c6cd4bf6e33a6784a054b5d6f91abe5c1fd005c619401e`。
附件的适用固件为 2119add 及之后的对应分支固件；此信息不是对现场板上二进制版本的认证。
本说明记录 GUI 实际实现，原件中的历史描述、提案与示例未被改写。

上位机基线为 `84d92be`，在现有 `feature/sensor-console-20261006` 工作区适配。
本轮不提交、不推送、不合入 main。主程序和全部测试已重新编译。
原来的六个页面、MainWindow 装配、IMU/深度计服务、自动水面参考和 OSR 联动保持不变。
不改动或烧录下位机；没有执行 CAN 速率写入、传感器参数/归零/启停流或固件命令。

## 2. 实际适配内容

### 2.1 AA55 普通 CAN / 状态提示包

| 格式 | BODY_LEN | 总长度 | 设备时间戳 |
|---|---|---|---|
| 旧版紧凑上行 | 8 + N | 14 + N | 无 |
| 新版上行 | 12 + N | 18 + N | 偏移 11 + N 的 u32 小端，位于 DATA 后、CRC 前 |
| 上位机下行 | 仍为 8 + N | 仍为 14 + N | 不添加 |

公共路由器和独立 CAN 解码器的 AA55 最大包体改为 76，最大整帧为 82 字节。
LEN 仍为真实 DATA 的长度（0..64），DATA 仍只取偏移 11 开始的 N 字节。
新时间戳不进入 `CanGatewayFrame::data`，不影响原电机反馈、调试帧或 Bootloader 的 payload。
UART_RX!/UART_TX! 等普通 AA55 状态提示也按同一规则解析。

CRC8 算法保持不变，范围仍由 BODY_LEN 决定，因此新版自然覆盖新增的 4 字节。
新增无状态 `decodeCanGatewayFrame`：完整帧头、长度、帧尾、CRC 和 DATA 长度校验通过后才提交输出。
解码失败不修改调用者输出，也不更新时钟。旧 `CanGatewayDecoder::feed` 复用此解码函数。
公共路由器在 AA55 校验失败时从候选头后重新扫描，不先吞掉整个坏候选包。
合法帧中的嵌套 AA55/AA5B 字节归 DATA 所有，不交给别的解析器重复识别。

### 2.2 CAN 速率配置回复

同时接受 FLAGS=0x80、命令字 0x81 的两种回复：

- 旧：BODY_LEN=17，总长 23；没有设备时间戳。
- 新：BODY_LEN=21，总长 27；偏移 20..23 为设备时间戳。

保留原请求序号、状态码、仲裁速率和数据速率校验；CRC 不通过不能完成在途设置。
配置回复不再落入普通 CAN 数据分支。原 `canBitrateConfigured(seq,status,nominal,data)`
信号与页面连接不变；增加 `canBitrateResponseReceived(response)` 提供匹配回复的时间元数据。
新增信号只在有效且匹配当前请求时发出；无请求或序号不符不被当作本次设置成功。
`encodeCanGatewayConfigRequest` 不变；响应编码器仅增加可选时间戳，用于回复构造及测试。

### 2.3 设备时间戳保存与回绕

`CanGatewayFrame` 和 `CanGatewayConfigResponse` 追加：

| 字段 | 类型 | 语义 |
|---|---|---|
| hasTimestamp | bool | 帧是否确实携带设备时间；false 不等于设备时间为零 |
| timestampUs | quint32 | 原始设备微秒值，分辨率按附件为 1 ms |
| timestampExtendedUs | qint64 | 服务层的连接内扩展值；-1 表示尚无/不可判定 |

原始值为 0 的新版帧仍是有效时间戳。旧版、下行帧和纯无状态解码不伪造扩展时间。
协议层只保存原始值；既有通信服务对正常 CAN 和匹配的配置回复共用一个 AA55 扩展器。
相同毫秒允许相同时间戳；小幅倒退按延迟/队列重排保留，不额外增加回绕次数。
跨 u32 回绕后的延迟旧帧按最近纪元解释，不把下一帧再次错误推进一个纪元。
半程恰好相等无法判定，扩展值返回 -1，原始值仍保留。扩展锚点只前进，不被迟到样本拉回。
连接关闭和重新打开时清空扩展状态；无效 CRC/长度帧不改变锚点。

**主机实现假设，不是新增线协议保证**：相邻有效 AA55 时间参考的间隔和重排跨度须小于
u32 半程（约 35.8 分钟）。无法单凭 u32 区分任意长静默、多次回绕或不掉 USB 的 MCU 重启。
长时间中断或重启后应重建连接，不声称能自动恢复绝对开机时长。扩展值不是 UTC。
本轮未统一重写 AA58/AA59/AA5B 的 64 位时钟；跨帧族仍保留各协议的原始时间值供分析。
不按时间戳重排串口帧，不替换现有 UI 刷新周期、电机在线判断或波形快照时间轴。

### 2.4 科研记录器

沿用原后台队列与线程，把收到的 CAN 帧设备时间保存到 JSONL，并在 CSV 最末尾追加三列：
`gateway_timestamp_valid`、`gateway_timestamp_us`、`gateway_timestamp_extended_us`。

旧版与 TX 行的设备时间为 null / CSV 空白，valid=false；即使调用者复用 RX 对象发送，
TX 记录也不冒充具有设备测量时间。原 UTC、monotonic_us、DATA、电机字段和旧列顺序不变。
schema_version=1 下增加可选字段，meta 增加 gateway_timestamp_policy；读者应按列名取列。
原时间列表示主机记录时刻，新列表示网关时间，不能直接相减当作已校准传输延迟。
配置回复时间通过服务信号保留，本轮没有另建配置回复记录表。

## 3. 附件歧义与历史兼容边界

这些是明确的主机实现取舍，不改写原附件：

1. 附件第 4 节提到单看 BODY_LEN 的歧义。实现同时核对 BODY_LEN 与实际 LEN，
   例如 BODY_LEN=20 时 N=8 为新、N=12 为旧；不能仅按整帧长度判断。
   两种候选使用的 CRC 范围都由同一个 BODY_LEN 决定，CRC 失败不会通过截断时间戳来“修好”。
2. 旧源码还额外支持历史固定 72 字节包体，这不属于附件描述的旧紧凑格式。
   继续保留此兼容分支，但 BODY_LEN=72、LEN=60 与新版 12+60 在线缆上无法区分；
   **优先按新版精确匹配处理**，不凭最后 4 字节是否为零猜测格式。
   不宣称能无歧义兼容历史固定填充格式的 N=60。
3. 附件第 3 节简述“new<old 就回绕”，而第 7 节示例另写了半程阈值。
   实现采用可处理小倒序的最近纪元/高水位策略，并且只在 CRC 与结构校验通过后更新，
   没有逐句照抄示例中先更新时钟再返回 crc_ok 的方式。

IMU AA5B 布局未改变：频率 0001 仍为 u16，不把下位机原生 7E23 的单字节频率套用到 AA5B。
IMU 无原生 ACK、独立有效位、500 ms 新鲜度、频率观察与 PIN_BLOCKED 保护均未改变。
深度计继续 0x82/0x83，未启用专用协议中仍标“当前没有”的 0x0D/0x4D 提案。

## 4. 测试与实机边界

### 4.1 软件验证（当前源代码重新构建）

| 项目 | 结果 |
|---|---|
| 主程序、全部测试与原硬件探针构建 | 通过 |
| 新 AA55 测试目标 | 48,060 checks / 0 failures |
| 全套 CTest，每个测试执行两次 | 11 个测试 × 2 = 22/22 通过 |
| 原传感器协议/服务/路由测试 | 每次 7,091 checks / 0 failures |
| 原页面测试 | 每次 165 checks / 0 failures |
| Windows 窗口模式页面测试 | 165 checks / 0 failures |

新增测试使用附件四条字面量向量和独立表驱动 CRC8，覆盖两代 0..64 字节 DATA、
每个分片位置、逐字节读取、长度篡改、82 字节最大帧的 656 次单比特破坏、配置回复、
无效输出不污染、回绕/重复/小倒序/上一纪元迟到帧/重连，以及四帧族混流。
混流阶段注入 2000 组，每组包含新版 CAN、旧版 CAN、AA5B 两目标、AA58 和 AA59；
合法 DATA 中嵌套帧不逸出，坏 AA55 候选不吞掉后续可恢复帧。
科研记录测试核对原值零、回绕后的扩展值、旧帧、TX、JSON null、CSV 追加列与 DATA 不变。
断言数不是独立场景数，也不是硬件帧数。页面测试使用合成快照，不连接设备。

### 4.2 COM11 被动接收（2026-10-07 13:18:04–13:19:04，UTC+08:00）

专用程序只构造正式通信服务，未创建自动深度初始化的 MainWindow。
约 60 秒、84,168 字节、应用层发送调用 0 次；已关闭串口。

| 实际收到 | 数量 |
|---|---:|
| AA58 心跳 | 60 |
| AA5B TARGET=1 状态 0x83 | 59 |
| AA5B TARGET=2 深度 0x82 | 1499 |
| AA5B TARGET=2 状态 0x83 | 59 |
| AA55 普通帧或配置回复 | 0 |
| IMU 0x80 / 0x81 | 0 |
| 通信/帧解析错误 | 0 |

**新版 AA55 真机收包、64 字节 CAN FD 实机上报和实际 CAN 速率写入/回复未验收。**
现场没有相关帧，不能以被动接收正常替代这些验证；新版 AA55 目前通过的是离线注入测试。
同样不能把本轮适配等同于 IMU 已恢复，未收到有效 IMU 测量。
本轮没有为制造测试流而发送电机动作、写 CAN 速率、开关上传、归零、复位或烧录。
未启动生产主窗口，避免其既有自动 02BA/水面初始化写入；生产可执行文件已构建。

## 5. 修改文件与复现

运行时：
`src/communication/protocol/CanGatewayProtocol.h/.cpp`、
`src/communication/protocol/CanGatewayConfigProtocol.h/.cpp`、
`src/communication/service/BootloaderCommunicationService.h/.cpp`、
`src/data/recording/ResearchDataRecorder.cpp`。

测试/构建：`tests/CanGatewayTimestampTest.cpp`（新增）、`tests/ResearchDataRecorderTest.cpp`、
`CMakeLists.txt`（只增加显式测试目标，不进入主程序默认构建）。

文档：本说明、README、科研记录说明和架构规范的 AA55 补充入口。
MainWindow、各页面、SerialTransport、SensorProtocol、SensorDataService、
ObserverMotorProtocol 与 Bootloader 升级状态机未改动；没有引入依赖、串口或服务实例。

```powershell
. '.\toolchain\env\activate.ps1'
cmake --build build/gui --target rov_ui rov_can_gateway_timestamp_test rov_research_data_recorder_test --parallel 4
ctest --test-dir build/gui -R 'rov_(can_gateway_timestamp|research_data_recorder)_test' -V
# 全套回归应先显式构建全部测试目标；它们使用 EXCLUDE_FROM_ALL。
ctest --test-dir build/gui --repeat until-fail:2 --output-on-failure
git diff --check
```

程序：`build/gui/rov_ui.exe`。
完整构建、CTest、窗口测试、被动串口记录和前后审计：`build/aa55-adaptation-20261007/`。
该目录被 Git 忽略，不提交二进制、抓包或一次性脚本；现有历史验收记录未删除。
