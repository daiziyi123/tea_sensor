# 土壤 AD0 采样、两点相对湿润度与 USART2 输出

当前流程和模块职责见 [代码架构](code_architecture.md)，本轮标定参数和验证见 [两点换算记录](soil_two_point_calibration.md)。

- AD0 端子连接 PB1，使用 ADC1 Channel 5；不要混淆端子号、引脚号与 ADC 通道号。
- ADC1 保持 16 位、异步分频 8、软件触发、单 Rank、387.5 cycles；初始化保留 H7 单端 offset + linearity 硬件自校准。
- 每 1000 ms 采集一次 raw，Sensors 按 436 / 31146 两点换算为相对湿润度，App 直接 printf，不重复读取 ADC，也不叠加电压或增益补偿。
- 电脑 USART2 为 PD5/PD6、115200、8N1；stdout 通过 bsp_console.c 中的标准 Arm C 库 _sys_write 发送，CRLF 换行且不缓存 stdout。
- 有效 raw=0 输出 0.00% 并标记 LOW；高于湿端仍输出真实 raw、100.00% 并标记 HIGH。这两种范围标记不代表传感器故障。ADC 校准、启动、轮询、停止或 16 位范围检查失败时 raw 和百分比均为 NA。SensorSoilData.adc 还保留 HAL 与停止清理结果。
- App_GetDiagnostics 查看序号和 printf 错误数；BspConsole_GetDiagnostics 查看发送调用、错误数、最后结果及 stdout 无缓冲状态。
- AUTO 方向配置保留，默认不访问方向 GPIO。手动配置必须通过既有装配确认、单引脚和极性校验，仅作用于 USART2。

```text
t_ms=1000,channel=AD0,adc_raw=15791,moisture_pct=50.00%,status=OK
t_ms=2000,channel=AD0,adc_raw=NA,moisture_pct=NA,status=ADC_POLL_FAILED
```

示例不是上板记录。此次换算使用用户给定的全部有效样本统计，90 个空气擦干样本均值 435.8222 取 436；37 个完全浸水样本均值 31145.8919 取 31146。它表示相对湿润度，不代表体积含水率；换算不依赖额外 VREF 或分压参数。旧硬件检查及此前实现记录完整保存在 [历史资料](history/2026-11-12-before-printf/soil_sensor_bringup.md)，不作为本次构建或实测依据。本轮未烧录或上板验证。
