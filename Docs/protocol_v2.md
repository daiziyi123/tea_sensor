# 当前V2协议与采样身份
2026-11-23。完整参数、字段单位、测试及切换步骤见[本次交付](three_minute_statistics.md)。当前实现为App/app_data.h、app_statistics.c、app_main.c、app_upload.c及服务器protocol.py/storage.py/tcp_server.py/web/app.js。

## 当前帧与分类元数据
USART6透明TCP，JSON+CRLF；接收器兼容无换行完整对象/半包/粘包。包络schema_version=2、device_id、seq、t_ms、window_ms、data、token不变，boot_id仍可选，现固件不发。seq计发送尝试，t_ms是计划周期末tick，默认window_ms=180000；初始化完成后首个完整180秒才出首包。11个物理键、单位及密钥不变。
每项value/status/sample_seq/sample_t_ms/age_ms不变，适用有效range保留。OK/PARTIAL/CACHED可带有限数值；NO_DATA身份年龄为空；其他故障及WINDOW_INSUFFICIENT/STALE/CALM/VARIABLE为空，无range。有效0严格保留。CALM/VARIABLE只用于风向，不伪造北向0。

180秒帧每项均有statistics.method：
- 土壤湿度/pH：median，最近5000ms，每秒最多一个真实样本，各5槽，至少4有效、最新参与源年龄≤2000ms。5值取中间，4值取中间两raw精确分数后沿原标定；sample_seq/t_ms保留最新参与源。
- 风速：mean，三分钟有效物理样本算术平均，含合法0和高值；有效槽/1800≥80%。同对象附wind_speed_sample_max_mps“采样最大风速”，主值无效时也null，不是gust/第12路。
- 风向：circular_mean，先检查配对覆盖；均速<0.3为CALM，达到阈值的方向至少3个，sin/cos/atan2，R<0.2为VARIABLE；360舍入归0。
- 雨强：time_weighted_mean，前后有效且间隔≤300ms才确认前值保持区间，跨窗拆分，缺测不补0；有效时长≥80%，不新增累计量。
- 六气象量：latest，原查询周期10/30/60s和三倍源过期不变，按字段保留最近有效真实查询。

非latest的statistics完整字段：method/window_ms/start_ms/end_ms/expected_count/valid_count/used_count；rain另有valid_duration_ms。start/end是tick，差与window_ms相符；median预期5、周期1800。median.used_count为中间1或2个值；风向used_count为有风方向数。周期sample_seq/t_ms是封存统计结果身份。覆盖达标未满为PARTIAL，不能标OK。
latest只有method，已完成查询附cached及latest_query_status/seq/t_ms；缓存sample_seq/t_ms不改。最新失败、旧缓存未过期可CACHED；过期STALE/null。无成功缓存用真实失败身份，尚未完成为NO_DATA；缓存不重复产生历史点。

## ACK、数据库、页面与GPS
提交后原ACK：ok=true、device_id、seq、正整数id，可有duplicate；固件严格解析类型/重复字段/设备/序号。TC后10秒ACK、早ACK/分片/溢出及故障恢复不变。Bsp唯一TX缓冲6144字节，可编码6142（恢复前缀2字节），逐次snprintf及静态凭据预算检查；无新增TX副本、重试或离线队列。明确boot_id重试保持幂等，无boot_id跨重启重放仍有原不可区分限制。

SQLite增量schema4只给measurements加可空result_metadata，旧payload/ID/三秒含义不重写。V1、旧3000ms十/十一项仍兼容；旧点不补标三分钟。新周期以业务窗口身份存储，latest仍按源身份去重。API/WS携带元数据/峰值/缓存诊断；CSV statistics为JSON文本。
overview对180秒帧返回quality_frozen：正常等待不变成新采集故障，源年龄仍增加，下帧可报告STALE。业务在线540s；GPS独立540s，fix/首次接收/最新报文接收分开计龄。旧3000ms结果仍按原老化规则。认证后空闲600s；未认证/半JSON期限300s独立保留，滴入字节不能延长半包期限。8192对象/50连接/20帧每秒不变。

GPS仍同连接先V2密钥鉴权和私有IMEI绑定，无GPS ACK/seq/坐标平均，UTC及UNKNOWN基准不变；首窗认证前GPS丢弃计诊断，可能下一180秒周期才接受。DTU须用户手动图形设置，见交付文档。
当前USART2应用打印全部关闭，无原始AD、中文物理行或启动摘要，保留内部诊断；不因打印触发上传。唯一版本来源app_version.h：0.10.0-dev/V2/baseline-3；本次未改标定，不递增标定修订。
