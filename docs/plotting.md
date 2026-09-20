# 高速多窗口曲线方案

## 目标

Motor Debug 需要同时满足：

- 100/1000 Hz 数据接收时不阻塞通信；
- 一个页面可以打开多个独立曲线窗口；
- 每个窗口可以显示不同变量组合；
- 提供默认组合：`IABC` 一组、速度一组，并预留电压/位置/温度等组合；
- 用户可以从变量目录中选择任意变量，自定义窗口和颜色/单位/缩放；
- 支持拖拽、缩放、自动适配、游标、图例和有限长度历史；
- 组件可嵌入 Qt Widgets，项目源码公开时许可证清晰。

## 候选比较

| 项目 | Qt 嵌入 | 高速相关能力 | 多曲线/多窗口 | 许可证 | 结论 |
| --- | --- | --- | --- | --- | --- |
| [Qwt 6.3](https://qwt.sourceforge.io/) | 原生 QWidget | `QwtPlotOpenGLCanvas`、`QwtPlotDirectPainter`、曲线数据容器 | 一个 plot 多曲线；多个 QWidget/QMdi 子窗口 | Qwt License 1.0（LGPL 2.1 + 例外） | **首选**，与 Qt 5.15 和开源发布最稳妥 |
| [QCustomPlot 2.x](https://github.com/ET-BE/QCustomPlot) | 原生 QWidget | 自适应采样、批量 `setData/addData`，适合实时 2D | 一个 plot 多 graph；窗口管理需自己做 | GPL-3.0 | 可作为备选；若采用，项目发布许可证需与 GPL 兼容 |
| [Qt Charts](https://doc.qt.io/qt-5/qtcharts-index.html) | Qt Widgets/QGraphics | 使用方便，但基于 Graphics View，不作为高频首选 | 支持多 series；窗口管理需自己做 | GPL-3.0 或商业 | 不选作本项目主后端，性能和许可证约束都不如 Qwt |
| [ImPlot](https://github.com/epezent/implot) | 不是 Qt 控件 | GPU/即时模式，适合高密度实时图 | 依赖 Dear ImGui，自建 Qt 桥接和窗口层 | MIT | 性能有吸引力，但会引入第二套 UI 栈，暂不采用 |

Qwt 官方文档明确提供 OpenGL 画布和 DirectPainter：前者直接使用
`QOpenGLWidget`，后者只绘制新增点，避免每个采样周期完整重绘。Qwt 6.3
也明确兼容 Qt 5/6。QCustomPlot 的仓库和示例适合快速嵌入，但其 GPL-3.0
许可证需要在本项目公开发布时一并遵守。

## 选型结论

本项目已采用 Qwt 6.3.1 作为第一实现后端。上游源码完整保存在
`third_party/qwt-6.3`，保留 `COPYING` 和原始目录；工程通过独立 CMake 静态库目标
`rov_third_party_qwt` 接入，没有截取或改写成不可追溯的私有绘图代码。本项目只维护
窗口管理、变量目录、数据缓存和预设。

当前实现边界：

- `MotorDebugPage` 支持多个曲线窗口，每个窗口可叠加多个 `DebugSeries` 并独立缩放；
- 已提供 `IABC`、`Speed` 和自定义窗口，变量下拉框可继续向当前窗口叠加曲线；
- Qwt 后端已提供图例、自动适配、框选缩放、中键平移和坐标跟踪；
- `ObserverMotorDataService` 当前发布约 50 ms 的节点快照，高频 debug 样本环尚未
  接入曲线链路；
- 窗口布局保存、原始高速样本 ring buffer 和 1 kHz 压力测试仍属于后续实现。

当前实现不直接用 Qwt 接收串口线程数据，而是保持以下边界：

```text
ObserverMotorDataService
    ↓ 结构化快照/批量样本
PlotDataRouter
    ↓ 有界 ring buffer，按变量 ID 分流
CurveWorkspace
    ↓ 20~60 Hz UI 定时器
QwtPlot / QwtPlotCurve
```

## 窗口与默认预设

每个 `CurveWindow` 保存自己的曲线选择和缩放状态。已落地的预设是
`IABC` 和 `Speed`；其他预设在对应协议字段确认后补入：

| 预设 | 默认变量 | 典型用途 |
| --- | --- | --- |
| `IABC` | `phase_current_u`, `phase_current_v`, `phase_current_w` | 观察三相电流是否平衡、饱和或畸变 |
| `Speed` | `speed_rpm`, `pll_electrical_speed` | 对比实际转速与观测速度；目标转速待反馈字段确认 |
| `Voltage` | `vbus`, `vd`, `vq` | 观察母线和 D/Q 电压 |
| `Position` | `electrical_angle`, `observer_angle` | 观察角度跟踪和跳变 |
| `Temperature` | `mos_temperature`, `board_temperature` | 观察热状态 |
| `Custom` | 用户从变量目录选择 | 临时研究和故障复现 |

后续窗口增强项：

- 新建、关闭、复制窗口；
- 选择预设或切换为自定义；
- 变量勾选、显示名、单位、颜色和左/右 Y 轴；
- 自动适配、时间范围、暂停/继续、清空缓存；
- 鼠标游标和同一 X 时刻的多变量读数；
- 窗口布局保存与恢复。

## 性能规则

1. 通信线程只解析和批量提交，不调用任何 QWidget/Qwt 方法；
2. 每个变量使用有界 ring buffer，历史长度按时间窗口计算；
3. UI 定时器按 20~60 Hz 批量刷新曲线；
4. 视口内点数明显多于像素列时启用抽稀/最值保留，不绘制不可见点；
5. 改变坐标范围或窗口大小时允许完整 replot；普通追加优先使用增量绘制；
6. 不绘图、不在 Motor Debug 页面时直接丢弃绘图路径，不打印、不保存高频文本；
7. 任何高频测试都记录采样数、绘图刷新数、丢弃数、队列最大长度和耗时，不能
   只凭“窗口看起来流畅”声称达到 1000 Hz。

## 已完成与后续顺序

1. 已将可追溯的 Qwt 6.3.1 第三方目录和 CMake 目标加入工程，保留许可证；
2. 已把原先的单变量 `QPainter` 绘图替换为 `QwtCurvePlotWidget`；
3. 已接入 `IABC`、`Speed` 两个预设，以及可叠加变量的自定义窗口；
4. 下一步接入按节点选择的真实反馈快照到曲线样本；
5. 再增加有界 ring buffer 和无设备的 1 kHz 合成数据压力测试，检查 UI 刷新、缓存和日志计数；
6. 最后用真实示波器/设备数据验证协议采样时间和曲线单位。

## 许可证记录

- Qwt：保留 `Qwt License, Version 1.0`，并在发布说明中声明使用 Qwt；
- QCustomPlot：若改用，保留 GPL-3.0 文本并评估整个应用的 GPL 兼容性；
- Qt 本身和现有项目许可证按 `docs/toolchain.md` 与仓库许可证执行。
