# 当前整合入口
2026-11-23：固件0.10.0-dev、schema_version=2、标定baseline-3。业务已改为180秒分类统计；采样100ms及原气象查询不变；串口继续静默。服务器兼容旧三秒记录与新180秒帧，增量数据库schema4，GPS独立。

- [实际改动、参数表、方法、验证、构建/启动/回滚及DTU手动步骤](../../Docs/three_minute_statistics.md)
- [当前协议](../../Docs/protocol_v2.md)
- [三层架构](code_architecture.md)
- [原接线/供电](../../Docs/wiring_power_interfaces.md)
- [标定登记](../../Docs/device_calibration_register.md)
- [GPS解析与安全](../../Docs/gps_integration.md)

先由用户备份并切换服务器，再手动构建/下载固件。本轮未停止现场服务、改真实历史、自动烧录或改DTU。旧Docs/validation日志是对应历史轮次的证据，不代表当前固件实机运行。
