> 当前业务于2026-11-14改为四路合格窗口与十项V2，见 [integration_v2.md](integration_v2.md)。下文保留为此前阶段记录，一秒/raw输出与旧单土壤协议不再代表当前行为。

# AD2 实测电气映射与标称风速输出

2026-11-14，北京时间。更新现有 tea_sense 风速模块，未自动烧录。

## 测量依据与唯一换算

AD2 → PA6 → ADC1 Channel 3。参数集中在 `Sensors/Inc/sensor_wind_speed.h`，计算在原 `sensor_wind_speed.c`。不使用土壤、AD3、AD4、假定分压或未实测 VREF 参数。

| 点 | ADC 原始均值 | AD2 端子电压 | 标称风速 |
| --- | ---: | ---: | ---: |
| 静止零位 | 67.79 | 40mV | 0.00m/s |
| 中点核验 | 15793.21 | 2540mV | 15.00m/s |
| 满量程 | 31518.63 | 5040mV | 30.00m/s |

```text
RAW_ZERO_X100 = 6779
RAW_FULL_X100 = 3151863
RAW_SPAN_X100 = 3145084

input_mv = 40 + (raw_mean - 67.79) * 5000 / 31450.84
wind_speed_mps = (raw_mean - 67.79) * 30 / 31450.84
```

40mV 是静止输出零偏，故对应零风速；满量程为30m/s。电气映射具有本次测量依据，固定启用。风速满量程按用户指定的标称关系换算，**尚未经过参考风速仪对照，不声称完成实际风速精度标定**。

现有 `SensorWindSpeed_ConvertMean` 改为 `SensorWindSpeed_ConvertMean(uint64_t raw_sum, uint32_t count)`，单点传 raw/1，窗口传全部有效 raw_sum/valid_count。原未确认参数结构、开关、getter、校准状态枚举和绝对电压算风速的实现已删除；只有这一条换算路径，没有新旧两套实现或运行模式。

## 精度、范围与失败

采用有符号64位的 `delta=raw_sum*100-6779*count` 和 `denominator=3145084*count`。电压和 `wind_speed_mps_x100` 分别从 delta 计算，不先舍入原始均值、也不先舍入电压再算风速。

为支持 UINT32_MAX 次16位样本，比例计算先拆整数商并保留两个除法余数，再做最终四舍五入。这样避免直接 `delta*5000` 超出64位；保留的余数保证结果仍来自完整 sum/count。有效16位 raw 和 uint32 count 的最大 delta 小于 2.82×10^16，最大残差小于 1.36×10^16，均在 int64_t 范围内。没有浮点计算或浮点 printf。

范围在舍入、限幅前判断：delta<0 为 LOW，delta>denominator 为 HIGH，含端点为 OK。显示风速限制0.00～30.00m/s，LOW/HIGH保留；不存在额外死区、强制归零或自动标定。

有效 ADC raw=0 对应约29mV、显示0.00m/s、LOW，仍是成功采样。采集失败（包括取值后停止失败）使单点 value.valid=0，电压、风速、范围输出NA。无有效窗口样本也输出NA，不除零。部分有效窗口只使用有效样本，继续保留实际尝试数、有效数和错误/清理诊断。

`SensorWindSpeed_WindowFinish` 在原始窗口数据上计算普通均值的 value，位于原有质量门槛之前。原 `raw_mean`（去极值显示值）、`trimmed_sum`、`used_count`、原窗口质量状态保持原语义，仅供既有诊断；新增风速输出不使用这些去极值字段。没有新增滤波、去极值算法或累加器。

例如样本0、0、1、65535：全部有效raw均值为16384，转换结果2634mV、15.56m/s；原去极值诊断仍为trimmed_sum=1、used_count=2、raw_mean=1。若错误地先将单点风速限幅再平均，会得到7.50m/s，本次已测试排除。

## 实际输出格式

直接在 App 主循环的原 AD2 行末追加 `input_mv`、`wind_speed_mps`、`range`；原字段顺序和错误诊断保留。每条是一整行，结尾CRLF。以下取自真实ARM C库/CPU仿真，HAL和输入为测试数据，并非板上采集记录：

```text
ADC_SAMPLE,round_id=11,sample_t_ms=1000,channel=AD2,pin=PA6,adc_channel=3,adc_raw=12345,status=OK,input_mv=1992,wind_speed_mps=11.71,range=OK
ADC_WINDOW,channel=AD2,window_end_ms=3000,sample_count=30,valid_count=30,adc_raw_min=12345,adc_raw_max=12345,adc_raw_mean=12345.00,error_count=0,status=OK,cleanup_error_count=0,prepare_error_count=0,config_error_count=0,last_error=NONE,last_stage=NONE,last_hal_status=NA,last_prepare_status=NA,last_config_status=NA,last_stop_status=NA,input_mv=1992,wind_speed_mps=11.71,range=OK
```

失败单点格式示例：

```text
ADC_SAMPLE,round_id=71,sample_t_ms=7000,channel=AD2,pin=PA6,adc_channel=3,adc_raw=NA,status=ADC_POLL_FAILED,stage=POLL,hal_status=HAL_TIMEOUT,prepare_status=HAL_OK,config_status=HAL_OK,stop_status=HAL_OK,input_mv=NA,wind_speed_mps=NA,range=NA
```

主机测试还验证14次真实采样、raw_sum=221105的三秒窗口：显示`sample_count=14,valid_count=14,adc_raw_mean=15793.21`，换算为`input_mv=2540,wind_speed_mps=15.00,range=OK`。转换使用221105/14，而非日志舍入后的15793.21。实际count不固定为30。

主观察值为三秒窗口风速。单点只是最近一个完整轮次的即时观察，不为打印重采样。AD4记录不增加电压、风速或pH字段。完整CRLF字节记录见 `Docs/validation/wind_speed_calibration_arm_console.txt`。

## 修改文件

生产代码只有3个文件：

- `Sensors/Inc/sensor_wind_speed.h`：实测常量、x100定点字段、单一纯换算接口。
- `Sensors/Src/sensor_wind_speed.c`：替换原模型、带余数的有符号64位计算、全体有效raw窗口换算。
- `App/Src/app_main.c`：原AD2单点/窗口行追加字段，直接整数printf，错误和无有效样本输出NA。

测试更新8个文件：`Tests/wind_speed_cases.h`、`Tests/adc_raw_cases.h`、`Tests/dtu_host_test.c`、`Tests/soil_host_test.c`、`Tests/ad3_cases.h`、`Tests/wind_direction_cases.h`、`Tests/weather_cases.h`、`Tests/check_arm_printf.py`。原边界检查、配置变体和硬件故障注入保留。

文档更新 `Docs/code_architecture.md`，新增本文。验证产物为 `Docs/validation/wind_speed_calibration_{host,architecture,arm_build,arm_printf,protection}.log` 和 `wind_speed_calibration_arm_console.txt`。MDK全量构建刷新其生成产物，未修改工程源引用、启动文件或链接配置。

## 执行验证

- `py Tests/run_host_tests.py`：4种配置×2个程序，共8组通过，GCC `-Wall -Wextra -Werror`。覆盖指定零点/中点/满量程、舍入后相同显示值但不同LOW/HIGH、有效raw=0、超过满量程、失败/清理失败、无有效样本、窗口精度、全16位单点域单调性、UINT32_MAX计数安全边界。配置VREF=2500时结果相同。
- 精度反例：raw=73时电压41mV、速度0.00m/s，排除先舍入毫伏再算风速；raw_sum=73032/count=1000时电压41mV、速度0.01m/s，排除先将raw均值舍入到73.03。窗口测试同时排除先限幅单点再平均、去极值数据混入和质量门槛错误遮蔽普通均值。
- 复用打印、实际计数、同轮时间、窗口切换/清空、tick回绕/迟到、AD4原始输出、土壤全域换算、风向、百叶箱CRC/调度/退避/缓存、DTU三秒JSON/ACK/拆包/溢出/恢复回归通过。
- `py Tests/check_architecture.py`：分层、无可写extern、GPIO/ADC、UART/IRQ与MDK引用检查通过。无新增源模块，无需改构建引用。
- `py Tests/check_arm_build.py`：ARMCC5.06 update7全量编译链接 **0错误、0警告**；Code=40406、RO-data=2018、RW-data=96、ZI-data=6304。唯一USART2重定向、无半主机/浮点printf检查通过；未下载固件。
- `py Tests/check_arm_printf.py`：真实ARM App/Sensors/Bsp及Arm C库，HAL为模拟。91个正常轮次、364次明确5/3/19/18通道选择、728次停止、自校准1次；新增9个纯换算检查点覆盖测量点、有效0、空输入、分数均值和最大计数。三次原土壤JSON发送、两次ACK、六项百叶箱事务及printf错误恢复通过；无运行时malloc。
- 与上一轮保存的ARM控制台记录逐行比较：AD2仅在原记录末尾追加三个字段；AD4、中文业务行和百叶箱行保持相同。字节检查保证CRLF，无裸LF/CRCRLF。
- 修改前源文件及SHA-256清单已保存于本机临时快照，保护审计见 `wind_speed_calibration_protection.log`。Bsp、Core、Drivers、.ioc、App配置、VREF未实测标志、ADC流程、上传JSON/私有密钥及其他传感器生产源文件未改，既有验证记录保留。

## 未验证项目

未连接板卡、未自动烧录；未验证本次固件在实际串口打印耗时下的采样次数、真实电压复核、通道串扰、DTU公网和长时间稳定性。模拟HAL测试不代表真实硬件测试。标称0～30m/s换算尚未做参考风速仪对照。
