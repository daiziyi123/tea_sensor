# USART6 / 4G DTU 固定文本回显验证

> 历史记录：以下描述固定文本回显接入时的验证，不代表当前运行流程。当前代码已移除DTU_TEST和MATCH判断，改为真实土壤JSON上传及服务器ACK，见[本轮JSON上传](json_upload_validation.md)。

更新：2026-11-13（北京时间）。本轮只接入 H743 经 A6/B6 的串口透传回显；没有发送土壤记录、AT 配置命令，也没有接入 MQTT/HTTP。

## 生成配置核对

接入前核对最新 .ioc、Core/Src/usart.c、stm32h7xx_it.c 和 main.c：

- .ioc 已有 NVIC.USART6_IRQn=true；MspInit 已执行 HAL_NVIC_EnableIRQ(USART6_IRQn)。
- USART6_IRQHandler 已调用 HAL_UART_IRQHandler(&huart6)。
- USART6 使用 PC6 TX / PC7 RX（AF7），115200、8N1；main 在 App_Init 前执行 MX_USART6_UART_Init。
- USART6 没有配置 DMA。HAL UART 注册回调关闭，原工程没有用户 UART RxCplt/Error 回调。

配置已经由当前生成代码具备。本轮未修改 Core、.ioc、HAL/CMSIS 或时钟，未执行 CubeMX 重新生成，未手改生成代码。

## 修改文件

| 文件 | 内容 |
| --- | --- |
| Bsp/Inc/bsp_dtu.h、Bsp/Src/bsp_dtu.c | 固定 huart6，Init / Send / Read，单字节 IT 环形接收、回调、恢复及诊断；公开类型无 HAL |
| Bsp/Inc/bsp_config.h | DTU 发送超时 100 ms 及校验 |
| App/Inc/app_config.h | DTU 发送周期 5000 ms、回显超时 10000 ms；土壤周期仍为 1000 ms |
| App/Src/app_main.c | 初始化接收，每循环取数/拼行、单请求回显/超时、直接 printf |
| MDK-ARM/tea_sense.uvprojx、uvoptx | Bsp 组增加 bsp_dtu.c，同步 IDE 编号，保留目标和调试配置 |
| Tests/stubs/stm32h7xx_hal.h、usart.h、stdio_fault.h | 新增 huart6/IT/错误/PRIMASK 桩声明及仅测试使用的 snprintf 故障拦截 |
| Tests/dtu_host_test.c、soil_host_test.c、run_host_tests.py | 新增 DTU 行为验证，保留土壤回归；四组配置运行两套程序 |
| Tests/check_arm_printf.py、check_architecture.py | ARM C 库执行回显路径、USART6 IRQ/配置与分层检查 |
| Docs/code_architecture.md、本文、validation/dtu_*.log | 当前结构和本次证据 |

回调位于 bsp_dtu.c 的 HAL_UART_RxCpltCallback 和 HAL_UART_ErrorCallback，只处理 &huart6。其他 UART 分支直接返回，与此前无用户回调的行为一致；其他外设代码保持。

## 缓冲、恢复与调度

BspDtu_Init 立即启动 HAL_UART_Receive_IT 单字节接收，早于第一次发送。不重新初始化外设，不调用 BspRs485Dir 或方向 GPIO，不操作 PB7/PD3/PD4。

接收数组固定 256 字节，保留一个空槽，可排队 255 字节；满时丢弃新字节并累计 rx_overflows，绝不覆盖未读内容。主循环与 ISR 的共享索引/状态使用 volatile，主循环通过保存/恢复 PRIMASK 的短临界区取数及读取诊断，每次最多复制 32 字节。阻塞 TX 不在临界区内执行。

完成回调只入队和重启接收，不打印、延时或解析。错误回调保留原 UART 错误码，通过非 DMA HAL_UART_AbortReceive 清理失效 RX 并重启；接收启动/重启/终止失败保留状态。每次主循环 Read 都会重试尚未恢复的接收，不会因一次错误永久停收。

BspDtu_GetDiagnostics 返回一致快照：RX 字节数、溢出、错误、启动失败、最后 UART 错误码、RX/abort/TX 状态和 rx_active。发送使用有 100 ms 上限的 HAL_UART_Transmit；当前 H7 HAL 的成功返回已等待 TC。RX 中断在发送期间可运行。

出现溢出、UART 错误或重启间隙后，下次 Read 清空排队数据并报告 data_lost；App 清除半行，丢弃到下一个 LF 后才接受新行。这一保守策略可能牺牲恢复后的第一行，但不会拼接丢字节前后的碎片误报成功。App 接收行上限 64 字节，超长行同样同步到 LF。

App_Process 每次先执行 DTU 处理，再进入土壤“未到 1000 ms 就 return”的判断。每轮最多取 8 批、每批 32 字节，连续噪声不会使土壤任务永久饥饿。

空闲时每 5 秒尝试发送一条 DTU_TEST,seq=N,t_ms=T，实际末尾 CRLF；静态发送数组为 64 字节，检查 snprintf 失败与截断。只在 USART6 发送成功后保留一个待验证请求，从发送完成后开始等待 10 秒，不阻塞等待。

按 LF 组行，支持分段和一批多行；长度、全部字节及 CRLF 与本次请求一致且未超时才 MATCH。不匹配、重复及过期行忽略。超时后重新等待 5 秒再尝试，不补发积压请求。周期、超时、RTT 使用 uint32_t 时间差处理回绕；RTT 是发送完成到主循环处理完整回显的时间，包含处理延迟。

## 上板后 USART2 预期输出

```text
DTU_TX,seq=1,status=OK
DTU_RX,seq=1,status=MATCH,rtt_ms=182
DTU_TX,seq=2,status=OK
DTU_TIMEOUT,seq=2
DTU_TX,seq=3,status=UART_TIMEOUT
```

TX 的 OK 仅表示 USART6 发送完成，不表示服务器收到。只有匹配完整回显才输出 MATCH。UART_ERROR/BUSY/TIMEOUT 表示发送失败；RX_NOT_READY 表示接收尚未启动，此时没有实际发送。

DTU_INIT,status=... 表示初始化接收失败；DTU_RX_DROP,reason=STREAM_GAP 或 LINE_TOO_LONG 表示丢数据/超长行后重新同步，不是 MATCH。所有业务打印仍在 App，printf 仍走 USART2 115200、8N1，中断中不打印。原土壤 raw、百分比与 LOW/HIGH 继续输出，但不经 DTU 上传。

以上是预期示例，不是本轮联网记录。用户此前用电脑发送 DTU_TEST_001 收到厂家服务器回显是已知前提；MCU 不改变 DTU 网络参数。

## 实际验证与限制

- [Host](validation/dtu_host_tests.log)：4 组配置 × 土壤/DTU 两套程序，共 8 个程序通过。覆盖分段、多行、非匹配、严格 CRLF、重复、超时边界/回绕、单请求、等待时土壤采样、超时后无补发、环形顺序/回绕、溢出/超长行同步、UART 接收重启/终止失败恢复、TX 错误、snprintf 失败/截断、其他 UART 隔离与发送期间即时回显。
- [MDK 完整构建](validation/dtu_arm_build.log)：当前源码 Rebuild，ARMCC 5.06 update 7，0 errors / 0 warnings，本次生成 HEX/AXF；无重复回调、无半主机依赖、无浮点 printf。
- [ARM CPU 执行](validation/dtu_arm_execution.log)：真实 Arm C 库与生产 App/Bsp/Sensors，模拟 HAL，跑通接收先启动、USART6 发送、生产回调入队、主循环 MATCH，同时保留土壤输出。测试路径未调用 malloc。这不是硬件 IRQ 或服务器验证。
- [静态检查](validation/dtu_architecture.log)及[源码保护审计/哈希](validation/dtu_source_audit.log)：最新 Core、.ioc、HAL/CMSIS、Sensors、已有 Bsp 实现及原土壤处理保持；端点仍为 436/31146，USART2 重定向不变。

验证命令：py -3 Tests/run_host_tests.py、py -3 Tests/check_arm_build.py、py -3 Tests/check_architecture.py。可选 CPU 模拟：设置 APP_ARM_EMULATOR_PATH 指向 Unicorn 包目录后运行 py -3 Tests/check_arm_printf.py。

本轮未连接板卡、烧录、执行 H743→DTU→厂家服务器的上板回显或提交 Git。DTU 装配、自动方向、实际无线网络、服务端原样回显和现场 RTT 仍待上板确认。完成本轮后停止，没有继续开发真实传感器上传。
