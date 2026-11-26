> 当前雨强增量见[本轮实现与验证](Docs/rain_integration_validation.md)、rain_release_record.json和rain_test_results.json；旧“固定十项/雨量暂停”等以下内容是前次2026-11-14整合历史。当前十一项，先人工重启更新后的服务器，再手动下载固件；生产数据库/配置与旧备份保持。

> 当前AD5原始采样增量（2026-11-15）：[说明与验证](Docs/ad5_raw_validation.md)、[当前构建/修改记录](Docs/ad5_validation.json)。旧发布包仍为前次快照，不包含AD5；本轮仅电脑口raw，不改V2、服务器或前端。

> 当前维护入口（2026-11-15）：[根README](README.md)、[交付六文档](Docs/README.md)、[验收/实际测试](Docs/acceptance_records.md)、[发布清单](Docs/release_record.json)。本轮完成稳定化、App中立模型、coreJSON隔离、单一版本和受控发布工具；以下原整合报告按原日期/证据保留，不代表本轮实板验收。

# Tea Sense 目录整合与交付

更新：2026-11-14。

## 统一字段合同（schema_version=2）
UTF-8单行JSON+CRLF，顶层：schema_version、device_id、seq、t_ms、window_ms=3000、data、私有token。当前固件不发boot_id；模拟器可选UUID。ACK仍为ok/device_id/seq/id/duplicate。
| data键 | 单位 | 周期 / 过期阈值 |
|---|---|---|
| soil_moisture_pct | %相对湿润度 | 3s / 9s |
| soil_ph | 无量纲 | 3s / 9s |
| wind_speed_mps | m/s | 3s / 9s |
| wind_direction_deg | °参考角度[0,360) | 3s / 9s |
| temperature_c | ℃ | 10s / 30s |
| air_humidity_rh_pct | %RH | 10s / 30s |
| light_lux | lx | 10s / 30s |
| pressure_kpa | kPa | 60s / 180s |
| pm25_ug_m3 | μg/m³ | 30s / 90s |
| pm10_ug_m3 | μg/m³ | 30s / 90s |

每项固定对象：value数字/null、status、sample_seq、sample_t_ms、age_ms，可选range=LOW/OK/HIGH。只有OK带数字，失败null；NO_DATA身份/年龄全部null。有效0与缺失严格区分。
模拟量源seq为窗口编号、tick为窗口末；上报seq为发送尝试编号，允许busy跳窗。天气源seq为最近完成查询编号，成功tick为响应结束，失败tick为事务真实完成；查询中保留上次完成结果。age_ms为编码时无符号tick差，不是UTC，不覆盖未知网络滞留。不上传ADC、电压、引脚或窗口诊断。

## API、数据库与网页
/api/config返回脱敏V2/旧例、指标规格、端口与鉴权开关；/api/overview返回上报/测量总数、设备连接和十项当前状态；当前值独立于历史过滤。
/api/readings保留原上报分页，/api/measurements按device_id/metric/hours/status/limit/before_id查独立测量；/api/export.csv按相同条件导出物理长表，UTF-8 BOM，最多10000行，无ADC/电压/token。
/ws保留VIEW_TOKEN首条鉴权、同源Origin、ready/reading/resync/ping；网页3秒重连、5秒HTTP兜底。

SQLite user_version=2，readings原ID/payload保留，新增schema_version/run_id；device_runs和measurements增量回填旧湿度，幂等。上报次数与测量点数分开。运行段+指标+源seq+源tick去重缓存，同值新查询仍加点；全部事务提交后ACK。报告age加服务器流逝时间，缓存重复不会重置/年轻化；晚到旧结果不覆盖新故障。
无boot_id不能全局区分重启与重放：已建立段（旧seq>=5、tick>=60s）同时发生早期seq<=3、tick<=30s双倒退时才保守切段；单TCP重连不切，回绕采用模差。短时重启与不可区分重放仍有局限。
页面四主卡+六气象卡、独立质量/年龄；风向指针走最短路，跨0曲线断线；不同量纲分图，趋势最多1000点并标实际覆盖。真实优先，demo需显式选择；无气体/雨量/ADC卡。

## 最终目录、保留与迁入
- D:/12-Project/1-EC-Project/tea_sense：在现有工程原地更新。
- D:/12-Project/1-EC-Project/tea_sense_server：从旧实际源码复制，无额外嵌套；与最新版(2)ZIP核对，未用旧ZIP覆盖用户文件。
- 旧服务器D:/4-横向项目/2-茶叶物联网项目/tea_sense_tcp_server/tea_sense_server完整保留且哈希核对未变。
- integration_backups/integration_before_20261114：source_before.zip、原文件SHA256 manifest、一致性tea_sense_before.db、migration.json。备份含私有配置，仅本机保管，勿外发。
- 虚拟环境从Python3.12及原requirements-dev.txt重建，无升级；旧.venv/缓存未复制。
- 原INGEST_TOKEN/VIEW_TOKEN保持，固件密钥匹配；.env只更新DATABASE_PATH为新data/tea_sense.db，监听127.0.0.1:8000/9000保持。
- 使用SQLite backup API迁入，正式825条、ID1～825、所有原字段/payload逐项相同，quick_check=ok。迁移后user_version=2/825独立湿度点，重复初始化无重复；未写正式库模拟数据。
- 1485个受保护文件哈希一致（含Core/Drivers/.ioc、ADC封装、soil原算法、MDK工程/启动、私有配置/coreJSON）；风速/pH/风向系数逐宏复核不变。未初始化/提交Git、未烧录、未改公网/cpolar/DTU配置。
- 未新增生产源文件，扩展现有模块，MDK源清单原样；新测试harness只在测试临时链接使用。所有原始历史资料和验证日志保留。

## 启动与当前状态
从任意目录运行本根start_server.cmd；脚本按自身路径定位，缺.venv明确报错，一次只启动一个服务。此次已从TEMP目录通过脚本隐藏启动，工作进程42748，8000和9000均由此进程监听，父启动器来自新服务器.venv。
网页 http://127.0.0.1:8000 ，/healthz返回ok:true；/api/overview计数825上报、825测量、2历史设备。cpolar仍转发TCP9000，未更改已有映射；未验证隧道当前连通性。不要重复启动旧目录或新副本；需要停服时只停止本项目进程。
启动/错误日志为integration_server.log与integration_server_error.log。测试预览18000/19000已验证后停止，仅正式服务保留。

## 本次验证结果
| 验证 | 结果 |
|---|---|
| Host | 五配置×soil/dtu=10次通过，额外未确认手动方向/零跨度编译拒绝、缺私钥禁上传通过 |
| 架构 | App/Sensors/Bsp层次、GPIO/ADC/时钟、三类HAL回调唯一、异步TX、无raw常规打印、默认停气体通过 |
| ARM MDK | 全量重建0error/0warning；Code44608、RO1144、RW120、ZI10560；标准库、无float printf/半主机 |
| ARM CPU | 182轮、728选通、1456 Stop、一次自校准、6上传/ACK；完整十字段、中文CRLF与printf故障恢复通过；应用实测栈624/1024字节 |
| 跨工程C回环 | 24个3秒包、128独立点；真实C格式化/31字节分片TCP/临时SQLite/鉴权API与CSV/C ACK解析通过 |
| 服务器 | 27项unittest通过，原16项保留；V2严格性、迁移、缓存、失败/零、独立年龄/运行段、分页CSV、模拟器合同通过 |
| 前端 | Node语法/逻辑通过；1920×1080、1280×900、390×844实际截图；10卡、无横向溢出、混合失败/0、过期、全屏、单位图表、显式demo、CSV下载通过；无浏览器error |
| 正式迁入 | 原825条完全保留、幂等回填、受保护文件与密钥校验、唯一进程/health/API通过 |

固件证据位于tea_sense/Docs/validation/integration_*；服务器证据tests/integration_*.log及tests/dashboard_*.jpg。ARM仿真使用测试main和测试密钥重编译同一app_upload源码，其余业务/标准库为本次生产对象，未写真实token到产物。
本次build可用，不用旧HEX/旧日志代替结果；早期printf_mdk_rebuild.log已恢复为原历史，最新结果另存integration_arm_build.log。
物理串口输出和手工对齐步骤见tea_sense/Docs/integration_v2.md，服务器接入见tea_sense_server/STM32_INTEGRATION.md。本次没有真实上板电压/ISR时序/AUTO方向/4G/cpolar端到端/参考风速仪或真实pH准确度验证；新固件必须用户手动编译下载，启动服务器不表示十项实物已接通。

## 修改文件
以下与执行前manifest比较；全部文件清单也见integration_changed_files.json。
- tea_sense/Docs/code_architecture.md
- tea_sense/Docs/json_upload_validation.md
- tea_sense/Docs/soil_ph_validation.md
- tea_sense/Docs/soil_window_validation.md
- tea_sense/Docs/weather_rs485_validation.md
- tea_sense/Docs/wind_speed_calibration_validation.md
- tea_sense/Tests/ad3_cases.h
- tea_sense/Tests/adc_raw_cases.h
- tea_sense/Tests/check_architecture.py
- tea_sense/Tests/check_arm_printf.py
- tea_sense/Tests/check_server_upload.py
- tea_sense/Tests/dtu_host_test.c
- tea_sense/Tests/dtu_window_cases.h
- tea_sense/Tests/ph_cases.h
- tea_sense/Tests/run_host_tests.py
- tea_sense/Tests/soil_host_test.c
- tea_sense/Tests/upload_wire_fixture.c
- tea_sense/Tests/weather_cases.h
- tea_sense/Tests/wind_direction_cases.h
- tea_sense/Tests/wind_speed_cases.h
- tea_sense/App/Inc/app_config.h
- tea_sense/App/Inc/app_main.h
- tea_sense/App/Inc/app_upload.h
- tea_sense/App/Inc/app_weather.h
- tea_sense/App/Src/app_main.c
- tea_sense/App/Src/app_upload.c
- tea_sense/App/Src/app_weather.c
- tea_sense/Bsp/Inc/bsp_config.h
- tea_sense/Bsp/Inc/bsp_dtu.h
- tea_sense/Bsp/Src/bsp_dtu.c
- tea_sense/Sensors/Inc/sensor_ph.h
- tea_sense/Sensors/Inc/sensor_weather.h
- tea_sense/Sensors/Inc/sensor_wind_direction.h
- tea_sense/Sensors/Inc/sensor_wind_speed.h
- tea_sense/Sensors/Src/sensor_ph.c
- tea_sense/Sensors/Src/sensor_weather.c
- tea_sense/Sensors/Src/sensor_wind_direction.c
- tea_sense/Sensors/Src/sensor_wind_speed.c
- tea_sense/Tests/stubs/stm32h7xx_hal.h
- tea_sense_server/.env
- tea_sense_server/README.md
- tea_sense_server/STM32_INTEGRATION.md
- tea_sense_server/VALIDATION.md
- tea_sense_server/app/main.py
- tea_sense_server/app/protocol.py
- tea_sense_server/app/storage.py
- tea_sense_server/tests/preview_dashboard.py
- tea_sense_server/tests/test_dashboard.js
- tea_sense_server/tests/test_server.py
- tea_sense_server/tools/send_sample.py
- tea_sense_server/web/app.js
- tea_sense_server/web/index.html
- tea_sense_server/web/style.css
- tea_sense/Tests/arm_integration_main.c
- tea_sense/Tests/integration_cases.h
- tea_sense/Docs/integration_v2.md
- tea_sense_server/tests/test_sample.py
- tea_sense_server/tests/test_v2.py

新增根交付：start_server.cmd、integration_notes.md、integration_changed_files.json、integration_migration_verified.json；构建产物位于tea_sense/MDK-ARM/tea_sense；测试日志/截图为验证产物。
