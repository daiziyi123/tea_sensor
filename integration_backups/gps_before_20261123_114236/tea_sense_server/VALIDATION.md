> 当前雨强接入（2026-11-15）：36项服务器unittest、Node逻辑/语法、C→临时TCP/SQLite/API/CSV/WS/ACK及十一项真实页面布局/状态/自动重连验证通过；详见共同Docs/rain_integration_validation.md和rain_test_results.json。下文旧轮次数与截图保持历史，不改写为当前结果。实板/4G/真实雨强/持续运行待执行。

> 本轮稳定化/四项补充结果（2026-11-15）统一见[验收记录](../Docs/acceptance_records.md)和release_test_results.json；当前31项服务器测试通过。下文前次软件/页面验证按原日期保留，不作为本次实板证据。

# 本次验证（2026-11-14）

27项unittest通过，原16项保留；新增V2严格类型/十字段/失败null/迁移幂等/缓存去重/年龄/回绕与运行段/分页CSV。Node语法和逻辑通过。1920×1080、1280×900、390×844检查通过，十卡、有效0、独立字段失败、过期灰显、单位切图、全屏、显式demo及CSV下载无浏览器错误。截图tests/dashboard_*.jpg；CSV物理列和值与API一致。

跨工程真实C格式化/分片TCP/临时SQLite/鉴权API与CSV/C ACK通过：24上报/128独立测量。仅fixture/demo/临时库，无模拟写正式库。固件MDK/ARM执行见../tea_sense/Docs/integration_v2.md。

未验证上板、电气、ISR实时负荷、AUTO方向、4G/cpolar实链路和实际精度；未自动烧录。
