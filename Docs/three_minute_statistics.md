# Tea Sense 三分钟上传与分类统计交付

2026-11-23，北京时间。当前源码增量修改；固件 **0.10.0-dev / V2 / baseline-3**。本次没有修改标定端点/硬件通道、初始化 Git、提交、烧录、操作 DTU、重启现场服务或部署云资源。原串口输出已关闭，本次保持关闭。

## 当前实现与职责

App 初始化完成后建立连续、不重叠的 [start,end) 窗口，首包在首个完整180秒结束时产生；端点周期样本归下一窗。采样仍为100ms，气象仍按10/30/60秒查询。App_Process只把真实完成的新轮次交给唯一新增的 app_statistics.c/h；传感器模块只保留采集/标定/物理转换，已删除被替代的 WindowReset/Add/Finish、三秒封存和方向数组。

统计按源身份及计划槽去重，数值相同的新身份正常计入。无追赶密集采样、历史补发或离线队列。延迟跨多窗仅封存最新已完成窗；空白时段记缺测。慢变量先纳入本次已完成的新样本，再封存短窗。下一窗继续采样，上传缓冲在TX/ACK期间不可改写。网络当前不重试业务包；失败尝试也推进节拍，下个计划窗口才再尝试。seq是发送尝试号，不是ADC号。

RAM固定：土壤/pH各5槽，风速sum/count/max，方向C/S/count，雨强weighted_sum/valid_duration及前值。双精度物理累加不先舍入原始均值。唯一本来就存在的Bsp TX缓冲从4096增至6144字节（可编码6142，预留2字节恢复前缀），原因是元数据及最坏转义密钥需要4298字节；没有新增TX副本或动态内存。

## 时间和质量参数

| 参数 | 当前默认值 |
|---|---|
| 业务上传/周期窗口 | 180000ms |
| 五路ADC轮次 | 100ms，不变 |
| 慢变量槽间隔/槽数 | 1000ms / 各5槽 |
| 慢变量最低有效槽/最新有效源年龄 | 4 / 不超过2000ms |
| 慢变量统计范围 | 最近约5000ms |
| 周期最低覆盖 | 80% |
| 周期预期风速/配对槽 | 1800 |
| 静风工程阈值 | 0.3m/s |
| 方向最低集中度R / 最少有风样本 | 0.2 / 3 |
| 速度与方向配对最大时间差 | 当前轮次周期100ms |
| 雨强最大有效相邻间隔 | 3×实际采样周期=300ms |
| 温湿度/光照查询、源过期 | 10s / 30s |
| PM查询、源过期 | 30s / 90s |
| 气压查询、源过期 | 60s / 180s |
| 业务离线 / GPS过期 | 各540s，独立计龄 |
| 已认证连接空闲 | 默认600s，配置不得小于600s |
| 未认证空闲 / 未完成JSON期限 | 各300s，独立于认证空闲 |
| UART/ACK超时及受控恢复 | 原配置不变，ACK仍10s |

80%、0.3m/s、R=0.2及时间策略是项目工程初值，不是厂商精度或气象标准。所有tick调度采用无符号差，无HAL_Delay或ISR格式化/三角函数。

## 各字段的方法与单位

| 字段 | 方法 | 单位/输出精度 |
|---|---|---|
| soil_moisture_pct | median，最近5槽；5有效取中间，4有效取中间两raw的精确分数再沿原公式转换 | %，2位 |
| soil_ph | median，同上；原54.64→3、62741.50→9端点不变 | 无量纲，2位 |
| wind_speed_mps | mean，三分钟有效物理样本普通算术平均，含合法零和高值，不去极值 | m/s，2位 |
| wind_speed_mps.wind_speed_sample_max_mps | 同窗附属采样最大风速，随主窗口质量一起置空；不是gust或第12路 | m/s，2位 |
| wind_direction_deg | circular_mean，先检查配对覆盖，静风CALM，否则只统计达到阈值的方向；atan2、R门槛、360归0 | °，2位，[0,360) |
| rain_intensity_mm_min | time_weighted_mean，相邻有效端点确认前值保持区间，跨边界分配；缺测不入分母 | mm/min，2位，不是累计mm |
| temperature_c | latest，最近有效真实查询 | ℃，1位 |
| air_humidity_rh_pct | latest | %RH，1位 |
| light_lux | latest | lx，整数 |
| pressure_kpa | latest | kPa，2位 |
| pm25_ug_m3、pm10_ug_m3 | latest | μg/m³，整数 |
| GPS | 独立最近有效定位，不平均 | 经纬度，显示6位 |

雨强确认区间超过300ms则整段缺测，任一端点无效同样不计；尚无后续有效端点确认的末尾不无限外推。达到80%但有缺测输出PARTIAL。合法raw=0是有效值；失败从不补零或复用旧模拟量。

## V2/数据库/页面兼容

schema_version仍为2，包络、11个物理键、单位、私有token、seq和提交后ACK不变。每项新增statistics.method；周期/短窗增加window_ms/start_ms/end_ms/expected_count/valid_count/used_count，雨强另有valid_duration_ms。周期结果sample_t_ms是封存末tick，短窗是最新参与源tick；start/end明确这是统计结果。时间不是UTC。

有效状态OK/PARTIAL/CACHED可带数值；CALM/VARIABLE/WINDOW_INSUFFICIENT/STALE和原故障状态为空值。方向静风不伪造0°。气象按字段保留有效缓存身份/源时间，并附cached及latest_query_status/seq/t_ms；最新查询失败而未过期可CACHED，源超过原三倍周期在新帧生成时变STALE。旧缓存不反复产生历史测量点。

SQLite user_version增量升至4：在原GPS schema3基础上仅给measurements增加可空result_metadata列；更旧库依次补齐旧迁移。readings原payload、ID和历史三秒含义不改写。旧V1、十项/十一项3000ms V2仍兼容；旧记录没有统计元数据，不补标为三分钟平均。明确boot_id的重试仍幂等；固件不发boot_id且不重试，跨重启不可区分重放的限制仍保留。

/api/config新增business_window_ms=180000并展示当前分类统计例包；旧三秒例包单列legacy_v2_example，旧metrics老化参数留给历史路径。API/WS保留元数据，CSV增加statistics(JSON文本)、峰值和缓存诊断列。180秒帧的原始质量按帧生成时冻结；HTTP刷新/正常等待不把天气源年龄增加当成新故障，业务离线另按最近真实收包540秒判断。GPS年龄与接收年龄独立，不由传感器包刷新。旧3000ms记录仍沿原老化语义。布局不变，风速卡附“采样最大风速”；空值显示—，CALM显示静风，VARIABLE显示方向不稳定。

## 实际验证

- host：5种配置×2套真实App/HAL程序通过；另有AppStatistics专项、最大转义密钥/设备号容量、缺密钥、非法手动方向和零标定跨度检查。
- 专项覆盖中位数20/四值分数/少于4/陈旧缓存/有效0/身份与槽去重；2与4风速均值3、峰4；359/1北向、90/270 VARIABLE、CALM、配对独立性与80%边界；雨强2×30s+0×150s得到0.33，另有不等间隔、跨窗、长缺测、无效端点、回绕和迟到跳窗。
- 真实App虚拟tick验证首包180s/后续180s、无3s业务包、ACK等待不暂停采样、错误/容量失败不突发重试、TX不可变、早ACK/分片/溢出/恢复以及原天气CRC/查询/故障。
- 服务器55项unittest通过（含旧合同、schema1/3增量迁移、缓存、鉴权/ACK、GPS安全和独立年龄）；注入时钟验证读等待300→600→300秒，未实际睡眠三分钟。
- Node语法及3套逻辑回归通过：空值/0、PARTIAL/CACHED/CALM/VARIABLE、正常帧保持、540秒业务/GPS、历史标签。
- C→分片TCP→临时SQLite提交→API/CSV/WS→C ACK：24帧、43200轮100ms合成采样、4320虚拟秒，264个独立测量点，通过。帧2725～2781字节；最大转义凭据夹具4298字节。
- 当前ARMCC5全量编译链接：0错误/0警告；Code43764、RO1764、RW192、ZI13024。链接器静态最大栈720字节加未知路径；统计路径576字节（不等同实测栈）。架构、无控制台/无半主机/无浮点printf审计通过。
- 可选ARM CPU仿真因当前Unicorn不可导入未执行（更新后的仿真入口已单独通过ARM编译）；真实板卡/DTU/4G、采样实时性/长期运行、物理精度均未验证。没有用旧HEX或历史日志冒充本次运行。新建浏览器隔离预览进程被自动审批拦截，工具未给具体原因，故本轮浏览器目视验收未执行；前端语法与逻辑检查通过。

日志：tea_sense/Docs/validation/three_minute_{host,architecture,arm,wire,arm_execution}.log；tea_sense_server/validation/three_minute_{server,frontend}.log。全部网络和数据库模拟使用隔离库与临时端口。沿用现有.venv及依赖，未升级Python或要求现场升级3.12。

## 构建、启动与回滚

PowerShell，先在tea_sense目录运行：
~~~powershell
$env:PYTHONUTF8='1'
py -3 Tests/run_host_tests.py
py -3 Tests/check_architecture.py
py -3 Tests/check_arm_build.py
..\tea_sense_server\.venv\Scripts\python.exe Tests/check_server_upload.py
~~~
服务器目录运行：
~~~powershell
$env:PYTHONUTF8='1'
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
node --check web/app.js
node tests/test_dashboard.js
node tests/test_gps_dashboard.js
node tests/test_three_minute_dashboard.js
~~~

正式切换由用户选择时间：先用tools/backup_database.py按现有DATABASE_PATH生成新的SQLite一致性备份（目标必须不存在，命令见build_operations.md），保留.env和app_secrets.h；在原服务窗口停止唯一实例，再用根start_server.cmd启动更新后的服务器。首次启动才执行schema4迁移，本轮没有对生产库运行迁移。确认/api/config在线/GPS阈值540及历史数据，再手动Rebuild并下载当前固件，刷新网页。外部进程环境优先于.env，旧ONLINE_SECONDS环境变量需一并核对。

本轮执行前源码/私有配置备份：C:\Users\15995\AppData\Local\Temp\tea_three_minute_rucb_1fj。私有目录勿公开。更早的一致性生产库备份仍在integration_backups/gps_before_20261123_114236；切换前应再次备份以保留最新历史。回退时先停本项目写入，保留故障现场库及WAL/SHM；匹配回退固件/服务器/网页/.env。schema4仅加可空列，旧服务可忽略，优先保留现有库；若必须恢复旧库，先用backup_database.py verify检查，将当前库及sidecar整体保留到另一目录，再人工恢复至干净目标位置，勿混用不匹配sidecar。恢复旧库会丢失该备份之后记录，切勿清库试错。本轮不自动恢复。

## 用户手动调整DTU

MCU常量不能改变DTU定位周期。使用现有DTU图形配置工具，将定位刷新设180秒、定位上传设180秒，正式接板目标仅通道A，保存并重启；预期回读：
~~~
+LOCATION:ON,1,180,180
~~~
不提供未经本机型号确认的设置AT命令。本次没有修改DTU。原生GPS必须同一连接先由V2完成密钥鉴权及私有IMEI绑定；首个180秒V2前到达的GPS仍丢弃计诊断，可能下一定位周期才出现可接受位置。UTC解释和UNKNOWN坐标基准保持现有规则，无地图猜测转换。

## 后续实测边界

无雨微小正雨强仍按原标定保留，没有新增死区或零点修正；其长期积分影响需与真实降雨计量一起验证，本轮只做平均雨强。减少业务包及关闭打印不等于完成整机低功耗，需测MCU/DTU/探头实际供电、电流及峰值。本轮到此停止，不新增累计雨量或低功耗框架。

逐文件修改见three_minute_changed_files.json；保护核对与执行状态见three_minute_validation.json。
