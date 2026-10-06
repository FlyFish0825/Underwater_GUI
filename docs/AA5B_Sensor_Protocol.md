# AA5B 传感器数据与管理协议 v1

适用：Qt 上位机与 STM32H750 的 USB CDC 链路。IMU 原生 7E23、MS5837 原生 I2C 均止于 H750 后端；GUI 不直接组这些设备私有命令。本文对应 `SensorProtocol.*`、`SensorDataService.*` 和 H750 `sensor_protocol.* / sensor_service.*`。

## 1. 公共帧

所有多字节整数小端，浮点 IEEE754 binary32、小端；不能把 C 结构体直接 memcpy 到线缆。

| 偏移 | 长度 | 字段 |
|---|---:|---|
| 0 | 1 | AA |
| 1 | 1 | 5B |
| 2 | 1 | VERSION=01 |
| 3 | 1 | CMD |
| 4 | 1 | FLAGS |
| 5 | 1 | TARGET：01 主 IMU，02 深度计 |
| 6 | 4 | SEQ |
| 10 | 2 | PAYLOAD_LEN=N，0..58 |
| 12 | 4 | TIMESTAMP_US，H750 单调时间低32位，当前分辨率1ms |
| 16 | N | PAYLOAD |
| 16+N | 2 | CRC16，小端 |
| 18+N | 2 | 5B AA |

总长20+N，最大78字节，兼容现有 USB TX 队列槽大小。CRC16-CCITT-FALSE：poly=1021、init=FFFF、refin/refout=false、xorout=0000；覆盖偏移1..15+N，排除第一个AA、CRC和帧尾。

FLAGS：01请求；02正常回复；06错误/未确认回复；08遥测。回复CMD=request CMD+40，SEQ原样回显。RESULT=UNCONFIRMED也用06，不能因为UART提交成功就返回OK。禁止广播配置；目标必须是01或02。

GET_INFO测试向量（SEQ=12345678，TARGET=01）：

```
AA 5B 01 01 01 01 78 56 34 12 00 00 00 00 00 00 52 D6 5B AA
```

## 2. 命令和结果

| CMD | 名称 | 请求负载 | 当前实现 |
|---|---|---|---|
| 01 | GET_INFO | 空 | 两个目标 |
| 02 | GET_STATUS | 空 | 两个目标 |
| 03 | GET_PARAMETER | PARAM_ID:u16 | 参数表见下 |
| 04 | SET_PARAMETER | 参数元组 | 参数表见下 |
| 05 | SAVE_CONFIG | 空 | 预留，UNSUPPORTED；没有伪装成Flash保存 |
| 06 | RESTORE_DEFAULTS | 空 | 预留，UNSUPPORTED |
| 07 | START_STREAM | 空 | 开启该目标转发，不自动发IMU配置命令 |
| 08 | STOP_STREAM | 空 | 停止该目标数据转发，不停止采样/控制/状态消息 |
| 09 | CALIBRATE | type:u8,action:u8,reference_centi_C:i16 | IMU type1陀螺仪+加速度、type2磁力计；action0清除/1开始 |
| 0A | SELF_TEST | 空 | 预留，UNSUPPORTED |
| 0B | REBOOT | 空 | 预留，UNSUPPORTED |
| 0C | ZERO_DEPTH | 空 | 深度计显式记录水面压力，必须已有有效压力 |

所有回复负载第一个字节为RESULT，其余为该命令的结果体。无额外结果体的动作只回1字节。

| RESULT | 含义 |
|---|---|
| 0 | OK：对应后端确认完成；深度参数是H750本地RAM配置，不是MS5837非易失存储 |
| 1 | UNSUPPORTED |
| 2 | BAD_VALUE：类型/长度/参数范围/采样周期不合法 |
| 3 | BUSY：新请求未执行 |
| 4 | TIMEOUT：无法确认结果，不自动重发动作 |
| 5 | OFFLINE |
| 6 | IO_ERROR |
| 7 | UNCONFIRMED：已完成发送或返回最后下发缓存，但无设备ACK/读回 |
| 8 | NOT_READY：尚无有效数据、零点或参数缓存 |
| 9 | PIN_BLOCKED：配置发送通路受保护；被动RX仍可独立运行 |

IMU原协议限制：附件《通信协议.xlsx》明确0x60频率、0x61算法无回复；GET_PARAMETER无原生读取命令，因此IMU读写配置不能冒充已验证的设备配置。0x70/71具有0x81回复（0失败/1成功）。0x73温度校准长度表存在矛盾，暂不实现；0xA0重置用户数据不等于REBOOT，本版未暴露。禁止用一次UART发送代替执行确认。

## 3. 参数元组

`PARAM_ID:u16, TYPE:u8, LEN:u8, VALUE[LEN]`。GET成功的结果体、SET成功/未确认的结果体均返回此元组。TYPE=2:u8、4:u16、7:f32；保留6:u32的解码能力。长度和类型必须精确匹配。未知参数返回UNSUPPORTED，无缓存返回NOT_READY。

| ID | 目标 | 类型 | 含义/范围 |
|---|---|---|---|
| 0001 | IMU | u16 | 原生输出频率10..100Hz；无ACK |
| 0001 | 深度计 | u16 | 请求采样频率1..50Hz，并受型号/OSR保守转换时间限制 |
| 0003 | IMU | u8 | 算法模式6或9；无ACK，不自动推断IMU型号 |
| 0101 | 深度计 | u16 | OSR=256/512/1024/2048/4096/8192 |
| 0102 | 深度计 | f32 | 水密度900..1300 kg/m³ |
| 0103 | 深度计 | f32 | 显式水面参考压力10000..200000 Pa |
| 0104 | 深度计 | f32 | 滤波K=0..0.99，y=K*y_old+(1-K)*x |
| 0105 | 深度计 | u8 | 型号0=未确认、2=02BA、30=30BA |

深度计默认型号0，不猜硬件型号；默认OSR4096/25Hz、密度1029kg/m³、K=0、无零点。GUI编辑框是待写入值，实际反馈另列显示，不会偷偷发送界面默认值。修改只保存在RAM；掉电后需要重新设置。物理量与配置值都不允许NaN/Inf作为有效值。

## 4. 信息、状态、遥测

GET_INFO结果体（不含RESULT）固定30字节：`sensor_type:u8, model:u8, capabilities:u32, name:ASCII[16], firmware:ASCII[8]`。不足用0填充。capabilities为本固件后端可处理的能力，不等于已完成物理设备识别。IMU版本未读取时为unknown，不能虚构固件版本。

capability位：0 RAW、1四元数、2欧拉角、3压力、4温度、5频率配置、6算法配置、7加速度/陀螺仪校准、8磁校准、9温度校准、10水面归零、11深度参数配置、12保存、13恢复。

GET_STATUS结果体和0x83状态消息固定20字节：`status:u32, sample_seq:u32, sample_age_ms:u32, good_frames:u32, errors:u32`。从未采样age=FFFFFFFF。

状态位：0 ONLINE、1 RAW_VALID、2 QUAT_VALID、3 EULER_VALID、4 PRESSURE_VALID、5 TEMPERATURE_VALID、6 DEPTH_VALID、7 ZERO_VALID、8 PROM_VALID、9 PIN_BLOCKED、10 CONFIG_UNKNOWN、11 MODEL_CONFIRMED。

| 遥测CMD | 目标 | 精确负载 |
|---|---|---|
| 80 IMU_RAW | 01 | status:u32 + accel_g[3]:f32 + gyro_rad_s[3]:f32 + mag_protocol_units[3]:f32，共40字节 |
| 81 IMU_ATTITUDE | 01 | status:u32 + quaternion_wxyz[4]:f32 + euler_rpy_rad[3]:f32，共32字节 |
| 82 DEPTH_DATA | 02 | status:u32 + pressure_pa:f32 + temperature_c:f32 + raw_depth_m:f32 + filtered_depth_m:f32 + surface_pressure_pa:f32 + D1:u32 + D2:u32，共32字节 |
| 83 SENSOR_STATUS | 01/02 | 上述20字节状态体，不带RESULT，默认约1Hz |
| 84 EVENT | 预留 | 本版不发送 |

无效物理量在线缆上填有限占位0，但必须清除对应VALID位；GUI显示“--”，不能把占位0显示成真实测量。IMU磁场物理单位尚未由附件证明，保持协议单位，不标uT。MS5837量程/补偿型号必须显式确认；IMU内置气压计不作为水下深度计。

GUI通过匹配SEQ的回复建立网关时间基准；握手前和明显过期的排队遥测不显示为新鲜数据。各数据组独立新鲜度，不让状态心跳或另一组数据把旧数据刷新为有效。重连清理待处理命令，不自动重发写入/校准/归零动作。

## 5. 集成与使用

GUI在现有“总览”中提供“IMU / 深度计配置”展开入口，不新增导航页，也不使用独立的`--sensors`启动页。面板数据是传感器直接测量，不替代AA58的整机融合状态，不借在线传感器启用电机控制。共享串口由已有通信服务统一路由AA55/AA58/AA59/AA5B，不能让各解析器同时扫描其他协议的负载。

H750接口：`sensor_board`负责I2C3和可选UART DMA；`sensor_service`负责后端映射；`sensor_protocol`负责帧、回复关联及有界背压。MS5837驱动的总线文件命名为`sensor_i2c_bus.*`，避免覆盖CubeMX的`i2c.*`。

默认 `SENSOR_IMU_UART_ENABLED=ON, SENSOR_IMU_UART_TX_ENABLED=OFF`：PA10仅被动接收，PA9不配置为发送，配置命令返回PIN_BLOCKED。现场确认WCH TX和IMU TX不会共驱PA10、PA9线路允许发送后，才可在独立测试构建中开启TX。代码里的保护开关不能替代物理排线。

测试入口：CTest中的`rov_sensor_protocol_test`、`rov_sensor_page_test`及既有8项回归；硬件工具`rov_sensor_hardware_probe --port COM11 --output report.json`不加入CTest，必须显式调用。`--exercise-ram`测试深度计频率/OSR/密度/滤波的往返并恢复原值，不设置型号、不归零、不校准、不发CAN动作。`--capture-only`只读旧链路原始字节。原始采集与日志不能当成已完成的标定精度验收。


## 总览界面入口（2026-10-06）

不新增导航页。原独立 `SensorPage` 已改成 `pages/dashboard/SensorPanel`，由
`DashboardPage` 持有；配置、校准和实时详情通过“IMU / 深度计配置”在总览内展开。
收起、展开、切换 IMU/深度计标签不会发送设备命令，也不会停止采集或丢弃未提交的输入。
页面只使用 `SensorSnapshot` / `SensorRequest` / `SensorParameterFeedback`。

总览深度/姿态卡片优先显示有效整机快照；否则显示有效传感器测量并注明来源。
传感器数据独立于电机快照刷新，不能把 IMU 测量当成融合状态，也不能把气压高度当水深。
`PIN_BLOCKED` 限制 IMU 配置发送，不抹掉带有效标志的被动接收数据；离线/过期仍清空显示。

软件测试：`rov_sensor_protocol_test` 覆盖协议/服务/路由；`rov_sensor_page_test`
覆盖总览嵌入、默认折叠、读写/忙状态/能力限制、离线显示、来源标识与折叠不发命令。
页面测试用合成快照，不打开串口；测试图片不是实测数据证据。
