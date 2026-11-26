# Tea Sense 当前服务器与前端
2026-11-23。FastAPI/SQLite/原生JS，复用现有.venv及依赖，未升级Python或要求现场升级。设备自身180秒发送，服务器收到即解析/入库/推送。
支持V1、旧3000ms V2和新180000ms分类统计V2；statistics、PARTIAL/CACHED/CALM/VARIABLE及风速附属峰值，增量schema4只加可空元数据列，旧ID/payload/语义不改。业务/GPS过期各540s，认证空闲600s，未认证及半JSON各300s。GPS同连接鉴权/私有绑定及UTC不变。

前端在web/index.html/app.js/style.css，由原服务提供，无需npm/CDN。默认 http://127.0.0.1:8000，TCP9000。运行根start_server.cmd或本目录 .venv/Scripts/python.exe -m app.main；已有实例勿重复启动。本轮未停止/重启现场服务，代码未热加载。保留.env/密钥/绑定/DATABASE_PATH，勿用.example覆盖。

[本次参数/方法/验证/构建启动回滚/DTU人工步骤](../Docs/three_minute_statistics.md)、[协议](../Docs/protocol_v2.md)、[运维](../Docs/build_operations.md)。55项服务器测试、Node语法及3套逻辑通过，日志validation/three_minute_*；隔离C/TCP/SQLite/API/CSV/WS/C ACK通过，非实物数据。
测试命令：.venv/Scripts/python.exe -m unittest discover -s tests -v；node --check web/app.js；node tests/test_dashboard.js；node tests/test_gps_dashboard.js；node tests/test_three_minute_dashboard.js。
tools/send_sample.py的snapshot和旧preview_*保留三秒夹具以测历史兼容；不得向正式库写模拟数据。当前已有临时预览未被本轮停止，不代表新固件。tools/backup_database.py提供一致性备份/verify，拒绝覆盖目标。切换顺序为用户先备份/更新服务器，再手动下载固件。
