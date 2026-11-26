# AD5 原始采样与电脑串口对齐
更新：2026-11-15。本轮仅增加原始采样、USART2打印和必要诊断/测试；未换算雨量、电压、累计量或雨强，未新增滤波/标定/调试模式/打印框架或sensor_rain模块。未自动烧录。

## 硬件核对与边界
[本轮用户指定＋当前代码核对] AD5/AIN5→PA3→ADC1 ADC_CHANNEL_15，CubeMX标签RAIN_AD5。当前.ioc有PA3.Signal=ADCx_INP15及ADC1_INP15映射；Core/Inc/main.h定义GPIO_PIN_3/GPIOA，Core/Src/adc.c将RAIN_AD5_Pin配置为GPIO_MODE_ANALOG、GPIO_NOPULL。原PE15 RAIN_PULSE、GPIO专用EXTI/NVIC和C中断入口已撤销；启动向量表通用弱入口不代表启用了雨量EXTI。Core下Backup仍保留历史版本，不参与当前配置核对。
以上用户已生成配置作为本轮基线；未修改.ioc、Core、Drivers或在USER CODE内重复初始化GPIO。ADC1保持16位、异步分频8、软件单次、一个Rank、初始Channel5、387.5Cycles，无ADC中断/DMA。时钟/串口/方向及受控恢复保持。

**接线是AD5板级模拟输入，当前只输出raw。实际输出电压/电流量程、对应雨量范围及换算关系尚待确认。** 不把板级端子电压当成PA3引脚电压，不假定分压比例，不根据raw猜传感器端电压或雨量。万用表对齐、实际线束/供电/量程与后续标定待执行或待确认。

## 实现与所有权
- BspAdcInput末尾追加BSP_ADC_INPUT_RAIN_AD5=4，原AD0/AD2/AD3/AD4枚举值0/1/2/3不变。复用唯一BspAdc_ReadRaw，显式配置Channel15；准备Stop→Config→Start→有限Poll→原始范围检查→Stop，主故障及配置/清理结果完整保留。
- App_AcquireRound沿用100ms轮次，在AD4后追加AD5；顺序5→3→19→18→15→下一轮5。AD5末尾正常Stop，下一轮正常显式选5，不额外转换“恢复通道”。
- 只在AppDiagnostics中保存ad5_sequence（尝试数，失败也递增）、ad5_sample_t_ms（Bsp读取返回后完成tick）、last_ad5（完整BspAdcResult）及ad5_sample_errors。无extern可写全局，无AD5窗口累加器。last_round的round_span_ms包含第五路耗时，原AD0/AD2 pair时间语义不变。
- AppSensorSnapshot、AppUpload及V2十项保持原样，AD5不参加四路3秒窗口、不上传。原24/30门槛、精确换算、圆周统计、气象及DTU调度不变；AD5错误不覆盖其他路有效结果。
- 原ADC自校准/60000ms受控恢复、DTU竞态/锁定恢复、单TX缓冲、coreJSON和版本摘要均保留。

## 实际串口格式
app_config.h新增APP_RAIN_RAW_PRINT_PERIOD_MS，默认1000U；仅控制电脑口打印。App_Process在主循环直接printf，不在Bsp/ISR打印。每次到期仅输出最新一次已完成尝试；按无符号tick差调度、迟到不补打，不为打印启动ADC。首个采样完成前不输出假值；seq不是打印次数，t_ms不是打印执行时间。有效raw0及65535照常打印。
以下每条实际为一整行CRLF：
~~~
AD5_SAMPLE,seq=120,t_ms=12345,channel=AD5,adc_raw=15700,status=OK
AD5_SAMPLE,seq=130,t_ms=13345,channel=AD5,adc_raw=NA,status=ADC_POLL_FAILED,stage=POLL,hal=HAL_TIMEOUT,prepare=HAL_OK,config=HAL_OK,stop=HAL_OK
~~~
失败时raw始终NA；prepare/config/stop分别为真实阶段结果，未执行时NA。Poll失败同时Stop失败时保留hal=HAL_TIMEOUT、stop=HAL_ERROR，不用清理错误覆盖主故障。配置失败沿用ADC_START_FAILED类，stage=CONFIG/config=具体HAL原因区分。零位以下不做任何物理限幅；raw超65535为ADC_OUT_OF_RANGE、stage=VALUE、无效。
沿用printf返回值检查、printf_errors和clearerr(stdout)，失败也推进打印间隔，避免热重试。原三秒中文物理行和一次启动版本摘要保持；只新增AD5_SAMPLE，不恢复其他通道raw日志，无浮点printf。

## 本轮验证与证据
当前基线host先通过；原架构检查因仍要求PE15下降沿失败，已定向改为检查新PA3/CH15模拟无上下拉以及雨量EXTI已撤销，其他分层/硬件/唯一回调检查保留。未用重复GPIO初始化掩盖配置。
最终命令、退出码和产物哈希见ad5_validation.json，日志ad5_host.log、ad5_architecture.log、ad5_arm_build.log、ad5_arm_cpu.log、ad5_wire.log。
- Host五配置×soil/dtu=10程序及2000ms窗口变体：新增AD5枚举/通道/0/65535/越界/prepare/config/start/Poll/Stop/校准故障与双故障、精确五通道顺序、无额外采样、真实完成时间、每秒最多一条、迟到/回绕、printf失败恢复和四窗/V2独立性；原竞态、受控恢复、CRC/严格ACK等断言保留。
- 架构：核对当前生成GPIO/ADC，Sensors/Bsp分层与唯一UART回调；AD5未进入App数据模型/上传，不新增雨量业务模块。
- ARM：当前源码完整重建0error/0warning，标准Arm C库，无float printf/半主机。CPU仿真182轮/910次显式选通/1820次Stop/一次校准，核对AD5有效0/65535及Poll+Stop双故障原始行、CRLF/身份/时间、完整V2六次上传/ACK和原物理值；实测应用调用栈656/1024字节，不含真实ISR抢占。
- C→临时TCP→临时SQLite→鉴权API/CSV→C ACK继续通过：24包/128独立点。仅demo/fixture/临时库，不读写生产.env或数据库；服务器与前端源码未修改。
实际上板、PA3与万用表对齐、真实供电/量程、ISR/完整轮次实时负荷和传感器精度尚待执行/确认。未烧录、未改服务器或DTU配置。

## 记录与下一步
本轮执行前副本见../ad5_backup_path.txt（本机私下保管，备份可能含私有配置，不外发）；当前用户生成的硬件与现有有效源码均保留。前次Releases/source-preview-20261115和release_record.json是稳定化交付快照，不含本轮AD5，未覆盖或重新打包；本轮新HEX/AXF对应ad5_validation.json中的哈希。
用户手动编译/下载后，USART2按115200/8N1、UTF-8接收，保存AD5_SAMPLE及同期万用表实际读数，记录输入档位/接线/供电/仪表信息。等待提供实测数据后再做对齐和标定；本轮到此停止，不推算比例或雨量。
