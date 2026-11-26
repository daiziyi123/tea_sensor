> 当前业务于2026-11-14改为四路合格窗口与十项V2，见 [integration_v2.md](integration_v2.md)。下文保留为此前阶段记录，一秒/raw输出与旧单土壤协议不再代表当前行为。

# 3秒去极值窗口交付

2026-11-13，北京时间。沿用现有工程、私有密钥、USART6字节收发和严格JSON ACK；未提交Git，未烧录。

## 配置与流程

App/Inc/app_config.h集中定义：

| 配置 | 当前值 | 含义 |
| --- | --- | --- |
| APP_SOIL_SAMPLE_PERIOD_MS | 100ms | 主循环到期时实际采一次AD0，不连续补采 |
| APP_CONSOLE_PERIOD_MS | 1000ms | USART2直接printf最新单次采样，不为打印另采样 |
| APP_SOIL_WINDOW_MS | 3000ms | 固定不重叠窗口；同时是正常上传周期的唯一来源 |
| APP_DTU_PERIOD_MS | 引用APP_SOIL_WINDOW_MS | 两次物理发送尝试的最短间隔，不再保留5000ms常量 |
| APP_SOIL_EXPECTED_SAMPLES | 窗口/采样周期=30 | 传给Sensors的期望数，不伪造为实际计数 |
| APP_SOIL_MIN_VALID_PERCENT | 80 | ceil(30×80%)=24；还必须至少3个有效样本 |
| APP_DTU_TIMEOUT_MS | 10000ms | 单个待确认请求的非阻塞ACK超时 |

App_Init完成原有ADC自校准后记录窗口起点，首个主循环即可采样。每窗为[start,end)，以实际采集启动tick归属：在边界先冻结旧窗，再采边界的新样本到下一窗。正常采样为0、100…2900ms（相对启动），第一个完整窗口在3000ms结算；绝不首样本即上传。JSON的t_ms是该窗口结束tick，不是Unix时间或发送tick。

App每次循环先处理USART6接收和ACK/超时，再检查窗口和采样；发送前先完成边界新样本。ACK未结束时立即丢弃本窗上传机会，upload_skipped_busy累加，后续窗口采样继续，不等待10秒、不排队补发。已无待ACK但上次物理发送因迟到而不足3000ms时，仅保留当前冻结结果到间隔足够后发送；下一窗覆盖尚未发送结果，记录upload_skipped_rate，没有长队列，也不重发失败或超时报文。

普通调度迟到只实际采一次。若跨过多个完整窗口，直接推进到最新窗口边界，旧的部分窗和中间空窗跳过并计入upload_skipped_late；最新已结束的空窗如实报告sample_count=0和WINDOW_INSUFFICIENT，不把旧窗样本移过去。累计采样错误及分原因次数仍保留。所有tick判断使用无符号差值，覆盖32位回绕，不用HAL_Delay调度。

## 算法和诊断

SensorSoil_WindowReset/Add/Finish扩展现有sensor_soil模块。只保存尝试数、有效数、uint64总和、一个最小值、一个最大值、固定错误计数和最后错误，无样本数组、排序、动态内存或通用滤波框架。Sensors不包含app_config.h，期望数和有效比例由App传入。

成功且通过已有ADC有效性/清理检查的raw才参与。有效数至少24时：

```text
trimmed_sum = raw_sum - raw_min - raw_max
used_count = valid_count - 2
adc_raw = (trimmed_sum + used_count/2) / used_count
delta = trimmed_sum - 436 * used_count
denominator = 30710 * used_count
moisture_pct_x100 = (delta * 10000 + denominator/2) / denominator
```

百分比公式只在先比较边界并确认均值处于两端之间后计算，delta用int64，其他乘除保留64位中间量。LOW/HIGH严格比较trimmed_sum与436/31146乘used_count，相等为正常边界。raw先舍入只用于JSON整数显示，不作为百分比输入；单点和窗口共用同一个私有有理数换算路径。未平均已经限幅的百分比。

重复极值也各删一个；相同30值保持原值。0是有效值；无样本、少于3个有效值或未达质量门槛先失败，used_count=0，JSON两个数值null、status=WINDOW_INSUFFICIENT，不复用旧结果。100ms单次读取仍可提供本地打印百分比，窗口只累加其中raw，不使用单次百分比统计。

每窗的diagnostics保留各ADC错误次数、清理失败次数和最后BspAdcResult（主错误、HAL状态、清理标记及状态）。App_GetDiagnostics是只读视图，提供last_window、窗口结束tick、跳过数和累计采样/清理错误、各原因次数。sequence为累计软件采样尝试次数；JSON seq单独按发送尝试编号。

## JSON、服务器与页面

新JSON始终携带window_ms、sample_count、valid_count、used_count，以及sample_error_count、cleanup_error_count、last_sample_error诊断组。正常窗口例如下（密钥未展示）：

```json
{"device_id":"tea_h743_001","seq":1,"t_ms":3000,"channel":"AD0","adc_raw":15791,"moisture_pct":50.00,"status":"OK","window_ms":3000,"sample_count":30,"valid_count":30,"used_count":28,"sample_error_count":0,"cleanup_error_count":0,"last_sample_error":"OK"}
```

实际传输仍含私有token，并以实际CRLF结束；USART2不打印完整报文。保留snprintf两段长度检查，不发送截断内容。USART2、USART6重定向/回调、硬件引脚、16位ADC/异步8分频/387.5周期/软件触发/单次Channel5、PE15和CubeMX均未改。

服务器协议允许可选完整窗口组：window_ms为1～3600000，计数为0～1000000；valid≤sample、used≤valid；成功必须valid≥3且used=valid-2，失败数值null且used=0。诊断组可省略，提供时必须整组且与采样失败计数一致。不重做80%策略、滤波或换算。旧单点报文继续接收，无窗口字段的历史不补默认值。新字段存原payload，仅白名单暴露给API/WebSocket/CSV；无表结构修改、迁移、删除或重建，token仍排除在payload外。ACK及事务提交后确认行为不变。

网页显示“3秒去极值均值”、有效/尝试/参与数量、部分失败及实际错误原因；不足显示“本窗口有效样本不足”和“—”，有效0显示0.00%。历史单点明确独立标识。按记录id去重并只在选中记录集变化时重绘数值/曲线，秒数计时不改值或增点。1000条在3秒一条时约50分钟，显示实际覆盖范围，保留失败/缺测断线、设备/范围筛选、分页和CSV。

## 实际修改文件

| 文件 | 职责 |
| --- | --- |
| App/Inc/app_config.h、app_main.h，App/Src/app_main.c | 三周期配置、窗口归属/切换、冻结结果、单请求/最短发送间隔/跳过诊断、直接printf |
| Sensors/Inc/sensor_soil.h、Sensors/Src/sensor_soil.c | 常量空间窗口统计、质量判断、唯一有理数换算及诊断 |
| App/Inc/app_upload.h、App/Src/app_upload.c | 窗口JSON/诊断及有界缓冲，严格ACK解析沿用 |
| Tests/soil_host_test.c、soil_window_cases.h、dtu_host_test.c、dtu_window_cases.h | 算法、调度、UART及ACK回归；测试头不进MDK目标 |
| Tests/check_arm_printf.py、upload_wire_fixture.c、check_server_upload.py | 实际Arm库执行及C到TCP/SQLite/API/CSV集成适配 |
| Docs/code_architecture.md、json_upload_validation.md、本文件 | 当前架构与窗口约定；旧方案标为历史 |
| 服务器app/protocol.py、storage.py、main.py | 严格可选分组、原payload保存/白名单导出、当前窗口协议示例 |
| 服务器web/index.html、app.js、style.css | 窗口质量、历史区别、按id去重、保留大字/对比度/响应式布局 |
| 服务器tests/test_server.py、test_dashboard.js、preview_dashboard.py | 新字段与旧报文、元数据持久化、页面逻辑和独立临时demo浏览器验证 |
| 服务器README.md、STM32_INTEGRATION.md、validation/WINDOW_REPORT.md、validation/REPORT.md | 协议、启动、验收与截图；前一轮记录标为历史 |

没有新增目标源文件，现有MDK工程自动编译扩展后的App/Sensors；Bsp、Core、HAL/CMSIS、.ioc、私有密钥头及服务器.env不变。

## 本次验证与边界

- Host：四种板级配置×土壤/DTU程序共8组通过，保留自校准、ADC错误、USART2重定向/超时/方向、USART6分段多行/溢出/恢复、ACK错误/超时/回绕检查。另有缺失密钥禁用及编译配置保护检查。
- 算法：30个相同值；28正常+高低异常；重复极值；全0/65535；端点与端点外分数均值；437.5不提前舍入；先raw平均再限幅；24/23有效分界；0/1/2样本；所有ADC故障及清理失败；64位中间量。
- 调度：完整首窗、边界新样本、每窗实际30计数、tick回绕、迟到不补采/不复制、跨多窗空窗、待ACK继续采样/跳过上传、物理间隔至少3000ms/单个冻结结果、无突发补发。
- ARMCC5.06实际全量编译链接0错误0警告，检查真实USART2单一重定向、无半主机或浮点printf。当前构建尺寸见window_arm_build.log。
- ARM CPU仿真：生产App/Sensors/Bsp及真实Arm C库执行91次采样、3个窗口JSON、失败/部分失败、ACK和printf故障恢复；无运行时malloc。这是模拟HAL下的软件验证，不是上板。
- C生产报文→实际本地TCP分片→临时SQLite提交→鉴权API/CSV→生产C ACK解析通过，只使用demo设备和测试密钥。
- 服务器16项测试、JS语法/逻辑测试通过；浏览器实际验收见服务器validation/WINDOW_REPORT.md。

日志在Docs/validation/window_*.log。按原Tests/run_host_tests.py、check_architecture.py、check_arm_build.py、check_arm_printf.py及check_server_upload.py命令运行；使用当前安装Python3.12、GCC和MDK，不使用历史HEX冒充。既有文件和密钥保护审计见window_protection.log。

尚未烧录或接触实际串口：AD0的真实100ms采样稳定性、4G DTU公网ACK、现场窗口计数/湿润度变化、断网及恢复、长时间运行仍待用户上板验收。软件测试和demo截图不能替代实测。
