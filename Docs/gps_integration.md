# DTU GPS 接入与实机核对

2026-11-23 当前业务180秒，业务在线/GPS过期540秒、认证空闲600秒，未认证及半JSON仍300秒。DTU图形配置需用户手动刷新180秒、上传180秒、目标仅通道A、保存重启，预期回读 +LOCATION:ON,1,180,180。本轮未设置DTU；首窗V2前GPS仍丢弃计诊断，可能下个定位周期才接受。详见[本次交付](three_minute_statistics.md)。

以下按原日期保留初次GPS来源和验证，历史60秒观测不改写为新参数。

更新：2026-11-23（北京时间）。本次只修改 `tea_sense_server` 及本说明；STM32、CubeMX、ADC/UART、100ms/3000ms 采样上传和 baseline-3 pH 标定保持原样。未烧录、未改 DTU 参数、未修改 cpolar、未重启现场服务、未对生产库执行迁移。已用 SQLite backup API 保存迁移前生产库与旧服务器源码/私有配置，见下文。

## 报文与数据流

用户现场信息：ZQWL-GCOM10X、V1.14、GPS 定位版，定位成功，现为 `+LOCATION:ON,32769,60,60`（串口与网络 A 同时输出，每 60 秒定位/上传）。传感器仍走原网络 A 的同一 TCP 入口。下例 IMEI 为虚构标识；实际绑定只在私有 `.env`，不会出现在公开示例、API、页面或日志中。

```json
{"addr":"1","cmd":"upload","IMEI":990000000000001,"time":"2026-11-23 3:17:21","gps":[114.384038,30.491893],"gsmloc":[0,0],"gsminfo":[460,0,28709,10004164]}
```

第二条观测为 `time=2026-11-23 3:18:21`、`gps=[114.384035,30.491890]`，相隔 60 秒。用户串口北京时间接收分别为 11:17:22、11:18:23，因此**针对当前 DTU/固件**把无时区 `time` 按 UTC 解释；第一条定位时间显示为北京时间 11:17:21。服务器接收时间由服务器实际收包时另行记录，不拿串口接收时间代替。

数据流：DTU → 原 TCP 接收器 → 有界 JSON 对象分帧 → 同连接 V2 鉴权及私有绑定 → 独立位置表 → `/api/location`、overview、原 WebSocket → 原监测页位置卡片。GPS 不经 MCU 轮询、不引入 NMEA/AT 驱动，不进入传感器三秒窗口、不平均坐标。

| 原生字段 / 来源 | 存储与 API 字段 | 说明 |
| --- | --- | --- |
| 私有 IMEI 绑定 | device_id | 必须等于当前 TCP 连接最近一次通过有效 V2 密钥校验的设备编号 |
| gps[0] | longitude | 经度，[-180,180]，保留接收数值精度；页面显示 6 位小数 |
| gps[1] | latitude | 纬度，[-90,90]，保留接收数值精度；页面显示 6 位小数 |
| 定位类型 | source=GPS | 原生报文无卫星数、精度、海拔，未补造这些字段 |
| 私有配置 | coordinate_system=UNKNOWN | 当前坐标基准未确认，不声明 WGS84/GCJ-02/BD-09 |
| time | fix_time_utc | 带 UTC 时区的标准 ISO 时间 |
| 服务器实际接收 | received_at_utc | **当前有效 fix 的第一次接收时间**；重复报文不会改写 |
| 最新已关联位置报文接收 | last_received_at_utc | 包含重复、迟到和无效报文；与 fix 时间分别计龄 |

`gsmloc=[0,0]` 不当位置使用；`gsminfo` 不解析为经纬度。本轮无地图标记，启用地图前必须确认 DTU 坐标基准及所用地图的输入约定，不做猜测转换。

## 接收、鉴权与位置状态

- 一个连接兼容无换行完整 JSON、CRLF、跨 recv 半包和粘连对象；跟踪嵌套和字符串转义，不用搜索第一个 `}` 切帧。保持8192字节对象上限、50连接、20帧/秒，未认证/半JSON各300秒、认证空闲600秒及错误限制；不完整输入不能靠持续滴入字节延长期限。
- V1/V2 仍走原结构、token 校验、提交后 ACK、seq/boot_id 去重。GPS 不产生 V2 ACK、seq 或传感器数据点。
- **GPS 必须同一 TCP 连接先通过有密钥的 V2 校验**，再匹配私有 IMEI→设备。V1、其他连接的认证、未知 IMEI、绑定不一致均不授权 GPS。连接认证前的 GPS 计数丢弃但不主动断线，后续 V2 仍可认证。
- `[0,0]` 为当前 DTU 的无定位占位；`[0,30]`、`[114,0]` 合法。拒绝布尔、非数值、NaN/Infinity、越界/错误数组和非法时间。超过服务器当前 UTC 300 秒的未来定位拒绝，可配置。
- 无效报文更新最近接收/异常诊断，保留上条有效位置。同设备按 fix_time 排序；迟到旧 fix 可保存为独立历史但不能覆盖当前位置。同设备同 fix_time 同坐标只计重复、不新增历史；同时间不同坐标标 `FIX_CONFLICT`，不改写原 fix。
- 位置状态为 `NO_DATA` / `FRESH` / `STALE`；默认 540 秒阈值，任一当前 fix 年龄或该 fix 首次接收年龄超过阈值即过期。`fix_age_seconds`、`received_age_seconds` 与 `last_received_age_seconds` 分开；新传感器包、GPS 重复/迟到包不会让旧 fix 恢复正常。
- `last_report_status` 保留最近位置报文结果；`latest_report_error` 提示本次异常。位置过期仍显示最后坐标和原定位时间。无有效位置显示“暂无定位”和空值，不显示地图零点。

## 私有配置与 API

当前固件设备编号确认仍为 `tea_h743_001`；真实绑定已按用户方案写入 `D:\12-Project\1-EC-Project\tea_sense_server\.env`，原 INGEST_TOKEN/VIEW_TOKEN 保留。公开示例仅示意，勿用虚构 IMEI 覆盖现场值：

```dotenv
# 此处 IMEI/设备均为虚构示例；真实值只填写在私有 .env
GPS_IMEI_BINDINGS={"990000000000001":"demo_gps_001"}
GPS_COORDINATE_SYSTEM=UNKNOWN
GPS_STALE_SECONDS=540
GPS_FUTURE_TOLERANCE_SECONDS=300
```

未配置绑定时位置接入关闭；有绑定却无 INGEST_TOKEN 时启动校验失败。`Settings.from_env` 仍遵循既有进程环境优先于 `.env` 的规则。Docker compose 已转发这四项；本机原启动方式不变，无新依赖、无 npm/CDN/地图 Key。

- `GET /api/location?device_id=tea_h743_001`：当前位置、独立年龄及接收/错误/重复/迟到计数；沿用 `X-View-Token` 权限。
- `GET /api/overview`：每个设备新增 `location`；顶层 `gps` 只有脱敏接收计数，不包含绑定。原设备在线时间只由传感器记录决定。
- `GET /api/config`：新增 GPS 开启状态、新鲜度阈值及坐标基准，不返回 IMEI/绑定/密钥。
- 原 `/ws` 新增 `{type:"location",device_id,location}`；无效位置也推送诊断。首次进入、刷新、WS 重连/队列 resync 后从 overview 恢复，客户端每秒独立计算定位年龄。GPS 不改变原 11 项、曲线和传感器 CSV。
- 日志 `tea.tcp` 记录设备、处理结果与 fix_time_utc；拒绝原因有 before_auth、unknown_imei、binding_mismatch、invalid 等，不打印原生 payload/IMEI/token。

## 数据迁移、备份、启动和回滚

SQLite `user_version` 从 2 增量升级到 3，只增加 `locations`、`location_receipts` 和一个当前位置索引；位置按 `(device_id,fix_time_utc)` 唯一。既有 readings / measurements / device_runs、历史 ID、payload 和传感器合同不改写。启动重复迁移幂等。**生产库仍为 schema2，首次运行更新服务器时才迁移。**

本轮已保存迁移前的一致性生产库备份（quick_check=ok、schema2）与旧服务器/私有 `.env`：

`D:\12-Project\1-EC-Project\integration_backups\gps_before_20261123_114236`

此回滚目录含私有配置，不作为公开交付物。既有用户备份未覆盖。

正式切换前可再次按当前配置备份；以下为 PowerShell，在服务器目录执行，使用新的目标名，工具拒绝覆盖已有备份：

```powershell
Set-Location 'D:\12-Project\1-EC-Project\tea_sense_server'
$stamp = [TimeZoneInfo]::ConvertTimeBySystemTimeZoneId([datetime]::UtcNow, 'China Standard Time').ToString('yyyyMMdd_HHmmss')
$backup = Join-Path '..\integration_backups' ('gps_' + $stamp)
New-Item -ItemType Directory -Path $backup
Copy-Item -LiteralPath '.env' -Destination (Join-Path $backup '.env')
$database = & '.\.venv\Scripts\python.exe' -c "from app.config import Settings; print(Settings.from_env().database.resolve())"
& '.\.venv\Scripts\python.exe' tools/backup_database.py backup --source $database --destination (Join-Path $backup 'tea_sense.db')
& '.\.venv\Scripts\python.exe' tools/backup_database.py verify (Join-Path $backup 'tea_sense.db')
```

确认备份成功。如果原实例在运行，在它自己的窗口 Ctrl+C 停止**本项目**旧服务；不结束其他 Python，不停止 cpolar，不重复占用 8000/9000。然后按既有方式启动唯一实例：

```powershell
& 'D:\12-Project\1-EC-Project\start_server.cmd'
```

保持该服务窗口运行。另一个 PowerShell 窗口检查：

```powershell
Invoke-RestMethod 'http://127.0.0.1:8000/healthz'
Set-Location 'D:\12-Project\1-EC-Project\tea_sense_server'
& '.\.venv\Scripts\python.exe' -c "import urllib.request; from app.config import Settings; c=Settings.from_env(); r=urllib.request.Request('http://127.0.0.1:8000/api/location?device_id=tea_h743_001',headers={'X-View-Token':c.view_token}); print(urllib.request.urlopen(r).read().decode())"
```

保持现有 HTTP/TCP 入口与 cpolar 映射；浏览器打开原监测地址并强制刷新以载入新版静态资源。没有真实 GPS 到达前显示“暂无定位”是预期，不运行模拟器向生产库造坐标。

回滚：先只停本项目服务；保留当前库及 WAL/SHM 和新软件/私有配置。旧源码/网页可从上述回滚目录按文件恢复；如需回到 schema2，使用 backup 工具把 `database_before.db` 备份到一个**全新的恢复路径**，verify 后将私有 `.env` 的 `DATABASE_PATH` 指向它，再启动旧代码。不要覆盖正在写入的库，不单独替换 `.db` 却留下不匹配 WAL/SHM，不清库。回滚到备份会回到该备份时点；升级后的当前库应保留以便后续处理。

## DTU 与 STM32 实机步骤（用户操作，尚未代执行）

1. USB-RS485 调试已确认自动 JSON 正常。正式接回 STM32 前，在现用 DTU 配置软件把定位上传目标改为“仅通道 A”，取消“串口”，保存并重启。
2. 核对预期回读 **`+LOCATION:ON,1,60,60`**。这里仅给核对结果，不猜测设置用 AT 参数数量，不声称已改好 DTU。
3. 取消串口定位输出能避免自动 JSON 混入 USART6 原 ACK 解析；GPS 仍由 DTU 直接走网络 A。断电后接回原 A6/B6，保持 USART6/DTU 的 115200、8N1 与现场已验证 A/B 极性。GPS 天线放回已实测可定位的位置。
4. 先从日志/overview 确认传感器 V2 正常入库、ACK 和鉴权，再等待下一条 60 秒 GPS。连接鉴权前先到的 GPS 会丢弃，下一周期重试即可；重连后也必须重新 V2 鉴权。
5. 用 `tea.tcp` 的 fix_time_utc、`/api/location` 和页面位置卡片核对**同一条**经度、纬度和定位时间。UTC 03:17:21 对应北京时间 11:17:21，只换算一次。确认 `source=GPS`、坐标基准 UNKNOWN，未将 `[0,0]` 作为位置。
6. 保持传感器上传，停止/遮挡定位后核对位置独立过期、旧坐标仍可识别；最新异常不会被旧成功位置掩盖。确认刷新和重连后最后位置仍可恢复。

## 验证记录与边界

在 `D:\12-Project\1-EC-Project\tea_sense_server` 使用现有 `.venv\Scripts\python.exe`（3.12.10）和已有 Node 执行，没有安装/升级 Python 或项目依赖：

```powershell
& '.\.venv\Scripts\python.exe' -m unittest discover -s tests -v
node --check web/app.js
node tests/test_dashboard.js
node tests/test_gps_dashboard.js
```

- **自动化已验证**：原36项加14项GPS测试，共 **50项全部通过**，退出码0；前端语法与两套Node检查均退出码0。覆盖两条现场格式的脱敏报文与北京时间、逐字节/CRLF/粘包/混合对象/转义/长度/未完成对象超时、同连接V2认证/未知绑定/错设备/重连/V1不授权、NaN/Infinity/布尔/范围/零占位/单轴零、无GPS ACK、原传感器ACK/去重、重复/迟到/冲突/异常、独立过期、schema2→3/幂等/历史保留/一致性备份及临时恢复、API/WS及重启恢复。库与网络端口均隔离。
- **模拟联调已验证**：用虚构IMEI、fixture凭据、临时SQLite与127.0.0.1随机TCP端口完成真实socket→事务提交→API/WS链路；另用 `tests/preview_gps.py` 在18763/19763做浏览器隔离预览（不加载真实.env）。过期画面由测试夹具将定位时钟后移240秒准备，随后传感器仍继续上传；该时钟调整仅作用于临时库，不是生产数据处理逻辑。
- **页面已检查**：浏览器实际检查“位置正常”、传感器仍近期收到数据时“位置已过期”、旧坐标与“最新定位报文异常”同时保留、刷新恢复、多设备切换到无定位后显示空值；实际视口417×708，无水平溢出，检查时无浏览器控制台错误。截图是模拟器证据，不是实机。
- **实际DTU待验证**：没有真实DTU经4G/公网进入更新服务器的证据；未改DTU、未自动重启现场服务、未宣称实机接通。
- **地图待确认**：当前 `coordinate_system=UNKNOWN`，尚缺DTU坐标基准与目标地图输入约定。

证据：[服务器测试](../tea_sense_server/validation/gps-server-tests.log)、[前端检查](../tea_sense_server/validation/gps-frontend-tests.log)、[页面记录](../tea_sense_server/validation/gps-browser-results.json)、[正常截图](../tea_sense_server/validation/gps-fresh-preview.jpg)、[过期截图](../tea_sense_server/validation/gps-stale-preview.jpg)、[旧位置与异常截图](../tea_sense_server/validation/gps-error-preview.jpg)。

## 实际变更文件

路径相对 `D:\12-Project\1-EC-Project`：

| 文件 | 变更 |
| --- | --- |
| tea_sense_server/app/json_stream.py（新增） | 跨recv的有界完整JSON对象分帧 |
| tea_sense_server/app/location.py（新增） | 原生定位字段、UTC、坐标/占位/未来时间校验 |
| tea_sense_server/app/tcp_server.py | 混合报文路由、同连接V2认证、私有绑定、GPS诊断/事件，无GPS ACK |
| tea_sense_server/app/config.py | 私有绑定/坐标基准/180秒阈值/未来容差配置 |
| tea_sense_server/app/storage.py | 独立位置/报文诊断表，增量schema3、去重/排序/状态 |
| tea_sense_server/app/main.py | 位置API、overview、脱敏配置与计数，沿用WS鉴权/队列 |
| tea_sense_server/web/index.html、app.js、style.css | 新增位置卡片、北京时间、独立过期、异常及设备切换 |
| tea_sense_server/.env（私有） | 仅新增GPS真实绑定/策略，原密钥、端口和数据库路径保留 |
| tea_sense_server/.env.example、compose.yaml | 脱敏配置示例及容器变量传递；默认空绑定关闭GPS |
| tea_sense_server/tools/backup_database.py | 校验/备份结果增加两个位置表计数 |
| tea_sense_server/tests/test_gps.py（新增） | 14项GPS解析/接收/存储/API/WS/恢复检查 |
| tea_sense_server/tests/test_gps_dashboard.js（新增） | 前端位置/时区/精度/过期/异常/空值检查 |
| tea_sense_server/tests/preview_gps.py（新增） | 隔离GPS浏览器测试夹具 |
| tea_sense_server/tests/test_server.py | 半包EOF断言改用真正不完整JSON（完整无换行对象现可接收） |
| tea_sense_server/tests/test_rain.py、test_v2.py | 迁移版本预期改为3，其余旧行为断言保留 |
| tea_sense_server/README.md、Docs/README.md、Docs/gps_integration.md | 入口、运行说明、字段/状态、备份/回滚与实机步骤 |

本轮没有初始化Git、提交、烧录、修改固件文件或替换历史数据库。修改范围与密钥保护记录见[保护审计](../tea_sense_server/validation/gps-scope-audit.json)。
