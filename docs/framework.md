# ROV UI framework delivery

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
future Transport → future Protocol → future Service/Data → contracts → pages
                                                               ↑
                                                            MainWindow
```

This delivery contains only the UI, contracts, deterministic preview data,
and request logging. `src/communication/README.md` and `src/data/README.md`
mark the future boundaries without inventing device code.

## Reference alignment

- Dashboard: six thrusters, vehicle top view, telemetry, 6-DOF control,
  motor summary, alarms, and quick actions.
- Motor Debug: waveform, signal selection, motor parameters, capture and log.
- Firmware: file metadata, six target nodes, progress, actions and log; no
  bootloader/CAN implementation.
- Manipulator: side-view arm drawing, Cartesian targets, joint table, status
  and quick actions; no kinematics.
- Vision: camera placeholder, connection/stream/node status, planned display
  modes and roadmap; no camera, OpenCV, GStreamer or vision algorithm.

All five use the same light scientific/industrial visual language: white and
light-gray surfaces, dark-blue typography, blue emphasis, and green/orange/red
status colors. The reference screenshots are not embedded as UI assets.

## Preview and verification

`src/preview/PreviewData.cpp` provides fixed values and the UI labels preview
state explicitly. The executable accepts `--page N --capture PATH` for local
visual inspection; it starts with a normal Windows platform, captures the
window, and exits. No real device or communication result is implied.
