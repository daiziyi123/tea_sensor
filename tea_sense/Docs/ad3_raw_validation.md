# AD3原始采样与电气校准观察

本文保留原始采样阶段的交付记录。当前已根据实测1V/5V端点增加单点电压与角度换算，秒级输出改为WIND_DIRECTION；AD3_WINDOW原始统计保持，详见[当前换算与验证](wind_direction_validation.md)。

2026-11-13（北京时间）。本轮仅修改tea_sense固件及测试/文档，未烧录、未初始化或提交Git。

## 接入位置与接口

- Bsp/Inc/bsp_adc.h：BspAdcInput增加BSP_ADC_INPUT_WIND_DIRECTION_AD3，原有BspAdc_ReadRaw(input)签名不变。
- Bsp/Src/bsp_adc.c：新增AD3→ADC_CHANNEL_19映射；原有停止、配置、有限超时采集及清理流程复用，无新驱动。
- App/Inc/app_main.h：最近完整轮次增加AD3独立结果、读取起止tick和round_span_ms；增加只读AD3窗口快照和累计错误数。
- App/Src/app_main.c：原100ms轮次依次读取AD0→AD2→AD3；每秒直接printf最近AD3；共用3秒边界冻结/清空AD3独立普通均值统计。
- Tests/stubs/stm32h7xx_hal.h、soil_host_test.c、dtu_host_test.c：第三通道HAL桩和独立数据/错误注入。
- 新增Tests/ad3_cases.h：通道轮换、原始统计、时间、边界、故障及土壤上传回归。
- Tests/check_architecture.py、check_arm_printf.py：PA5/Channel19核对、真实Arm printf及三路顺序验证。
- Docs/code_architecture.md、本文及Docs/validation/ad3_*.log：架构、验收与本次验证证据。

没有新增生产源文件，MDK原有App/Bsp分组与Sensors头文件路径已覆盖全部改动，工程文件无需增删引用；架构检查验证无重复编译。Sensors模块、App上传格式化/解析器、配置和私有密钥未改，AD2的s_voltage_calibration仍为UNCONFIRMED。

## 硬件与调度

实际核对tea_sense.ioc：PA5.Signal=ADCx_INP19；Core/Inc/main.h定义WIND_DIR_AD3_Pin=GPIO_PIN_5、GPIOA。Core/Src/adc.c的ADC1_MspInit已将该引脚配置为GPIO_MODE_ANALOG、GPIO_NOPULL。无需CubeMX补项，本轮未修改生成代码。

ADC1仍为16位、异步分频8、单次/软件触发、一个Rank、初始Channel5、387.5 Cycles，无ADC中断/DMA。每次读取重新选择5→3→19，下一轮重新选择5；每次配置前先Stop，检查Config/Start/Poll/GetValue范围/Stop，初始化自校准不重复。

APP_SAMPLE_ROUND_PERIOD_MS=100、APP_CONSOLE_PERIOD_MS=1000、APP_SAMPLE_WINDOW_MS=3000均沿用app_config.h。三路按轮次round_t_ms进入[start,end)窗口，边界轮次进入新窗；AD3完成时间跨界仍属于原轮次。AD3的t_ms为读取开始tick，另保存读取结束tick。pair_span_ms仍表示AD0+AD2，round_span_ms覆盖三路。毫秒相同不代表同时转换。

AD3独立累计sample_count、valid_count、uint64_t raw_sum、min/max及错误诊断，全部有效raw参与普通均值；没有去极值、滤波、80%质量门槛、电压或角度换算。均值x100=(raw_sum×100+valid_count/2)/valid_count；valid_count为0时不除。先冻结再重置，打印不读新窗口累加器。raw=0有效；读取或清理失败仅计错误，不加入统计。同步串口输出与有限超时采集可能造成调度迟到，统计实际次数，不补采/复制缓存；跨多个窗口时沿用已有跳过诊断，最近已结束的空窗输出NA。

## 实际执行路径输出示例

以下来自本次Arm CPU执行的真实Arm C库→_sys_write→BspConsole→模拟HAL，不是上板测量；串口实际结尾为CRLF。

```text
t_ms=1000,channel=AD3,pin=PA5,adc_channel=19,adc_raw=23456,status=OK
AD3_WINDOW,window_end_ms=3000,sample_count=30,valid_count=30,adc_raw_min=0,adc_raw_max=23456,adc_raw_mean=15637.33,error_count=0,cleanup_errors=0,prepare_errors=0,config_errors=0,cal_failed=0,start_failed=0,poll_failed=0,stop_failed=0,raw_range=0,last_error=NONE,last_stage=NONE,hal=NA,prepare=NA,config=NA,stop=NA
```

Host故障注入验证的采样失败格式，例如Poll超时且清理失败：

```text
t_ms=3000,channel=AD3,pin=PA5,adc_channel=19,adc_raw=NA,status=ADC_POLL_FAILED,stage=POLL,hal=HAL_TIMEOUT,prepare=HAL_OK,config=HAL_OK,stop=HAL_ERROR
```

无有效样本时min/max/mean均为NA；错误计数和last_error/stage/HAL/prepare/config/stop仍保留。CONFIG阶段沿用既有ADC_START_FAILED类别，以stage=CONFIG及config=HAL_*区分；PREPARE沿用ADC_STOP_FAILED。土壤SOIL_WINDOW、AD2 WIND_WINDOW和DTU_TX/ACK/错误日志保持。

## 本次验证

- Host：GCC -Wall -Wextra -Werror，4种配置×土壤/DTU两个程序，共8组通过；另通过原有编译配置保护。新增覆盖5→3→19→5、独立值和错误、共享不可恢复错误、Config/Start/Poll/Stop失败、硬件自校准只在初始化、有效0/65535、异常宽raw、普通均值保留极值/两位小数、无/仅一个有效样本、窗口重置/跨界/tick回绕/迟到、使用样本时间、打印不触发额外采样及ACK等待中继续采集。
- ARMCC 5.06完整MDK重编译链接：0错误、0警告；Code=35896、RO-data=2384、RW-data=88、ZI-data=4720字节。重定向符号归属、无重复定义、无半主机及浮点printf检查通过。启动文件/链接配置未改。
- Arm CPU模拟：91个完整轮次、273次严格5/3/19选择、546次前后Stop、一次自校准；真实Arm printf验证AD3原始值、普通均值15637.33、0.00与65535.00，以及USART2发送错误恢复；土壤3条JSON及USART6 ACK仍通过，无AD3报文字段，无运行时malloc。HAL模拟不能代替上板验证。
- 土壤报文回归：生产C formatter→真实本机TCP→提交临时SQLite→鉴权API/CSV→生产C ACK parser通过，包括无效窗口null和配置错误类别；使用测试密钥/演示设备，不打开生产数据库，不访问公网DTU。
- 架构与保护审计：Core/Drivers/.ioc、Sensors、上传格式化器/配置/密钥、串口模块、MDK源分组/启动链接设置及服务器代码/数据/界面保持不变。

证据分别见validation/ad3_host_tests.log、ad3_arm_build.log、ad3_arm_printf.log、ad3_soil_wire_regression.log、ad3_architecture.log、ad3_protection.log。以上均为本轮执行结果，未拿历史HEX或日志替代。

## 待上板验证

1. 用户自行烧录后用USART2 115200、8N1查看每秒AD3行及每3秒AD3_WINDOW；核对时间、实际次数、min/max/mean和错误，成功0应显示0。
2. 固定AD0/AD2，仅改变AD3端子输入；确认AD3变化、AD0/AD2不应明显跟随，土壤DTU上报仍为AD0。再反向改变AD0/AD2，检查AD3无通道串入。
3. 对照同一输入单独读取和5→3→19交替读取的结果；若发现建立时间或串扰，再依据实测决定措施，本轮不添加猜测性延时/重试。
4. 仅将已知电压接到确认适用的板卡AD3端子，不能直接给MCU PA5施加5V。每个稳定状态保存原始日志/3秒统计，并同步记录万用表实测端子电压，供后续独立电气校准。
5. 检查连续运行、故障恢复、调度迟到/欠采与公网DTU ACK；这些硬件、电气精度和实际网络行为尚未实测。
