# ADC1双输入轮次与AD2独立电压校准

2026-11-13，北京时间。仅扩展现有tea_sense固件；未初始化/提交Git，未烧录，未修改服务器、数据库、前端或私有密钥。测试记录不代表真实硬件数据。

## 配置核对与Bsp接口

- 土壤：端子AD0 → PB1 → ADC1 Channel5。
- 风速：端子AD2 → PA6 → ADC1 Channel3。只有hadc1，没有启用ADC2。
- .ioc已有PA6.Signal=ADCx_INP3、WIND_SPEED_AD2标签；生成adc.c将PA6包含在GPIOA模拟输入、GPIO_NOPULL中。PB1同样已配置。无需CubeMX补项，本轮未修改/声称重新生成Core或.ioc。
- 保留16位、异步8分频、单次转换、软件触发、Scan禁用、一个Rank、初始Channel5、387.5 Cycles，无ADC DMA/中断。main中App_Init/App_Process接入不变。

唯一采样接口改为`BspAdc_ReadRaw(BspAdcInput input)`，公开枚举为`BSP_ADC_INPUT_SOIL_AD0`和`BSP_ADC_INPUT_WIND_SPEED_AD2`。原无参版本已更新全部调用方，没有兼容采样壳或第二套实现。HAL通道常量、hadc1和调用均在Bsp。

每次读取：检查输入/初始化自校准结果 → Stop并检查 → ConfigChannel(5或3，Rank1/单端/387.5/原offset)并检查 → Start → 有限超时Poll → GetValue/16位有效性检查 → Stop清理。校准只在初始化做一次；不因切换通道重校准、不加延时、重试或重初始化。预停止失败时不配置/启动，下一输入按其真实Stop结果判断是否可恢复。

`BspAdcResult`新增stage、prepare_status/config_status和attempted标记，原主错误及清理错误同时保留。为保持当前服务器枚举兼容，土壤wire错误类别仍是既有ADC_CAL/START/POLL/STOP/OUT_OF_RANGE：配置/非法输入属于START_FAILED，预停止属于STOP_FAILED。具体原因看CONFIG/INPUT/PREPARE等stage，不能仅按旧类别把配置错误看成一次HAL_Start调用失败。本地错误统计保留单独prepare/config次数；没有吞掉这些故障或新增网络字段。

## 轮次与窗口

App/Inc/app_config.h统一管理100ms轮次、1000ms打印和3000ms公共窗口；期望30轮、最低80%=24个有效样本。宏改为APP_SAMPLE_ROUND_PERIOD_MS、APP_SAMPLE_WINDOW_MS、APP_WINDOW_EXPECTED_ROUNDS、APP_WINDOW_MIN_VALID_PERCENT，值及土壤处理语义不变。

每次App_Process先处理USART6接收/ACK，然后按统一round_t_ms结算到期窗口；若轮次到期，只采一轮：分配round_id → 记录土壤开始/结束tick → 记录风速开始/结束tick → 分别提交两个窗口 → 保存最近完整轮次。固定顺序AD0后AD2，即5→3，下一轮重新明确选5。一路错误仍尝试另一路，不缓存旧成功值。

窗口为[start,end)，只按round_t_ms归属。2995ms开始、AD2到3005ms才结束的同一轮仍两路都进入旧窗；3000ms开始的新轮两路都进入新窗。每路分别维护sum/min/max、尝试/有效/参与数和错误，不混用。迟到不补采或复制；跨多个窗口的空窗与跳过处理沿用并同步作用于两路。窗口冻结后才重置，WIND_WINDOW使用冻结结果，不取新窗数据。

土壤436/31146、先raw去极值再对分数均值换算、24/23门槛、3秒JSON上传及单请求/10秒ACK处理不变。风速也去掉各一个最高/最低，保留trimmed_sum和used_count，未校准不妨碍raw窗口有效。风速不上传，服务器协议无需调整。

pair_span_ms从统一round_t_ms算到两路读取/窗口提交完成，不含后续printf/网络发送；各路实际读取起止tick单独保留。SysTick是毫秒分辨率，相等时间戳和0ms跨度不表示硬件同时转换。阻塞UART打印/发送及错误超时可带来调度迟到，须观察真实窗口次数，不保证用软件周期伪造整30点。

## 标定填写位置与接口

参数集中在Sensors/Src/sensor_wind_speed.c的`static const SensorWindSpeedCalibration s_voltage_calibration`，当前`{0}`表示未设置、confirmed=0，不是实测raw端点。没有复制土壤端点、猜分压或假定0/65535。收到实测记录后填写四项并将confirmed置1：

| 字段 | 需要的实测数据 |
| --- | --- |
| raw_low / input_low_mv | AD2低电压稳定raw均值 / 万用表实测端子mV |
| raw_high / input_high_mv | AD2高电压稳定raw均值 / 万用表实测端子mV |
| confirmed | 全部测量确认后才置1 |

`SensorWindSpeed_GetCalibration`提供只读查看；没有在线setter、菜单或Flash写入。`SensorWindSpeed_ConvertMean(calibration, raw_sum, count)`为纯换算接口，单次用count=1，窗口传trimmed_sum/used_count；不再采样。确认状态独立于ADC状态和原始窗口质量。

校验raw_high>raw_low、raw处于16位域、0≤input_low_mv<input_high_mv≤5000、confirmed合法。参数缺失返回UNCONFIRMED，无效跨度/方向/范围返回INVALID；都不提供虚构电压/风速。有效配置但无有效raw时转换值仍不可用。当前端点电压校验限于厂家0～5000mV量程，未把此范围当作板卡电气安全认证。

用有符号64位有理数恢复AD2端子电压：

```text
den = (raw_high - raw_low) * count
num = input_low_mv * den + (raw_sum - raw_low * count) * (input_high_mv - input_low_mv)
input_mv = signed_round(num / den)
wind_speed_mps_x1000 = round(num * 6 / den), 显示限制0..30000
```

速度从未舍入电压分数直接算（30000/5000=6），不能先舍入raw或input_mv再算。厂商满量程为30m/s，不是32.4。恢复电压可保留负值或高于5000以便诊断，速度限幅但LOW/HIGH严格按原有理数电压比较；恰好0或5000为OK。电气标定确认及厂家线性映射不等同于真实风速校准。本轮不输出adc_pin_mv，未把MCU节点电压和AD2端子混用。

## 固定printf格式

USART2每秒直接printf一条最近完整轮次，以下仅为格式示例（非上板记录；未填标定）：

```text
ADC_ROUND,round_id=11,round_t_ms=1000,soil_read_start_ms=1000,soil_read_end_ms=1002,wind_read_start_ms=1002,wind_read_end_ms=1006,pair_span_ms=6,soil_adc_raw=15791,soil_moisture_pct=50.00%,soil_status=OK,soil_stage=COMPLETE,soil_range=OK,wind_adc_raw=12345,wind_adc_status=OK,wind_adc_stage=COMPLETE,wind_cal_status=UNCONFIRMED,wind_input_mv=NA,wind_speed_mps=NA,wind_range=NA
```

字段顺序固定，无效数值为NA，有效0仍输出0。确认电气标定后wind_input_mv为带符号整数mV，wind_speed_mps以整数定点拼接三位小数，range保留OK/LOW/HIGH。只在主循环输出，不另采样、不封装业务打印、不启用浮点printf。

每3秒保留SOIL_WINDOW、DTU_TX/ACK/错误日志，增加：

```text
WIND_WINDOW,t_ms=3000,window_ms=3000,sample_count=30,valid_count=30,used_count=28,raw_min=12345,raw_max=12345,raw_mean=12345,trimmed_sum=345660,status=OK,cal_status=UNCONFIRMED,input_mv=NA,wind_speed_mps=NA,range=NA,error_count=0,cleanup_error_count=0,prepare_error_count=0,config_error_count=0,last_error=NONE,last_stage=NONE
```

有部分有效值但窗口不足时min/max仍是实际有效极值，raw_mean/trimmed_sum为NA、used_count=0；无有效样本时极值也为NA。两个窗口及累计错误可由只读App_GetDiagnostics查看，不靠日志缓存旧数据。

## 修改文件和验证

- Bsp/Inc/bsp_adc.h、Bsp/Src/bsp_adc.c：显式输入和每读通道切换/阶段诊断。
- Sensors/Inc/Src中的sensor_soil：仅改显式AD0调用和细化诊断，原端点/有理数换算不变。
- 新增Sensors/Inc/sensor_wind_speed.h、Sensors/Src/sensor_wind_speed.c：实际AD2采样、独立窗口、电压校准/GH-FS映射。
- App/Inc/app_config.h、app_main.h、App/Src/app_main.c：统一轮次、完整快照/时间、双窗口及直接printf。采集使用私有函数以减少接收/解析路径的栈占用，不改启动/链接配置。
- MDK-ARM/tea_sense.uvprojx、tea_sense.uvoptx：Sensors组加入新源文件，复用既有Sensors/Inc路径，无重复编译。
- Tests/stubs/stm32h7xx_hal.h、host_stdio.c、soil_host_test.c、dtu_host_test.c、dtu_window_cases.h、wind_speed_cases.h、check_arm_printf.py、check_architecture.py：更新HAL桩和实际新路径回归。
- Tests/upload_wire_fixture.c、check_server_upload.py：土壤新本地阶段继续兼容原服务器wire错误类别，风速不进入报文。
- Docs/code_architecture.md和本文：当前架构、输出、校准及验收约定。

验证证据均为本次执行的Docs/validation/ad2_*.log：

- 4种板级配置×土壤/DTU host程序共8组，覆盖5→3→5每次配置、停止/配置/Start/Poll/GetValue范围/清理错误、共享不可恢复故障、单路不污染、读取时间、跨窗/回绕/迟到、未确认/无效标定、端点/中点/分数均值、LOW/HIGH及0/满量程。保留土壤全域换算、3秒窗口、ACK/拆包/溢出/UART恢复等回归及配置保护。
- ARMCC5.06实际全量编译链接0错误0警告，单一USART2重定向、无半主机/浮点printf，启动及链接参数未改。最终尺寸见ad2_arm_build.log。
- ARM CPU仿真执行生产App/Sensors/Bsp和真实Arm库：91完整轮次、182次严格交替ADC1通道配置、364次前后停止、一次自校准；土壤故障时AD2仍有效、3次土壤JSON和ACK/printf恢复；无运行时malloc。HAL为模拟，不是上板。
- 生产C报文经真实本地TCP/临时SQLite/鉴权API/CSV/生产C ACK解析通过；配置阶段故障沿用ADC_START_FAILED类别被原服务器接受，没有风速字段。只有demo设备及测试密钥。
- 架构及保护审计核对Core/Drivers/.ioc、私有密钥、服务器代码/测试/数据/前端等保持不变，见ad2_protection.log。

## 待上板核对

1. 用户自行烧录后记录ADC_ROUND与两路窗口，确认每轮顺序、实际窗口次数、错误及pair_span_ms。毫秒相同不能作为同步转换证明。
2. 固定AD0，只改变AD2；再固定AD2，只改变AD0，观察另一通道不应明显跟随，确保土壤JSON数值仍来自AD0。
3. 受控试验中通过同一BspAdc_ReadRaw(input)比较单独读取和正式交替读取，不增加运行模式。若发现建立时间/串扰，再根据实测决定措施，本轮没有猜测性延时或丢弃首样本。
4. 已知电压只接板卡已确认适用的AD端子，不直接向PB1/PA6等MCU引脚施加5V。每个电压状态记录稳定raw、min/max/mean、计数和万用表实际端子电压，之后由用户提供AD2独立端点。
5. 确认ADC1自校准、双路错误隔离、土壤DTU公网ACK/断网恢复及长时间稳定性。未自动烧录，以上硬件、电压和风速精度均尚未实测。
