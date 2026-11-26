> 当前雨强增量（2026-11-15）：软件/CPU/临时库结果见[rain_integration_validation.md](rain_integration_validation.md)、rain_test_results.json；旧release_*和AD5 raw阶段证据保留为历史，不作为当前构建结果。

# 软件验证与待执行现场验收
交付初稿更新：2026-11-15，版本管理基线见tea_sense/App/Inc/app_version.h。
证据分类：[代码核对]、[历史用户实测]、[资料记录]、[待确认]、[待执行]；软件测试通过不等于实物准确或现场签收。

## 本次雨强软件结果与边界
本次真实执行以rain_test_results.json、rain_*.log及受控源码包清单为准。五组host配置×两程序、2000ms同步变体、雨强精确分数/全raw域/宽整数与边界、24/23门槛、错误/有效0/窗口回绕及原业务/恢复回归通过。架构与ARM全量重建0error/0warning；CPU执行182轮、910次5→3→19→18→15显式选通、1820 Stop、一次校准、六次十一项上传/ACK；应用CPU调用栈观察640/1024字节，**不是实际ISR栈高水位**。
C端到端24包/152独立测量，含雨强24点，临时TCP/SQLite/鉴权API/CSV/WS和真实C ACK通过。服务器36项unittest、Node语法/行为通过。浏览器1920/1280/390与全屏，4.00/有效0/失败/旧缺失/陈旧，mm/min曲线、历史CSV与重连恢复均检查；模拟数据不作降雨实测。
软件测试不包含真实串口/4G/雨强精度/持续运行。本次未覆盖真实数据库、未烧录、未重启生产实例。下表现场状态全部保持待执行，并追加雨强中点线性、定量喷淋与标准雨强对照、先服务器后新固件的实际升级验收。

## 前一份稳定化完成核对
| 要求 | 本机实际实施与证据 |
|---|---|
| 较旧now/较新TC误超时 | AppUpload_Process消费状态后重新获取时间；修复前两个独立用例退出3，见stabilization_repro_1/2.log；修复后跨1ms、回绕、超时前1ms/恰到、早ACK通过host |
| 启动中错误被HAL_OK覆盖 | Bsp独立终态/原因锁存，短临界区，不在HAL发送/ACK期间关中断；返回前ErrorCallback注入、TC/错误前后、单次消费、迟TC/新传输保护通过host |
| TX锁定恢复 | 三次清理失败保持缓冲只读，每60000ms局部AbortTransmit探测；失败不释放、不热重试，成功给下帧CRLF，RX/ADC继续，累计诊断保持；永久失败/回绕/恢复通过host |
| 初始校准失败恢复 | CAL_FAILED保留原HAL，60000ms停止确认后重试；停止失败禁校准，成功不重复校准；失败/间隔/恢复/回绕通过host |
| 三层职责 | app_main采集/窗口/私有快照/printf；AppUpload唯一TX/ACK应用状态；Bsp硬件缓冲与故障恢复；bsp_uart_irq唯一固定回调分发，MDK只引用一次 |
| 保持行为 | 四路100ms/3秒/24门槛、端点、圆周算法、气象周期/单字段质量、零/失败/精度、printf恢复、ACK边界及RX故障原覆盖保留 |
| 运维与资料 | 新增独立备份工具，仅临时库测试；六份交付初稿、旧雨量规划醒目标历史，真实数据库未恢复/覆盖 |

## 四项增量完成核对
| 项目 | 实际变更与边界 |
|---|---|
| 公共类型 | AppUploadValue→AppSensorValue、AppUploadSnapshot→AppSensorSnapshot，单一App/Inc/app_data.h，只有stdint依赖，无app_data.c、valid_mask、全局可写帧或Sensors反向依赖 |
| coreJSON | 原字节迁入Middlewares/Third_Party/coreJSON，独立MDK ThirdParty/coreJSON组和include路径；无App副本，源码/头/许可迁移哈希见corejson_relocation_audit.json；不声称上游完全未修改 |
| 单一版本 | app_version.h定义三种独立版本；V2数字schema来源相同，无新增JSON字段；USART2每App_Init一次UTF-8摘要，失败不重印且继续采样，host/ARM执行验证 |
| 发布 | Tools/package_release.py固定规则、新输出拒绝覆盖、链接拒绝、预览/清单/校验/解包；6项fixture测试及实际包解包host/静态见最终记录；源码预览与私下固件产物分开 |

## 前次稳定化软件结果（历史，原证据保留）
最终执行命令、工作目录、实际退出码和执行时间存于release_test_results.json；最终汇总只能以该文件与同名release_*.log为准，不用历史HEX或旧日志冒充。下列软件测试在本机执行，均不是实板：
- Host：5组配置×soil/dtu=10程序，另2000ms窗口/20样本元数据同步变体；原拒绝/缺密钥检查保留。
- 架构/MDK：三层无循环依赖，HAL在Bsp，回调唯一、无重复源；全量ARMCC编译链接0error/0warning，无浮点printf/半主机。
- ARM CPU：测试main和fixture凭据、真实业务对象/标准库；182轮/728选通/1456 Stop/一次校准、6完整V2上传/ACK，启动版本一次、中文CRLF/printf恢复；实测应用调用栈624/1024字节，不含真实ISR抢占，不是板上栈高水位。
- C端到端：720个100ms模拟样本/路、72虚拟秒、24包/128独立点，分片TCP/临时SQLite/鉴权API/CSV/真实C ACK。仅demo ID/fixture token，不读生产.env/DB。
- 服务器：31项unittest（原27项保留，新增4个临时WAL备份用例）；Node语法和逻辑通过；本轮未重构网页或重复浏览器布局验收。
- 发布：临时fixture6项覆盖必要源/静态资源、私密/DB/Backup/.venv排除、已有输出/源码内递归、哈希/缺文件/ZIP穿越、Windows目录联接拒绝；实际源码包解包后执行host/架构，结果另记。预览包不含真实配置，未执行真实上板。

## 早期历史资料独立列示
[历史用户实测/资料记录] 土壤raw19122→约60.85%及A/B调整后串口→服务器→网页/CSV反馈来自提供初稿和现有工程记录；不等同本次稳定化十项签收。前次1920/1280/手机截图和软件日志保留，未改原时间/结果；本轮无UI改动，仅Node回归。
标定端点记录见device_calibration_register.md；缺失原始统计日志、仪表信息的附件摘要明确待核实。显示小数位、ADC成功/ACK成功均不证明传感器准确。

## 现场验收记录：保持待执行
| 编号 | 场景与需采集证据 | 本轮现场结果 |
|---|---|---|
| A01 | 同窗五路串口/有效数/物理值与ACK/API/CSV对齐 | 待执行 |
| A02 | 实际气象10/10/30/60秒、质量/年龄、缓存去重 | 待执行 |
| A03 | 有效0、探头/线路故障、失败null与越界range | 待执行 |
| A04 | 实际ISR跨tick/启动错误/迟TC；不误超时 | 待执行（host已通过） |
| A05 | TX清理失败锁定、60秒恢复、RX/采样不中断 | 待执行（host已通过） |
| A06 | 初始ADC自校准失败、安全停止与低频恢复 | 待执行（host已通过） |
| A07 | 短时断网→恢复，继续新窗；不承诺补历史 | 待执行 |
| A08 | MCU重启，seq/tick复位与运行段边界，不造假boot_id | 待执行 |
| A09 | 服务器重启/WS重连/DTU恢复和历史保留 | 待执行 |
| A10 | 生产备份恢复演练，停写/sidecar/兼容/历史验收 | 待执行（临时库演练已通过） |
| A11 | 24～72h真实持续运行、窗口/ACK/丢失/栈和缓冲峰值 | 待执行，未自动启动长时任务 |
| A12 | 实物板卡/下载器/私有配置对应本次HEX/AXF与源码签收 | 待执行 |

每项现场记录需真实操作者、设备/探头/仪表编号、软件与配置版本、步骤、起止时间、实际观察、脱敏日志、判定与遗留。以上没有填造签名/温度/掉线数或默认“通过”。

## 待确认硬件
雨强探头型号、真实供电、线束及0～5V版本实物对应；本次用户给定0～5V→0～8mm/min，实际降雨精度待执行；实际带载供电/电流/极性、线束及A/B和参考地/隔离方式、板上分压/VREF；风速参考风场/pH标准液与真实土样精度、风向对北、下载器/板卡修订。未启用看门狗/硬件复位/电源控制；需要后续实板方案，不以软件单测代签。

## 旧入口到当前职责/API映射
| 原实现 | 当前唯一入口/数据所有者 |
|---|---|
| app_main中的App_DtuConsumeByte/Read/Process | app_upload内部拼行/接收、AppUpload_Process；ACK/TX/节流诊断改读AppUpload_GetDiagnostics |
| app_main中的App_UploadWindow通信状态 | app_main只准备私有快照；AppUpload_Submit同步编码，主循环保留一窗rate候选，不保存外部指针 |
| AppUploadValue / AppUploadSnapshot | app_data.h中的AppSensorValue / AppSensorSnapshot，无旧类型兼容别名 |
| App_GetDiagnostics中的dtu_*、upload_skipped_* | AppUploadDiagnostics中的ack_*/last_sequence/timeouts/tx_errors等及skipped_*；采样诊断不复制通信状态 |
| bsp_dtu中的三类HAL回调 | bsp_uart_irq.c固定分发，BspDtu_*Isr与BspWeatherRs485_*Isr私有拥有各自状态 |
| App内core_json.c/h | Middlewares/Third_Party/coreJSON，独立工程源/包含路径，源文件只编译一次 |

发布文件级实际增改/迁移列表见release_record.json；source_manifest.sha256只列受控公开源码/文档，不包含私有配置内容或哈希。工作区原始备份仍保留。日志原始执行时间由运行环境记录，交付文档日期按客户端北京时间；未将软件时间戳填入现场测试表。
