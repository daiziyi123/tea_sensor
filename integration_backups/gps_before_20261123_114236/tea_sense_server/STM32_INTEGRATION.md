# STM32当前接入入口
更新：2026-11-15。协议及参数统一维护于[共同V2文档](../Docs/protocol_v2.md)，硬件/端点见[接线](../Docs/wiring_power_interfaces.md)、[标定](../Docs/device_calibration_register.md)，不在本页维护另一份字段表。
本轮保持严格V1兼容，V2 JSON+CRLF/ACK扩展为十一项、兼容旧十项缺雨强；固件无boot_id、无离线回灌。有效0/失败null/采样身份/年龄/缓存去重语义不变，雨强新增严格可选字段，复用通用measurements/schema2而不改表结构。
现场私有app_secrets.h与.env单独交接；仅源码预览包不能承诺直接向生产鉴权上传。设备号多台应唯一。服务器启动不等于传感器准确或十一项实物已联通；用户手动下载与验收步骤见[构建运维](../Docs/build_operations.md)及[验收表](../Docs/acceptance_records.md)。气体继续暂停，雨强为AD5模拟电气端点换算；先更新并人工重启服务器确认新字段，再手动下载新固件。
