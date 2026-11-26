# AD3电压还原、角度换算与验证

此文保留电气换算实现时的详细输出记录。当前按用户最新要求，USART2已精简为每秒一行中文土壤湿度和风向角度，不再输出下文中的详细报文及窗口/DTU日志；换算、采样、原始统计和上传行为保持。当前输出见[代码架构](code_architecture.md)。

2026-11-13（北京时间）。本轮采用用户提供的实测端点，修改固件、测试和文档；未初始化Git、提交或自动烧录。

## 标定与纯换算接口

用户给出的时间顺序为：5.0V→31000.43、1.0V→6400.67、3.5V→21920.14、2.5V→15614.18。仅使用1V、5V两个端点拟合；另两个点独立核验，不分段强制拟合。

参数集中在Sensors/Src/sensor_wind_direction.c：

```c
#define SENSOR_WIND_DIRECTION_RAW_1V_X100 640067L
#define SENSOR_WIND_DIRECTION_RAW_5V_X100 3100043L
/* RAW_SPAN_X100 = 2459976 */
```

SensorWindDirection_Convert(uint32_t raw_x100)接收raw×100：实际ADC样本传raw*100，测试实测小数点可精确保留。返回SensorWindDirectionValue：valid、带符号input_mv、angle_deg_x100及range。函数不调用ADC、HAL、UART或printf，不读取App配置，不增加可写跨模块全局变量。无效输入超过65535*100时返回valid=0、range=NA。

以delta=raw_x100-640067、span=2459976计算：

```text
input_mv = round_signed((1000*span + delta*4000) / span)
angle_deg_x100 = round(delta*36000 / span)，显示限幅0..36000
```

使用有符号64位中间量，避免负差值下溢；角度直接从raw计算，未经过整数mV舍入。range按未舍入delta与0/span比较，等端点为OK；略低/略高即使显示仍是0.00/360.00，也保留LOW/HIGH。

input_mv是AD3端子电压模型，不是MCU PA5节点电压。超出1～5V仍保留带符号线性外推，例如有效raw=0得到约-41mV、角度0.00、ADC状态OK、range=LOW；这个负数是端点外的模型外推，不是对负电压的独立实测。raw=65535得到约10615mV、角度360.00、range=HIGH。没有按65535→5V假设，未套用土壤/AD2参数或分压/VREF补偿。

该标定只用于当前板卡电气换算，未确认传感器安装朝向、方位偏移或真实风向精度，不增加任何未设置的偏移。

## 应用与实际输出

App_AcquireRound保持100ms、5→3→19的顺序；AD3成功后复用本次BspAdcResult调用换算一次，存入最近完整轮次的wind_direction。失败时新轮次结果清零为不可用，不换算、不复用上次值。USART2每1000ms直接printf已保存结果，t_ms仍是该次AD3读取开始tick。结尾为实际CRLF，不启用浮点printf。

本次Arm CPU模拟执行真实C库和生产App/Sensors/Bsp所得完整行（HAL模拟，非本轮上板测量）：

```text
WIND_DIRECTION,t_ms=1000,channel=AD3,pin=PA5,adc_channel=19,adc_raw=23456,input_mv=3773,angle_deg=249.59,adc_status=OK,range=OK
```

Host生产路径还验证了：

```text
WIND_DIRECTION,t_ms=1000,channel=AD3,pin=PA5,adc_channel=19,adc_raw=15614,input_mv=2498,angle_deg=134.83,adc_status=OK,range=OK
WIND_DIRECTION,t_ms=3000,channel=AD3,pin=PA5,adc_channel=19,adc_raw=0,input_mv=-41,angle_deg=0.00,adc_status=OK,range=LOW
WIND_DIRECTION,t_ms=2000,channel=AD3,pin=PA5,adc_channel=19,adc_raw=NA,input_mv=NA,angle_deg=NA,adc_status=ADC_POLL_FAILED,range=NA,stage=POLL,hal=HAL_TIMEOUT,prepare=HAL_OK,config=HAL_OK,stop=HAL_OK
```

其他ADC失败沿用实际类别和stage/HAL/prepare/config/stop信息。成功但转换输入无效的防御分支保留raw及adc_status=OK，电压和角度为NA，并标记conversion_status=INVALID_RAW，不冒充ADC故障。

AD3_WINDOW代码和统计规则保持：只对本窗有效raw统计min/max/普通均值，按实际valid_count计算，均值保留两位小数；无有效值时为NA。仅用于固定角度的电气校验，不从窗口均值输出动态平均角度。跨越1V/5V对应0°/360°边界时，普通平均raw会产生错误的中间方向，本轮未增加方向平均算法。

## 修改文件

- 新增Sensors/Inc/sensor_wind_direction.h、Sensors/Src/sensor_wind_direction.c：纯电压/角度换算及范围名称，私有定点除法和模块内标定参数。
- App/Inc/app_main.h、App/Src/app_main.c：保存本次换算结果，WIND_DIRECTION直接printf。
- MDK-ARM/tea_sense.uvprojx、tea_sense.uvoptx：Sensors分组加入一次新源文件，复用Sensors/Inc路径，保留编译/启动/链接/调试选项。
- 新增Tests/wind_direction_cases.h，更新Tests/dtu_host_test.c、ad3_cases.h、check_arm_printf.py：换算、输出/NA、原始窗口及土壤回归。
- Docs/code_architecture.md、ad3_raw_validation.md、本文及validation/wind_direction_*.log：当前架构和本轮证据。

## 验证结果

Host GCC使用-Wall -Wextra -Werror，原有4种配置×两个程序共8组全部通过，包含编译配置保护；本轮换算核验：

| 原始值raw | 端子电压mV | 角度 | 范围 |
| --- | ---: | ---: | --- |
| 6400.67 | 1000 | 0.00° | OK |
| 15614.18 | 2498 | 134.83° | OK |
| 21920.14 | 3524 | 227.12° | OK |
| 31000.43 | 5000 | 360.00° | OK |

另覆盖全65536个整数raw的单调性、LOW/HIGH、端点外0.01raw在显示舍入后仍保留越界、有效0/满量程/无效宽输入、先算角度而非由舍入mV反算、失败不保留旧结果、printf使用样本时间且不额外采样。AD0土壤换算/3秒上传/ACK、AD2未标定、AD3原始统计/边界/故障回归保持通过。

ARMCC 5.06实际MDK完整重编译、链接：0错误、0警告；Code=36304、RO-data=2656、RW-data=88、ZI-data=4736字节。新模块实际编译链接一次，USART2唯一重定向、无半主机和浮点printf检查通过。静态调用图最大栈报告仍为864字节+Unknown，不代替目标板栈测量，启动/链接选项未改。

Arm CPU模拟执行真实Arm C库与生产模块：91完整轮次、273次通道选择、一次初始化自校准；新电压/角度/负数/LOW/HIGH输出、原始窗口均值及stdout发送失败恢复通过，仍只有3条土壤JSON和相应USART6 ACK，无AD3上传、无运行时malloc。该验证模拟HAL，不是上板验证。

生产C土壤formatter→本机真实TCP→临时SQLite事务提交→鉴权API/CSV→生产C ACK解析通过，包含无效窗口null和配置故障类别。使用临时测试设备/密钥/数据库，不读取或改写现有生产数据。

保护审计核对原有Bsp、Core、Drivers、.ioc、土壤/AD2模块、调度配置、上传JSON、密钥和服务器源码/界面不变；MDK除新增Sensors引用及IDE文件编号外，其余选项保持。日志见validation/wind_direction_host.log、wind_direction_architecture.log、wind_direction_arm_build.log、wind_direction_arm_printf.log、wind_direction_soil_wire.log、wind_direction_protection.log。

## 尚待上板

用户自行下载当前固件后复位，打开USART2对应串口115200/8N1，确认每秒WIND_DIRECTION与每3秒AD3_WINDOW。用固定端子电压复核端点和两个独立点；单次raw是整数，表中的小数raw是稳定记录均值，瞬时打印允许采样波动。固定方向时用原始窗口均值与万用表记录对照，不当作动态平均方向。

检查raw跨端点时LOW/HIGH仍保留，故障时输出NA；同时确认土壤上传/ACK和AD2未标定输出。已知电压仅接确认适用的AD3板卡端子，不直接向MCU PA5施加5V。安装朝向、真实风向精度和本次新固件的连续上板运行尚未验证。
