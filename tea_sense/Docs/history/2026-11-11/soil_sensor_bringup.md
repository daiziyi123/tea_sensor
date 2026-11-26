# 土壤水分传感器 ADC / USART2-RS485 实现与验证

检查日期：2026-11-11（北京时间）。工程：`D:\12-Project\1-EC-Project\tea_sense`。

## 1. 修改前的实际检查

检查来源为现有 `tea_sense.ioc`、`Core/Src/{adc,usart,gpio,main}.c`、HAL 源码、MDK 工程，以及
`D:\4-横向项目\2-茶叶物联网项目\H743VIT_8路485-8入出蓝\原理图\STM32H743VIT8RS485.pdf`。
已渲染并查看完整原理图，放大检查 ADC、第二路 RS485 和参考源。

| 项目 | .ioc 和生成代码核对结果 |
| --- | --- |
| 芯片 | STM32H743VIT6 / LQFP100 |
| ADC 输入 | PB1，ADC1，Channel 5，Rank 1，单端 |
| ADC 参数 | 16 位、软件触发、非连续、387.5 cycles、异步分频 8 |
| ADC 数据 | 单次转换、DR 读取、无左移、无硬件过采样；无 ADC DMA / ADC 中断初始化 |
| USART2 | PD5 TX、PD6 RX，AF7，115200、8N1、无流控 |
| 方向 GPIO | .ioc 无 PD3，gpio.c 未初始化 PD3 |
| 固件包 / HAL | .ioc 为 STM32Cube FW_H7 V1.12.1；实际 HAL 版本宏为 1.11.5 |
| 校准接口 | `HAL_ADCEx_Calibration_Start(hadc, CalibrationMode, SingleDiff)`，三个参数 |

未发现上述用户指定参数与生成代码冲突。`.ioc` 仅保存了部分非默认设置：例如它没有显式保存 USART2 波特率/字长，及 ADC 分辨率/触发等默认值；这些值已经在生成 C 代码中逐项确认。没有为补全默认字段而修改 `.ioc`。

### 第二路 RS485

- PD5 → U5（ADuM1201）3 脚 Vib → 6 脚 Vob → `485_T2` → U12 的 DI。
- U12 的 RO → `485_R2` → U5 的 7 脚 Via → 2 脚 Voa → PD6。
- U12 图上标 SP3485E；2 脚 `/RE` 与 3 脚 DE 共接 `RS485_RE2`，A/B 对应 A2/B2。
- Q2（标 M6）、R84（10 kΩ，连 TX）、R87（4.7 kΩ，连 RX）、R102（1 kΩ）、C1（0.01 μF）构成自动收发相关连接。`RS485_RE2` 与 Q2、R102、C1 的节点相连。
- R120 标为 0 Ω，连接 PD3 和 `RS485_RE2`。同一 PD3 在其他电路还经 R145 接 `RS485_RE4`、R167 接 `RS485_RE7`；不能只考虑第二路。
- 控制节点在 +5VG/GNDG 一侧；U5 隔离了 TX/RX，R120 的 GPIO 连接又跨到该侧。图上还有 R109（+5V/+5VG）、R116（GND/GNDG）0 Ω 跨接，实际隔离和电压关系由装配决定。

**必须确认的装配信息：**

1. 板卡版本及 U5、U12 实际型号；+5VG、GNDG 与 MCU 电源/地的实际关系；控制节点静态及发送时电平。
2. Q2、R84、R87、R102、C1 是否实装；自动收发电路在 115200 下是否能正确转换方向。
3. R120 是否实装，PD3 是否另接 R145/R167 或其他通道。
4. 手动模式时如何隔离自动电路、如何满足控制端电平与隔离要求，最终安全的控制 GPIO 和 TX/RX 极性是什么。

软件无法从 PDF 判定实装情况。默认 `AUTO` 只表示“不操作方向 GPIO”，不代表已经证实板卡自动收发可用；日志为 `dir=AUTO_UNCONFIRMED`。**没有把 PD3 设成输出，也没有直接驱动 RS485_RE2。**

### AIN0 与参考源

- AIN0 经 R46（标 10 kΩ）到 PB1；PB1 处 R6 对地，阻值处出现重叠的 `4.7K` / `3.3K` 标注，不能据此选定实际比例。
- AIN0 端还有 R68（标 100 Ω）对地，并有 D6 到 +3.3V 的钳位支路。必须确认 R68 是否安装及实际阻值；若确为 100 Ω，5 V 时该支路约 50 mA / 0.25 W，需核对传感器输出带载能力。
- 图上 U7 为 TL431A，网络名为 `VERF+`；MCU VREF+ 的实际电压必须实测。不能把 +3.3V 标注当成实测参考值。
- 因分压/装配未确认，默认 `sensor_mV=NA`；不会输出含水量百分比。

## 2. 文件和实现

| 文件 | 用途 |
| --- | --- |
| `Core/Src/main.c` | 仅 USER CODE 区增加 include、`SoilApp_Init()`、`SoilApp_Process()` |
| `MDK-ARM/tea_sense.uvprojx` | 新增 Application/SoilSensor 源文件组，增加 `../App/Inc` 头文件路径 |
| `App/Inc/soil_app_config.h` | 模式、周期、超时、参考电压、分压增益及方向方式的集中配置 |
| `App/Inc/soil_sensor_app.h`、`App/Src/soil_sensor_app.c` | 主循环调度、记录格式化、发送及诊断 |
| `App/Inc/soil_adc.h`、`App/Src/soil_adc.c` | 单端 offset+linearity 校准、单次采样、错误及整数电压换算 |
| `App/Inc/usart2_rs485.h`、`App/Src/usart2_rs485.c` | USART2 阻塞发送、长度检查、TC 有界等待及超时后的轮询 |
| `App/Inc/rs485_direction.h`、`App/Src/rs485_direction.c` | 独立方向控制；默认无 GPIO 操作，手动模式有编译保护 |
| `Tests/stubs/*`、`Tests/soil_host_test.c`、`Tests/run_host_tests.py` | 模拟 HAL 的主机测试，不加入固件编译 |
| `Tests/build_adc_variants.py` | 利用现有 Keil 编译产物，编译/链接两个 ADC 模式，不改工程配置或链接脚本 |
| 本文 | 检查记录、参数和上板验证说明 |

没有更改 ADC/UART 生成初始化、`.ioc`、时钟、启动文件、链接脚本或无关外设。
CubeMX 原工程中已有 DMA/HAL 驱动文件不等于业务启用 DMA；本实现没有调用 DMA、ADC 中断或 RTOS。
再次使用 CubeMX 生成代码后，请确认用户源文件组和 `../App/Inc` 仍保留。

## 3. 模式与需要填写的参数

修改 `App/Inc/soil_app_config.h` 的 `SOIL_APP_MODE`，重新编译并烧录：

| 配置值 | 模式 | 输出 |
| --- | --- | --- |
| `SOIL_MODE_UART_TEST`（0，默认） | 固定文字测试 | 每秒固定文字及序号，ADC/电压为 NA，不执行 ADC 校准/采样 |
| `SOIL_MODE_ADC_RAW`（1） | 原始采样 | 初始化校准；每秒采一笔，输出 raw；电压为 NA |
| `SOIL_MODE_ADC_VOLTAGE`（2） | 电压 | raw + ADC 引脚毫伏；仅在确认增益后计算传感器输出电压 |

可用编译器 `-D` 覆盖配置；默认值均有 `#ifndef` 保护。

- `SOIL_ADC_VREF_MV=3300` 是名义占位值；`SOIL_ADC_VREF_IS_MEASURED=0` 时日志 `vref=EST`，代表所有由此得到的电压都是估算值。实测 VREF+ 对 VREF- 后填写毫伏并设为 1，日志变为 `MEASURED`。
- `SOIL_SENSOR_GAIN_CONFIRMED=0` 时不计算传感器电压。核对/测量整个 AIN0→PB1 线性传递关系后，填写 `SOIL_SENSOR_GAIN_NUM / SOIL_SENSOR_GAIN_DEN` 并置 1。占位的 1/1 不是电路比例。
- 若确认只有理想分压且无钳位，增益为 `(R46 + R6) / R6`；仍需结合实际负载验证。若存在偏置或非线性，当前单一比例不足以还原，应继续保留 NA，再补充实际传递函数。
- `SOIL_APP_PERIOD_MS=1000`；ADC 等待 20 ms，UART 发送超时 100 ms，额外 TC 等待 5 ms。方向建立/保持时间集中配置，默认 0 ms；按实际控制电路需求调整。

计算使用 16 位满量程 `65535` 和 64 位整数中间值，四舍五入到毫伏：

```text
ADC_pin_mV = round(raw × VREF_mV / 65535)
sensor_mV = round(raw × VREF_mV × GAIN_NUM / (65535 × GAIN_DEN))
```

不会从电压计算或猜测实际含水量。

### 自动 / 手动方向入口

自动：`SOIL_RS485_DIRECTION_MODE=SOIL_RS485_AUTO`，确认装配后可设 `SOIL_RS485_AUTO_ASSEMBLY_CONFIRMED=1`，仅改变日志标识，不操作 GPIO。

手动：先完成第 1 节装配核实和必要的板级电路处理，再设 `SOIL_RS485_DIRECTION_MODE=SOIL_RS485_MANUAL`、`SOIL_RS485_MANUAL_WIRING_CONFIRMED=1`，显式定义：

```c
SOIL_RS485_GPIO_PORT
SOIL_RS485_GPIO_PIN
SOIL_RS485_GPIO_CLOCK_ENABLE()
SOIL_RS485_TX_LEVEL
SOIL_RS485_RX_LEVEL
```

没有默认控制引脚；缺少确认或参数会编译报错。只有已确认电平安全且自动电路已妥善隔离的控制连接才能使用推挽 GPIO 初始化。不要直接照原理图把 PD3 设为推挽输出。

发送层单独调用方向接口。当前 H7 HAL 的 `HAL_UART_Transmit()` 成功返回前会等 TC；发送层还显式检查 TC。任何返回路径都不会仅依据 TXE 切回接收。若额外 TC 等待超时，保留 TX 方向，后续 `Usart2Rs485_Process()` 观察到 TC 后才释放；期间拒绝新发送。TC 永不出现时需通过调试器排查 UART/时钟/状态，软件不会无限阻塞主循环，也不会强行截断正在发送的字节。

## 4. 串口助手与输出

第二路 A2/B2 接 USB-RS485 转接器对应 A/B，按板卡与转接器说明连接 GNDG 参考地，核对两端 A/B 命名。使用对应 COM 口：**115200、8 数据位、无校验、1 停止位、无流控、ASCII 文本显示、关闭 HEX 显示和自动发送**。每条记录自身已带 `\r\n`。

以下数字只用于说明格式，不是实测结果；实际 `t_ms` 包含外设初始化/校准耗时：

```text
seq=1,t_ms=1000,adc_raw=NA,adc_pin_mV=NA,sensor_mV=NA,status=UART_TEST,text=TEA_SENSE_USART2_TEST,vref=NA,sensor_status=NA,adc_hal=NA,adc_stop=NA,dir=AUTO_UNCONFIRMED,tx_prev=NONE,tx_errors=0
seq=2,t_ms=2000,adc_raw=32768,adc_pin_mV=NA,sensor_mV=NA,status=OK,text=ADC_RAW,vref=NA,sensor_status=DEFERRED,adc_hal=HAL_OK,adc_stop=HAL_OK,dir=AUTO_UNCONFIRMED,tx_prev=OK,tx_errors=0
seq=3,t_ms=3000,adc_raw=32768,adc_pin_mV=1650,sensor_mV=NA,status=OK,text=ADC_VOLTAGE,vref=EST,sensor_status=GAIN_UNCONFIRMED,adc_hal=HAL_OK,adc_stop=HAL_OK,dir=AUTO_UNCONFIRMED,tx_prev=OK,tx_errors=0
seq=4,t_ms=4000,adc_raw=NA,adc_pin_mV=NA,sensor_mV=NA,status=ADC_POLL_FAILED,text=ADC_VOLTAGE,vref=NA,sensor_status=ADC_INVALID,adc_hal=HAL_TIMEOUT,adc_stop=HAL_OK,dir=AUTO_UNCONFIRMED,tx_prev=OK,tx_errors=0
```

这些是不同模式/情形的独立例子，并非一次运行中自动切换模式。`status=OK` 表示采样过程成功，`vref=EST` 仍表示电压估算；原始 0 是有效转换结果时会显示 `0`，失败则一律 `NA`。

- 校准/启动/等待/停止错误分别为 `ADC_CAL_FAILED`、`ADC_START_FAILED`、`ADC_POLL_FAILED`、`ADC_STOP_FAILED`；`adc_hal` 保留 HAL 错误类型，`adc_stop` 保留清理结果。
- 校准失败后不继续采样，每秒报告错误，修复后复位重试。启动/等待失败会尝试停止，停止失败也不输出有效测量。
- 正常记录包含前一次发送状态 `tx_prev` 与累计发送错误 `tx_errors`。UART 自身故障可能无法即时报告；调试器可观察 soil_sensor_app.c 的 `diagnostics`（序号、最后发送状态、发送/格式化错误计数），以及发送层的 `drain_pending`。
- 所有 `snprintf` 结果均检查负值、截断和发送长度。失败发送固定 `FORMAT_ERROR` 行，序号/时间/测量为 NA；不会发送截断内容或越界读取缓冲区。
- 时间为 `HAL_GetTick()` 启动后毫秒，32 位约 49.7 天回绕；周期用无符号减法处理。序号及累计计数也按 uint32_t 自然回绕。主循环延误后只采一次，不密集补采。

## 5. 按顺序上板验证

1. **固定文字。** 先用默认 UART_TEST 镜像。确认板卡第二路方向控制的装配方式；保持 PD3 不被驱动。接 A2/B2 到 USB-RS485，按上述串口参数打开助手，复位后应约每秒收到 `TEA_SENSE_USART2_TEST` 和递增序号。若 PD5 有波形但 A2/B2 没有，检查 U5/U12 电源、自动方向电路与实装，不能靠盲目驱动 PD3 排障。乱码先查串口参数、地参考及 A/B 对应。
2. **ADC 原始值。** 切 ADC_RAW 并重新编译烧录。先核实 AIN0 的实际输入负载、分压和允许电压，再接限流且已知电压源或传感器。传感器按其 10～30 V 要求独立供电，模拟输出接 AIN0，模拟地接输入 GND；不要将 0～5 V 输出直接接 PB1。用万用表同时测 AIN0 与 PB1，保证 PB1 在 ADC 允许范围内。检查 raw 随电压变化、无超时/停止错误；接近两端的 0/65535 还需排查接线、负载及饱和，不能当成含水量百分比。
3. **电压。** 实测 VREF+，填写参考参数，再切 ADC_VOLTAGE。对照万用表检查 `adc_pin_mV`；名义参考期间必须看到 `vref=EST`。在确认 R46/R6/R68、实际传递比例和传感器带载能力之前，`sensor_mV` 应一直为 NA。验证多个输入点后才能填写增益并启用传感器电压还原。含水量标定另待传感器量程与换算资料确认。

## 6. 编译与本机验证

Keil：ARM Compiler 5.06 update 7 build 960，现有 `tea_sense` 目标。默认 UART_TEST 已完整编译生成 HEX，0 errors / 0 warnings。ADC_RAW、ADC_VOLTAGE 使用同一工程现有其余对象文件及原链接配置，重新编译全部 App 源文件并分别链接生成 HEX。

固件输出均位于 `MDK-ARM/tea_sense/`：

- `tea_sense.hex`：默认 UART_TEST，正常 MDK 工程编译输出。
- `soil_adc_raw.hex`：ADC_RAW、自动方向不操作 GPIO。
- `soil_adc_voltage.hex`：ADC_VOLTAGE、名义 3300 mV / EST、增益未确认 / sensor_mV=NA。

后两个文件由 `Tests/build_adc_variants.py` 生成；修改配置后需要重新运行脚本以更新，正常 Keil Build 只更新默认工程镜像，不会自动更新另外两个镜像。

主机测试使用实际四个 App C 源文件和模拟 HAL，检查三种模式、确认增益的模拟手动 GPIO 模式、未确认手动模式的编译保护、计时回绕、ADC 各阶段失败及停止清理、有效 0、16 位范围与电压、格式化负值/截断、发送失败、TC 超时/延迟释放和自动模式不操作 GPIO。模拟手动模式用的是虚拟引脚，不构成对 PD3 或实际硬件的确认。

PowerShell 复现（先切到工程根目录；Python 可替换为本机可用路径）：

```powershell
& 'C:\Users\15995\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' Tests\run_host_tests.py
# 先在 Keil 中编译默认 tea_sense 目标，再运行：
& 'C:\Users\15995\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' Tests\build_adc_variants.py
```

可通过环境变量 `SOIL_TEST_GCC` / `SOIL_ARM_BIN` 指定本机工具路径。
编译日志为 `MDK-ARM/soil_build.log`，补充验证日志见 `Docs/validation/`。

**本次没有连接板卡、烧录或实测传感器/RS485。编译与模拟 HAL 测试不等于上板验证；方向装配、参考电压、输入负载和分压参数仍待确认。**
