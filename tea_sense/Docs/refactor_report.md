# 单一采集流程与直接 printf 交付

本文保留此前简化采集流程的交付记录。当前保持 1000 ms 周期，已根据用户样本统计接入两点相对湿润度；最新参数、输出和验证见 [两点换算记录](soil_two_point_calibration.md)。

日期：2026-11-12（北京时间）。在已经完成 App / Sensors / Bsp 分层的 tea_sense 上继续简化，没有调整分层或模块命名体系。

## 本轮变更

- 删除三套调试运行选择、选择宏和枚举、选择分支与条件编译、UART 测试文本、独立测试运行分支，以及输出中的运行选择字段。
- 删除 App/Inc/app_log.h、App/Src/app_log.c，移除 MDK/IDE、include 和测试引用，清除该模块的失效构建产物。
- 删除 Tests/check_arm_modes.py；使用 Tests/check_arm_build.py 完整构建唯一正式目标，不再生成调试运行变体。
- SensorSoil_Read(void) 每次只返回本次 AD0 原始采样和 BspAdcResult 状态。删除读取选择参数及不再使用的电压结果结构成员。已有引脚电压、传感器电压及水分纯换算函数内容保持不变，也不进入当前运行链路。
- App_Process 每 1000 ms 采集一次，然后直接使用成功/失败两个 printf 分支输出。没有业务打印包装、额外格式化函数或日志框架，没有为了打印再次采样。

当前数据格式（示例不是实测）：

```text
t_ms=1000,channel=AD0,adc_raw=12345,status=OK
t_ms=2000,channel=AD0,adc_raw=0,status=OK
t_ms=3000,channel=AD0,adc_raw=NA,status=ADC_POLL_FAILED
```

每行实际以 CRLF 结束。有效 0 不变为 NA，失败不复用旧值。采集继续保留校准、启动、轮询、停止和 16 位范围错误；BspAdcResult 同时保留主故障和停止清理故障。

## printf 重定向

检查发现此前没有 printf 重定向。现有 MDK 配置为 ARMCC 5.06 update 7 build 960，标准 Arm C 库（useUlib=0），因此选用 rt_sys.h 规定的 _sys_write，位置在 Bsp/Src/bsp_console.c。没有并行添加 fputc 或 POSIX _write。

```text
App printf -> 真实 Arm C 库整数格式化 -> _sys_write
  -> BspConsole_Send -> 原有限超时发送/TC 检查/方向控制 -> USART2
```

同一文件提供该 C 库要求的标准流名称和必要系统钩子；未支持的文件或输入操作返回失败。启用 __use_no_semihosting_swi，让链接器拒绝半主机依赖。沿用真正的 C 库 FILE，BspConsole_Init 设置 stdout 为 _IONBF，不添加业务输出缓冲或动态分配调用。

BspConsole_GetDiagnostics 保存发送调用次数、失败数、最后状态和 stdout 无缓冲设置结果。计数单位为底层发送调用，不是行数。App_GetDiagnostics 保存序号和 printf 失败次数，失败后 clearerr(stdout) 允许下一周期重试，不递归 printf 报错。stdout 适用于 App_Init 之后的主循环；中断回调没有打印。

## 实际验证

| 类别 | 结果 | 本次证据 |
| --- | --- | --- |
| Host 行为测试 | AUTO、短硬件超时覆盖、虚拟 GPIO 手动配置三组全部通过；未确认手动方向仍被编译拒绝 | [host 日志](validation/printf_host_tests.log) |
| Host 覆盖范围 | tick 回绕与无补发、每周期一次采样、有效 0/65535、异常 65536、ADC 各阶段故障及双故障保留、printf 字节输出、发送统计与重试、长度边界、HAL busy/error/timeout、TC 延后释放 | 同上 |
| ARM 正式构建 | 从当前源码完整 Rebuild，0 errors / 0 warnings；生成本次 HEX/AXF，不烧录 | [MDK 日志](validation/printf_mdk_rebuild.log) |
| C 库链接检查 | 标准库选项未改；输出钩子归属 bsp_console.o；无重复定义、无 sys_io.o、无 SVC/SWI/BKPT、无浮点 printf 支持对象 | Tests/check_arm_build.py 本次通过 |
| ARM CPU 执行验证 | 真正 Arm libc printf、生产 App/Sensors/Bsp 和模拟 HAL 执行通过；验证即时 CRLF、零值、NA、printf 失败计数、下一周期恢复及独立直接 printf；测试路径 malloc 调用为 0 | [执行日志](validation/printf_arm_execution.log) |
| 静态架构 | 分层、路径、无循环、无重复源、无已删除运行选择及业务日志模块的活动代码引用 | [静态检查](validation/printf_architecture.log) |
| 保护审计 | 全部 Core（含 main）与 .ioc 字节未改；1446 个 Drivers 文件 SHA256 未改；ADC/方向/时基/配置校验实现未改；旧换算函数及 UART 超时/TC 处理代码未改；MDK 编译器/C 库/链接/调试选项未改 | [源码审计与镜像哈希](validation/printf_source_audit.log) |

Host 的 Tests/host_stdio.c 用原生 vsnprintf 作为测试用 printf 前端，再进入生产 _sys_write/BspConsole，未模拟已删除的业务日志模块。真实 Arm 标准库调用路径另由 ARM CPU 执行测试验证；不能将 host 前端视为 Arm C 库本身。

ARM CPU 测试使用 Unicorn 2.1.4，依赖本次安装在系统临时目录 tea_sense_arm_emulator；通过 APP_ARM_EMULATOR_PATH 指定此目录。该测试仅替换临时镜像的 main，HAL 操作由 CPU 测试端模拟，生产实现与真实 C 库没有替换。临时镜像在结束后删除，不替换正式 HEX。

常规命令（工程根目录）：

```powershell
py -3 Tests/run_host_tests.py
py -3 Tests/check_arm_build.py
py -3 Tests/check_architecture.py
# 可选：已安装 Python unicorn，或设置 APP_ARM_EMULATOR_PATH
py -3 Tests/check_arm_printf.py
```

BSP_ARM_BIN / APP_MDK_UV4 可指定既有 MDK 工具路径；APP_TEST_GCC 可指定 native GCC。未增加输出选择开关。

## 保持与未验证项

main 中 App_Init / App_Process 的接入不变。AD0/PB1/ADC1 Channel 5、ADC 16 位/异步分频 8/单 Rank/软件触发/387.5 cycles、全部时钟、USART2 PD5/PD6 115200 8N1、其他串口、PE15 下降沿及无上下拉均保留；RS485 AUTO/MANUAL 是既有硬件配置并完整保留。未修改 HAL/CMSIS，未新增其他传感器、雨量、Modbus、RTOS、DMA 或复杂状态机。

未连接板卡、未烧录、未进行上板验证。CPU 模拟不能证明真实 USART2 波形、方向装配或 ADC 接线正确；VREF、分压、输入负载与装配仍待现场核验。未执行 Git 提交。

修改前快照：C:/Users/15995/AppData/Local/Temp/tea_sense_before_printf_20261112_211558。此前操作说明和验证日志已归档至 Docs/history/2026-11-12-before-printf，现有历史资料保留；这些历史内容不作为本次验证证据。本轮完成后停止。
