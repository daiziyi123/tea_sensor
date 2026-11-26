# tea_sense 当前架构
2026-11-23，0.10.0-dev / V2 / baseline-3。当前策略及验证见[三分钟交付](../../Docs/three_minute_statistics.md)。

| 模块 | 当前职责 |
|---|---|
| App/app_main | 初始化后计时；100ms五路真实ADC轮次及硬件故障诊断；气象按字段最新有效缓存；封存同一快照后直接交上传 |
| App/app_statistics（唯一新增模块） | 各5槽土壤/pH中位数；180s风速sum/count/max、方向C/S、雨强按真实相邻时间加权；覆盖、身份/槽去重、回绕和迟到跳窗 |
| App/app_upload | 唯一180s发送尝试、snprintf、TX/TC/ACK、早ACK/分片/超时/跳窗；不采样、不保留快照指针、不补传 |
| App/app_weather | 原10/30/60s查询、事务、退避，寄存器/CRC/超时不变 |
| App/app_data.h | 唯一中立物理快照及分类元数据，仅依赖stdint，无可写extern |
| Sensors | 现有标定/采集和纯分数转换、气象解析及每字段真实缓存身份；已移除旧窗口/方向数组，不依赖App |
| Bsp | 原ADC/UART硬件、单TX缓冲/IT/故障恢复、唯一UART IRQ分发；无业务统计 |

main→App_Process→真实完成轮次→AppStatistics_Update→只读封存快照→AppUpload_Submit→Bsp唯一6144字节TX缓冲→UART6 IT/TC→10s ACK。下一窗统计不等待网络。长期延迟不突发重放旧窗口，无动态内存/RTOS/DMA/注册框架。
ADC仍按5/3/19/18/15，CubeMX/Core/Drivers、参考电压、标定系数不变。土壤/pH仅对中间原始值的精确分数调用既有Sensors公式，最终才舍入；风雨累计未舍入的物理量。
电脑串口保持关闭：无启动/原始/中文物理值printf，无控制台初始化或轮询；保留本地错误诊断。Bsp旧控制台API未进入App或最终链接输出路径。GPS仍由服务器独立接收，已取消的GPS随ACK回传不实现。
coreJSON仍唯一位于Middlewares/Third_Party/coreJSON，只由app_upload包含，许可及来源见该目录README；本轮未改第三方代码。MDK两份项目文件均新增app_statistics一次。
