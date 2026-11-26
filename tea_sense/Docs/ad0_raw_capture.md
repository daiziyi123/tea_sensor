# AD0 原始 ADC 记录

> 本文保留此前原始采集阶段的记录。当前已按用户提供的样本统计接入两点相对湿润度，最新参数、输出与验证见 [两点换算记录](soil_two_point_calibration.md)。

2026-11-12：按最新要求保留现有 1000 ms 采集周期。App_Process 复用 SensorSoil_Read / BspAdc_ReadRaw，完成一次采集后直接 printf，沿用 USART2 重定向，每行以 CRLF 结束。

```text
t_ms=1000,channel=AD0,adc_raw=12345,status=OK
t_ms=2000,channel=AD0,adc_raw=0,status=OK
t_ms=3000,channel=AD0,adc_raw=NA,status=ADC_POLL_FAILED
```

示例不是实测记录。有效 0 输出 0；失败输出 NA 和实际主故障，不复用旧值。保留硬件自校准、采集超时及停止清理结果。端子 AD0、PB1、ADC1 Channel 5 对应关系不变；时钟、ADC 分频和采样时间不变。

本轮没有设置 raw_0 或 raw_100，也没有添加或启用标定百分比换算。ADC 16 位范围检查不代表标定端点为 0/65535。既有电压和水分纯换算函数保持未修改、未接入当前采集流程。

后续用户提供接近 0 V、接近 5 V 的两段稳定 raw 记录及对应万用表实测电压后，再在 sensor_soil 中实现两点换算。端点使用稳定记录均值；使用有符号中间量、校验非零有效跨度，并以整数定点值表达百分比。低于或高于标定区间时保留越界状态，不能静默标为正常。本轮不实现这些算法。

本次验证：

- [Host 测试](validation/ad0_raw_1000ms_host.log)：AUTO、短硬件超时、虚拟 GPIO 手动配置三组通过；原始值、失败、回绕和发送异常断言保留。
- [MDK 完整构建](validation/ad0_raw_1000ms_arm_build.log)：ARMCC 5.06，0 errors / 0 warnings；整数 printf 重定向与无半主机链接检查通过。
- [ARM CPU 执行](validation/ad0_raw_1000ms_arm_execution.log)：真实 Arm C 库及生产采集/输出代码配合模拟 HAL，通过 1000 ms 周期、有效零值、失败 NA 和发送恢复检查。
- [架构检查](validation/ad0_raw_1000ms_architecture.log)与[源码保护审计](validation/ad0_raw_1000ms_source_audit.log)通过：1490 个受保护文件哈希未变，周期配置与修改前完全一致；生产代码仅调整 printf 字段及参数。

没有连接板卡、烧录或上板验证，没有 Git 提交。主机与 CPU 模拟结果不能替代实际 ADC 电压、RS485 波形及装配核验。
