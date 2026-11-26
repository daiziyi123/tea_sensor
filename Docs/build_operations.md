# 构建、运维、版本与受控发布

2026-11-23 当前0.10.0-dev/V2/baseline-3、schema4、180秒分类统计、串口静默。当前命令/结果/SQLite备份回滚/人工切换与DTU步骤以[本次交付](three_minute_statistics.md)为准。复用.venv，未升级依赖；可选Unicorn当前不可导入，ARM CPU仿真未执行，ARM编译链接0错误/0警告。

以下保留2026-11-15详细工具/备份/受控发布说明。旧三秒打印、baseline-2/schema2和旧发布物名属于历史，不作当前下载依据。
交付初稿更新：2026-11-15，版本管理基线见tea_sense/App/Inc/app_version.h。
证据分类：[代码核对]、[历史用户实测]、[资料记录]、[待确认]、[待执行]；软件测试通过不等于实物准确或现场签收。

## 实际环境与来源
[本机工具核对] Python3.12.10、SQLite3.49.1；GCC8.1.0（D:/ming/mingw64/bin/gcc.exe）、Node v24.16.0；MDK Professional5.36/UV4 5.36.0.0、ARMCC5.06 update7 build960；可选Unicorn2.1.4在临时工具目录。CubeMX包STM32Cube FW_H7 V1.12.1来自.ioc；未升级工具或依赖。
[代码核对] requirements.txt：fastapi0.141.1、uvicorn0.52.1、websockets16.0、pydantic2.13.4；requirements-dev另含httpx0.28.1。运行环境来自本地.venv，不复制Windows虚拟环境作为发布物。
[代码核对] 权威固件版本、协议版本、标定修订均在App/Inc/app_version.h：0.9.1-dev、数字2、baseline-2（baseline-1记录作为历史保留）。前者为开发版本，后者为当前参数集管理标识，不是标定日期/精度认证。服务器无独立声明版本，发布清单以所收录服务端源码哈希标识，数据库schema从app/storage.py读取为2。coreJSON的权威版本/许可记录只在Middlewares/Third_Party/coreJSON/README.md（当前头定义v3.3.1），不猜上游提交号。

## 固件构建与手动下载
在共同根目录执行，当前目录与执行项目必须对应：
~~~
cd /d D:\12-Project\1-EC-Project\tea_sense
py -3 Tests/run_host_tests.py
py -3 Tests/check_architecture.py
py -3 Tests/check_arm_build.py
py -3 Tests/check_arm_printf.py
..\tea_sense_server\.venv\Scripts\python.exe Tests/check_server_upload.py --server-root ..\tea_sense_server
~~~
以上是cmd写法；PowerShell使用Set-Location或cd路径，不使用cd /d。脚本支持APP_TEST_GCC、BSP_ARM_BIN、APP_MDK_UV4。Unicorn不在默认路径时设APP_ARM_EMULATOR_PATH指向已安装包目录；本机为%TEMP%/tea_sense_arm_emulator。可将APP_ARM_BUILD_LOG及APP_ARM_EVIDENCE_DIR指向新的验证输出，避免覆盖历史日志。
MDK打开tea_sense/MDK-ARM/tea_sense.uvprojx，tea_sense目标Rebuild；标准Arm C库、MicroLIB关闭，无浮点printf/半主机。保留启动/链接/RTE和当前ARMCC配置。输出tea_sense/MDK-ARM/tea_sense/tea_sense.hex及tea_sense.axf；本次实际构建哈希/源码身份见rain_release_record.json；release_record.json为旧稳定化快照。旧历史Objects/HEX不作本次证据。
私有app_secrets.h只在app_upload实现使用；公开源码包中不含它，可参考tea_sense/Docs/app_secrets.h.example在现场单独配置。占位/禁用配置可以编译测试，但不能声称已向真实服务器鉴权上传。
[待确认] 下载器型号、实物板卡修订、连接和复位方式；[待执行] 用户人工下载本次固件并上板验收。本轮未烧录，也未自动操作下载器。

## 本地服务器启动和升级
[代码核对] 根start_server.cmd按脚本自身路径cd到tea_sense_server，检查.venv后运行-m app.main，单进程，不启用reload或多workers。网页http://127.0.0.1:8000，TCP127.0.0.1:9000，健康检查/healthz。本轮未重复启动或重启生产监听，原cpolar/DTU目标不动；实际公网透传验证待执行。
~~~
D:\12-Project\1-EC-Project\start_server.cmd
~~~
或者服务器目录执行.venv/Scripts/python.exe -m app.main，保持窗口打开，Ctrl+C停该实例。已有服务运行时不重复启动。服务API/WS与权限见protocol_v2.md。
确需新环境时，先保留原环境/配置/数据，在新目录用本机Python3.12创建.venv，再由该环境pip install -r requirements-dev.txt；勿删除正在使用的环境、复制旧.venv或无关升级依赖。
Settings.from_env先读取项目.env，使用os.environ.setdefault；已存在的进程环境优先。数据库配置来源是DATABASE_PATH，本机文件记录为D:/12-Project/1-EC-Project/tea_sense_server/data/tea_sense.db；不能仅看默认值。真实.env、app_secrets.h均保持，密钥不写本文或URL。
升级记录固件/服务源码哈希、三种版本、参数集、schema和验证范围；保留旧软件、私有配置及一致性数据库备份。新旧库兼容必须独立核对，不能承诺旧程序能打开任意新库。

## 一致性备份（工具不读取.env，不覆盖已有目标）
新增tea_sense_server/tools/backup_database.py，仅标准库。显式源/目标，SQLite backup API，quick_check，既有目标拒绝，错误退出；低频分页备份有60秒上限。私有配置另行私下备份。
从服务器目录执行，先人工创建目标父目录；文件名使用新的实际时间标记：
~~~
.venv\Scripts\python.exe tools/backup_database.py backup --source "D:\12-Project\1-EC-Project\tea_sense_server\data\tea_sense.db" --destination "D:\12-Project\1-EC-Project\integration_backups\tea_sense_schema2_YYYYMMDD_HHMMSS.db"
.venv\Scripts\python.exe tools/backup_database.py verify "D:\12-Project\1-EC-Project\integration_backups\tea_sense_schema2_YYYYMMDD_HHMMSS.db"
~~~
将YYYYMMDD_HHMMSS替换为真实执行时间，选不存在的目标；本轮仅用临时fixture调用，未对生产库执行备份或恢复。日志只有schema/计数/校验结果，无payload或密钥。

## 人工恢复边界（生产恢复待执行）
1. 确认当前DATABASE_PATH及本项目PID，只停本项目写入；不要结束全部Python。确认连接关闭。
2. 保留当前整个data目录及相关.db/-wal/-shm的可恢复副本，不在写入期间只复制.db；可先用backup工具生成一致性新备份。
3. verify候选备份的quick_check/schema；核对将运行软件兼容，记录行数/ID。校验成功不等于版本兼容已证明。
4. 在停写状态将当前库与sidecar一并移到新保留目录，禁止只替换.db却留下不匹配WAL/SHM；将已验证备份恢复到全新的目标数据位置，保留回退副本。此步骤人工执行，本工具不自动覆盖。
5. 启动唯一实例，核对历史ID/计数、API/网页/WS及新写入、DTU恢复情况；异常停止并按版本方案回退，不清库。
[本轮临时库软件验证] WAL一致性备份、不包含未提交记录、保留历史ID42、恢复至另一个临时库、原库后续写入不影响备份均通过。生产停服/恢复、服务器重启及真实DTU恢复仍待执行。

## 受控源码预览包
共同根Tools/package_release.py是实际发布入口，只用标准库；固定收录固件App/Sensors/Bsp/Core/Drivers/Middlewares/MDK启动/RTE、服务器源码/网页/requirements/测试/工具、当前六份文档及指定脱敏证据。排除CubeMX Backup、历史、缓存、.venv、真实app_secrets.h/.env、其他私密配置/DTU导出、所有数据库/sidecar/备份和旧日志/固件二进制。
~~~
py Tools/package_release.py preview --root "D:\12-Project\1-EC-Project"
py Tools/package_release.py build --root "D:\12-Project\1-EC-Project" --output "D:\12-Project\1-EC-Project\Releases\rain-source-preview-20261115"
py Tools/package_release.py verify --package-dir "D:\12-Project\1-EC-Project\Releases\rain-source-preview-20261115"
py Tools/package_release.py extract --package-dir "D:\12-Project\1-EC-Project\Releases\rain-source-preview-20261115" --destination "<一个不存在的新临时目录>"
~~~
先创建Releases父目录；输出目录已有则拒绝覆盖，不能位于tea_sense/tea_sense_server/Docs/Tools收录树内；链接/目录联接不跟随。预览只列路径/原因，不读取私有文件内容。verify校验清单完整性、每个实际交付字节哈希与ZIP旁置SHA256；extract先verify，拒绝路径穿越/已有目标。校验和不是数字签名/来源认证。
实际本次包路径及其哈希见rain_release_record.json；若目标已有，保留旧包，换新明确目录，不清理工作区。
清单版本直接解析app_version.h；无Git时记录受控源码哈希，不捏造干净提交。测试结论优先引用当前rain_test_results.json（旧release_test_results.json保留历史），未上板/长期等单列。
**本工具生成source-preview，不收录HEX/AXF。** 本机构建产物确为本次生成，哈希单独记录；固件含现场私有配置，应私下控制交付。包内无真实密钥，不能同时承诺未配置源码能直接向真实服务器上传。打包成功也不代表构建成功；隔离解包host/静态验证使用专用fixture，不能当实板验证。公开清单不含私有配置哈希或内容，本轮不制作对外发布或上传外部服务。

## 本次雨强升级顺序（人工操作，尚未执行生产升级）
1. 保留现有.env、app_secrets.h、旧软件/包和数据库；需备份时按上文SQLite backup API输出新目标，禁止拿测试库覆盖真实库。
2. 先更新本地服务器源码/网页（本工作区已完成），在原服务窗口Ctrl+C停止唯一实例；本轮没有替用户停止正在运行的生产实例。再双击根start_server.cmd，仍用原.env/DATABASE_PATH及8000/9000，不启动旧目录第二份服务。
3. 打开http://127.0.0.1:8000，强制刷新页面；核对/healthz及/api/config（如启用VIEW_TOKEN，使用既有鉴权而非把密钥写URL），雨强单位mm/min、范围0～8、新鲜度9000ms。旧设备十项报文继续接受，雨强应未采集，不造0。前端随服务器提供，无需独立npm启动。
4. 再在MDK打开tea_sense/MDK-ARM/tea_sense.uvprojx，保留现场私有配置，Rebuild目标tea_sense；确认本次0error/0warning及版本0.9.1-dev/baseline-2，用户手动下载刚生成的HEX/AXF。本轮只构建，不自动烧录。
5. USART2按115200/8N1、UTF-8观察每3秒五路中文行；和同窗rain_intensity_mm_min、ACK诊断、网页及CSV对齐，失败不得复用旧值。再验收真实4G、断网重连、重启、现场接线/供电及持续运行，并登记实测仪表/时间/操作者。
6. 异常时先保留现场日志/库；旧严格服务器不能接收新固件十一项，回退必须匹配固件与服务器，不能只回退服务器或清空数据库。

新源码预览包目录rain-source-preview-20261115，原source-preview-20261115及备份保留。包不含密钥、生产数据库或HEX/AXF，不能当已鉴权可即用二进制。当前构建字节哈希在rain_release_record.json，ZIP旁置.sha256校验压缩包。
