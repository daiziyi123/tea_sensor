# Tea Sense 本地十一项接收服务器
更新：2026-11-15。FastAPI+SQLite+原生JavaScript，网页无需npm或CDN，固件为并列tea_sense。
当前统一权威文档：[交付索引](../Docs/README.md)、[V2协议](../Docs/protocol_v2.md)、[构建运维](../Docs/build_operations.md)、[本轮验收](../Docs/acceptance_records.md)。

从共同根运行start_server.cmd，或本目录.venv/Scripts/python.exe -m app.main；单进程，勿启用reload/workers或重复启动。网页http://127.0.0.1:8000，TCP127.0.0.1:9000，健康/healthz。原.env/密钥/DATABASE_PATH/数据和cpolar不变；不以.env.example覆盖现有配置。
Python3.12.10及原requirements-dev.txt当前已验证；跨目录/系统不复制.venv，保留原环境和私有配置后按运维文档重建。VIEW_TOKEN开启时API使用X-View-Token，WS首条鉴权，凭据不放URL。
~~~
.venv/Scripts/python.exe -m unittest discover -s tests -v
node --check web/app.js
node tests/test_dashboard.js
~~~
36项服务器测试及Node回归见共同Docs/rain_test_results.json。tools/send_sample.py为demo V2模拟器，默认3秒、可选分片与boot_id，从显式进程环境取fixture凭据，不加载.env；不要向真实库写模拟数据。
tests/preview_dashboard.py仅隔离临时库/18000/19000，不加载.env；本次十一项页面验证见共同Docs/rain_dashboard_*.jpg，均为模拟器证据，非实板验收。
tools/backup_database.py提供显式SQLite一致性备份/验证，拒绝覆盖目标；具体命令/人工恢复边界见构建运维文档。本轮只对临时库演练，未恢复生产数据。

雨强rain_intensity_mm_min为mm/min、范围0～8，3秒/9秒新鲜度。严格接受十一项新V2及缺雨强的十项旧V2；缺失为NO_DATA/null身份、不造历史0。复用通用measurements，SQLite schema仍2，无结构升级/清库。保留V1、鉴权、去重和历史ID/payload。网页增加第五主卡和对应趋势/历史/CSV/WS，正常4.00、有效0.00，失败/未采集/陈旧显示—。

**先服务器、后固件**：工作区文件已更新，但正在运行的旧进程未自动重启；在原窗口Ctrl+C，仅停本项目，再运行根start_server.cmd；确认/api/config含rain_intensity_mm_min后，用户手动编译下载0.9.1-dev/baseline-2。刷新浏览器以加载新静态资源。详细顺序和私有配置/数据库保护见共同Docs/build_operations.md。不向真实库运行send_sample或preview。
