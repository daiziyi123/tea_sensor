> 当前业务于2026-11-14改为四路合格窗口与十项V2，见 [integration_v2.md](integration_v2.md)。下文保留为此前阶段记录，一秒/raw输出与旧单土壤协议不再代表当前行为。

# 土壤 JSON 上传交付与验证

> 历史记录：本文件描述窗口统计加入前的5秒单点快照方案。当前已改为100ms采样/1秒打印/3秒去极值窗口，详见[当前交付](soil_window_validation.md)。

2026-11-13，北京时间。本轮没有初始化或提交 Git，没有烧录。

## 修改文件及职责

| 文件 | 修改 |
| --- | --- |
| App/Src/app_main.c | 移除固定文本回显；保存最新采样快照及采样tick；每5秒上传、最多一个待确认、10秒超时；每次主循环先处理接收；USART2直接printf |
| App/Inc/app_config.h | 默认设备号tea_h743_001；保留1秒采样、5秒上传、10秒ACK超时 |
| App/Inc/app_upload.h、App/Src/app_upload.c | 有界JSON格式化、严格ACK字段解释；整数两位小数，不增加浮点printf |
| App/Inc/core_json.h、App/Src/core_json.c、Docs/coreJSON-LICENSE.txt | FreeRTOS coreJSON v3.3.1固定内存解析器及MIT许可证；不使用strstr判定ACK |
| App/Inc/app_secrets.h、.gitignore | 本地私有上传密钥，已忽略；实际值不在文档、测试或日志中展示 |
| Docs/app_secrets.h.example、tools/configure_upload_secret.py | 无密钥模板及从授权服务器.env仅读取INGEST_TOKEN的配置工具 |
| MDK-ARM/tea_sense.uvprojx、tea_sense.uvoptx | App分组加入app_upload/core_json，无重复源文件 |
| Tests/dtu_host_test.c、soil_host_test.c、run_host_tests.py | JSON上传/ACK替代旧回显断言；保留采样、错误、溢出、UART恢复、超时等回归，使用隔离测试密钥 |
| Tests/check_architecture.py、check_arm_printf.py | 允许第三方标准头形式；在真实Arm库仿真中验证JSON及ACK |
| Tests/upload_wire_fixture.c、check_server_upload.py | 生产C格式化/ACK解析与真实本地TCP、临时SQLite、鉴权API/CSV集成 |
| Docs/code_architecture.md | 当前单一JSON上传流程与配置归属 |
| Docs/dtu_echo_validation.md | 标为历史记录，指向当前JSON流程，避免误认为仍运行固定文本回显 |

Bsp、Sensors、Core、Drivers和.ioc均未修改。USART6接收回调仍在Bsp/Src/bsp_dtu.c（HAL_UART_RxCpltCallback / HAL_UART_ErrorCallback），只收字节、重启接收及处理硬件错误；应用解析在app_main.c的App_DtuRead / App_DtuConsumeByte，JSON解释在app_upload.c。USART2重定向仍为Bsp/Src/bsp_console.c的标准Arm C库_sys_write路径。

服务器修改web/index.html、style.css、app.js和README.md、STM32_INTEGRATION.md；新增tests/test_dashboard.js、preview_dashboard.py及validation截图/报告。tests/test_server.py仅修正Windows测试SQLite连接显式关闭。app/protocol.py、tcp_server.py、storage.py、main.py等后端实现未改，鉴权、SQLite结构、API及导出保持兼容。

## 私有配置和运行

服务器目录：`D:\4-横向项目\2-茶叶物联网项目\tea_sense_tcp_server\tea_sense_server`。

本地app_secrets.h已经从该服务器.env读取INGEST_TOKEN配置；工具不回显密钥。需要更新时在固件根目录运行：

```powershell
py -3.12 tools/configure_upload_secret.py --env 'D:\4-横向项目\2-茶叶物联网项目\tea_sense_tcp_server\tea_sense_server\.env'
```

模板未配置时输出DTU_CONFIG,status=TOKEN_MISSING并禁止上传；私有头文件不存在时编译明确失败。构建产物包含设备上传所需密钥，按私有固件保管。公网地址由现有DTU配置管理，不写入固件。

本地1秒采样输出不变，例如：

```text
t_ms=5000,channel=AD0,adc_raw=17548,moisture_pct=55.72%,status=OK
DTU_TX,seq=1,status=OK
DTU_ACK,seq=1,id=14
DTU_TIMEOUT,seq=2
```

以上为验收预期示例，不是本次硬件日志。TX仅表示USART6串口发送完成；ACK表示服务器匹配设备/seq的入库确认。有效0正常上传为0/0.00；低/高端保留range；采集失败上传null/null及实际错误，没有首个样本不上传。最新失败覆盖旧快照，不自动重发或增加boot_id。

## 本次实际验证

| 层次 | 结果 / 证据 |
| --- | --- |
| Host C | 4种板级配置×土壤/DTU共8个程序通过，及手动方向未确认、零跨度编译拒绝、缺失密钥禁用上传检查；json_upload_host.log |
| JSON与调度边界 | 拆包、多行、LF/CRLF、错误ok类型、设备/seq不匹配、旧/重复ACK、乱序/嵌套未知字段、id64位边界、超时及tick回绕、长行/环缓冲溢出恢复、UART发送失败/接收恢复、截断不发送、等待时土壤持续采集、0值/失败null及快照时间一致 |
| ARMCC5.06实际编译链接 | 当前源文件全量重建：0错误、0警告；Code31112、RO1120、RW88、ZI3712；json_upload_arm_build.log和printf_mdk_rebuild.log |
| 实际链接审计 | USART2单一重定向；无重复符号、半主机指令或浮点printf；没有使用历史HEX冒充构建 |
| ARM CPU仿真 | 生产App/Sensors/Bsp和真实Arm C库，在假HAL下执行printf、土壤边界及USART6中断接收/JSON ACK；无运行时malloc；json_upload_arm_execution.log。不是上板测试 |
| C→服务器软件集成 | 生产C报文→实际loopback TCP分片→临时SQLite提交→鉴权API/CSV→生产C解析服务器ACK，通过；json_upload_server_integration.log。仅demo_h743_001及测试密钥 |
| 服务器 | 13项unittest通过；JS语法检查及设备选择/零值/失败/新鲜度/单设备曲线断线断言通过；服务器validation/server-tests.log |
| 浏览器 | 1920×1080、2560×1440、3840×2160、1366×768、390×844 CSS视口实际检查；所选demo上下文、无真实设备提示、字号、趋势及无整页横向溢出已验证。截图是独立临时库中的明确demo数据 |
| 保护审计 | 1492个受保护文件SHA256与修改前一致，包含硬件源码、.ioc、服务器.env与已有数据库；实际密钥未出现在源码/网页/测试/文档/日志交付中（私有头和固件产物除外） |

验证命令（从固件目录；Python/GCC/MDK按实际安装路径设置）：

```powershell
$env:APP_TEST_GCC='D:\ming\mingw64\bin\gcc.exe'
py -3.12 Tests/run_host_tests.py
py -3.12 Tests/check_architecture.py
py -3.12 Tests/check_arm_build.py
$env:APP_ARM_EMULATOR_PATH='C:\Users\15995\AppData\Local\Temp\tea_sense_arm_emulator'
py -3.12 Tests/check_arm_printf.py
& 'D:\4-横向项目\2-茶叶物联网项目\tea_sense_tcp_server\tea_sense_server\.venv\Scripts\python.exe' Tests/check_server_upload.py --server-root 'D:\4-横向项目\2-茶叶物联网项目\tea_sense_tcp_server\tea_sense_server'
```

日志均在Docs/validation。本次实际使用已安装Python3.12完整路径运行，未依赖Windows的python别名。第三方来源：[FreeRTOS coreJSON v3.3.1](https://github.com/FreeRTOS/coreJSON/tree/v3.3.1)。

## 尚未验证

没有烧录，真实AD0→USART6→4G DTU公网→服务器→网页链路未实测；需用户改变传感器状态，核对USART2样本、DTU_ACK及网页tea_h743_001百分比，断开上传确认超时。现场网络恢复和长时间运行也待验收。

全屏按钮已实现；自动化点击被浏览器用户手势限制拒绝，转用实际Windows窗口操作时浏览器控制因不能可靠识别窗口URL而停止。因此全屏进入/退出、浏览器历史分页/CSV点击，以及失败/超时/API异常的实际浏览器画面仍待复核，不能用host/API测试代替这些浏览器验收。相应纯逻辑/后端分页和CSV测试已通过。实际截图和布局尺寸详见服务器validation/REPORT.md。临时预览服务已停止，未修改正式端口、.env或数据库。
