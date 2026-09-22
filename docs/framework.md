# ROV UI framework

分层架构图请先阅读 [`architecture.md`](architecture.md)；本文只保留页面骨架和
契约边界，避免把通信、数据和绘图细节重复画在同一张图里。

## Public skeleton

The framework has one `rov::MainWindow`, one `QStackedWidget`, one shared
header/sidebar/footer shell, and six fixed navigation IDs:

```text
dashboard → motor_debug → firmware → manipulator → vision → settings
```

The five formal pages are independent `QWidget` classes in the `rov`
namespace. They do not include transport or protocol headers. Each receives a
value snapshot through `setSnapshot(const XxxSnapshot&)` and emits typed
business requests. The Settings item is an intentionally minimal placeholder.

## Layer boundaries

```text
Transport → Protocol → Service/Data → MainWindow → Pages
                                      ↑            ↓
                                  Requests ← Contracts/Snapshots
```

当前工程已经包含 USB CDC、AA55 网关、Observer Motor 和 Bootloader 服务；页面仍然
通过快照和类型化请求与这些服务交互，不直接读取原始帧。Motor Debug 的控制请求由
`MainWindow` 校验后交给通信服务编码，曲线由 `QwtCurvePlotWidget` 负责显示；高速曲线
的组件选择、窗口预设和尚未接入的原始样本环单独记录在 [`plotting.md`](plotting.md)。
Qt、Fluent-Qt、Qwt 的版本和许可证边界见 [`dependencies.md`](dependencies.md)。

## Reference alignment

- Dashboard: six thrusters, vehicle top view, telemetry, 6-DOF control,
  motor summary, alarms, and quick actions.
- Motor Debug: waveform, signal selection, motor parameters, capture and log.
- Firmware: file metadata, six target nodes, progress, actions and log; no
  bootloader/CAN implementation.
- Manipulator: side-view arm drawing, Cartesian targets, joint table, status
  and quick actions; no kinematics.
- Vision: Windows DirectShow device enumeration, RGB24 frame capture, live
  preview, stream status and still-frame saving. Image-processing modes remain
  typed requests; OpenCV, GStreamer and vision algorithms are not integrated.

All five use the same light scientific/industrial visual language: white and
light-gray surfaces, dark-blue typography, blue emphasis, and green/orange/red
status colors. The reference screenshots are not embedded as UI assets.

## Preview and verification

`src/preview/PreviewData.cpp` provides fixed values and the UI labels preview
state explicitly. The executable accepts `--page N --capture PATH` for local
visual inspection; it starts with a normal Windows platform, captures the
window, and exits. No real device or communication result is implied. The
current preview deliberately covers the IABC and speed series used by the
Motor Debug presets.
