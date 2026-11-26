# 当前 AD4 土壤 pH 标定：baseline-3

2026-11-23（北京时间）。本次只更新 pH 电气端点、撤销 AD4_SAMPLE 临时打印，并按现有规则将 `APP_CALIBRATION_REVISION` 从 `baseline-2` 递增为 `baseline-3`；固件仍为 `0.9.1-dev`，协议仍为 V2。其他通道系数、采样与上传周期、DTU、服务器、前端和历史数据库未改动，未自动烧录。

## 新旧参数与来源

AD4：PA4 → ADC1 Channel18，采用用户本次提供的 0～10V → pH3～9 关系及有效日志统计。0V 组 100 条、总和 5464、raw 均值 54.64，其中 38 个有效 raw=0 全部计入；10V 组 101 条、总和 6336892、raw 均值 62741.504950495，按用户要求采用 62741.50。原始日志未附在本次工程输入中，以上统计来源为用户给定；本次验证使用合成样本。

| 参数 | baseline-2 旧值 | baseline-3 新值 |
| --- | ---: | ---: |
| SENSOR_PH_RAW_LOW_X100 | 6222 | 5464 |
| SENSOR_PH_RAW_HIGH_X100 | 3137026 | 6274150 |
| SENSOR_PH_RAW_SPAN_X100（由端点相减） | 3130804 | 6268686 |
| SENSOR_PH_INPUT_SPAN_MV | 5000 | 10000 |
| pH 下限 / 上限 / 跨度（×100） | 300 / 900 / 600 | 300 / 900 / 600 |

## 当前换算、窗口和输出

唯一实现仍在 `Sensors/Src/sensor_ph.c`。先检查 N>0 及原始和的范围；令 S 为参与均值的 raw 总和，N 为数量：`delta=100*S-5464*N`，`denominator=6268686*N`。范围内 `ph_x100=300+round(600*delta/denominator)`，使用有符号 64 位商/余数运算，避免大计数的乘法溢出并只在最终舍入；不先舍入 raw 均值或诊断电压。delta<0 为 3.00/LOW，delta>denominator 为 9.00/HIGH，精确端点属于 OK。

正式窗口仍为 100ms 一轮、3000ms 一窗、至少 24/30 有效；只去掉一个最大与一个最小，保留精确 trimmed_sum/used_count。有效 raw=0 仍计数并可换算为 3.00/LOW；ADC 故障、空窗或有效数不足仍无效，不用 3.00 或旧成功值代替。未发现临时“pH待校准”屏蔽。

临时 `AD4_SAMPLE` 行及独立 1 秒定时状态已删除，AD4 原始值、完成时间和错误诊断保留。中文物理行继续打印 `土壤 pH：6.00`，与现有 `soil_ph` V2 字段共用 `App_CloseWindow` 生成的同一个窗口结果；未更改上传实现和合同。

## 本次验证

`Tests/ph_cases.h` 覆盖精确分数均值 54.64→3.00、31398.07→6.00、41845.88→7.00、62741.50→9.00，端点两侧、LOW/HIGH、有效零与失败、最终舍入边界、全 16 位单点单调性、UINT32_MAX 数量、23/24 有效数门槛、重复极值仅去各一个及先限幅的反例。应用测试继续核对中文 pH 与 V2 同窗数值，以及无临时 raw 输出。旧测试断言保留并更新对应输入和预期。

- Host：`py -3.12 Tests/run_host_tests.py` 退出码 0；GCC 使用 `-Wall -Wextra -Werror`，5 种配置 × 2 套程序全部通过，另通过 2000ms 编译变体、V2 容量及既有编译拒绝/缺密钥检查。指定四个精确分数均值、范围/故障/窗口/打印与上传一致性断言均执行通过。[完整日志](validation/ph_10v_host.log)
- 架构：`py -3.12 Tests/check_architecture.py` 退出码 0；分层、MDK 引用、ADC1 通道映射、UART 归属与无 AD4_SAMPLE/独立定时状态检查通过。[完整日志](validation/ph_10v_architecture.log)
- ARM：`py -3.12 Tests/check_arm_build.py`，本次完整 Rebuild `tea_sense`；ARMCC 5.06 update 7 build 960，0 Error(s)、0 Warning(s)，Code=45908、RO-data=1180、RW-data=128、ZI-data=10816。标准 Arm C 库、唯一 USART2 printf 重定向、无半主机/浮点 printf 检查通过，已刷新 AXF/HEX，未下载。[构建日志](validation/ph_10v_arm_build.log)

本次未运行可选 `check_arm_printf.py` CPU 仿真；其 pH 输入和取消临时打印后的断言已同步，ARM 固件构建及链接审计已实际执行。未执行服务器或前端验证，不访问历史数据库。

## 修改文件清单

- 生产：`Sensors/Inc/sensor_ph.h`（端点与电压跨度）、`Sensors/Src/sensor_ph.c`（64 位防溢出说明）、`App/Src/app_main.c`（移除临时打印）、`App/Inc/app_version.h`（baseline-3）。原窗口实现与上传代码保持不变。
- 测试：`Tests/ph_cases.h`、`Tests/adc_raw_cases.h`、`Tests/soil_host_test.c`、`Tests/rain_cases.h`、`Tests/ad5_cases.h`、`Tests/integration_cases.h`、`Tests/arm_integration_main.c`、`Tests/upload_wire_fixture.c`、`Tests/check_arm_printf.py`、`Tests/check_architecture.py`。只同步 pH 输入/预期、临时打印撤销与相关检查，未删除旧断言。
- 文档：本文与 `Docs/code_architecture.md`；本次验证新增 `Docs/validation/ph_10v_*.log`，MDK 目录下构建产物由全量构建刷新。历史验证日志保留。

---

以下为 2026-11-14 旧 0～5V 阶段历史记录；旧系数、旧输出和旧验证日志不代表当前行为。

# AD4 土壤 pH 电气映射与串口输出

2026-11-14，北京时间。直接更新当前 tea_sense，保留既有修改，未自动烧录。以下测试和日志为主机/ARM CPU仿真，HAL为模拟，不能作为板上实测记录。

## 系数来源与换算

AD4 → PA4 → ADC1 Channel 18，本次采用0～5V版本。系数来自用户提供的AD4电气测量端点，pH按厂家标称3～9量程线性换算。说明书的0～10V举例不用于本次实现，0V对应pH 3而非pH 0。

| 原始均值 | AD4端子电压 | 显示 soil_ph | range |
| ---: | ---: | ---: | --- |
| 62.22 | 0mV | 3.00 | OK |
| 15716.24 | 2500mV | 6.00 | OK |
| 约20934.25 | 约3333mV | 7.00 | OK |
| 31370.26 | 5000mV | 9.00 | OK |
| 0（有效采集） | -10mV | 3.00（限幅） | LOW |

```text
SENSOR_PH_RAW_LOW_X100  = 6222
SENSOR_PH_RAW_HIGH_X100 = 3137026
SENSOR_PH_RAW_SPAN_X100 = 3130804

input_mv = (raw_mean - 62.22) * 5000 / 31308.04
soil_ph  = 3 + (raw_mean - 62.22) * 6 / 31308.04
```

这是AD4电气端点校准与厂家标称量程换算，**尚未完成真实土壤pH精度对照**。没有温度补偿、电极电势模型、现场标定、持久化、死区、自动归零或运行模式开关。有效ADC值只表示采集结果可用，不能据此确认探头供电、线路或真实土壤pH准确性。

## 模块与精度

没有既有pH业务模块，因此新增 `Sensors/Inc/sensor_ph.h`、`Sensors/Src/sensor_ph.c`。公开入口为 `SensorPh_Convert(uint64_t raw_sum, uint32_t count)`，结果为 `SensorPhValue`；电压使用有符号 `input_mv`，pH使用 `soil_ph_x100`。模块只依赖标准整数/内存头文件，不访问HAL或Bsp，不采样、不打印；所有参数属于sensor_ph，未引用AD2、AD3或土壤水分系数。

单点在原AD4成功读取后传raw/1；窗口在原始统计冻结时传全部有效raw_sum/valid_count。电压和pH各自直接从该分数计算，不先舍入原始均值，也不从整数毫伏反算pH。

计算采用有符号64位 delta=`raw_sum*100-6222*count` 和 denominator=`3130804*count`。比例计算拆分商、保留余数，避免大计数时直接delta×5000溢出，再执行一次最终四舍五入。16位raw、uint32 count范围内中间量均在int64_t范围内；测试包括UINT32_MAX计数。不启用浮点printf。

LOW/OK/HIGH从未舍入、未限幅的delta判断，端点为OK；显示pH限制3.00～9.00，同时保留LOW/HIGH，端子电压保留有符号外推。失败、清理失败、首次有效采集之前或count=0均使valid=0，打印NA，不能限幅成有效3.00。

主要观察值是三秒窗口pH：原始有效样本求平均后再换算和限幅。例：raw为0、0、65535时，原始均值21845，输出3479mV、pH 7.17；若先将单点pH限幅再平均会错误得到5.00。没有新增滤波、去极值或统计累加器，实际sample_count/valid_count、min/max/sum和错误诊断保留。

## 打印格式

在既有AD4 ADC_SAMPLE/ADC_WINDOW行末追加 `input_mv`、`soil_ph`、`range`；原字段与顺序保留，没有第二条pH打印任务。每条记录是一整行，CRLF结束；pH两位小数，无百分号。

以下直接取自ARM CPU仿真的真实Arm C库输出，输入为人工测试值：

```text
ADC_SAMPLE,round_id=11,sample_t_ms=1000,channel=AD4,pin=PA4,adc_channel=18,adc_raw=34567,status=OK,input_mv=5511,soil_ph=9.00,range=HIGH
ADC_SAMPLE,round_id=21,sample_t_ms=2000,channel=AD4,pin=PA4,adc_channel=18,adc_raw=0,status=OK,input_mv=-10,soil_ph=3.00,range=LOW
ADC_SAMPLE,round_id=31,sample_t_ms=3000,channel=AD4,pin=PA4,adc_channel=18,adc_raw=NA,status=ADC_POLL_FAILED,stage=POLL,hal_status=HAL_TIMEOUT,prepare_status=HAL_OK,config_status=HAL_OK,stop_status=HAL_OK,input_mv=NA,soil_ph=NA,range=NA
ADC_WINDOW,channel=AD4,window_end_ms=3000,sample_count=30,valid_count=21,adc_raw_min=0,adc_raw_max=34567,adc_raw_mean=32920.95,error_count=9,status=PARTIAL,cleanup_error_count=0,prepare_error_count=0,config_error_count=0,last_error=ADC_POLL_FAILED,last_stage=POLL,last_hal_status=HAL_TIMEOUT,last_prepare_status=HAL_OK,last_config_status=HAL_OK,last_stop_status=HAL_OK,input_mv=5248,soil_ph=9.00,range=HIGH
```

主机集成测试还验证了25次有效采集的三秒窗口：sum=392906、count=25，`adc_raw_mean=15716.24,input_mv=2500,soil_ph=6.00,range=OK`，实际记录中的原错误诊断字段仍保留。打印转换使用完整sum/count；原始均值字段仅用于显示。窗口冻结之后的最新单点可为raw=0、pH 3.00/LOW，不影响上一窗pH 6.00。

无有效样本时原始min/max/mean和新字段均为NA；部分有效窗口仍换算有效样本，不设80%质量门槛。空窗口沿用既有NO_SAMPLES状态。所有HAL/准备/配置/停止错误及清理计数保留，未执行阶段仍显示NA。

完整CRLF字节记录见 `Docs/validation/soil_ph_arm_console.txt`。

## 修改文件清单

生产代码：

- 新增 `Sensors/Inc/sensor_ph.h`、`Sensors/Src/sensor_ph.c`：唯一pH纯换算。
- 修改 `App/Inc/app_main.h`：完整轮次中的soil_ph与冻结窗口的last_soil_ph只读诊断。
- 修改 `App/Src/app_main.c`：成功采集后换算，冻结原窗口后换算，并在现有AD4行追加字段。

构建引用：`MDK-ARM/tea_sense.uvprojx`、`MDK-ARM/tea_sense.uvoptx`，Sensors组加入sensor_ph.c且不重复；IDE后续FileNumber顺延。包含目录、目标配置、启动和链接设置保留。

测试：新增 `Tests/ph_cases.h`；修改 `Tests/adc_raw_cases.h`、`Tests/dtu_host_test.c`、`Tests/check_arm_printf.py`。保留原缓冲边界检查和其余回归。

文档：更新 `Docs/code_architecture.md`，新增本文。验证文件为 `Docs/validation/soil_ph_{host_tests,architecture,arm_build,arm_printf,protection}.log`、`soil_ph_arm_console.txt`。全量构建刷新MDK生成产物，未自动下载固件。

## 已执行验证

- `py Tests/run_host_tests.py`：4种配置×2个程序，共8组通过，GCC使用 `-Wall -Wextra -Werror`。覆盖四个指定点、端点上下未舍入范围判定、负电压、有效raw=0、全16位域单调性、失败/首次有效之前/无有效样本/空窗、最大计数。
- raw_sum=88311/count=1000时，电压4mV、pH 3.01；先舍入raw到88.31或先用4mV计算都会错误得到3.00。该反例通过，确认窗口精度和两个独立转换。
- 集成测试验证25个真实尝试的中点窗口、三点原始均值先算再限幅、部分有效及27次清理失败计数、窗口切换/清空、打印复用最近轮次、tick回绕/迟到、成功后失败不复用旧pH、ADC操作次数不因转换增加。
- 原AD0土壤换算/三秒JSON、AD2风速、AD3风向、百叶箱协议/查询/退避/缓存、DTU ACK/分包/溢出/UART恢复及配置保护回归通过。
- `py Tests/check_architecture.py`：分层、无HAL上移/可写extern、GPIO/ADC/UART/IRQ配置和MDK源文件/包含路径检查通过；新源文件仅编译一次。
- `py Tests/check_arm_build.py`：ARMCC5.06 update7全量编译链接 **0错误、0警告**，Code=41210、RO-data=2018、RW-data=96、ZI-data=6336。标准Arm C库、单一USART2重定向、无半主机/浮点printf检查通过。
- `py Tests/check_arm_printf.py`：真实ARM生产App/Sensors/Bsp与Arm C库运行，HAL为模拟。原91轮、364次明确5/3/19/18配置、728次停止、自校准1次保持；新增11个pH纯换算检查点，覆盖pH 3/6/7/9、signed mV、LOW/HIGH、有效0/空输入、分数均值、最大计数。原9个风速检查、三次土壤上传、两次ACK、六项百叶箱事务和printf恢复通过，无运行时malloc。
- 控制台字节与上一版逐行比较：AD4只在原行末追加三个字段；AD2、中文土壤/风向和百叶箱输出字节不变。43条记录均为CRLF，无裸LF或CRCRLF。
- 修改前源码与SHA-256清单已保存到本机临时快照，保护审计见 `soil_ph_protection.log`。Bsp、Core、Drivers、.ioc、硬件/调度配置、其他Sensors生产源码、上传JSON/私有密钥和既有验证记录保持不变。MDK XML核对仅有新文件引用及必要编号顺延，其他设置保持。

## 未验证项目

没有连接板卡或自动烧录。未实测本版在真实USART2打印耗时下的采样次数、AD4电压复核、探头供电/接线、ADC串扰、公网DTU与长时间稳定性；尚未做真实土壤pH精度对照。CPU仿真与故障注入不替代这些硬件验证。
