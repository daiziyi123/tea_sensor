# 框架与命名重构交付

日期：2026-11-12（北京时间）。范围为目录分层、职责拆分、命名、配置归属和现有功能迁移。

## 工作区与改动范围

工作目录：`D:\12-Project\1-EC-Project\tea_sense`。检查时没有 Git 仓库，未初始化、提交或重置 Git。以当前磁盘内容为基线，重构前快照保存在：

`C:\Users\15995\AppData\Local\Temp\tea_sense_before_refactor_20261112_203844`

目录、模块职责和调用关系见 [code_architecture.md](code_architecture.md)。已经完整替代的九个旧自编文件已删除；旧实现没有兼容壳。MDK 的 uvprojx 与 uvoptx 均同步三层分组和路径，保留编译器、启动、链接和调试设置。旧模块在当前输出目录中的失效对象与依赖文件清除；CubeMX 备份及 history 保留。

`Core/Src/main.c` 只替换 USER CODE 中的 include 和两个应用调用。其余 Core 文件保持原字节内容。`.ioc` 只将 `ProjectManager.DeletePrevious=true` 改为 false；BackupPrevious、KeepUserCode、固件包和所有硬件设置保留。未修改 Drivers 源码，未执行 CubeMX 重新生成。

## 文件映射

| 原文件（Inc/Src 表示 .h/.c） | 新文件 | 迁移职责 |
| --- | --- | --- |
| `App/{Inc,Src}/soil_sensor_app` | `App/{Inc,Src}/app_main`、`app_log` | 调度、诊断与日志分开 |
| `App/{Inc,Src}/soil_adc` | `Bsp/{Inc,Src}/bsp_adc`、`Sensors/{Inc,Src}/sensor_soil` | HAL 校准采集与土壤电压、水分业务分开 |
| `App/{Inc,Src}/usart2_rs485` | `Bsp/{Inc,Src}/bsp_console` | 固定 USART2 电脑输出 |
| `App/{Inc,Src}/rs485_direction` | `Bsp/{Inc,Src}/bsp_rs485_dir` | 现有 AUTO/MANUAL 方向控制 |
| `App/Inc/soil_app_config.h` | `App/Inc/app_config.h`、`Bsp/Inc/bsp_config.h`、`Sensors/Inc/sensor_soil.h` | 按运行、板级、土壤归属拆分 |
| 原 App 中 `HAL_GetTick` 调度依赖 | `Bsp/{Inc,Src}/bsp_time` | 将硬件时基引用收敛到 Bsp |

## API、类型与状态映射

| 原名称 | 新名称或归属 |
| --- | --- |
| `SoilApp_Init / Process / GetDiagnostics` | `App_Init / Process / GetDiagnostics` |
| 原私有 `EmitRecord` | 采样移至 `SensorSoil_Read`；格式与输出移至 `AppLog_WriteSoil` |
| 原私有 `FormatUnsigned / FormatFixed / FormatError` | `AppLog_FormatUnsigned / FormatFixed / WriteFormatError`（static） |
| 原私有 `SendLine` | 输出由 AppLog 完成；发送诊断由 App_Process 汇总，不保留转发壳 |
| `SoilAdc_Init(ADC_HandleTypeDef *)` | `BspAdc_Init(void)`，内部固定 ADC1 |
| `SoilAdc_Read` | `BspAdc_ReadRaw`；业务调用方使用 `SensorSoil_Read` |
| `SoilAdc_StatusName / HalStatusName` | `BspAdc_StatusName / HalStatusName` |
| `SoilAdc_PinMillivolts` | `BspAdc_PinMillivolts` |
| `SoilAdc_SensorMillivolts` | `SensorSoil_SensorMillivolts` |
| `SoilAdc_MoisturePercentX100` | `SensorSoil_MoisturePercentX100` |
| `Usart2Rs485_Init(UART_HandleTypeDef *)` | `BspConsole_Init(void)`，内部固定 USART2 |
| `Usart2Rs485_Process / Send / StatusName` | `BspConsole_Process / Send / StatusName` |
| `Rs485Direction_Init / Transmit / Receive / Name` | `BspRs485Dir_Init / Transmit / Receive / Name` |
| `SoilApp_Diagnostics` | `AppDiagnostics` |
| `SoilAdc_Result / SoilAdc_Status` | `BspAdcResult / BspAdcStatus` |
| 原结果内 `HAL_StatusTypeDef` | `BspAdcHalStatus`；HAL 的四种结果含义逐项映射，日志文字不变 |
| `Usart2Rs485_Status` | `BspConsoleStatus` |
| `SOIL_ADC_{OK,CAL_FAILED,START_FAILED,POLL_FAILED,STOP_FAILED,OUT_OF_RANGE}` | 对应 `BSP_ADC_` 同后缀成员 |
| `USART2_RS485_{OK,BAD_LENGTH,UART_ERROR,UART_BUSY,UART_TIMEOUT,TC_TIMEOUT,DRAIN_PENDING}` | 对应 `BSP_CONSOLE_` 同后缀成员 |

新增 `SensorSoilData / SensorSoilStatus / SensorSoilReadMode` 表达土壤业务、量程与有效性；`AppLogResult` 只返回本次发送结果和格式化是否失败。没有引入万能状态对象或新传感器框架。

## 配置宏映射

下表每个旧宏均迁移到对应新名称；没有保留旧名别名。原来支持 `#ifndef` 的参数继续支持编译器覆盖。量程、型号、模式枚举及 ADC 满量程仍为原来的固定常量。

| 旧名称 | 新名称 | 归属 |
| --- | --- | --- |
| `SOIL_APP_MODE` | `APP_MODE` | app_config.h |
| `SOIL_MODE_UART_TEST` | `APP_MODE_UART_TEST` | app_config.h |
| `SOIL_MODE_ADC_RAW` | `APP_MODE_ADC_RAW` | app_config.h |
| `SOIL_MODE_ADC_VOLTAGE` | `APP_MODE_ADC_VOLTAGE` | app_config.h |
| `SOIL_APP_PERIOD_MS` | `APP_PERIOD_MS` | app_config.h |
| `SOIL_ADC_POLL_TIMEOUT_MS` | `BSP_ADC_POLL_TIMEOUT_MS` | bsp_config.h |
| `SOIL_ADC_VREF_MV` | `BSP_ADC_VREF_MV` | bsp_config.h |
| `SOIL_ADC_VREF_IS_MEASURED` | `BSP_ADC_VREF_IS_MEASURED` | bsp_config.h |
| `SOIL_ADC_FULL_SCALE` | `BSP_ADC_FULL_SCALE` | bsp_config.h |
| `SOIL_UART_TX_TIMEOUT_MS` | `BSP_CONSOLE_TX_TIMEOUT_MS` | bsp_config.h |
| `SOIL_UART_TC_TIMEOUT_MS` | `BSP_CONSOLE_TC_TIMEOUT_MS` | bsp_config.h |
| `SOIL_RS485_AUTO` | `BSP_RS485_AUTO` | bsp_config.h |
| `SOIL_RS485_MANUAL` | `BSP_RS485_MANUAL` | bsp_config.h |
| `SOIL_RS485_DIRECTION_MODE` | `BSP_RS485_DIRECTION_MODE` | bsp_config.h |
| `SOIL_RS485_AUTO_ASSEMBLY_CONFIRMED` | `BSP_RS485_AUTO_ASSEMBLY_CONFIRMED` | bsp_config.h |
| `SOIL_RS485_MANUAL_WIRING_CONFIRMED` | `BSP_RS485_MANUAL_WIRING_CONFIRMED` | bsp_config.h |
| `SOIL_RS485_SETUP_MS` | `BSP_RS485_SETUP_MS` | bsp_config.h |
| `SOIL_RS485_HOLD_MS` | `BSP_RS485_HOLD_MS` | bsp_config.h |
| `SOIL_RS485_GPIO_PORT` | `BSP_RS485_GPIO_PORT` | 手动模式板级编译定义 |
| `SOIL_RS485_GPIO_PIN` | `BSP_RS485_GPIO_PIN` | 手动模式板级编译定义 |
| `SOIL_RS485_GPIO_CLOCK_ENABLE` | `BSP_RS485_GPIO_CLOCK_ENABLE` | 手动模式板级编译定义，仅 Bsp .c 执行 |
| `SOIL_RS485_TX_LEVEL` | `BSP_RS485_TX_LEVEL` | 手动模式板级编译定义 |
| `SOIL_RS485_RX_LEVEL` | `BSP_RS485_RX_LEVEL` | 手动模式板级编译定义 |
| `SOIL_SENSOR_CHANNEL_NAME` | `SENSOR_SOIL_CHANNEL_NAME` | sensor_soil.h |
| `SOIL_SENSOR_MODEL` | `SENSOR_SOIL_MODEL` | sensor_soil.h |
| `SOIL_SENSOR_OUTPUT_MIN_MV` | `SENSOR_SOIL_OUTPUT_MIN_MV` | sensor_soil.h |
| `SOIL_SENSOR_OUTPUT_MAX_MV` | `SENSOR_SOIL_OUTPUT_MAX_MV` | sensor_soil.h |
| `SOIL_SENSOR_GAIN_CONFIRMED` | `SENSOR_SOIL_GAIN_CONFIRMED` | sensor_soil.h |
| `SOIL_SENSOR_GAIN_NUM` | `SENSOR_SOIL_GAIN_NUM` | sensor_soil.h |
| `SOIL_SENSOR_GAIN_DEN` | `SENSOR_SOIL_GAIN_DEN` | sensor_soil.h |

原日志缓冲区 512 字节改为 App 常量 `APP_LOG_BUFFER_SIZE=512`。测试工具环境变量 `SOIL_TEST_GCC` → `APP_TEST_GCC`，`SOIL_ARM_BIN` → `BSP_ARM_BIN`；ARM 脚本增加可选 `APP_MDK_UV4`。环境变量不是板卡配置。

默认值仍为模式 2、1000 ms、ADC poll 20 ms、UART TX 100 ms、TC 5 ms、VREF 3300 mV 且未实测、增益 1/1 且未确认、方向 AUTO 且装配未确认。没有为了输出数值改变确认状态或标定参数。

## 本次实际验证

| 类别 | 结果与证据 |
| --- | --- |
| 重构前基线 | 原有 7 组 host 配置及未确认手动模式拒绝编译检查通过 |
| 重构后 host | 同样 7 组全部通过，使用真实三层业务源文件及 fake HAL，GCC `-Wall -Wextra -Werror`；[日志](validation/refactor_host_tests.log) |
| 保留的行为断言 | 三模式、tick 回绕、不补发周期、有效零值、16 位范围、校准/启动/轮询/清理失败、EST/NA、换算端点、日志格式错误、TX 统计、TC 延后释放及手动 GPIO；另验证拆分没有引入中间舍入 |
| 模拟串口协议比对 | 与修改前快照对照，7 组共 130 次模拟 UART 输出逐字节一致；[日志](validation/refactor_protocol_comparison.log)；不代表实际串口测量 |
| MDK 正式目标 | ARM Compiler 5.06 update 7 build 960，完整 Rebuild，0 errors / 0 warnings；所有 Core/HAL/启动对象本次重新编译；[日志](validation/refactor_mdk_rebuild.log) |
| ARM 模式与参数 | 模式 0、1、2，已确认换算分支、已实测参考标记、2500 mV/2 倍增益端点配置，共 6 组编译链接通过；[日志](validation/refactor_arm_modes.log) |
| 静态分层与引用 | 平坦目录、依赖方向、HAL 隔离、无循环、无生产可写 extern；MDK 路径存在、无重复/旧源引用、旧业务符号无活动引用；[日志](validation/refactor_architecture.log) |
| 基线保护检查 | Core 非 USER CODE 保持；.ioc 仅改删除选项；MDK 编译链接配置保持；[审计](validation/refactor_source_audit.log) |

运行命令（工程根目录）：

```powershell
py -3 Tests/run_host_tests.py
py -3 Tests/check_arm_modes.py
py -3 Tests/check_architecture.py
```

ARM 脚本先完整重编译默认 MDK 目标，生成本次 `MDK-ARM/tea_sense/tea_sense.hex`；六组模式变体只写临时目录并在结束后清除，不替换默认 HEX。历史日志与镜像只保留为资料，没有作为本次验证证据。

核对并保持 AD0/PB1/ADC1 Channel 5、AD2/PA6/Channel 3、AD3/PA5/Channel 19、AD4/PA4/Channel 18；USART2 PD5/PD6 115200 8N1；USART3 PD8/PD9 与 UART4 PC10/PC11 4800 8N1；雨量 PE15 下降沿、无上下拉及现有 EXTI15_10。ADC1 保持 16 位、异步分频 8、单 Rank、软件触发、387.5 cycles，无 ADC 中断/DMA。时钟树未改。

未连接板卡、未烧录、未进行上板验证；VREF、分压、输入负载和 RS485 装配状态仍待实测。本轮未开发 AD2/AD3/AD4 采样、风速/风向/pH 换算、雨量处理、Modbus、标定/滤波/持久化、RTOS、DMA 或异步框架。完成本轮框架重构后停止。
