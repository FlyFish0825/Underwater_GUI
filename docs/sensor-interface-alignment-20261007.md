# IMU / 深度计接口对齐（2026-10-07）

## 依据与范围

GUI 分支为 `feature/sensor-console-20261006`，提交基线 `84d92be`。
下位机核对基线为 CAN_To_Uart 的 `机器人中控` 分支 `f690594`。
读取并对照下列下位机说明与当前源码，不仅依据 GUI 历史接入记录：

- `docs/imu-aa5b-host-protocol.md`：TARGET=1 的命令、参数缓存、三组有效位及频率观测。
- `docs/ms5837-aa5b-host-protocol.md`：TARGET=2 的参数、OSR 预算、零点及时间戳。
- `Core/Src/sensor_service.c`、`Core/Inc/sensor_protocol.h`：实际命令分派、回复、推流和状态调度。

本次只修改 GUI。保留工作区中原有 AA55 时间戳兼容与科研记录改动，不改下位机，
不合并分支、不提交或推送。本机固定 02BA、启动缺失水面参考初始化、OSR/频率联动策略继续沿用。

## 线协议核对

继续使用同一 USB CDC 上的 AA5B v1：小端整数 / IEEE754 f32，总长 `20 + N`，
最大负载 58 字节；CRC16-CCITT-FALSE 覆盖偏移 1 至负载末尾。
TARGET=1 为 IMU，TARGET=2 为深度计；回复使用请求 CMD+0x40，并匹配 TARGET/CMD/SEQ。

| 接口 | IMU | 深度计 |
|---|---|---|
| GET_INFO / GET_STATUS | 31 / 21 字节回复负载，含 RESULT | 同左 |
| GET/SET_PARAMETER | 0001:u16，10–100 Hz；0003:u8，6/9 | 0001:u16，1–100 Hz；0101:u16 OSR；0102/0103/0104:f32；0105:u8 型号 |
| 参数成功语义 | RESULT=7，最后下发缓存 / UART 下发，不能当作设备确认 | RESULT=0，H750 RAM 参数确认，不代表非易失保存 |
| START/STOP_STREAM | 07/08 请求空负载；47/48 回复单字节 RESULT=0 | 同左；按 TARGET 独立 |
| ZERO_DEPTH | 禁止构造 | 0C 请求空负载；4C 单字节 RESULT，依赖有效压力 |
| 测量流 | 80：40B 原始；81：32B 合并姿态 | 82：32B 压力、温度、水深、P0、D1/D2 |
| 状态流 | 83：20B，无 RESULT | 同左 |

IMU 加速度为 g、角速度为 rad/s，欧拉角由 rad 转为界面上的度；磁场保持协议单位。
深度计压力为 Pa、温度为 °C、水深为 m（向下为正）。无效、缺失、过期测量显示 `--`。
AA5B 时间戳保留设备原值，分辨率 1 ms；0x82 是 D2 读完时刻，不能用 USB 收包时间代替。

## 本次完善

### 1. 损坏 AA5B 包的生产链路恢复

共享路由器在 AA5B CRC、版本或字段校验失败时从候选帧头后重新扫描。
不能先吞掉整个候选长度，否则长度损坏或截断拼接可能连同下一份有效 IMU/深度计帧一起丢弃。
完整校验通过的包仍拥有其全部负载，其中的 AA55/AA5B 字节不会被重复拆成其他帧。
此规则与现有 AA55 恢复路径保持一致；有效包处理与下行编码不变。

### 2. 两个目标分别确认上传状态

`SensorDeviceState::streamState` 增加 Unknown / Running / Stopped 主机契约，未增加线上字段。
只有匹配且格式正确的 START/STOP 的 OK 回复才能确认状态；发送请求本身不是确认。
连接重新建立时回到 Unknown，不把连接默认自动推流当作命令执行回执。

成功启停后清除该目标旧测量的有效性与样本龄，不清除另一目标。
停止期间不再接纳该目标测量帧；恢复后等待产生于回复时刻及之后的新测量。
IMU 原始与姿态组都受恢复边界约束，原始组的新帧不会放开旧姿态帧。
恢复边界只需覆盖新鲜度窗口，之后依靠原设备时间/传输年龄校验，避免长期保留 u32 旧纪元。
深度计暂停不清除设备零点配置，也不自动重发归零、开流或参数设置。

**文档歧义按当前固件源码处理**：下位机专用文档部分表格将 STOP 描述为停止 82/83，
但 `SensorService_Process` 的 83 状态调度位于 `streaming[]` 判断之前，实际停止的是测量转发。
因此停止上传后仍接收 83，设备仍可能在线，状态帧也不能恢复已清除的测量值。
总览原详情面板分别显示“上传开关尚未确认”“上传已开启（设备确认）”或“测量上传已停止，底层仍采样”。

### 3. 回复与禁止操作边界

- RESULT=7 仅用于 IMU 参数 GET/SET；继续兼容 FLAGS=02 和历史 FLAGS=06，但不升级成 OK。
- 深度计参数、START/STOP、ZERO_DEPTH 等不接受 UNCONFIRMED 作为合法完成结果，格式不符报告通信内容错误。
- 深度 GET_INFO 的 model 只接受 0/2/30；GUI 本机配置仍固定写 2，不新增型号选择器。
- 两个目标的业务请求均拒绝保存、恢复、自检、复位；保留协议枚举供诊断，不暴露未实现动作。
- CALIBRATE 仍拒绝构造，不新增 IMU 硬件校准或未定义的软件校准算法。
- 未实现的深度单次采样 0D/4D 不接入。
- IMU 上传已停止时，频率参数可按现有协议提交，但不会启动无法取得样本的频率观测；结果仍为未确认。

## 文件与验证入口

运行时修改：`SensorProtocol.cpp`、`BootloaderCommunicationService.cpp` 的 AA5B 分支、
`SensorContract.h`、`SensorDataService.h/.cpp`、`SensorPanel.cpp`。
测试修改：`SensorProtocolTest.cpp`、`SensorPageTest.cpp`，不新增测试框架或依赖。

测试覆盖两个目标的所有损坏候选分片位置、嵌套帧所有权、成功/错误/UNCONFIRMED 回复、
目标独立启停、停止后状态在线但测量无效、恢复前旧测量、IMU 旧姿态、u32 时间回绕、
重连清理、型号校验、禁止操作及界面状态文案。

构建使用仓库固定 Qt/MinGW，主程序保持 `build/gui`；测试单独配置到
`build-test/sensor-interface-20261007`，不复用主程序缓存。
测试构造合成帧，不打开 COM 口。硬件探针只编译，不运行。

本轮实际验证：

| 项目 | 结果 |
|---|---|
| 主程序 rov_ui（当前最终源码） | 构建通过 |
| 全部测试及原硬件探针 | 构建通过 |
| 完整 CTest | 11/11 通过，总计 47.26 s |
| 传感器协议 / 服务 / 共享路由 | 7,644 checks，0 failures |
| 传感器页面（offscreen） | 172 checks，0 failures |
| 传感器页面（Windows 原生窗口） | 172 checks，0 failures；已查看 IMU / 深度计截图 |
| 既有 AA55 时间戳回归 | 48,060 checks，0 failures |
| git diff --check | 通过 |

offscreen 平台的截图不能正确渲染部分中文，因此额外使用 Windows 原生平台重跑
同一合成页面测试并检查中文文案、测量表与参数区；这些图片仍是合成快照，不是实机数据。

复现（测试目录已经独立配置并构建全部目标）：

```powershell
. '.\toolchain\env\activate.ps1'
cmake --build build/gui --target rov_ui --parallel 4
ctest --test-dir build-test/sensor-interface-20261007 --output-on-failure
& '.\build-test\sensor-interface-20261007\rov_sensor_page_test.exe' -platform windows
git diff --check
```

测试构建与完整 CTest 日志分别为该目录下的 `build-validation.log`、`ctest-validation.log`，
单项输出为 `Testing/Temporary/LastTest.log`；原生页面日志为 `page-windows-validation.log`，
合成截图位于 `sensor-test-artifacts/`。这些文件保留在忽略目录，不纳入源码交付。

本轮没有打开现场串口、发送参数/归零/启停/电机命令、烧录或复位设备。
历史文档的 COM11 实测属于原阶段证据，不能作为本次修改的实机验收。
