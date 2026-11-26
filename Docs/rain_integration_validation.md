# 雨强接入、验证与交付边界

更新：2026-11-15。当前源码0.9.1-dev / schema_version=2 / baseline-2。已完整阅读本次附件，以当前本机两份工程为基准原地修改；执行前备份由根rain_backup_path.txt定位。此前架构/恢复和原四路系数保留，旧包、备份、raw阶段日志仍在原处。

## 换算与职责

AD5/AIN5→PA3→ADC1 Channel15，RAIN_AD5、Analog/NOPULL，轮次保持5→3→19→18→15→下一轮5。Bsp/生成代码、时钟/串口和ADC配置不改，现有100ms每轮AD5读一次。

用户0V组84条已打印OK记录：sum12078、mean143.7857142857、min0/max666；40个有效零全部计入。5V组66条：sum2065968、mean31302.5454545455、min30506/max31823。仅是一秒打印日志，不能称已取得所有100ms数据。日志提供日期2026-11-15，实际测量日期/仪表型号/精度/操作者未记录。端点×100为14379/3130255、跨度3115876，原数组和复算脚本在Tests。

令R为正式三秒窗口通过至少24/30有效门槛后，恰好去掉一个最大和一个最小的精确raw分数均值：

~~~text
降雨强度(mm/min) = clamp(8 × (R − 143.79) / 31158.76, 0, 8)
~~~

SensorRain转换与窗口操作是真实生产入口，没有Init/Read转发壳、HAL/App依赖或额外ADC。signed64保留sum/count精度，分拆商和余数避免delta×800溢出，仅最终mm/min×100舍入。LOW/HIGH按未舍入未限幅结果判断；精确端点为OK；有效raw0为有效0.00/LOW，失败/有效不足为无效，不复用上次值。没有死区、自动归零、累计量或雨强分级；零端噪声可能有小幅非零，不能承诺每窗0.00。

## 文件与跨端映射

| 层次 | 当前修改位置/行为 |
|---|---|
| Sensors | 新sensor_rain.h/.c：独立常量、Convert、RangeName及WindowReset/Add/Finish，保留计数/extrema/sum和主故障/prepare/config/cleanup |
| App | app_main.c复用第五次ADC读数入窗、同边界结算/迟到/回绕，中文printf与提交同一私有快照；app_data.h增加rain_intensity_mm_min（内部x100），app_diagnostics.h增加last_rain_window |
| 编码/配置 | app_upload.c增加唯一第十一物理字段，原十键/TX/ACK路径不改；app_config.h删raw打印周期；app_version.h仅本次递增dev/参数集 |
| MDK | uvprojx/uvoptx各接入sensor_rain.c一次，保持标准Arm C库/启动/链接配置 |
| 服务器 | protocol.py指标/严格可选字段；storage.py旧十项查询补NO_DATA和旧boot重试兼容，原通用measurements/schema2不改表、不重写旧payload/ID |
| 前端 | app.js新指标，index.html说明，style.css五主卡布局；原指标选择/历史/CSV/WS和9s新鲜度复用，mm/min独立0～8轴 |
| 测试/模拟器 | 新rain_cases.h、check_rain_calibration.py、rain_calibration_samples.json、test_rain.py；现有host/ADC/ARM/C回环/Node/架构/模拟器断言扩为十一项；preview仅隔离临时库，重启能继续新身份 |
| 文档/发布 | 共同六份现行资料、架构/服务器入口/根索引同步；Tools/package_release.py要求新模块/测试并收录本次证据；旧事实日志不改写 |

精确实际新增/改动列表以rain_changed_files.json为准。Sensors rain_intensity_mm_min_x100→App rain_intensity_mm_min.value（内部x100）→JSON data.rain_intensity_mm_min.value（物理数字）→SQLite measurements.metric同名→API/CSV/WS同名→网页“降雨强度 mm/min”。不是累计雨量mm，不将0～5V直接等同PA3可承受电压。

## 实际格式

每三秒USART2输出一整行UTF-8、CRLF，直接整数拆分printf；有效示例来自软件测试：

~~~text
土壤湿度：60.85%，风速：15.00 m/s，风向角度：134.83°，土壤 pH：6.00，降雨强度：4.00 mm/min
土壤湿度：60.85%，风速：15.00 m/s，风向角度：134.83°，土壤 pH：6.00，降雨强度：采集失败
~~~

保留一次启动摘要0.9.1-dev/V2/baseline-2和气象物理行、printf错误/clearerr/串口恢复；删除AD5_SAMPLE/无用1000ms配置，无raw/电压/完整JSON常规日志、无浮点printf。上传每3秒仍一份完整快照，片段为：

~~~json
"rain_intensity_mm_min":{"value":4.00,"status":"OK","sample_seq":12,"sample_t_ms":36000,"age_ms":0,"range":"OK"}
~~~

有效不足为value:null/status:WINDOW_INSUFFICIENT、保留实际窗口身份且无range。旧十项缺失为NO_DATA，value/身份/年龄全null，无历史0点。旧V1和其他十项语义保留，其他未知键、bool、非有限/非法状态值仍拒绝。详见protocol_v2.md。

## 本次软件证据

最终执行命令、实际退出码/时间见rain_test_results.json；日志均以rain_命名，与旧release_*、ad5_*证据分开。

| 可执行验证 | 实际覆盖/结果 |
|---|---|
| Host | 五配置×soil/dtu共10程序；2000ms同步变体；端点0/4/8、LOW/HIGH、有效0/65535、分数均值、全raw域及UINT32_MAX宽计数、24/23门槛、重复极值、零有效/错误诊断、窗口清空/迟到/tick回绕；原风向/气象/ADC/TX恢复回归通过 |
| 缓冲边界 | 十一项长状态/最大身份、截断哨兵、缺凭据禁发送；256字符全部需JSON转义的fixture token+64字符ID生成2795字节报文，低于4094；单4096 TX及512 ACK不扩大 |
| 架构/MDK | 三层边界、PA3模拟/旧雨量EXTI撤销、五路显式选通、唯一源；当前全量ARMCC Rebuild 0error/0warning；无浮点printf/半主机 |
| ARM CPU | 测试main/fixture凭据，真实业务对象/标准库；182轮、910选通、1820 Stop、一次校准、6完整十一项上传/ACK；29有效去两点和22不足、范围/有效0/中文同窗一致；应用CPU调用栈640/1024字节（不含真实ISR抢占） |
| C端到端 | 每路720个100ms合成样本/72虚拟秒，24个C帧→分片临时TCP→提交临时SQLite→API/CSV/WS→真实C ACK；152个独立点，其中雨强24；正常/失败/配置错误不假零；典型帧1222～1242字节 |
| 后端/Node | 36项服务器unittest及Node逻辑/语法；旧十项/十一项、NO_DATA无点、有效0、null、严格性、同身份去重/同值新点、陈旧、旧payload/ID/schema2/重复初始化、旧boot重试；既有备份/回归均保留 |
| 浏览器 | 1920×1080、1280×900、390×844及全屏；正常4.00、有效0.00、失败/陈旧/旧缺失—；雨强0～8轴/历史/CSV/自动重连恢复，未恢复raw；截图rain_dashboard_*.jpg |
| 发布 | 固定规则的6项fixture及当前包SHA256/逐文件完整性，当前归档的隔离解包host/架构验证也已通过；私有配置/数据库/历史备份不打入包 |

浏览器下载fixture CSV1294行，1273行4.0、5行有效0.0、9行WINDOW_INSUFFICIENT、7行STALE；单位均mm/min，与测试数据对应。先正常阶段无浏览器error；主动停止临时服务时预期fetch/WS错误，随后自动恢复4.00且保留所选设备/指标。复核发现预览脚本自身重启会重用旧fixture身份，已改为读取临时库最大seq并用新会话boot身份，再实际重启验证；未改生产重连策略。测试监听仅18000/19000，结束后停止；未接触生产.env/DB。

## 发布和人工联调顺序

1. 先保留现有配置、历史库和旧软件；需要数据库备份时按build_operations.md用backup API写新目标，不覆盖现有库。
2. **先更新服务器**：文件已完成；正在运行的生产旧实例本轮未重启。用户在原窗口Ctrl+C停唯一实例，再运行根start_server.cmd，仍使用原.env及DATABASE_PATH。网页http://127.0.0.1:8000随服务启动，无需npm；刷新并核对/api/config含雨强、旧十项仍可用。
3. **再手动下载固件**：MDK Rebuild tea_sense目标，确认0.9.1-dev/baseline-2及本次产物；用户手动下载，不使用历史HEX。新源码预览包不含私有配置或HEX/AXF，现场需私下配置/受控固件。
4. 对齐同窗串口雨强、ACK/API、网页和CSV，再开展真实电压中点、喷淋/标准雨强、4G、断线/重启和持续运行验收，记录真实仪表、设备和时间。

新包Releases/rain-source-preview-20261115，原包/备份保留。源码受控清单和构建哈希见rain_release_record.json；ZIP SHA256在旁置文件，非数字签名。

## 未验证项

实板PA3输入/供电/线束/ADC噪声和时间、真实ISR/栈高水位、实际串口中文与printf恢复、4G/cpolar真实上传、实际服务器升级和DTU恢复、断电/重启/断网、24～72小时持续运行、雨强中点线性/定量喷淋/标准雨强精度均待执行。实物型号、仪表信息和现场供电仍待确认。
本次只是用户电气端点＋声明量程的线性换算，未完成实际降雨精度认证。没有部署云端、自动烧录、覆盖真实数据库、自动恢复/重启生产服务、开发累计量或气体功能。
