# 土壤水分传感器 ADC / USART2-RS485 实现与验证

> 以下保留重构前的 bringup 记录。2026-11-12 框架与命名已迁移，当前代码入口、参数名、构建方法和本次验证请以 [代码架构](code_architecture.md) 与 [重构交付](refactor_report.md) 为准；本文旧路径与历史日志不作为本次验证证据。

原始检查日期：2026-11-11；模式2与比例输出更新：2026-11-12（北京时间）。工程：`D:\12-Project\1-EC-Project\tea_sense`。

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
- 用户于 2026-11-12 确认型号 RE-SD-V05-TR，0 V 对应 0%，5 V 对应 100%。已加入线性含水率换算；板卡分压尚未确认，因此当前 `sensor_mV=NA`、`moisture_pct=NA`。

## 2. 文件和实现

| 文件 | 用途 |
| --- | --- |
| `Core/Src/main.c` | 仅 USER CODE 区增加 include、`SoilApp_Init()`、`SoilApp_Process()` |
| `MDK-ARM/tea_sense.uvprojx` | 新增 Application/SoilSensor 源文件组，增加 `../App/Inc` 头文件路径 |
| `App/Inc/soil_app_config.h` | 固定 AD0 接口、传感器型号及 0～5 V 量程、周期、超时、参考电压、分压增益及方向控制 |
| `App/Inc/soil_sensor_app.h`、`App/Src/soil_sensor_app.c` | 主循环调度、记录格式化、发送及诊断 |
| `App/Inc/soil_adc.h`、`App/Src/soil_adc.c` | 单端 offset+linearity 校准、单次采样、错误、整数电压与含水率换算 |
| `App/Inc/usart2_rs485.h`、`App/Src/usart2_rs485.c` | USART2 阻塞发送、长度检查、TC 有界等待及超时后的轮询 |
| `App/Inc/rs485_direction.h`、`App/Src/rs485_direction.c` | 独立方向控制；默认无 GPIO 操作，手动模式有编译保护 |
| `Tests/stubs/*`、`Tests/soil_host_test.c`、`Tests/run_host_tests.py` | 模拟 HAL 的主机测试，不加入固件编译 |
| 本文 | 检查记录、参数和上板验证说明 |

没有更改 ADC/UART 生成初始化、`.ioc`、时钟、启动文件、链接脚本或无关外设。
CubeMX 原工程中已有 DMA/HAL 驱动文件不等于业务启用 DMA；本实现没有调用 DMA、ADC 中断或 RTOS。
再次使用 CubeMX 生成代码后，请确认用户源文件组和 `../App/Inc` 仍保留。

## 3. 当前模式与集中配置

按本次要求保留/恢复模式 0、1，模式 2 为默认。修改 `App/Inc/soil_app_config.h` 的 `SOIL_APP_MODE` 并重新编译即可切换：

| 值 | 宏 | 行为 |
| --- | --- | --- |
| 0 | `SOIL_MODE_UART_TEST` | 每秒固定文字 `TEA_SENSE_USART2_TEST` 及序号；不执行业务 ADC 校准或采样 |
| 1 | `SOIL_MODE_ADC_RAW` | 初始化单端校准，每秒一笔原始 ADC；电压及含水量字段为 NA |
| 2（默认） | `SOIL_MODE_ADC_VOLTAGE` | 每秒原始值、ADC 引脚电压、传感器电压、比例与百分数，以及各级状态 |

本次检查前的磁盘版本已按此前指令移除了模式选择。本轮复用原有发送层和采样层恢复模式 0/1 的功能，没有新增第二套串口或 ADC 业务。所有模式的采集输入固定为 **AD0/AIN0 → PB1 → ADC1 Channel 5**；旧系统的 AD7 不参与当前工程的通道或数组寻址。

| 集中参数 | 当前值及含义 |
| --- | --- |
| `SOIL_APP_MODE` | 2 |
| `SOIL_SENSOR_MODEL` | `RE-SD-V05-TR`，由用户提供 |
| `SOIL_SENSOR_OUTPUT_MIN_MV / MAX_MV` | 0 / 5000 mV；当前项目采用 0～100% 映射 |
| `SOIL_APP_PERIOD_MS` | 1000 ms |
| `SOIL_ADC_VREF_MV` | 3300 mV，保留原名义值 |
| `SOIL_ADC_VREF_IS_MEASURED` | 0，保留 EST 标记，没有自动置 1 |
| `SOIL_SENSOR_GAIN_CONFIRMED` | 0，保持未确认；传感器电压和含水量均为 NA |
| `SOIL_SENSOR_GAIN_NUM / DEN` | 1 / 1 仍为未启用占位，没有选择 133/33 或 147/47 |

### 已有计算的复用

- `soil_adc.c` 原有 H7 三参数单端 offset+linearity 校准、启动/轮询/停止及错误处理继续使用。
- `raw` 明确为 `uint16_t`。HAL 返回值仍先用 `uint32_t` 接收并检查不超过 65535，再赋给 raw，避免异常高位被截成有效零值。
- 现有电压和百分数函数继续使用；本轮没有复制旧 ADS1115 驱动、0.1875 系数或补码转换。
- 原 `SoilAdc_MoisturePercentX100()` 结果同时等于 `moisture_ratio × 10000`，因此计算一次后分别格式化为四位小数比例和两位小数百分数，没有新增重复的含水量换算函数。

```text
adc_pin_mV     = raw × VREF_mV / 65535
sensor_mV      = adc_pin_mV × GAIN_NUM / GAIN_DEN
moisture_ratio = sensor_mV / 5000
moisture_pct   = sensor_mV / 50
```

计算采用整数和 64 位中间量，四舍五入到毫伏。传感器电压继续复用原函数的等价合并公式 `raw × VREF_mV × NUM / (65535 × DEN)`：在中间引脚电压还未舍入时还原增益，最后统一舍入。日志中的整数 `adc_pin_mV` 已单独舍入，所以把该显示值再乘增益，可能与内部结果有少量毫伏差异。

当前固件**不启用滤波**，模式 2 每秒输出本次采样的原始值。本轮没有引入未实现的滤波开关。后续若加入 12 点去极值均值，应使用独立开关、默认关闭，并保留原始值、有效样本数和明确的刷新周期。

## 4. 模式2输出与状态

每行以 `\r\n` 结束，正常记录包含 `seq`、`t_ms`、`adc_raw`、`adc_pin_mV`、`sensor_mV`、`moisture_ratio`、`moisture_pct`，以及测量、ADC、参考值、传感器和含水量状态。

当前未确认增益时的格式示例（数字不是实测数据）：

```text
seq=1,t_ms=1000,adc_raw=32768,adc_pin_mV=1650,sensor_mV=NA,moisture_ratio=NA,moisture_pct=NA,status=GAIN_UNCONFIRMED,vref=EST,sensor_status=GAIN_UNCONFIRMED,moisture_status=GAIN_UNCONFIRMED,channel=AD0,model=RE-SD-V05-TR,mode=2,text=ADC_VOLTAGE,adc_status=OK,adc_hal=HAL_OK,adc_stop=HAL_OK,dir=AUTO_UNCONFIRMED,tx_prev=NONE,tx_errors=0
```

| 情形 | 输出与状态 |
| --- | --- |
| ADC 成功、增益未确认 | raw 与 ADC 引脚电压可用；传感器电压、比例、百分数为 NA；`GAIN_UNCONFIRMED` |
| 增益确认，参考电压为名义值 | 可计算值显示数值；`vref=EST`，`sensor_status=EST`，`moisture_status=EST` |
| 增益确认，参考已实测 | 来源标识为 `MEASURED`；显示位数不代表实际传感器精度 |
| 校准/启动/等待/停止失败 | 测量字段均为 NA；`ADC_CAL_FAILED / ADC_START_FAILED / ADC_POLL_FAILED / ADC_STOP_FAILED`，附 HAL 状态及停止结果 |
| HAL 数据超出 16 位 | `ADC_OUT_OF_RANGE`；没有窄化成有效 0 |
| 还原传感器电压超过 5000 mV | 保留还原电压用于排查，比例和百分数为 NA；`SENSOR_OUTPUT_RANGE`、`OUT_OF_RANGE` |
| 有效零电压 | 增益确认后可显示 `0.0000`、`0.00`；与失败的 NA 区分 |
| snprintf 失败或截断 | 只发送固定 `FORMAT_ERROR` 行，所有测量字段含比例均为 NA |

`adc_status` 单独保留采样阶段结果，`status` 表示整体测量状态。`moisture_status` 同时作用于比例和百分数，两者使用同一有效性判定。`tx_prev / tx_errors` 表示前次发送及累计错误；UART 自身故障也可通过 `SoilApp_GetDiagnostics()` 对应的模块诊断数据查看。

三点换算已用既有函数和串口记录路径验证：

| 传感器电压 | `moisture_ratio` | `moisture_pct` |
| --- | --- | --- |
| 0 mV | 0.0000 | 0.00 |
| 2500 mV | 0.5000 | 50.00 |
| 5000 mV | 1.0000 | 100.00 |

模拟测试中的参考值和增益仅用于构造端点输入，没有写入实际板卡配置。当前默认固件仍因增益未确认而显示 NA。

## 5. 接线与仍待实测的参数

1. 传感器按实物铭牌的 10～30 V 要求供电；外接 12 V DC 在范围内。按已核对线序接模拟输出到 AD0、模拟地到 AD 端子排 GND。板卡与传感器若分别供电，负端共地，独立电源正端不并联。
2. 核实 R46/R6/R68 实装和输入负载，并测 AD0 与 PB1 等效节点在同一稳定状态下的电压，以确认还原增益。原理图的两种阻值候选不能直接作为实装结论。
3. 实测 MCU VREF+ 相对 VREF−，填写真实参考值。此前 AIN0 的 18～25 mV 不能自动用于扣零或确定分压；也不要求有一个“最高浓度”的土样。
4. 第二路 RS485 的自动/手动装配、电平域与 R120 连接仍按第 1 节核实。本轮完全保留现有 USART2/RS485 驱动，不配置 PD3 为输出。
5. 烧录当前 `MDK-ARM/tea_sense/tea_sense.hex`；默认模式 2。电脑串口助手使用 115200、8N1、无流控、ASCII，关闭 HEX 显示和自动发送。

## 6. 本轮文件与验证

| 文件 | 修改用途 |
| --- | --- |
| `App/Inc/soil_app_config.h` | 恢复模式 0/1/2 定义，默认 2；保留现有 VREF、增益及方向配置值 |
| `App/Inc/soil_adc.h` | raw 明确为 uint16_t；说明百分数整数结果与比例定点值的关系 |
| `App/Src/soil_adc.c` | HAL 返回数据先检查范围再窄化；校准、错误处理和正确换算继续复用 |
| `App/Src/soil_sensor_app.c` | 保留/恢复模式 0/1，模式 2 新增比例与独立 ADC 状态；所有格式化继续检查长度 |
| `Tests/soil_host_test.c` | 三模式、0/2500/5000 mV 端点及完整记录、16 位范围、ADC 失败、EST/NA、格式化和 RS485 测试 |
| `Tests/run_host_tests.py` | 运行七组主机模拟 HAL 配置；分压及参考参数覆盖仅用于测试 |
| `Tests/check_arm_modes.py` | 使用现有 Keil 其余对象和链接配置，编译/链接三模式与计算分支；临时验证产物不替换正式 HEX |
| 本文及 `Docs/validation/` | 当前设置、输出说明、测试和源文件范围核对记录 |

没有新增业务 .c/.h 模块。原 `main.c` USER CODE 入口、`Core`、`Drivers`、`.ioc`、MDK 工程及 USART2/RS485 方向与发送层均保持不变；新测试脚本不加入 MCU 编译路径。不使用 FreeRTOS、DMA、Linux 线程、SQLite、上传或其他设备阻塞读取。

正式镜像：`MDK-ARM/tea_sense/tea_sense.hex`，默认模式 2。旧的历史分模式镜像仍在 `MDK-ARM/history/2026-11-11/`，不用于当前验证。模式切换后请重新编译正式工程。

验证工具为现有 ARM Compiler 5.06 update 7 build 960。正式目标编译为 0 errors / 0 warnings，模式 0、1、2 及含水量计算分支均完成 ARM 编译和链接；主机测试七组配置均通过。日志：

- `MDK-ARM/soil_build.log`
- `Docs/validation/host_tests.log`
- `Docs/validation/arm_modes_build.log`
- `Docs/validation/source_and_hex_audit.log`

```powershell
# 在工程根目录运行；先完成一次 Keil 正式目标编译。
& 'C:\Program Files\WindowsApps\OpenAI.CodexPrimaryRuntime.v26-1111-570-0_26.1111.570.0_x64__3k8sg7r9htsxt\dependencies\python\python.exe' Tests\run_host_tests.py
& 'C:\Program Files\WindowsApps\OpenAI.CodexPrimaryRuntime.v26-1111-570-0_26.1111.570.0_x64__3k8sg7r9htsxt\dependencies\python\python.exe' Tests\check_arm_modes.py
```

本轮没有连接板卡、烧录或进行 ADC/RS485 实测。编译和模拟测试通过，不等于确认实际参考电压、分压、输入负载或方向装配。

## 7. 此前新增资料核对记录

检查目录：`D:\4-横向项目\2-茶叶物联网项目\传感器信息`。该次检查只补充资料核对结果；当前固件完善情况以第 3～6 节为准。

### 资料版本与接线

- `RS-WS--TR 土壤湿度传感器.pdf` 只有一页图像参数表。该表写明 DC 4.5～30 V 供电、土壤水分量程 0～100%、分辨率 0.1%，但输出信号是 RS485（Modbus）。该页没有模拟输出换算公式、接线颜色、Modbus 寄存器表或与 RE-SD-V05-TR 相同的型号标识。因此不能据此确认手中模拟型号的线性公式、精度或板卡增益。
- `香橙派\传感器部署说明文档.docx` 的设备概览将土壤湿度传感器列为 0～5 V。其嵌入的《第五代设备端口连接表》给出：棕线 VCC、黑线 GND、黄线 AD7；这是旧设备的 P10 接口安排。本次 H743 工程仍使用 AD0/AIN0，不改为 AD7。
- 用户实物照片的铭牌选择为 VWC、DC 10～30 V、0～5 V 输出。手中传感器应继续按模拟信号接入 ADC，板卡 USART2 / 第二路 RS485 用于向电脑发送采集记录。

接线时先核对实际线标与部署表一致：棕线接外部直流 +12 V，黑线接该电源负端并连接 H743 板卡 AD 输入端子排 GND，黄线为模拟信号、经已核实的调理电路接 AD0/AIN0。两套独立电源共地时不将正极并联；12 V 电源正端不接 AD0/PB1。

### 12 V 与换算结论

12 V DC 在实物铭牌的 10～30 V 范围内，也在新 PDF 的 4.5～30 V 范围内。因此供电电压选择符合范围；实际输出是否稳定、极性与接线是否正确，仍需现场测量。PDF 的 0.7 W 标注是在 24 V DC 条件下给出的参数，不表示要求使用 24 V，也不直接作为当前模拟型号在 12 V 下的功耗。

现有程序继续采用用户此前确认的线性关系：`moisture_pct = sensor_mV / 50`。这与 0 V→0%、5 V→100% 的端点一致，但此次上传的 RS485 参数表没有独立证实该模拟型号的电压映射。

| 传感器输出电压 | 按当前用户给定关系计算的含水率 |
| --- | --- |
| 0 mV | 0% |
| 500 mV | 10% |
| 1000 mV | 20% |
| 2500 mV | 50% |
| 4000 mV | 80% |
| 5000 mV | 100% |

ADC 链路仍为 `V_pin = raw × VREF / 65535`、`V_sensor = V_pin × GAIN_NUM / GAIN_DEN`。这里的传感器电压必须先由实际分压恢复，不能直接用 PB1 电压代入传感器公式。

当前可确定 `SOIL_SENSOR_OUTPUT_MIN_MV=0`、`SOIL_SENSOR_OUTPUT_MAX_MV=5000`、16 位满量程 65535。`SOIL_ADC_VREF_MV=3300` 仍为名义值、`SOIL_ADC_VREF_IS_MEASURED=0`；板卡分压没有从这些新文件中得到确认，保持 `SOIL_SENSOR_GAIN_CONFIRMED=0`，传感器电压和含水率继续为 NA。

参考源实测、R46/R6/R68 装配与输入负载确认仍按前述步骤进行。仅有 AIN0 的 18～25 mV 单点，且此前是否已正确给传感器供电尚未确认，不能据此标定零点或增益。
