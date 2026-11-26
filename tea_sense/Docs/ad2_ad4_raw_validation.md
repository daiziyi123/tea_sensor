# AD2 / AD4 原始采集与统计打印

2026-11-14，北京时间。直接修改当前 tea_sense；未自动烧录。下列输出来自主机或 ARM CPU 仿真及模拟 HAL，均不是板上实测数据。

## 修改位置与行为

- `Bsp/Inc/bsp_adc.h`、`Bsp/Src/bsp_adc.c`：在原接口增加 `BSP_ADC_INPUT_PH_AD4` → ADC1 Channel 18。每次读取仍执行明确输入映射、预停止、通道配置、启动、有限超时 Poll、取值及停止清理。初始化自校准、16 位、异步分频 8、单 Rank、软件触发、387.5 Cycles 均保持现有实现。
- `App/Inc/app_main.h`、`App/Src/app_main.c`：在现有 AD0→AD2→AD3 轮次末尾增加 AD4，顺序为 5→3→19→18，下一轮重新选择 5。AD4 使用独立结果、读取起止时间和窗口；原 AD3 原始窗口的 reset/add 改为私有共用函数，保留原类型别名及统计语义。最近完整轮次全部采完后才发布。
- `Sensors/Inc/sensor_wind_speed.h`、`Sensors/Src/sensor_wind_speed.c`：仅为已有冻结窗口结果增加并复制全体有效样本的 `uint64_t raw_sum`。AD2 继续只走原有读取和 `SensorWindSpeed_WindowAdd`；没有第二个 AD2 累加器。原 `raw_mean`、`trimmed_sum`、`used_count`、质量门槛及既有换算语义保留。
- App 主循环直接使用已有 USART2 `printf`。复用 `APP_CONSOLE_PERIOD_MS=1000`、`APP_SAMPLE_WINDOW_MS=3000`、`APP_SAMPLE_ROUND_PERIOD_MS=100`，不增加同义配置、打印模块、菜单或调试模式。硬件超时仍在 `bsp_config.h`。
- 没有新增源模块，因此无需修改 MDK 工程引用；现有包含目录和源文件引用已通过架构检查、全量构建验证。

`Core/Src/adc.c` 已将 PA6/WIND_SPEED_AD2 和 PA4/PH_AD4 配置为 GPIO_MODE_ANALOG、GPIO_NOPULL；`.ioc` 已分别指定 ADCx_INP3 / ADCx_INP18。本次没有修改 Core、Drivers、.ioc 或重复初始化 GPIO。

土壤 AD0 采样及换算、风向 AD3 采样及输出、三秒土壤上传 JSON、USART6 DTU、USART3/UART4 百叶箱代码保持原业务。未新增风速或 pH 换算、标定算法、滤波、端子毫伏输出，也没有修改服务器、前端或私有上传密钥。

## 时间、计数和均值

- `round_id` 来自已完成轮次；两条 `ADC_SAMPLE` 使用同一轮快照。
- `sample_t_ms` 为各路实际调用读取接口前记录的毫秒 tick，即 `wind_read_start_ms` / `ad4_read_start_ms`，不使用打印时刻。它是软件读取开始时间，包含随后 Bsp 的准备和转换过程；并非示波器测得的采样保持瞬间。读取结束 tick 同时保存在只读诊断中。相同毫秒时间戳不表示两路同时转换。
- 1000ms 到期而采样未到期时，只打印最近轮次，不启动 ADC。所有调度沿用无符号 tick 差，无 HAL_Delay。
- 每个 3000ms 窗口分别冻结 AD2 / AD4 的真实尝试数、有效数、原始和、最小值、最大值和错误。`error_count=sample_count-valid_count`，一次采集失败即使同时清理失败，也只计一次采集错误；清理错误另计。
- `adc_raw_mean` 使用所有有效样本普通算术均值，计算式 `(raw_sum * 100 + valid_count / 2) / valid_count`，中间量为 64 位整数，最后定点输出两位小数。它不使用 AD2 的去极值结果；即使未达到原有质量门槛，只要有有效样本也输出普通均值。
- `status=OK` 表示本窗有样本且全部有效；`PARTIAL` 表示有效和失败样本并存；`NO_VALID_SAMPLES` 表示有尝试但全部失败；`NO_SAMPLES` 表示没有尝试。无有效样本时 min/max/mean 全部为 NA，有效 0 则输出 0 / 0.00。
- `cleanup_error_count`、`prepare_error_count`、`config_error_count` 和最后一次错误的阶段、HAL/准备/配置/清理结果附在同一窗口行。未执行的步骤为 NA，无错误的 `last_error/last_stage` 为 NONE。各采集错误类别的计数仍保留在只读诊断数组中。
- 窗口按轮次起始 tick 归属，同轮跨越边界也一起进入原窗口；边界新轮进入新窗口。窗口冻结后 reset。保留既有严重迟到处理：跨多个窗口时不补采、不重放，最新完成空窗输出实际计数 0，并更新跳过计数。

## 实际打印格式

每条记录都是一个完整行，以 CRLF（`0D 0A`）结尾。以下直接取自 ARM 仿真输出：

```text
ADC_SAMPLE,round_id=11,sample_t_ms=1000,channel=AD2,pin=PA6,adc_channel=3,adc_raw=12345,status=OK
ADC_SAMPLE,round_id=11,sample_t_ms=1000,channel=AD4,pin=PA4,adc_channel=18,adc_raw=34567,status=OK
ADC_SAMPLE,round_id=31,sample_t_ms=3000,channel=AD4,pin=PA4,adc_channel=18,adc_raw=NA,status=ADC_POLL_FAILED,stage=POLL,hal_status=HAL_TIMEOUT,prepare_status=HAL_OK,config_status=HAL_OK,stop_status=HAL_OK
ADC_WINDOW,channel=AD2,window_end_ms=3000,sample_count=30,valid_count=30,adc_raw_min=12345,adc_raw_max=12345,adc_raw_mean=12345.00,error_count=0,status=OK,cleanup_error_count=0,prepare_error_count=0,config_error_count=0,last_error=NONE,last_stage=NONE,last_hal_status=NA,last_prepare_status=NA,last_config_status=NA,last_stop_status=NA
ADC_WINDOW,channel=AD4,window_end_ms=3000,sample_count=30,valid_count=21,adc_raw_min=0,adc_raw_max=34567,adc_raw_mean=32920.95,error_count=9,status=PARTIAL,cleanup_error_count=0,prepare_error_count=0,config_error_count=0,last_error=ADC_POLL_FAILED,last_stage=POLL,last_hal_status=HAL_TIMEOUT,last_prepare_status=HAL_OK,last_config_status=HAL_OK,last_stop_status=HAL_OK
```

主机故障注入还检查了 Poll 超时与清理失败同时发生时，同一行保留 `hal_status=HAL_TIMEOUT`、`stop_status=HAL_ERROR`，原始值打印 NA；成功取到非零值但停止失败同样打印 NA。

完整原始 CRLF 字节记录：`Docs/validation/ad2_ad4_arm_console.txt`。该文件含原有中文土壤/风向与百叶箱输出，以及新增 ADC 记录。

## 修改文件清单

生产代码，共 6 个文件：

1. `Bsp/Inc/bsp_adc.h`
2. `Bsp/Src/bsp_adc.c`
3. `App/Inc/app_main.h`
4. `App/Src/app_main.c`
5. `Sensors/Inc/sensor_wind_speed.h`
6. `Sensors/Src/sensor_wind_speed.c`

测试，共 9 个文件，其中 `adc_raw_cases.h` 为新增：

1. `Tests/stubs/stm32h7xx_hal.h`
2. `Tests/soil_host_test.c`
3. `Tests/dtu_host_test.c`
4. `Tests/ad3_cases.h`
5. `Tests/wind_direction_cases.h`
6. `Tests/wind_speed_cases.h`
7. `Tests/adc_raw_cases.h`
8. `Tests/check_architecture.py`
9. `Tests/check_arm_printf.py`

文档及验证产物：本文；`Docs/validation/ad2_ad4_host_tests.log`、`ad2_ad4_architecture.log`、`ad2_ad4_arm_build.log`、`ad2_ad4_arm_printf.log`、`ad2_ad4_arm_console.txt`、`ad2_ad4_protection.log`。ARM 全量构建同时刷新 `MDK-ARM/tea_sense` 下的编译、链接产物，包括 axf/hex/map/htm；工程引用、IDE 配置、启动及链接配置均未修改。

## 已执行验证

- `py Tests/run_host_tests.py`：4 种配置 × 2 个真实应用主机程序，共 8 组通过，GCC 开启 `-Wall -Wextra -Werror`。验证 5→3→19→18→5、每读明确配置、单次初始化自校准、有限超时、失败停止清理、配置/启动/Poll/范围/停止故障、共享预停止故障、AD4 失败不覆盖 AD2、反向故障隔离、有效 0 / 失败 NA / 全失败 / 空窗区别。
- 新增窗口用例验证极值参与普通均值（AD2 全体均值 3117.83，而旧去极值均值仍为 1000）、不同有效次数（4 与 3）、四舍五入、窗口 reset、边界轮次归属、读取跨 tick 回绕、打印复用 950ms 已完成轮次、跨多窗迟到不补采，以及 UINT32_MAX 个 16 位有效样本的 64 位原始和计算边界。
- 原土壤全域换算、质量门槛、三秒 JSON 上传及 ACK/拆包/溢出/UART 恢复、AD3 风向、百叶箱 CRC/解析/查询周期/间隔/回绕/退避/缓存和并行通信回归通过；原有配置编译保护通过。
- `py Tests/check_architecture.py`：分层、GPIO/ADC 配置、无可写 extern、MDK 源文件及包含目录、USART6 与百叶箱 UART 配置检查通过。
- `py Tests/check_arm_build.py`：ARMCC 5.06 update 7 全量编译链接，0 Error(s)、0 Warning(s)。实际固件尺寸：Code=40036、RO-data=2044、RW-data=96、ZI-data=6304。标准 Arm C 库、唯一 USART2 重定向、无半主机或浮点 printf 检查通过。仅生成固件，未下载。
- `py Tests/check_arm_printf.py`，指定已有临时 Unicorn 包路径：真实 ARM App/Sensors/Bsp/Arm C 库运行通过，HAL 为模拟。91 完整轮次，364 次 5/3/19/18 通道配置、728 次前后停止、1 次自校准；AD4 正常/有效 0/Poll 失败及两路普通均值输出验证通过；三次原土壤 JSON 发送、两次 ACK、printf 故障恢复、六个百叶箱请求/回复通过，无运行时 malloc。
- 原始控制台字节检查：43 条 CRLF 记录，其中 ADC_SAMPLE 16 条、ADC_WINDOW 6 条；无额外裸 LF 或 CRCRLF。
- 修改前源文件已保存于本机临时快照；SHA-256 清单复核见 `ad2_ad4_protection.log`。既有代码以当前文件为基线修改，没有 Git 初始化、回退或覆盖旧实现。

## 尚未验证

没有连接板卡或自动烧录，未实测 PA6/PA4 电气接线、通道串扰、真实电压与 raw 对齐、ADC 精度、串口物理时序、加入打印后的实际窗口采样次数、DTU 公网联通及长时间稳定性。主机/CPU 仿真结论不替代这些上板检查。当前为阻塞 USART2 printf；真实串口耗时可能造成采样迟到，窗口按真实尝试次数报告，不保证每窗固定 30 点。
