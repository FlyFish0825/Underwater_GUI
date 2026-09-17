# UI contract boundary

The application has one `MainWindow`, one `QStackedWidget`, and six fixed
navigation IDs: `dashboard`, `motor_debug`, `firmware`, `manipulator`,
`vision`, and `settings`.

Each formal page is a `rov::*Page` QWidget with an explicit constructor and
`setSnapshot(const XxxSnapshot&)`. It emits typed request signals only. A
contract is a value type: it contains business values, units, validity,
freshness, and stable IDs; it does not contain QWidget pointers, transport
objects, protocol headers, command bytes, or CRC fields.

This phase uses deterministic preview values and request logging. It does not
claim a real device connection or implement transport, protocol parsing,
control allocation, firmware flashing, camera streaming, or vision algorithms.

Unknown values display `--`; zero is never used as a substitute for missing
data. Feedback, target values, and user requests remain separate.
