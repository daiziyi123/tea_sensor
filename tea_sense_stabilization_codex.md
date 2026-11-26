# tea_sense 定向修复、架构定稿与交付初稿：Codex执行指令

将本文与 `tea_sense_handover_draft.md` 一起放到 `D:\12-Project\1-EC-Project`，交给能够访问当前本地工程的Codex执行。配套初稿已经按聊天记录和tea_sense(3).zip填写主要事实；请读实际代码后校订，不要从空白模板重新猜参数。

本轮不处理云端。任务是修复已发现的固件问题、在现有三层内整理职责、补齐故障恢复，并形成有事实来源的交付资料初稿。请先简要说明修改文件与实施顺序，再直接完成代码、测试和文档，不要只输出建议。

## 1. 工作区、范围和不可变基线

工作根目录：`D:\12-Project\1-EC-Project`。

- 固件：`tea_sense`，当前基线为最新tea_sense(3).zip及本机之后的有效修改。
- 本地服务器：`tea_sense_server`，先阅读实际V2源码和README；不要拿早期单土壤版本覆盖它。
- 新交付文档：共同根目录的`Docs`，单层文件，不在App/Sensors/Bsp内加嵌套目录。

先读AGENTS.md并检查工作区、Git状态与已有修改；不初始化Git、不提交、不烧录。已有文件冲突先核对并保留用户更晚有效修改，不删除CubeMX备份、历史资料、旧日志或不相关项目。

必须保持：

1. App、Sensors、Bsp三层，每层只有Inc/Src；HAL访问在Bsp；无循环依赖和可写extern状态。
2. 当前十项物理值及V2 JSON/CRLF/ACK合同；保留既有鉴权、私有密钥和本地服务器端口。
3. 100ms采样、3000ms窗口、期望30/至少24有效、独立通道质量、去极值后精确换算；风向圆周统计、容量32及对消无定义逻辑。
4. 所有已确认端点与比例。土壤436/31146；风速raw×100=6779/3151863；风向640067/3100043；pH6222/3137026。
5. ADC16位、异步8分频、单次软件触发、一个Rank、初始CH5、387.5Cycles。成功运行时自校准一次，只有初始校准失败恢复例外。
6. USART2/USART6的115200 8N1、USART3/UART4的4800 8N1；四路ADC端子、雨量PE15下降沿和无内部上下拉配置。
7. FW_H7 V1.12.1、当前ARMCC5/MDK配置、标准Arm C库、无半主机、无浮点printf；保持CubeMX生成文件和Drivers不变。
8. 继续直接printf中文名称和物理值，不恢复ADC_SAMPLE/ADC_WINDOW/原始收发帧/完整JSON输出，不新增调试模式或打印包装模块。
9. 继续默认暂停气体和雨量。不实现云部署、网络配置变更、在线标定、持久化补传、boot_id框架、RTOS、DMA或传感器注册机制。
10. 不打印、重建或替换真实INGEST_TOKEN/VIEW_TOKEN，不改变DTU目标、现用cpolar及生产数据库。

## 2. 先修复两处可复现的时序问题

### 2.1 ACK计时：较旧now与较新完成tick配对

现状：App_Process先读now，再调用App_DtuProcess；后者从BspDtu_Process取得completed_ms后，用调用前的now做无符号差。

已复现顺序：

```text
TX已启动且还未完成。
主循环获取now=3100ms。
TC中断在3101ms完成。
App_DtuProcess(3100)读到completed_ms=3101。
(uint32_t)(3100-3101)变成极大值，立即误报10000ms ACK超时。
```

修复要求：

- 发送状态消费后重新获取当前时间，再判断ACK年龄；接口迁移到AppUpload后同样成立。
- 保持UINT32回绕处理，不改成简单的now>deadline或有符号绝对时间比较。
- ACK仍从真正TC完成时刻起算10s，不能从提交/启动发送时刻起算，也不能每循环刷新等待起点。
- 补充同一次循环跨1ms、回绕附近、超时前1ms/恰好超时、早到ACK的回归测试。
- 不通过延长超时、忽略超时或调整时钟掩盖问题。

### 2.2 启动期间的串口错误被HAL_OK覆盖

现状：BspDtu_StartSend设active后调用HAL_UART_Transmit_IT；调用期间ErrorCallback可能写failed/UART_ERROR；随后StartSend把诊断last_tx_status覆盖成HAL返回映射的OK。Process因failed进入清理，却把已覆盖的OK交给应用，错误地进入等待ACK。

已复现观察：rx_errors=1、tx_completed=0、app_tx_errors=0、awaiting_ACK=1。

修复要求：

- 启动被接受、正在发送、TC成功、错误、超时是不同状态。保存本次TX独立终态/失败原因，不能把共享诊断last_tx_status当唯一结果变量。
- 主循环与ISR的状态更新有清楚所有权和短临界区；不能在整个HAL发送或ACK等待期间关中断。
- 启动调用返回HAL_OK不能擦掉其间已锁存的错误。无TC且已失败时，App绝不进入“发送成功等待ACK”。
- 规定TC与错误先后发生时的处理，完成事件只消费一次；abort后迟到回调不能完成下一笔传输。
- 当前RX错误是否终止在途TX采用明确、可验证的保守策略即可，不在本轮擅自放宽。无论策略如何，故障不能被报告为成功。
- 在假HAL的HAL_UART_Transmit_IT返回前注入ErrorCallback，断言：TX错误可见、不进入成功等待、在途缓冲不被提前覆盖、后续恢复仍可发送。

## 3. 补齐受控恢复，不用无限重试或系统性重置

### 3.1 USART6发送故障

保留三次清理失败后锁住TX缓冲的保护，但补齐运行期退出锁定的路径。

- 新增明确的故障/恢复状态与必要计数；累计错误不能因恢复而清零，不能反复对同一事件累加完成/失败计数。
- 清理失败后保持缓冲只读，RX和ADC继续工作。默认每60000ms做一次局部恢复探测；该间隔是本轮新增工程策略，放在bsp_config.h并说明，不是假称已有实测结论。
- 恢复只能在Bsp确认TX已安全终止后释放缓冲；AbortTransmit仍失败时继续保持锁定，不能直接调用现有Init清标志后改写仍可能被硬件使用的字节。
- 恢复成功后，下一帧带既有CRLF边界恢复语义，发送新窗口；不回灌旧窗口、不加队列、不清空正常ACK RX。
- 恢复探测按tick到期执行，不HAL_Delay等待60s；每次调用有限工作量，不在主循环热重试。
- 普通ACK超时/断网只执行现有释放等待、继续后续新窗口的策略，不能每次网络超时就复位UART、DTU或整机。

如恢复需要收紧现有BspDtu_Init对active/blocked状态的语义，应明确实现安全前置条件；不要以无条件重初始化代替硬件停止确认。

### 3.2 ADC初始自校准失败

- 校准成功的正常路径仍只校准一次，不在每100ms轮次重复校准。
- 初次失败后保留CAL_FAILED和原HAL原因，四路不生成假有效值。
- 默认每60000ms允许一次有界的硬件自校准恢复尝试，间隔作为本轮新增bsp_config.h配置；只在Bsp确认ADC停止、没有采集在途时执行。
- 不改时钟树、不改原ADC配置，不在恢复里反复HAL_ADC_Init重建CubeMX初始化，不把未确认状态强行置OK。
- 恢复成功后从下一轮真实采样开始提供有效结果，不拿失败前旧数据填窗口；记录失败次数、恢复尝试/成功、最近结果。
- host验证失败→间隔前不重试→到期重试→恢复、持续失败低频重试、tick回绕、停止失败时禁止校准。仿真推进tick，不真的等待60秒。

本轮不启用新的IWDG/WWDG、不新增复位GPIO或DTU电源控制；这些需要实板验证，写入遗留项而不是自动改硬件配置。

## 4. 在现有模块内定稿职责

### 4.1 App层

`app_main.c`保留初始化调度、采样、窗口、物理快照与直接printf。将App_DtuConsumeByte/Read/Process、App_UploadWindow中的通信状态、节流、ACK等待等迁入现有`app_upload.c`，不要复制两份状态。

app_upload的实际职责接口统一为少量清楚动作，可采用：

```c
void AppUpload_Init(void);
void AppUpload_Process(void);
AppUploadSubmitStatus AppUpload_Submit(const AppUploadSnapshot *snapshot);
const AppUploadDiagnostics *AppUpload_GetDiagnostics(void);
```

类型只表达真实使用的提交结果和上传诊断，不创建万能错误码。公开AppUpload_Format/ParseAck继续作为可独立测试的纯格式化/解析入口；它们不能启动ADC、收发UART或读取当前传感器缓存。

AppUpload_Process负责消费Bsp传输结果和RX/ACK并维护一个有界应用状态。Submit只在允许时把传入快照序列化进空闲Bsp缓冲，不复制AppDiagnostics，不保存指向以后会被改写的窗口的悬空/过期引用。

继续使用一个固定TX缓冲：提交后到完成/安全终止前只读。保持busy/late/rate跳过的真实计数和无补发策略，不能因迁移职责悄悄改变三秒节奏。

采样诊断保留App_GetDiagnostics；上传诊断归AppUpload_GetDiagnostics，测试改读真实归属。不保留只为旧测试存在的转发壳或重复可写状态。若确需app_diagnostics.h，让app_main.h只暴露实际应用入口；不创建全工程总头文件。

`app_weather.c`继续气象调度，不依赖AppUpload来启动采样；上传只读已完成气象结果。Sensors不得反向依赖App或公开HAL类型。

### 4.2 Bsp与UART分发

在Bsp/Src中增加小型 `bsp_uart_irq.c` 固定分发文件，把唯一HAL_UART_RxCpltCallback/TxCpltCallback/ErrorCallback从bsp_dtu.c归位。

- 按huart3/huart4/huart6固定if/switch路由，不引入注册表/函数指针表。
- bsp_dtu和bsp_weather_rs485继续私有拥有接收环、发送状态及恢复；只公开确实被分发调用的ISR入口。
- HAL句柄、HAL_UART_GetError等访问只在Bsp；没有公开需求则不为分发文件机械增加头文件。
- Core标准IRQ入口和HAL名称不改，ISR不printf、不拼JSON、不执行长恢复。
- 同步MDK-ARM/tea_sense.uvprojx、需要时uvoptx和测试构建清单，保证每个源文件只编译一次。

### 4.3 配置与格式化

- AppUpload_Format的window_ms不再写死3000，使用与实际窗口一致的配置/快照来源；默认仍为3000。增加修改窗口周期的编译变体验证元数据同步，不能改默认采样策略来过测试。
- app_secrets.h依赖收进app_upload实现，不让公开接口传播私有密钥；公共容量定义与密钥值解耦。保留最大帧和Bsp实际容量的编译/运行保护与snprintf边界检查。
- 保持V2键、单位、status/null/range、sample_seq/采样身份、age与ACK合同。不得趁架构整理新增服务器不能识别的状态/字段；如内部新增恢复状态，用本地诊断表达即可。
- 不改标定系数，不改变有效0/失败区分，不恢复电压/ADC常规打印，不新增AppLog或DebugPrint模块。

## 5. 用配套初稿生成五类实际交付文档

读取 `tea_sense_handover_draft.md`，按本地修复后的固件、当前实际V2服务器和可核对历史资料校订。文档放在共同根目录`Docs`，建议文件：

| 文件 | 内容 |
|---|---|
| README.md | 范围、版本、证据分类、文档索引、已知限制 |
| wiring_power_interfaces.md | 接线、供电、电平、端子/引脚/通道、串口参数、未确认硬件 |
| device_calibration_register.md | 设备/型号/编号、系数、历史端点来源、标定记录与待验证精度 |
| protocol_v2.md | 实际V2模型、单位、状态、null、range、tick/年龄、ACK、刷新与过期规则 |
| build_operations.md | 实际固件构建和手动下载、本地安装/启动/升级、备份/恢复、版本对应 |
| acceptance_records.md | 已有历史证据、本轮真实软件结果、待上板/重启/断网/持续运行记录 |

每个已填写事实标明来源或状态：代码核对、历史用户实测、资料记录、待现场确认、待执行。不要把资料范围写成实测供电，不把程序可运行写成探头准确，不以显示小数位代替测量精度。

必须填写的现有事实：

- 四路端子和通道：AD0/PB1/CH5、AD2/PA6/CH3、AD3/PA5/CH19、AD4/PA4/CH18。
- 电脑USART2 PD5/PD6 115200 8N1；DTU USART6 PC6/PC7 A6/B6 115200 8N1；气象USART3 PD8/PD9 A3/B3 4800 8N1地址01。
- 默认device_id=tea_h743_001；从实际私有配置只核对一致性，不抄真实密钥。多台设备编号需唯一。
- 土壤相对湿润度436/31146；风速、风向、pH的当前端点和公式按配套初稿，必须与代码逐字核对。
- 3秒四路物理结果、气象10/30/60秒独立采样及3倍周期过期；V2十项固定键，失败null、有效0保留、age不包含未知网络滞留。
- 当前无boot_id和离线补传，不能承诺跨重启绝对幂等或断网历史完整。
- 风速标准风场、风向对北、pH标准液及真实土壤精度仍待验证。型号/实物/仪表/供电不清楚的字段明确待核实，不能根据线色或旧程序补猜。

**必须修正旧活动文档的冲突：**

- `tea_sense/Docs/sensor_interface_plan.md`仍写“用户确认两线雨量”“0.2mm/次”和“仅AD0实现”。当前事实是雨量三线红/绿/黄，无铭牌、无线标，接口和分辨率未确认；十项V2已在代码实现。
- 将旧规划明确标为历史并给当前结论，更新目录/索引指向现行交付文档，不让维护者按旧语句接线。保留历史正文/日志的时间和版本，不把旧测试输出改成新结果。
- `bsp_adc.h`中PH raw only等过时注释、纯换算/全均值等旧阶段描述逐项核对修订，不改实际系数。

## 6. 本地构建与服务器运维资料要能照着执行

本轮不换服务器框架、不重构数据库和前端，也不部署云端。读取当前本地V2服务器，仅完成一致性核对、必要的回归及交付文档；不得用旧server压缩包替换本机当前V2。

- 将实际Python/requirements、ARMCC/MDK/固件包、Node及可选Unicorn版本写入记录；工具缺失标未验证，不硬调用本机没有的版本或无关升级。
- 核对start_server.cmd从脚本目录定位服务器，入口为实际项目命令，单进程、网页8000/TCP9000保持；不为了写文档重复启动生产监听。
- 核对DATABASE_PATH实际来源、.env与app_secrets一致性；不把文件内容/真实token输出到文档。
- 优先使用现有备份工具。没有时可在服务器tools增加小型备份/校验脚本：SQLite backup API、一致性备份、拒绝覆盖既有目标、检查quick_check、错误退出；测试使用临时数据库。
- 恢复写成清楚人工步骤：停本项目写入→保留当前库与关联文件→验证备份与schema兼容→恢复→验收。不要自动恢复/覆盖生产数据库，不能只替换.db而留下不匹配WAL/SHM。
- 升级记录源码/固件/协议/系数/数据库schema及回退兼容性；不承诺旧程序能打开任意新库。
- 发布记录包含构建对应的HEX/AXF路径和校验值；有Git则记录版本及脏工作区状态，无Git则生成受控源码清单/哈希，不初始化仓库。私有配置不进入公开清单内容。
- 不复制Windows .venv作为可移植发布物，不删除现有环境、密钥、数据库或历史日志。

## 7. 验证必须对应新代码

先运行当前基线，再加有意义的回归。允许更新职责迁移造成的接口引用，不允许删断言或改变标定值掩盖回归。

至少覆盖：

1. 两处已复现时序：3100/3101跨tick不误超时；HAL启动返回前ErrorCallback不被OK覆盖。
2. TC后10s边界、早ACK、迟ACK、无关ACK、严格字段检查、JSON分片、多行/过长行和RX溢出。
3. TX故障锁定时缓冲仍只读；恢复间隔前不重试，到期局部恢复，永久故障低频探测不抢占主循环；RX和采样继续。
4. ADC初始失败后的安全重试，正常成功仍校准一次；ADC停止失败不校准，不伪造有效值。
5. 100ms采样、3秒窗口、24/23门槛、有效0、各通道独立故障、主/cleanup故障、精确分数均值、风向跨0/360及对消无定义。
6. 字段、单位、默认3秒、气象周期和新鲜度不变；改变窗口配置时window_ms随实际配置变化。
7. 上传/诊断移至AppUpload后无重复状态、无回调重复、无层级逆依赖、无多余缓冲，MDK源引用无重复。
8. 普通串口只打印中文物理值，无模式/原始日志/密钥；printf错误恢复仍工作。
9. 更新后的C报文→临时TCP→临时SQLite→鉴权API/CSV→C ACK集成；只使用demo设备、fixture token和临时库，不读生产.env或生产DB。
10. 本地服务器现有unittest和Node逻辑回归；备份工具的临时库备份与恢复验证。

重点同步 `Tests/dtu_host_test.c`、`dtu_window_cases.h`、`integration_cases.h`、`soil_host_test.c`、`check_architecture.py`、`check_arm_printf.py`、`arm_integration_main.c`、`upload_wire_fixture.c`、`check_server_upload.py`、`run_host_tests.py`。原测试中的“三次清理失败永久锁定”改为“先保护锁定、到恢复周期后受控恢复”的目标预期，保留停止失败不得改写缓冲的断言。

从固件目录执行可用检查：

```bat
py -3 Tests/run_host_tests.py
py -3 Tests/check_architecture.py
py -3 Tests/check_arm_build.py
py -3 Tests/check_arm_printf.py
..\tea_sense_server\.venv\Scripts\python.exe Tests/check_server_upload.py --server-root ..\tea_sense_server
```

从实际服务器目录执行其现有测试，例如：

```bat
.venv\Scripts\python.exe -m unittest discover -s tests -v
node --check web/app.js
node tests/test_dashboard.js
```

静态、host、ARM编译链接、Arm CPU仿真、实板、4G实链路和长时间运行分列。没有MDK或没有硬件就标未验证，不用旧HEX/历史报告代替，不自动烧录或启动24～72h长时任务。

## 8. 验收资料不能编造

把聊天中的历史联通、标定日志摘要作为“历史用户实测/资料记录”；把本轮真正执行的测试写入本轮验证记录，给命令、环境、退出码和脱敏日志路径。

断网、MCU重启、服务器重启、备份恢复和24～72h运行，若没有本轮可核对的实测证据，仅填写操作步骤、预期、待采集字段与“待执行”。禁止虚构起止时间、温度、掉线次数、签名和“全通过”。

保留以下明确未完成项：雨量接口/分辨率、真实供电与线束签收、风场/pH精度及风向对北、实际ISR/栈高水位与长期稳定性。不得以软件测试把这些项目划为完成。

## 9. 最终交付

完成后输出：

- 实际修改文件与新旧职责/API映射、修复原因；明确两处缺陷如何被回归覆盖。
- 三层最终调用关系、单缓冲所有权、上传状态转换、恢复条件/默认间隔、内部诊断位置。
- 当前十项协议与标定行为保持的证据，任何必需的对外变化单列说明；不悄悄新增服务器不识别的字段。
- 根目录Docs中的六份已填内容初稿，并更新固件Docs/code_architecture.md、integration_v2.md和共同integration_notes.md索引。
- 实际执行结果与未验证项；使用者下一步手动编译/下载和上板验收步骤。
- 私有配置单独管理的打包规则；不制作含真实密钥的公共示例包。

仅完成本轮稳定化和资料初稿后停止。云端、雨量、气体、离线补传和真实长期验收留待后续，不自行扩大开发范围。
