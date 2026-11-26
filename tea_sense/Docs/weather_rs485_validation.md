> 当前业务于2026-11-14改为四路合格窗口与十项V2，见 [integration_v2.md](integration_v2.md)。下文保留为此前阶段记录，一秒/raw输出与旧单土壤协议不再代表当前行为。

# 百叶箱两路RS485查询、缓存与检测

2026-11-13（北京时间）。本轮只接入两路固定Modbus RTU查询；不上传百叶箱数据，不修改服务器/数据库/前端/密钥，无Git初始化、提交或自动烧录。

## 硬件核对及配置

| 路线 | 端子/串口 | 引脚 | 串口参数 |
| --- | --- | --- | --- |
| 气象 | A3/B3，USART3 | PD8 TX、PD9 RX | 4800、8N1、地址01 |
| 气体 | A4/B4，UART4 | PC10 TX、PC11 RX | 4800、8N1、地址01 |

首次检查发现.ioc和生成代码均缺少USART3/UART4全局中断。用户已在CubeMX勾选这两项并重新生成，随后核对：NVIC两项均启用，usart.c包含相应EnableIRQ，stm32h7xx_it.c的USART3_IRQHandler/UART4_IRQHandler分别调用HAL_UART_IRQHandler(&huart3)/(&huart4)。两路波特率/引脚与.ioc一致。以用户重新生成后的文件建立保护基线，后续未手改Core生成区域、.ioc或Drivers。

采用现有自动方向电路；新Bsp不调用BspRs485Dir、不操作PB7/PD3/PD4，不重新初始化UART，不使用DMA或阻塞接收。

app_config.h集中管理六项周期、三次无合法响应门槛和60000ms探测周期；bsp_config.h集中管理50ms发送超时、1000ms响应超时、250ms事务间隔及8ms接收静默间隔。现有100ms ADC轮次、1000ms中文ADC输出和3000ms土壤窗口/上传配置不变。

## 请求与单位

| 项目 | 路线 | 周期ms | 请求（CRC低字节在前） |
| --- | --- | ---: | --- |
| TEMP_HUM | USART3 | 10000 | 01 03 00 00 00 02 C4 0B |
| LIGHT | USART3 | 10000 | 01 03 00 02 00 02 65 CB |
| PM | USART3 | 30000 | 01 03 00 14 00 02 84 0F |
| PRESSURE | USART3 | 60000 | 01 03 00 0B 00 01 F5 C8 |
| CO2 | UART4 | 30000 | 01 03 00 07 00 01 35 CB |
| OXYGEN | UART4 | 60000 | 01 03 00 08 00 01 05 C8 |

SensorWeather_BuildRequest运行时计算CRC；TX为8个原始二进制字节，不是ASCII命令。健康启动依表中顺序各查一次，之后各项目按自己的最近实际发起时间判到期；查询失败也记录该时间。没有周期积压队列或立即重试。同一路只有一笔事务，正常轮询可在两路独立进行。

温湿度的第一个寄存器为湿度×10（uint16），第二个为温度×10（int16）；光照按高字/低字合成uint32；PM25/PM10为两个整数；气压为kPa×100；CO2为ppm整数；氧气为百分比×10。App用整数除余打印负温度及一/两位小数，不启用浮点printf。未加入任何旧经验补偿。

## 收发与状态处理

每路64槽环形缓冲保留字节及接收ISR的毫秒tick，满时计数并报告数据丢失，不覆盖未读数据。主循环短临界区读出，每次最多16字节，每路每循环最多四批；ISR只收字节、记录事件、重启单字节HAL_UART_Receive_IT。UART错误在主循环只Abort/恢复本路接收，不重新初始化全部串口。

每次请求发送前清空残留软/硬件RX并准备接收，有限超时HAL_UART_Transmit期间中断保持开启。响应期限从发送返回成功的完成tick开始计1000ms；按字节实际到达时间判定期限，已在期限内完整到达的帧不会因主循环稍晚处理而误超时。合法完整响应立即结算，不等待满1000ms；无响应报TIMEOUT，收到无效候选后若期限内仍无合法帧，分别报CRC_ERROR/FRAME_ERROR。截断的不完整帧为FRAME_ERROR。

检查01地址、03/83功能、期望字节数、长度和CRC。仅逐字节匹配完整的本次请求才识别并跳过本机回显；不盲丢8字节。噪声/坏帧在16字节解析缓冲内滑动同步，分段支持；静默跨帧重置不完整候选。空闲和超期到达的字节丢弃计数，下一事务前再清除残留，并同时满足250ms事务间隔和接收静默条件。

Modbus 03响应不回显寄存器地址，也没有事务编号。因此对“已跨越静默/新请求、恰在新响应窗口内到达、且地址/长度/CRC完全相同”的任意迟到旧响应，协议本身不能唯一辨别。当前实现使用清残留、单事务、静默和时间戳防护处理可识别的迟到帧；该边界需结合真实传感器最大响应延迟上板核对，不能声称彻底识别任意迟到帧。

三笔连续无合法响应进入逐路退避，暂停该路正常查询；60000ms后探测，USART3用TEMP_HUM，UART4用CO2。探测失败以实际发起时间计算下一60秒周期，不补发错过的探测。正常03或合法83响应都说明链路恢复，清除退避/连续失败；83仍为MODBUS_EXCEPTION，保留异常码并不更新测量值。

HAL_UART_RxCpltCallback、HAL_UART_ErrorCallback各仅一个自定义定义，仍在bsp_dtu.c；新增USART3/UART4分支派发至BspWeatherRs485 ISR入口，USART6原分支保留。

## 缓存及打印

SensorWeatherMeasurement逐项保存has_value、最近有效value、valid_t_ms、valid_sequence、last_query_t_ms、query_sequence、last_status、exception_code。首次成功前为NO_DATA；有效零值可正常缓存。失败/异常不覆盖最近有效值、有效tick或有效序号。SensorWeather_GetFreshness(item,now,period)由App传入该项周期，超过三个周期判STALE；Sensors不包含app_config.h。不能用last_query_t_ms代替有效采集时间。

AppWeather_Init在两路启动后打印两条WEATHER_CONFIG和一条WEATHER_PERIODS。之后仅每笔事务结束打印一行WEATHER，CRLF结尾，不在每轮主循环重打印缓存。原有每秒中文土壤湿度/风向角度行保留。

以下完整行来自本次真实Arm库和生产模块的CPU执行，HAL为模拟，不是上板数据：

```text
WEATHER,seq=1,port=USART3,item=TEMP_HUM,tx_t_ms=10000,tx_end_ms=10000,rx_start_ms=10000,rx_end_ms=10000,TX=010300000002C40B,RX=01030401C8FFC9FB97,rx_count=9,rx_truncated=0,sample_t_ms=10000,temperature_c=-5.5,humidity_rh_pct=45.6,status=OK,exception_code=0x00,uart_error=0x00000000,cache=FRESH,backoff=0
```

其他成功值分别使用light_lux、pm25_ug_m3/pm10_ug_m3、pressure_kpa、co2_ppm、oxygen_pct。失败的本次数值及sample_t_ms为NA，不回填缓存旧值；status区分TIMEOUT、CRC_ERROR、FRAME_ERROR、MODBUS_EXCEPTION、UART_ERROR/BUSY/TIMEOUT、RX_NOT_READY、IRQ_DISABLED、RX_OVERFLOW，UART错误保留HAL错误位，异常保留exception_code。

TX字段是本次请求缓冲内容；UART发送失败时不表示8字节均已离开物理引脚。RX只保留最多32字节原始跟踪，rx_count报告实际收集数，超过时rx_truncated=1，避免伪装成完整抓包。发送/接收/sample时间均为设备开机毫秒tick，不是Unix时间；不同项目不伪装成同时采集。串口日志同步发送本身有有限耗时，但不会阻塞等待传感器响应。

## 修改文件

- 新增Bsp/Inc/bsp_weather_rs485.h、Bsp/Src/bsp_weather_rs485.c：固定两路收发、带tick缓冲与本路恢复。
- Bsp/Src/bsp_dtu.c：在唯一HAL回调中增加两路分派；USART6原逻辑不变。
- 新增Sensors/Inc/sensor_weather.h、Sensors/Src/sensor_weather.c：协议/CRC/解析、厂家单位、独立缓存与新鲜度。
- 新增App/Inc/app_weather.h、App/Src/app_weather.c：实际事务调度、退避、配置与结果直接printf。
- App/Src/app_main.c：初始化及每轮调用app_weather，原ADC/土壤上传/中文输出保留。
- App/Inc/app_config.h、Bsp/Inc/bsp_config.h：新增对应归属的周期/通信参数及合法性检查。
- MDK-ARM/tea_sense.uvprojx、tea_sense.uvoptx：三个模块各加入原层分组一次，复用原有头文件路径；旧源文件选项、启动/链接/编译选项保留。
- Tests/stubs/usart.h、stm32h7xx_hal.h、soil_host_test.c、dtu_host_test.c、新增weather_cases.h、check_arm_printf.py、check_architecture.py：HAL桩、协议/调度/缓存/硬件错误、原功能回归及Arm输出执行。
- Docs/code_architecture.md、本文及Docs/validation/weather_*.log：实际交付与证据。

Core/.ioc中两个中断的生成变更由用户在本轮完成；后续保护审计以该状态为基线，接入代码没有手改非USER CODE区域。

## 本次验证结果

- Host：GCC -Wall -Wextra -Werror，4种配置×土壤/DTU两程序，共8组通过。新用例覆盖六个CRC向量、全部单位/负温度/INT16_MIN/uint32满量程、有效零、所有分段位置、CRC/地址/长度错误、请求回显、异常响应、响应超时及tick回绕、250ms间隔、六项周期、三次失败退避/两路探测、迟到不补发、缓存失败不刷新与STALE边界、本路UART错误/启动失败/溢出恢复及ADC/土壤上传共存。
- 实际MDK ARMCC5.06完整编译链接：0错误、0警告；Code=38876、RO-data=1580、RW-data=96、ZI-data=6120字节。无重复自定义HAL回调、新源文件重复引用、半主机或浮点printf；启动和链接设置未改。静态栈报告832字节+Unknown，不替代目标板栈测量。
- Arm CPU执行：真实Arm C库→生产_sys_write→BspConsole→模拟HAL；保留91轮ADC/273次通道选择、土壤3条JSON与ACK检查，另外执行六笔百叶箱二进制请求、共享回调接收和实际结果打印，负温度/32位光照/气压/氧气小数正确，无运行时malloc。这里的HAL及物理时间为模拟，非上板验证。
- 土壤报文回归：生产C报文经本机TCP、临时SQLite提交、鉴权API/CSV及生产C ACK解析通过；仅使用演示设备和测试密钥，未打开生产数据库，不增加任何weather上传字段。
- 架构及保护审计：用户重新生成后的Core/.ioc、Drivers、原ADC/土壤/AD2/AD3模块、USART2重定向、私有密钥、上传JSON及服务器源码/界面保持；检查两路生成配置与唯一HAL回调一致。

证据：validation/weather_host.log、weather_arm_build.log、weather_arm_printf.log、weather_architecture.log、weather_soil_wire.log、weather_protection.log；均为本轮实际执行，不使用历史HEX或日志替代。

## 上板检测步骤（尚未执行）

1. 用户自行烧录当前MDK产物并复位。USART2接收软件选115200/8N1、UTF-8；先确认两条WEATHER_CONFIG的rx_status=OK及周期摘要，原中文土壤/风向行继续每秒出现。
2. 对照A3/B3→气象、A4/B4→气体，确认电源、共地及A/B极性符合板卡和传感器要求；本轮不改变供电/方向GPIO。核对首轮六项各一次及逐路250ms间隔，TX应逐条匹配上表，特别是O2末尾05 C8。
3. 检查每项完整RX地址、03/83、长度和CRC；真实传感器的湿度/温度寄存器次序、32位光照高低字、单位及异常码需与厂家说明和实物核对。成功零值应为OK，失败应为NA，不把缓存旧值当新值。
4. 分别观察10/30/60秒项目周期；暂时断开其中一路信号（不操作传感器供电），确认三次无合法响应后该路每60秒探测，另一条路线、ADC、土壤DTU/ACK继续运行。恢复连接后检查探测恢复与STALE/有效时间不被失败刷新。
5. 检查本机回显、残留/迟到数据、连续运行、实际响应延迟和日志/发送耗时对100ms采样的影响，以实际窗口计数判断欠采；如存在跨事务同长度迟到帧，按上述RTU协议边界进一步核对，不宣称已消除所有物理层问题。

真实两路RS485电气通信、传感器响应时延/回显、单位准确性及长时间稳定性尚待上板验证；本轮未自动烧录。
