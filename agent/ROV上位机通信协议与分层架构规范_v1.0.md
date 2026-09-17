# ROV 上位机通信协议与分层架构规范

**文档版本：v1.0**  
**适用对象：Windows / Linux 上位机、Qt GUI、通信层与协议层开发人员**  
**核心原则：通信层、协议层、数据层、UI 层严格分离，禁止跨层耦合。**

---

## 1. 文档目的

本文定义 ROV 上位机的软件分层、通信协议族、数据流向、业务接口与 UI 接入规则。

本文只描述**上位机看到的协议与软件架构**。  
上位机不关心通信对端内部使用什么 MCU、总线、转发方式或控制实现。

上位机只面对一个双向字节流设备：

```text
Peer Device
    │
    │ 双向字节流
    ▼
Transport
    │
    ▼
Protocol
    │
    ▼
Service / Data Layer
    │
    ▼
UI
```

任何 UI 页面都不得直接解析 `AA55 / AA56 / CRC / SEQ / PAYLOAD`。

---

# 2. 强制分层架构

正式工程必须采用以下依赖方向：

```text
┌──────────────────────────────────────┐
│                UI                    │
│ Dashboard / Motor Debug / Firmware   │
│ Manipulator / Vision / Settings      │
└─────────────────▲────────────────────┘
                  │ Snapshot / Request
┌─────────────────┴────────────────────┐
│          Service / Data Layer        │
│ MotorService / DebugService          │
│ SystemService / FirmwareService      │
│ DataStore / Snapshot Builder         │
└─────────────────▲────────────────────┘
                  │ 业务对象
┌─────────────────┴────────────────────┐
│              Protocol                │
│ ProtocolRouter                       │
│ CanGatewayProtocol                   │
│ MotorProtocol                        │
│ DebugProtocol                        │
│ SystemProtocol                       │
│ FirmwareProtocol                     │
└─────────────────▲────────────────────┘
                  │ 原始字节
┌─────────────────┴────────────────────┐
│             Transport                │
│ SerialTransport / UsbTransport       │
│ 自动发现 / 打开 / 关闭 / 收发         │
└──────────────────────────────────────┘
```

## 2.1 依赖规则

只能：

```text
UI → Service
Service → Protocol
Protocol → Transport
```

禁止：

```text
UI → Transport
UI → Protocol
Protocol → QWidget
Transport → DashboardPage
MotorDebugPage → serial.write()
FirmwarePage → 拼 AA55 字节
```

---

# 3. 各层职责

## 3.1 Transport 层

Transport 只负责“字节”。

职责：

- 自动发现设备；
- 打开 / 关闭通信端口；
- 收字节；
- 发字节；
- 重连；
- 连接状态；
- 接收缓存；
- TX 队列；
- 基础统计。

Transport 不知道：

- RPM；
- 电机；
- Firmware；
- Debug；
- Dashboard；
- 参数 ID；
- 业务状态。

推荐接口：

```cpp
class ITransport : public QObject
{
    Q_OBJECT

public:
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

public slots:
    virtual void writeBytes(const QByteArray& data) = 0;

signals:
    void bytesReceived(const QByteArray& data);
    void connected();
    void disconnected();
    void transportError(const QString& message);
};
```

---

## 3.2 Protocol 层

Protocol 负责：

```text
字节流
→ 拆帧
→ 长度检查
→ CRC
→ 字段解析
→ 生成结构化协议对象
```

Protocol 不负责：

- 更新 QWidget；
- 保存页面状态；
- 计算 UI 显示文本；
- 操作按钮；
- 绘图。

---

## 3.3 Service / Data Layer

这是 UI 和协议之间的核心隔离层。

职责：

- 将协议对象转换成业务对象；
- 保存最新状态；
- 管理请求 / 回复；
- 管理 timeout；
- 聚合多种协议数据；
- 生成 Snapshot；
- 提供数据有效性与新鲜度；
- 向 UI 发业务级信号；
- 接收 UI 的业务请求。

例如：

```text
AA56 MOTOR_TELEMETRY
        ↓
MotorProtocol
        ↓
MotorService
        ↓
MotorSnapshot
        ↓
MotorDebugPage
```

UI 永远只看到：

```text
MotorSnapshot
```

而不是：

```text
AA 56 01 80 ...
```

---

## 3.4 UI 层

页面只做：

- 展示；
- 用户输入；
- 发出业务请求。

例如：

```cpp
void MotorDebugPage::setSnapshot(const MotorDebugSnapshot& snapshot);

signals:
    void enableRequested(int motorId);
    void disableRequested(int motorId);
    void targetSpeedRequested(int motorId, double rpm);
```

页面不得：

```cpp
serial->write(...);
parseCrc(...);
parseAa56(...);
buildRawPacket(...);
```

---

# 4. 上位机协议族

统一按第二字节区分协议族。

| 帧头 | 协议 | 上位机用途 |
|---|---|---|
| `AA 55` | CAN Gateway | RAW CAN、兼容通道、固件数据通道 |
| `AA 56` | Motor | 电机状态、控制、参数 |
| `AA 57` | Debug | 高频波形、调试变量、Capture |
| `AA 58` | System / ROV | 整机状态、公共信息、整机控制 |
| `AA 59` | Firmware | 固件升级会话、进度、结果 |
| `AA 5A` | Manipulator | 机械臂协议，当前预留 |
| `AA 5B ~ AA 5F` | Reserved | 后续扩展 |

ProtocolRouter 必须根据协议族分发：

```text
AA55 → CanGatewayProtocol
AA56 → MotorProtocol
AA57 → DebugProtocol
AA58 → SystemProtocol
AA59 → FirmwareProtocol
AA5A → ManipulatorProtocol
```

---

# 5. 字节流处理要求

Transport 收到的是连续字节流。

不得假设：

```text
一次 read()
=
一帧
```

必须支持：

- 半帧；
- 粘包；
- 一次读到多帧；
- 帧头跨两次 read；
- 任意错误字节；
- CRC 错误；
- 长度错误；
- 错帧后重新同步。

推荐：

```text
Transport
   ↓
RX Ring Buffer
   ↓
ProtocolRouter
   ↓
Frame Decoder
```

协议解析器应是**增量式 parser**。

---

# 6. `AA 55`：CAN Gateway Protocol

`AA55` 为兼容协议，保持现有格式。

```text
AA 55
BODY_LEN
SEQ[2]
CAN_ID[4]
FLAGS
LEN
DATA[N]
CRC8
55 AA
```

多字节字段：

```text
Little Endian
```

字段：

```text
SEQ     uint16
CAN_ID  uint32
FLAGS   uint8
LEN     uint8
DATA    N bytes
```

CRC：

```text
CRC-8
Polynomial = 0x07
Initial    = 0x00
```

`BODY_LEN` 的精确定义必须与现有协议实现保持一致。  
上位机不得擅自重新定义旧协议。

上位机内部对象建议：

```cpp
struct RawCanFrame
{
    quint16 sequence = 0;
    quint32 canId = 0;
    quint8 flags = 0;
    QByteArray data;
};
```

推荐接口：

```cpp
class CanGatewayProtocol
{
public:
    QByteArray encode(const RawCanFrame& frame);
    bool feed(const QByteArray& bytes);

signals:
    void frameReceived(const RawCanFrame& frame);
    void protocolError(const ProtocolError& error);
};
```

---

# 7. `AA56 ~ AA5A` 新协议公共格式

除 `AA55` 外，新协议统一采用：

```text
Offset  Size    Field

0       1       0xAA
1       1       FAMILY

2       1       VERSION
3       1       CMD
4       1       FLAGS
5       1       TARGET

6       4       SEQ

10      2       PAYLOAD_LEN

12      4       TIMESTAMP_US

16      N       PAYLOAD

16+N    2       CRC16

18+N    1       FAMILY
19+N    1       0xAA
```

示例：

```text
Motor:
AA 56 ... CRC16 56 AA

Debug:
AA 57 ... CRC16 57 AA

System:
AA 58 ... CRC16 58 AA

Firmware:
AA 59 ... CRC16 59 AA
```

所有多字节整数：

```text
Little Endian
```

---

# 8. 公共字段

## 8.1 VERSION

当前：

```text
0x01
```

收到未知版本：

- 记录日志；
- 统计错误；
- 丢弃该帧；
- 不允许 UI 崩溃。

---

## 8.2 SEQ

```text
uint32
```

用途：

- 请求 / 回复匹配；
- timeout；
- Stream 丢帧检测；
- 日志追踪。

请求：

```text
SEQ = 123
```

对应回复：

```text
SEQ = 123
```

Stream：

```text
1001
1002
1003
1005
```

可以检测：

```text
1004 missing
```

---

## 8.3 TIMESTAMP_US

```text
uint32
单位：us
```

接收数据时，UI 波形优先使用协议时间戳作为采样时序依据。

UI 不应使用“本机收到包的时间”替代采样时间。

---

## 8.4 TARGET

统一：

```text
0x00      System
0x01~08   Device / Motor 1~8
0xFF      Broadcast
```

UI 使用稳定的业务 ID。

禁止：

```text
table row == device ID
```

---

# 9. FLAGS

```text
bit0 ACK_REQUIRED
bit1 RESPONSE
bit2 ERROR
bit3 STREAM
bit4 RELIABLE
bit5 HIGH_PRIORITY
bit6 RESERVED
bit7 RESERVED
```

典型组合：

普通请求：

```text
ACK_REQUIRED
```

正常回复：

```text
RESPONSE
```

错误回复：

```text
RESPONSE | ERROR
```

实时流：

```text
STREAM
```

---

# 10. CRC16

新协议统一：

```text
CRC16-CCITT
Polynomial = 0x1021
Initial    = 0xFFFF
```

CRC 覆盖：

```text
FAMILY
VERSION
CMD
FLAGS
TARGET
SEQ
PAYLOAD_LEN
TIMESTAMP_US
PAYLOAD
```

不包含：

```text
起始 0xAA
CRC16 本身
结尾 FAMILY + 0xAA
```

处理顺序：

```text
接收
 ↓
长度检查
 ↓
CRC检查
 ↓
Frame Object
 ↓
Service
```

CRC 错误数据不得进入数据层。

---

# 11. `AA 56` Motor Protocol

Motor 协议负责：

- 电机状态；
- 电机控制；
- 电机参数；
- 普通遥测。

命令：

```text
0x01 SET_TARGET
0x02 ENABLE
0x03 DISABLE
0x04 SET_MODE
0x05 SET_PARAMETER
0x06 GET_PARAMETER
0x07 SET_TELEMETRY_RATE
0x08 GET_STATUS
```

回复：

```text
0x41 SET_TARGET_REPLY
0x42 ENABLE_REPLY
0x43 DISABLE_REPLY
0x44 SET_MODE_REPLY
0x45 SET_PARAMETER_REPLY
0x46 GET_PARAMETER_REPLY
0x47 SET_TELEMETRY_RATE_REPLY
0x48 GET_STATUS_REPLY
```

Stream：

```text
0x80 MOTOR_TELEMETRY
0x81 MOTOR_EVENT
```

---

# 12. Motor Telemetry

基础单电机：

```cpp
#pragma pack(push, 1)

struct MotorFastTelemetry
{
    qint16 speedRpm;

    quint16 busVoltage_cV;   // 0.01 V

    qint16 iq_cA;            // 0.01 A

    quint16 status;
};

#pragma pack(pop)
```

大小：

```text
8 Byte
```

聚合 Payload：

```text
validMask        uint16
samplePeriodUs   uint16
motor[8]         8 × MotorFastTelemetry
```

总 Payload：

```text
68 Byte
```

收到后：

```text
MotorProtocol
      ↓
MotorService
      ↓
更新 MotorState[8]
      ↓
生成 MotorSnapshot / DashboardSnapshot
```

UI 不直接访问原始 telemetry packet。

---

# 13. Motor 参数协议

参数统一使用：

```text
SET_PARAMETER
GET_PARAMETER
```

不为每个参数创建一个 CMD。

Payload：

```text
PARAM_ID      uint16
DATA_TYPE     uint8
DATA_LEN      uint8
VALUE         N bytes
```

类型：

```text
0x01 int8
0x02 uint8
0x03 int16
0x04 uint16
0x05 int32
0x06 uint32
0x07 float32
```

参数 ID 表应独立维护：

```text
MotorParameterIds.h
```

UI 使用有意义的参数对象，不直接操作裸 `PARAM_ID`。

例如：

```cpp
struct MotorParameter
{
    quint16 id;
    QString name;
    QString unit;
    QVariant value;
    bool valid;
};
```

---

# 14. `AA 57` Debug Protocol

Debug 与普通 Motor Telemetry 必须严格分开。

负责：

- 高频变量；
- 波形；
- Stream；
- Capture；
- Debug 配置。

命令：

```text
0x01 CONFIG
0x02 START_STREAM
0x03 STOP_STREAM

0x04 START_CAPTURE
0x05 STOP_CAPTURE
0x06 READ_CAPTURE
0x07 CLEAR_CAPTURE

0x08 GET_STATUS
```

数据：

```text
0x80 DEBUG_STREAM_DATA
0x81 CAPTURE_INFO
0x82 CAPTURE_DATA
0x83 DEBUG_EVENT
```

---

# 15. Debug CONFIG

建议：

```text
sampleRateHz      uint32
channelCount      uint8
channelId[]       uint8 × N
```

例如：

```text
10000 Hz

Channels:
IQ
RPM
VBUS
```

UI 不允许写死“永远 5 条曲线”。

曲线数量和类型来自 DebugService。

---

# 16. Debug Channel ID

建议：

```text
0x01 IA
0x02 IB
0x03 IC

0x10 ID
0x11 IQ

0x20 VD
0x21 VQ

0x30 SPEED_RPM
0x31 SPEED_TARGET

0x40 VBUS

0x50 ELECTRICAL_ANGLE

0x60 OBSERVER_ANGLE
0x61 OBSERVER_SPEED

0x70 TEMPERATURE
```

上位机建立元数据表：

```cpp
struct DebugChannelMeta
{
    quint8 id;
    QString name;
    QString unit;
    double scale;
};
```

UI 只消费：

```text
DebugChannelMeta
+
DebugSeries
```

---

# 17. Debug Stream

禁止：

```text
一个 sample
→ 一个 UI signal
```

也禁止：

```text
一个 sample
→ 一次 repaint
```

Protocol 应批量解析 samples。

Data Layer 应批量存储。

UI 按自己的刷新频率取可视数据。

例如：

```text
采样：10 kHz
↓
协议批量数据
↓
Data Layer
↓
缓存
↓
UI 20~60 Hz刷新
```

采样频率和 UI 刷新频率必须完全分离。

---

# 18. Debug Capture

流程：

```text
START_CAPTURE
    ↓
等待采集
    ↓
CAPTURE_INFO
    ↓
READ_CAPTURE
    ↓
CAPTURE_DATA
    ↓
Data Layer重组
    ↓
UI显示
```

`CAPTURE_INFO` 建议：

```text
captureId
totalSamples
sampleRateHz
channelCount
totalBytes
triggerIndex
```

`CAPTURE_DATA`：

```text
captureId
offset
dataLength
data[]
```

Protocol 层负责：

- chunk解析。

Service 层负责：

- chunk重组；
- 完整性检查；
- 进度。

UI 只看：

```text
CaptureProgress
CaptureResult
```

---

# 19. `AA 58` System / ROV Protocol

负责：

- 整机状态；
- 公共设备信息；
- Dashboard 数据；
- 整机操作请求。

命令：

```text
0x01 PING
0x02 GET_DEVICE_INFO
0x03 GET_COMM_STATS
0x04 GET_TIME

0x10 SET_6DOF_COMMAND

0x11 ARM
0x12 DISARM

0x13 HOLD_POSITION
0x14 SURFACE

0x15 SET_THRUSTER_LIMIT
```

Stream：

```text
0x80 SYSTEM_STATUS
0x81 ROV_STATUS
```

Event：

```text
0xC0 SYSTEM_EVENT
```

Error：

```text
0xE0 SYSTEM_ERROR
```

---

# 20. Dashboard 数据模型

DashboardPage 不读取 `AA58`。

应接收：

```cpp
struct RovSnapshot
{
    bool connected = false;

    bool depthValid = false;
    double depthM = 0.0;

    bool attitudeValid = false;
    double rollDeg = 0.0;
    double pitchDeg = 0.0;
    double yawDeg = 0.0;

    bool voltageValid = false;
    double busVoltageV = 0.0;

    bool temperatureValid = false;
    double internalTemperatureC = 0.0;

    bool armed = false;
    bool leakDetected = false;

    quint32 workingMode = 0;
    quint32 alarmFlags = 0;

    quint32 timestampUs = 0;
};
```

实际工程可继续细分 freshness / validity。

Dashboard 只：

```cpp
setSnapshot(snapshot);
```

---

# 21. 六自由度控制请求

UI 发出的应该是业务请求：

```cpp
struct RovControlRequest
{
    double surge;
    double sway;
    double heave;

    double roll;
    double pitch;
    double yaw;
};
```

然后：

```text
DashboardPage
      ↓
controlRequested()
      ↓
SystemService
      ↓
范围检查 / 单位转换
      ↓
SystemProtocol
      ↓
AA58
```

页面不负责协议量化。

---

# 22. `AA 59` Firmware Protocol

Firmware 协议负责：

- 升级会话；
- 进度；
- 结果；
- 状态。

命令：

```text
0x01 ENTER_UPDATE_MODE
0x02 EXIT_UPDATE_MODE
0x03 GET_UPDATE_STATUS
0x04 ABORT_UPDATE
0x05 GET_UPDATE_STATS
```

状态：

```text
0x80 UPDATE_STATUS
0x81 UPDATE_PROGRESS
0x82 UPDATE_RESULT
```

Firmware 页面不直接处理 `AA55` 或 `AA59`。

---

# 23. Firmware 数据模型

推荐：

```cpp
enum class FirmwareState
{
    Idle,
    Preparing,
    Erasing,
    Programming,
    Verifying,
    Rebooting,
    Completed,
    Failed,
    Aborted
};
```

```cpp
struct FirmwareSnapshot
{
    int targetId = 0;

    QString currentVersion;
    QString targetVersion;
    QString firmwareFile;

    FirmwareState state = FirmwareState::Idle;

    int progressPercent = 0;

    QString statusText;
    QString errorText;

    bool canStart = false;
    bool canAbort = false;
};
```

页面接口：

```cpp
void FirmwarePage::setSnapshot(
    const FirmwareSnapshot& snapshot);

signals:
    void firmwareFileSelected(const QString& path);
    void upgradeRequested(int targetId);
    void abortRequested();
```

具体协议全部在 FirmwareService 下完成。

---

# 24. `AA 5A` Manipulator

当前只预留：

```text
FAMILY = 0x5A
```

未来 Data Layer 应提供：

```text
ManipulatorSnapshot
ManipulatorRequest
```

ManipulatorPage 不直接读取 AA5A。

---

# 25. 数据有效性与新鲜度

数据层必须区分：

```text
未知
有效
过期
离线
错误
```

禁止 UI 使用：

```text
0 == 未知
```

例如电压真的可能为 0。

推荐：

```cpp
template <typename T>
struct DataValue
{
    T value {};
    bool valid = false;
    bool stale = false;
    quint32 timestampUs = 0;
};
```

或者为不同业务定义明确字段。

---

# 26. UI 刷新和采样频率分离

这是强制规则。

例如：

```text
Motor Telemetry = 1 kHz

Debug Data = 10 kHz

UI repaint = 20~60 Hz
```

不得：

```text
收到1个数据包
→ repaint整个页面
```

Data Layer 应缓存最新数据。

UI 按固定刷新策略获取 snapshot。

---

# 27. 线程模型

推荐：

```text
Communication Thread
    │
    ├ Transport
    ├ ProtocolRouter
    └ Protocol Decoder
            │
            ▼
       Service/Data
            │
     queued signal
            ▼
        GUI Thread
            │
            ▼
           UI
```

规则：

1. QWidget 只能在 GUI Thread 更新；
2. 通信线程不得直接访问页面；
3. 跨线程传递使用 Qt signal/slot；
4. Snapshot 应是可复制的值类型；
5. 不把 `QWidget*` 放进协议或数据对象。

---

# 28. 推荐工程目录

```text
src/

  communication/

    transport/
      ITransport.h
      SerialTransport.h
      SerialTransport.cpp

    protocol/
      ProtocolTypes.h

      ProtocolRouter.h
      ProtocolRouter.cpp

      CanGatewayProtocol.h
      CanGatewayProtocol.cpp

      MotorProtocol.h
      MotorProtocol.cpp

      DebugProtocol.h
      DebugProtocol.cpp

      SystemProtocol.h
      SystemProtocol.cpp

      FirmwareProtocol.h
      FirmwareProtocol.cpp

  data/

    common/

    motor/
      MotorTypes.h
      MotorService.h
      MotorService.cpp

    debug/
      DebugTypes.h
      DebugService.h
      DebugService.cpp

    system/
      SystemTypes.h
      SystemService.h
      SystemService.cpp

    firmware/
      FirmwareTypes.h
      FirmwareService.h
      FirmwareService.cpp

  contracts/

    dashboard/
      DashboardSnapshot.h
      DashboardRequest.h

    motor_debug/
      MotorDebugSnapshot.h
      MotorDebugRequest.h

    firmware/
      FirmwareSnapshot.h
      FirmwareRequest.h

  pages/

    dashboard/
    motor_debug/
    firmware/
    manipulator/
    vision/
```

---

# 29. UI 与数据层的接口边界

正式页面只能依赖 `contracts/`。

例如：

```text
DashboardPage
        │
        ├ setSnapshot(DashboardSnapshot)
        │
        └ controlRequested(DashboardControlRequest)
```

```text
MotorDebugPage
        │
        ├ setSnapshot(MotorDebugSnapshot)
        ├ parameterWriteRequested(...)
        └ debugStartRequested(...)
```

```text
FirmwarePage
        │
        ├ setSnapshot(FirmwareSnapshot)
        ├ upgradeRequested(...)
        └ abortRequested()
```

页面不 include：

```text
SerialTransport.h
ProtocolRouter.h
MotorProtocol.h
DebugProtocol.h
CanGatewayProtocol.h
```

这是代码审查时的硬性要求。

---

# 30. 数据流示例：电机状态

```text
接收字节
    ↓
Transport
    ↓
ProtocolRouter
    ↓
MotorProtocol
    ↓
MotorTelemetry Object
    ↓
MotorService
    ↓
MotorState
    ↓
MotorDebugSnapshot
    ↓
MotorDebugPage
```

---

# 31. 数据流示例：UI 设置参数

```text
用户修改参数
    ↓
MotorDebugPage
    ↓
parameterWriteRequested()
    ↓
MotorService
    ↓
参数ID / 类型 / 范围检查
    ↓
MotorProtocol::encodeSetParameter()
    ↓
Transport::writeBytes()
```

---

# 32. 数据流示例：Firmware

```text
用户点击 Upgrade
      ↓
FirmwarePage
      ↓
upgradeRequested()
      ↓
FirmwareService
      ↓
升级业务状态机
      ↓
FirmwareProtocol / CanGatewayProtocol
      ↓
Transport
```

返回：

```text
Transport
    ↓
Protocol
    ↓
FirmwareService
    ↓
FirmwareSnapshot
    ↓
FirmwarePage
```

---

# 33. 错误处理

至少定义：

```cpp
enum class ProtocolErrorType
{
    InvalidHeader,
    InvalidLength,
    InvalidVersion,
    InvalidCrc,
    UnsupportedFamily,
    UnsupportedCommand,
    Timeout,
    SequenceMismatch
};
```

错误首先进入：

```text
Protocol / Service
```

不要直接弹 `QMessageBox`。

Service 决定：

```text
是否影响业务状态
是否记录日志
是否通知UI
```

UI 只显示最终业务错误。

---

# 34. 日志

建议统一日志类别：

```text
TRANSPORT
PROTOCOL
MOTOR
DEBUG
SYSTEM
FIRMWARE
UI
```

日志内容应包括必要时的：

```text
Direction
Family
CMD
SEQ
TARGET
Payload Length
Result
```

默认不要把高速 Debug 每一帧都写日志，否则日志本身会造成性能问题。

---

# 35. 自动设备发现

设备识别：

```text
VID     = 0x0483
PID     = 0x5740

Product =
Lamost Underwater Robot Gateway
```

启动流程：

```text
扫描端口
    ↓
匹配 VID/PID/Product
    ↓
自动连接
```

COM 号不是设备身份。

禁止把：

```text
COM12
```

作为永久配置。

---

# 36. 测试要求

## Protocol 层

必须做纯字节流测试：

- 完整帧；
- 半帧；
- 一个字节一个字节输入；
- 两帧粘包；
- 10帧粘包；
- 帧头跨 buffer；
- CRC 错误；
- 长度错误；
- 噪声 + 正常帧；
- 错帧后恢复；
- 不支持的 FAMILY；
- 不支持的 VERSION。

这些测试不需要启动 GUI。

---

## Service / Data Layer

必须能使用 Mock Protocol 数据测试：

```text
MotorService
DebugService
FirmwareService
SystemService
```

不需要真实通信设备。

---

## UI

UI 必须能只使用 Mock Snapshot 运行。

页面开发阶段不依赖真实串口或真实协议。

这也是 UI 与数据层真正解耦的验收方式。

---

# 37. Codex / Agent 强制规则

实现上位机时遵守：

1. 先建立 Transport / Protocol / Data / UI 分层；
2. 不把所有代码写进 MainWindow；
3. 不把协议 parser 写进 Page；
4. 不让 Service 持有 QWidget；
5. 页面不直接调用 serial；
6. 页面不自己算 CRC；
7. 页面不识别 `AA55/AA56/AA57/...`；
8. 通信线程不更新 QWidget；
9. 高频数据不逐 sample 发 GUI signal；
10. UI 与通信层必须可以分别独立测试；
11. 新增协议必须放独立 protocol 文件；
12. 新增业务必须优先放 Service，而不是往页面增加通信代码；
13. Snapshot / Request 是 UI 与数据层之间的唯一正式业务接口；
14. 协议字段变化不得直接扩散到 UI；
15. UI 需求变化也不得要求修改底层协议 parser。

---

# 38. 最终验收标准

以下结构必须成立：

```text
Transport
   ↓
Protocol
   ↓
Data / Service
   ↓
Contracts
   ↓
UI
```

同时反向请求：

```text
UI
 ↓
Request
 ↓
Service
 ↓
Protocol
 ↓
Transport
```

如果出现：

```text
Page → Serial
Page → CRC
Page → AA56
Protocol → QWidget
Transport → Dashboard
```

即视为架构违规。

---

# 39. 最重要的一句话

> **UI 不认识协议，协议不认识 UI。**

二者之间永远通过：

```text
Snapshot
Request
Service
```

连接。

这样后续即使通信协议、Transport、设备数量、刷新频率发生变化，UI 页面也不需要跟着重写；同样，即使 UI 改版，通信协议层也不需要修改。
