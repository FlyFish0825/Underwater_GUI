# UI contract boundary

The application has one `MainWindow`, one `QStackedWidget`, and six fixed
navigation IDs: `dashboard`, `motor_debug`, `firmware`, `manipulator`,
`vision`, and `settings`.

Each formal page is a `rov::*Page` QWidget with an explicit constructor and
`setSnapshot(const XxxSnapshot&)`. It emits typed request signals only. A
contract is a value type: it contains business values, units, validity,
freshness, and stable IDs; it does not contain QWidget pointers, transport
objects, protocol headers, command bytes, or CRC fields.

Preview mode still uses deterministic values, but the normal application now
also has USB CDC transport, AA55 gateway parsing, Observer Motor snapshots,
Bootloader services and a Windows DirectShow camera service. A UI contract still does not claim that a real device is
connected: pages consume snapshots and emit typed requests, while transport,
protocol parsing, control validation and firmware flashing remain below the
page boundary. `VisionPage` emits typed camera-control requests; `MainWindow`
routes them to `CameraCaptureService`, which publishes camera lists, snapshots
and `QImage` frames. Vision algorithms remain out of scope.

Unknown values display `--`; zero is never used as a substitute for missing
data. Feedback, target values, and user requests remain separate.
