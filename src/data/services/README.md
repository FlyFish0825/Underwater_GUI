# Service boundary

Future business services belong here. They will convert protocol objects to
business snapshots, validate typed requests, track timeout/freshness, and
publish values to the GUI thread. The current phase uses fixed preview data
instead of a service implementation.

`ObserverMotorDataService` is the live data service for the fixed-node motor
protocol. It consumes `CanGatewayFrame` objects after the USB CDC/AA55 gateway
layer, calls `ObserverMotorProtocol`, and publishes `ObserverMotorNodeSnapshot`
or the eight-node `ObserverMotorFleetSnapshot`. UI code should read these
snapshots and must not include `CanGatewayProtocol.h` or
`ObserverMotorProtocol.h`.
