# ROV UI team guide

The single authoritative Agent and collaboration reference is
`agent/agent.md`. Protocol-specific layering remains in
`agent/ROV上位机通信协议与分层架构规范_v1.0.md`; the public build uses the
versions recorded in `docs/toolchain.md`.

The current delivery has one Qt Widgets main window, one stacked page
container, shared theme/navigation/footer, five reference-aligned page
frames, a Settings placeholder, contracts, USB CDC/CAN gateway communication,
Observer Motor data handling, and Bootloader services. Hardware behavior must
still be verified separately; UI pages continue to consume snapshots and emit
typed requests rather than parsing raw frames.
