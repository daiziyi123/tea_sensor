# Tea Sense 当前工程
2026-11-23。0.10.0-dev / V2 / baseline-3，180秒业务分类统计，ADC100ms、气象10/30/60s不变，串口继续关闭。
- 前端在tea_sense_server/web，随服务器提供，无需npm。默认 http://127.0.0.1:8000，TCP9000；已有实例勿重复启动。
- [本次交付/参数/方法/验证/切换回滚/DTU步骤](Docs/three_minute_statistics.md)、[协议](Docs/protocol_v2.md)、[架构](tea_sense/Docs/code_architecture.md)、[运维](Docs/build_operations.md)、[GPS](Docs/gps_integration.md)。
- 用户选择时间备份、切换并重启唯一服务器（根start_server.cmd），然后手动Rebuild/下载新固件，刷新网页。本轮未停现场服务、未烧录、未改DTU或正式历史。
- 原包/备份/日志保留，旧包不是本轮发布物。逐文件清单见Docs/three_minute_changed_files.json。
# tea_sensor
