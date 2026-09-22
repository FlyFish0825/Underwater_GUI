# 服务边界

业务服务负责把协议对象转换为业务快照、校验类型化请求、维护超时与新鲜度，并把结果
发布到 GUI 线程。没有真实设备反馈时只发布离线状态，不生成模拟数据。

`ObserverMotorDataService` 是固定节点电机协议的实时数据服务。它接收 USB CDC/AA55
网关层输出的 `CanGatewayFrame`，调用 `ObserverMotorProtocol`，并发布
`ObserverMotorNodeSnapshot` 或包含八个节点的 `ObserverMotorFleetSnapshot`。UI 代码只读取
这些快照，不得包含 `CanGatewayProtocol.h` 或 `ObserverMotorProtocol.h`。
