# tea_sense 代码架构

更新：2026-11-12（北京时间）。STM32H743VIT6，MDK-ARM，STM32Cube FW_H7 V1.12.1。

## 分层与模块

自编代码只使用以下三层，每层只有 `Inc`、`Src`，不再嵌套目录。

| 层 | 实际模块 | 职责 |
| --- | --- | --- |
| App | `app_main`、`app_log`、`app_config.h` | 初始化、周期调度、应用诊断；现有串口日志格式与输出；运行模式和周期 |
| Sensors | `sensor_soil` | AD0 土壤电压还原、已有水分换算与业务有效性；土壤量程、增益及确认状态 |
| Bsp | `bsp_adc`、`bsp_console`、`bsp_rs485_dir`、`bsp_time`、`bsp_config.h` | ADC1 校准与原始读取、USART2 发送与 TC 处理、已有方向控制、毫秒时基；板级参考值和超时 |

App 可依赖 Sensors、Bsp；Sensors 只向下依赖 Bsp；Bsp 不依赖 App、Sensors。不设置总头文件或跨模块可写全局变量。CubeMX 的 Core、Drivers、MDK-ARM 保留。

HAL 句柄与硬件操作只出现在 Bsp 及原有 CubeMX 生成代码。Bsp 固定绑定 `hadc1` 和 `huart2`，上层初始化不传 HAL 句柄。`BspAdcHalStatus` 保存 OK/ERROR/BUSY/TIMEOUT 的含义，Sensors 公开接口不暴露 HAL 类型。

`bsp_time` 是调度器所需的唯一毫秒时基边界，让 App 无需直接引用 HAL；不提供延时或额外时钟框架。ADC 硬件校准由 App 初始化时直接调用 `BspAdc_Init`，没有为无状态的土壤模块添加转发式 Init。

## 当前调用关系

```text
main（CubeMX 初始化后）
  App_Init
    BspConsole_Init -> BspRs485Dir_Init
    BspAdc_Init（模式 1、2；ADC offset + linearity 自校准）
    BspTime_GetTickMs
  App_Process（主循环）
    BspConsole_Process（延后 TC 到达时释放方向）
    BspTime_GetTickMs -> 无符号 tick 差判断周期
    SensorSoil_Read（模式 1、2）-> BspAdc_ReadRaw
      模式 2：引脚 mV、已确认增益下的传感器 mV、水分
    AppLog_WriteSoil -> BspConsole_Send -> BspRs485Dir_Transmit/Receive
    保存发送结果与错误计数 -> App_GetDiagnostics（只读）
```

默认模式 2、1000 ms；模式 0 不校准或采样 ADC，模式 1 只请求 raw。调度不使用 `HAL_Delay`，回绕时使用 `uint32_t` 时间差，不补发积压周期。ADC、UART 继续使用原有有限超时的阻塞 HAL 调用。

端子 AD0、MCU 引脚 PB1、ADC1 Channel 5 是三个不同标识。当前不切换通道。传感器电压直接从 raw 使用 64 位合并公式还原，最后舍入一次；水分的百分数定点值也直接用于比例显示。

采样与清理错误分别保留；有效零值与失败区分。VREF 未实测继续 EST，增益未确认继续 GAIN_UNCONFIRMED 和 NA。日志的字段名、顺序、单位和状态含义保持，包括 `adc_pin_mV`。AUTO 默认不访问方向 GPIO，手动配置仅作用于 USART2，不能套用于 USART3/UART4。

## 命名与配置

- 文件使用小写 snake_case，与模块名相同。
- 公开函数使用模块前缀加动作，如 `App_Process`、`SensorSoil_Read`、`BspAdc_ReadRaw`；私有函数使用 static 和所属模块前缀。
- 类型使用模块前缀加 PascalCase；宏、枚举使用 `APP_`、`BSP_`、`SENSOR_SOIL_` 加 UPPER_SNAKE_CASE。
- 参数、局部变量和成员使用 snake_case；静态状态使用 `s_`；时间、电压使用 `_ms`、`_mv` 等单位后缀。
- 固定术语为 soil_moisture、wind_speed、wind_direction、soil_ph、rain。HAL、CMSIS、MX、标准中断及回调名称不改。
- `app_config.h` 管运行模式、周期和日志缓冲大小；`bsp_config.h` 管板级参考值、超时和方向常量；土壤常量及增益编译覆盖放在 `sensor_soil.h`，不增加通用传感器配置头。

原有可覆盖参数继续通过新名称 `-D` 覆盖，详见 [迁移映射与验证](refactor_report.md)。配置头不执行硬件访问。

## 后续增加传感器

未来模块仅规划：`sensor_wind_speed`、`sensor_wind_direction`、`sensor_ph`、`sensor_rain`、`sensor_weather`。本轮不创建空文件，不加入调度。

下一阶段每次只实现一个真实模块：在 Sensors 的 Inc/Src 中放业务接口和换算，在 Bsp 内封装对应硬件操作；公开类型不包含 HAL。模块具备真实采样、错误状态及验证后，再由 App 显式调度并更新 MDK 分组及测试。不引入注册表、函数指针表、插件机制或额外层。

CubeMX 当前保留 `BackupPrevious=true`、`KeepUserCode=true`，仅将 `DeletePrevious` 改为 false；没有执行 CubeMX 重新生成。重新生成后仍需检查 MDK 的三层源文件组与 include 路径。

验证入口：`py -3 Tests/run_host_tests.py`、`py -3 Tests/check_arm_modes.py`、`py -3 Tests/check_architecture.py`。ARM 脚本会先完整重编译正式默认目标，再临时编译链接各模式，不烧录。
