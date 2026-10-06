# 总览传感器集成与验证记录 — 2026-10-06

## 本轮要求与实现

用户要求：不新增独立传感器页面，IMU 和深度计放进总览。

已移除第七个导航入口、独立传感器滚动页和 `--sensors` 跳转。导航仍为总览、电机调试、
固件升级、机械臂、视觉、设置。原控制面板移至 `src/pages/dashboard/SensorPanel.*`，
由 `DashboardPage` 持有，默认折叠。总览状态卡右上“IMU / 深度计配置”在同页展开，
面板内“收起，返回总览”原地收起并滚回顶部。不创建新窗口或新导航目的地。

总览深度/横滚/俯仰/航向优先显示有效整机快照，否则显示有效传感器测量并标明来源。
传感器快照与电机快照独立；电机周期刷新不清空传感器测量。测量回退只用于显示，不改变
控制权限，不冒充融合状态或写入旧的整机科研记录列。切标签/展开/收起不发设备命令。

保留现有 AA5B 请求、回复、参数、能力限制、校准、超时和新鲜度链路。
修复了 PIN_BLOCKED 同时抹掉有效接收的错误：该状态只限制 IMU 配置发送，已校验的
被动接收仍可以显示；离线/过期仍清空。参数输入与设备返回分开显示。

## 软件验证（本轮实际运行）

GUI 使用仓库 Qt 5.15.19 / MinGW 8.1.0 / CMake 3.28.6。重新链接程序与全部测试目标：

- `rov_ui` 构建通过。
- CTest 10/10 通过，0 失败（含原有 8 项回归及 2 项传感器测试）。
- `rov_sensor_protocol_test`：282 个检查，0 失败。
- `rov_sensor_page_test`：43 个检查，0 失败。覆盖总览嵌入、单一页面标题、默认折叠、
  读写请求、能力/忙/引脚保护、参数返回不覆盖输入、实时值来源、传感器不被电机刷新清空、
  折叠/切页签不发命令、滚动与收起、断线恢复 `--`。
- `git diff --check` 通过（仓库有既有 LF/CRLF 提示，不属于 diff 空白错误）。

日志：`build/gui/dashboard-build.log`、`dashboard-ctest.log`。

另外运行了固定 clangd 14 检查：规范化 query-driver 路径后无先前的 `__rdtsc` 定义诊断，
但 ExtractFunction tweak 自测仍报告“Cannot extract break/continue without corresponding
loop/switch statement”，进程退出码 3。因此不声称 clangd 检查完全通过；这与上面真实 GCC
构建/CTest 通过是两项独立结果。日志 `build/gui/dashboard-clangd-normalized.log`。

## 实窗与当前硬件（不是合成样本）

实际启动新版 GUI，确认左栏只有 6 个入口；在总览点击展开，跳到同页配置区域；切到
深度计，点击频率“读取”，收到 `25（设备确认）`；再点击收起返回总览。未执行校准、
归零、型号选择或频率写入。

COM11 上当前运行的集成固件返回：I2C 设备在线、PROM 已校验、错误计数 0，D1/D2 持续
变化。例如本轮截图 D1=6313851、D2=7872127；读回参数只证明 H750 的当前配置，不证明
转换后的深度精度。型号仍未确认、零点未设定，压力/温度/水深继续显示 `--`。

本轮实窗截图 IMU 仍为接收 0、错误 0，PA9 配置发送处于保护状态；不能把这次界面测试
写成 IMU 实机通过。ZCode 之前的 COM42 原始采集报告只收到 `SELFTEST-7E23` 文本，
也不能当作 IMU 二进制帧通过。

实窗截图位于 `build/gui/sensor-test-artifacts/`：
`dashboard-final-collapsed.png`、`dashboard-final-expanded.png`、`dashboard-final-depth.png`、
`dashboard-depth-readback.png`。带 `fixture` 的文件是合成快照，仅用于界面测试。
MCP 截图/RPC 入口本轮不稳定，实窗截图由只读 Windows 截屏脚本取得。小窗口 resize RPC
失败，本轮不声称完成最小窗口尺寸的实窗验证。

## H750 交接与主线门槛

ZCode 的超时修正 `82c590a` 已挑选至独立集成工作树（本地为 `b222a6c`）；SensorService
改用后端默认超时：版本查询 1 秒、校准 30 秒。深度计包含 DeepSeek 的 `b89c60d` 修正。

本轮 H750 的 6 个主机测试程序全部通过（使用 `tests/run_sensor_host_tests.ps1`）：
IMU、MS5837、AA5B 协议、服务整合、USB 家族路由、既有固件流控。
其中 AA5B 协议 168 个检查、服务整合 6631 个检查、USB 路由 429 个检查，均 0 失败。
Release 链接成功，FLASH 55908 B / 128 KB，DTCM 55416 B，DMA D2 缓冲 512 B。
本轮仅构建该更新，未重新烧录；实窗读取的是板上此前已烧录的集成版本。

GUI 保持 `feature/sensor-console-20261006`；固件保持 `feature/sensor-integration-20261006`。
未合入主线，未改动 ZCode 原工作树既有的 5 个未提交文件。原始固件备份保留原处。

剩余整体验收门槛：核实 IMU 与 WCH 串口的实际接线/解除输出共驱，验证真实 IMU 帧与
配置命令；确认深度计实际为 02BA 或 30BA，之后显式设置水面零点并做深度实测。
软件测试和原始 ADC 在线不等于已通过上述硬件验收，不能据此宣布整机完成或统一合主线。
