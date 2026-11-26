# 土壤 AD0 两点相对湿润度

打印更新：成功值带两位小数及 %，失败值仍为 NA；仅修改 App_Process 的格式字符串，采集与换算结果直接复用。最新 [host 检查](validation/soil_percent_output_host.log)、[MDK 编译](validation/soil_percent_output_arm_build.log)、[真实 Arm C 库 CPU 执行](validation/soil_percent_output_arm_execution.log)、[分层](validation/soil_percent_output_architecture.log)及[差异审计](validation/soil_percent_output_source_audit.log)均通过，MDK 为 0 errors / 0 warnings。没有烧录或上板验证。

2026-11-12（北京时间）。端点采用用户提供的全部有效样本统计，未从其他资料或偶发最值估计：

| 状态 | 有效样本数 | 用户给定平均 raw | 四舍五入端点 | 显示值 |
| --- | --- | --- | --- | --- |
| 空气中擦干 | 90 | 435.8222 | 436 | 0.00% |
| 完全浸水 | 37 | 31145.8919 | 31146 | 100.00% |

这里定义空气干燥至完全浸水的相对湿润度，不代表体积含水率或经土样验证的绝对含水率。标定参数固定集中在 Sensors/Inc/sensor_soil.h：SENSOR_SOIL_RAW_DRY=436U、SENSOR_SOIL_RAW_WET=31146U，跨度 30710。编译时校验湿端大于干端且不超过 16 位 ADC 范围；没有在线端点更新、存储或开机自动校零。

## 唯一换算路径

SensorSoil_Read 复用 BspAdc_ReadRaw；仅在 adc.valid 有效时调用 SensorSoil_MoisturePercentX100(uint16_t raw)：

```text
raw <= 436：moisture_pct_x100 = 0
raw >= 31146：moisture_pct_x100 = 10000
其他：((raw - 436) * 10000 + 30710 / 2) / 30710
```

先判断边界再相减，乘法使用 uint64_t，中间范围只舍入一次。moisture_pct_x100 单位为 0.01%，5032 表示 50.32%。旧的土壤传感器毫伏还原接口、按 0～5 V 计算水分的旧签名及其增益/电压配置已删除；本路径不使用 VREF、分压增益或 0～5 V 公式。

SensorSoilData 保留本次原始 ADC 结果及清理诊断，增加百分比和范围状态。raw < 436 标记 LOW，raw > 31146 标记 HIGH；恰好等于端点属于正常标定范围。越界只是范围提示，ADC 成功仍为 status=OK，原始值不钳位；显示百分比限制为 0～100%。ADC 失败时范围不可用、不计算百分比、不使用上次结果，应用输出双 NA。

## App 直接输出

1000 ms 周期保持不变，App_Process 一次采集后直接 printf，通过整数除以 100 和余数 %02lu%% 拼接带百分号的两位小数，无业务打印包装、浮点 printf、模式或日志模块。

```text
t_ms=1000,channel=AD0,adc_raw=15791,moisture_pct=50.00%,status=OK
t_ms=2000,channel=AD0,adc_raw=0,moisture_pct=0.00%,status=OK,range=LOW
t_ms=3000,channel=AD0,adc_raw=65535,moisture_pct=100.00%,status=OK,range=HIGH
t_ms=4000,channel=AD0,adc_raw=NA,moisture_pct=NA,status=ADC_POLL_FAILED
```

示例不是上板实测记录，每行实际以 CRLF 结束。

## 实际修改文件

- Sensors/Inc/sensor_soil.h：端点、跨度校验、唯一 raw 换算接口、范围与数据字段。
- Sensors/Src/sensor_soil.c：整数两点换算、有效性及范围判定，移除旧土壤换算路径。
- App/Src/app_main.c：直接输出 raw、定点百分比、范围提示或失败双 NA。
- Tests/soil_host_test.c：端点、舍入、范围、全域单调性、失效结果及实际 printf 输出断言。
- Tests/run_host_tests.py：增加 VREF 独立性变体和零跨度编译拒绝检查。
- Tests/check_arm_printf.py：验证真实 Arm C 库的两位小数和六项指定情形。
- Docs/code_architecture.md、soil_sensor_bringup.md 更新当前行为；ad0_raw_capture.md、refactor_report.md 标明此前阶段；本文记录本轮交付。

## 本次验证

| 输入 | 固定点结果 | 显示 | 范围/有效性 |
| --- | --- | --- | --- |
| raw=0 | 0 | 0.00% | LOW，ADC 有效 |
| raw=436 | 0 | 0.00% | 标定范围内 |
| raw=15791 | 5000 | 50.00% | 标定范围内 |
| raw=31146 | 10000 | 100.00% | 标定范围内 |
| raw=65535 | 10000 | 100.00% | HIGH，保留原始值 |
| ADC 失败 | 不计算 | NA | 保留实际 ADC 主故障与清理结果 |

- [Host](validation/soil_two_point_host.log)：默认 AUTO、改变板级 VREF、短硬件超时、虚拟 GPIO 手动配置四组通过；所列映射、相邻端点、四舍五入和全 65536 个输入单调且落在 0～10000 的检查通过；未确认手动配置与零跨度均被编译拒绝。改变 VREF 不改变两点结果。
- [ARM 正式构建](validation/soil_two_point_arm_build.log)：本次从当前源码完整 Rebuild，ARMCC 5.06，0 errors / 0 warnings。唯一 USART2 printf 重定向、无重复符号、无半主机指令和无浮点 printf 检查通过。
- [ARM CPU 执行](validation/soil_two_point_arm_execution.log)：真实 Arm C 库、生产 App/Sensors/Bsp 与模拟 HAL 验证指定映射、0.01 舍入输出、LOW/HIGH、失败双 NA 和发送恢复；每周期仅一次 ADC 采集，测试路径无 malloc 调用。这是 CPU 模拟，不是上板验证。
- [分层检查](validation/soil_two_point_architecture.log)及[源码保护审计](validation/soil_two_point_source_audit.log)：1000 ms 配置、Core、全部 Bsp、HAL/CMSIS、.ioc 和 MDK 配置保持；旧土壤电压/增益换算没有活动引用。

AD0=PB1/ADC1 Channel 5，时钟、异步分频 8、387.5 cycles、单 Rank 等配置不变；ADC 硬件自校准、有限超时、主故障及停止清理故障处理不变。无其他传感器改动、滤波、参数存储、RTOS 或 DMA。本轮未连接板卡、烧录、上板验证或提交 Git。
