# IMU AA5B 上位机协议（TARGET = 1）——深度计协议的补充

> 用途：直接交给上位机（Qt）开发照着实现，与《ms5837-aa5b-host-protocol.md》配合使用。
> **依赖声明**：通用帧格式（AA 5B 帧头/CRC16-CCITT-FALSE/帧尾）、FLAGS 定义、RESULT 码表、
> 0x83 状态流结构与背压规则，见《ms5837-aa5b-host-protocol.md》§2/§3/§10，本文件不重复。
> 上位机按 `TARGET` 字段区分两台传感器：`TARGET=1` IMU（本文件），`TARGET=2` 深度计。
> 来源：**除标注 ⚠️ 外，全部字段 2026-10-06 真机实测**（CDC COM11，GET_INFO/GET_STATUS/
> 参数读写/边界/启停流共 13 条命令 + 多轮 24s 流；测试固件 = 集成分支接线 +
> imu_sensor@8367a93；证据见 docs/imu-sensor.md §7 与 aa5b-imu-test-report-20261006.md）。
> IMU 相关代码：`Core/Inc/imu_sensor.h`、`Core/Src/imu_sensor.c`；AA5B 编解码：
> `Core/Src/sensor_protocol.c`；业务层：`Core/Src/sensor_service.c`。

## 1. 推送策略（已定稿）

连接即**自动推流**（与深度计一致），无需 START_STREAM；`STOP_STREAM(0x08)` /
`START_STREAM(0x07)` 可随时暂停/恢复，**仅控制 CDC 转发、不影响传感器采样，且按
TARGET 独立**（实测：STOP t1 时深度流不受影响，反之亦然）。带宽不足时再议按需
单查模式（参考 ms5837.md §8 的 0x0D 提案，IMU 侧对称设计暂缓）。

## 2. IMU 命令集（TARGET=1）

| `CMD` | 名称 | 回复 `CMD` | 状态 | 实测结果 |
| --- | --- | --- | --- | --- |
| `0x01` | `GET_INFO` | `0x41` | ✓ 实测 | `RESULT=0`，负载 31B（§5） |
| `0x02` | `GET_STATUS` | `0x42` | ✓ 实测 | `RESULT=0`，负载 21B（§6） |
| `0x03` | `GET_PARAMETER` | `0x43` | ✓ 实测 | 仅 0001/0003，其余 ID `RESULT=1` |
| `0x04` | `SET_PARAMETER` | `0x44` | ✓ 实测 | 同上 |
| `0x07/0x08` | `START/STOP_STREAM` | `0x47/0x48` | ✓ 实测 | `RESULT=0`，**按 TARGET 独立**（STOP t1 不影响 t2 流） |
| `0x09` | `CALIBRATE` | `0x49` | ⚠️ 未真机发送 | 载荷见 §7（会改设备校准状态） |
| `0x05/0x0A/0x0B` | SAVE/SELF_TEST/REBOOT | `0x45/0x4A/0x4B` | ✓ 实测 | **`RESULT=1` UNSUPPORTED**，GUI 不出按钮 |
| `0x06` | `RESTORE_DEFAULTS` | `0x46` | ⚠️ 统筹待定 | 提议映射原生 0xA0（重置用户数据），当前固件回 `RESULT=1` |

## 3. 参数表（GET/SET_PARAMETER，IMU 特有语义）

| 参数 ID | 名称 | 类型 | 取值 | 默认 |
| --- | --- | --- | --- | --- |
| `0x0001` | 输出率 | u16 | 10 ~ 100 Hz | 25 |
| `0x0003` | 算法模式 | u8 | 6 = 六轴 / 9 = 九轴 | 未设置 |

**与深度计的关键差异（GUI 必须区分处理）：**

1. **GET 回 `RESULT=7 UNCONFIRMED` + 缓存值**，不是 OK——IMU 参数无原生读回，
   缓存只是"最后成功下发的值"。从未设置过 = `RESULT=8 NOT_READY`（实测 seq3/seq7）。
2. **SET 回 `RESULT=7 UNCONFIRMED`** = "UART 已发出"，**≠ 生效**。真机证据：
   旧帧格式下设备静默忽略、遥测流保持 25 Hz 不变（无 ACK 协议无法报告失败）。
   **生效确认方法：SET 后统计 0x80 遥测的实际帧率约 2 秒**（实测脚本即此做法）。
3. 非法参数（如 0001=5、0003=7）= `RESULT=2 BAD_VALUE`，同步回复。

## 4. CALIBRATE 载荷（**产品决策：不使用，GUI 不实现**）

> **2026-10-06 决策：本设备不使用 IMU 硬件校准，校准在上位机软件层面完成——
> GUI 不要实现 CALIBRATE 命令。** 以下载荷定义仅作协议完备性保留：命令在固件中
> 存在且会真实改变设备校准状态（`action=0` 清除属有损操作），误发会降低数据质量。

| 偏移 | 字段 | 说明 |
| --- | --- | --- |
| 0 | `type:u8` | 1 = 陀螺仪+加速度计，2 = 磁力计，3 = 温度（对应原生 0x70/0x71/0x73） |
| 1 | `action:u8` | 0 = 清除校准值，1 = 开始校准 |
| 2 | `reference_temperature_centiC:i16` | 小端，温度×100；type=3 时有效 |

**异步语义**：部分校准由设备回 `0x81 [原命令, 0失败|1成功]` 才完成，最长 30 s；
待决期间一切新命令回 `RESULT=3 BUSY`，GUI 应禁用其他传感器操作。回复可能是
`OK(0)`、`IO_ERROR(6)`（设备报告失败）或 `TIMEOUT(4)`。校准会改变设备状态，
GUI 触发前应向操作者确认（当前实现缺口：centiC 传参，见 docs/imu-sensor.md §5）。

## 5. GET_INFO 负载（31 字节，`LEN=31` 含 RESULT）

| 偏移 | 长度 | 字段 | 实测 |
| --- | --- | --- | --- |
| 0 | 1 | `RESULT` | 0 |
| 1 | 1 | `sensor_type` | **1（IMU）** |
| 2 | 1 | `model` | **恒 0**：原生协议无型号读回，禁止凭四元数/磁力计数据猜 6/9 轴 |
| 3 | 4 | `capabilities` | `0x000001E7`：bit0 raw、bit1 quaternion、bit2 euler、bit5 rate-config、bit6 algorithm-config、bit7 accel/gyro-cal、bit8 mag-cal |
| 7 | 16 | `name` | `"7E23 IMU"` 后补 `\0` |
| 23 | 8 | `firmware` | 版本串 `"1.0.0"`；**首次 GET_INFO 会自动触发原生版本查询并在收到版本后立即回复（实测请求后数十毫秒）**，之后走缓存即时回复 |

## 6. GET_STATUS 负载与状态位（IMU 侧取值）

20 字节统计体（RESULT + status + sample_seq + age_ms + good_frames + errors）同深度计。
实测 `status=0x0000043F`（含 bit10，因实测时速率/模式尚未设置过；SET 后清零）、
`sample_seq=62641`、`age_ms=3`、`errors=0`。

**IMU 状态位定义（与深度计共用编号，取值不同）：**

| 位 | 名称 | IMU 侧语义 |
| --- | --- | --- |
| 0 | ONLINE | 数据流新鲜（raw 组 500 ms 内有样本） |
| 1/2/3 | RAW_VALID / QUAT_VALID / EULER_VALID | **三组独立新鲜度**：raw 到达不刷新姿态位 |
| 4/5 | PRESSURE / TEMPERATURE_VALID | 来自气压计帧（0x32），十轴模块才有 |
| 9 | PIN_BLOCKED | 调试器/占用期间置位（实测 WCH-Link 插着即 1） |
| 10 | CONFIG_UNKNOWN | 速率或模式从未下发过（无读回，无法确认） |
| 11 | MODEL_CONFIRMED | **恒 0**：无读回，GUI 不要做"型号已确认"判断 |

`sample_seq` = 原始帧计数；`age_ms` 按四组最新时间算；`errors` = 校验和+长度+非有限
浮点+缓冲溢出+超时的合计。

## 7. 遥测流（IMU 专属）

| CMD | 负载 | 实测帧率 |
| --- | --- | --- |
| `0x80` IMU_RAW，**40B** | `status:u32` + `accel_g f32[3]` + `gyro_rad_s f32[3]` + `mag f32[3]` | **25.6 Hz**（=传感器输出率） |
| `0x81` IMU_ATTITUDE，**32B** | `status:u32` + `quat_wxyz f32[4]` + `roll/pitch/yaw_rad f32[3]` | **51 Hz**（≠25！） |
| `0x83` 状态流 | 同深度计（20B），按 TARGET 区分 | 各 1 Hz |

**0x81 合并语义（实测 51 Hz）**：四元数（原生 0x16）与欧拉角（原生 0x26）各 25 Hz
独立到达，**任一有新样本即发一帧**——GUI 按 `status` 的 `QUAT_VALID/EULER_VALID`
位分别取字段，不要假设 0x80 与 0x81 一一对应。帧率关系（GUI 带宽预估用）：
**0x80 ≈ 输出率；0x81 ≈ 2×输出率**（实测 25Hz 模式→51Hz，50Hz 模式→~100Hz）。

**单位（缩放经真机验证）**：accel = g；gyro = rad/s；euler = rad；**mag 的物理单位
未证实（800/32767 线性值），GUI 禁止标注 uT**。

**时间戳**：= 数据接收完成时刻（HAL 毫秒 ×1000，分辨率 1 ms），与深度计"数据产生
时刻"同口径——多传感器对齐用它，不要用上位机接收时刻。

## 8. 上位机注意事项（IMU 专属）

1. 帧可能偶尔丢失（背压设计，队列 ≥25/255 时传感器帧直接丢弃）——用 `sample_seq`/`SEQ` 判连续性。
2. 无持久化：复位后速率/模式/版本缓存全丢，每次连接重做 GET_INFO。
3. **改速率后必须验证实际帧率**（§3 第 2 条），UNCONFIRMED 不是成功。
4. PIN_BLOCKED(9) 表示 IMU 串口被占用（如调试器插着），不是通信故障。
5. **CALIBRATE 不实现**（产品决策：校准在上位机软件层面完成）。该命令在固件中存在且会真实改变设备校准状态（有损操作），不要误发。
