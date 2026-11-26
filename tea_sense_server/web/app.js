"use strict";
const REAL_DEVICE = "tea_h743_001";
const isDemo = id => /^demo(?:_|$)/i.test(id || "");
const escapeHtml = value => String(value ?? "—").replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
const DEFAULT_METRICS = {
    soil_moisture_pct: { name: "土壤湿度", unit: "%", decimals: 2, period_ms: 3000, stale_ms: 9000, minimum: 0, maximum: 100, group: "main" },
    soil_ph: { name: "土壤pH", unit: "", decimals: 2, period_ms: 3000, stale_ms: 9000, minimum: 3, maximum: 9, group: "main" },
    wind_speed_mps: { name: "风速", unit: "m/s", decimals: 2, period_ms: 3000, stale_ms: 9000, minimum: 0, maximum: 30, group: "main" },
    wind_direction_deg: { name: "风向角度", unit: "°", decimals: 2, period_ms: 3000, stale_ms: 9000, minimum: 0, maximum: 360, group: "main" },
    rain_intensity_mm_min: { name: "降雨强度", unit: "mm/min", decimals: 2, period_ms: 3000, stale_ms: 9000, minimum: 0, maximum: 8, group: "main" },
    temperature_c: { name: "气温", unit: "℃", decimals: 1, period_ms: 10000, stale_ms: 30000, group: "weather" },
    air_humidity_rh_pct: { name: "空气湿度", unit: "%RH", decimals: 1, period_ms: 10000, stale_ms: 30000, group: "weather" },
    light_lux: { name: "光照", unit: "lx", decimals: 0, period_ms: 10000, stale_ms: 30000, group: "weather" },
    pressure_kpa: { name: "气压", unit: "kPa", decimals: 2, period_ms: 60000, stale_ms: 180000, group: "weather" },
    pm25_ug_m3: { name: "PM2.5", unit: "μg/m³", decimals: 0, period_ms: 30000, stale_ms: 90000, group: "weather" },
    pm10_ug_m3: { name: "PM10", unit: "μg/m³", decimals: 0, period_ms: 30000, stale_ms: 90000, group: "weather" }
};
function chooseDevice(devices, current = "", explicit = false) {
    if (explicit && current) return current;
    if (devices.some(d => d.device_id === REAL_DEVICE)) return REAL_DEVICE;
    return devices.find(d => !isDemo(d.device_id))?.device_id || REAL_DEVICE;
}
function freshness(reading, nowMs, onlineSeconds, apiError = false) {
    const received = Date.parse(reading?.received_at || "");
    if (!Number.isFinite(received)) return { age: null, fresh: false, text: "尚未收到数据", kind: "neutral" };
    const elapsed = Math.max(0, nowMs - received), age = Math.floor(elapsed / 1000);
    if (apiError) return { age, fresh: false, text: "数据未刷新 · 历史最后值", kind: "error" };
    return elapsed > onlineSeconds * 1000 ? { age, fresh: false, text: "显示最近一次采集值", kind: "neutral" } : { age, fresh: true, text: "近期收到数据", kind: "fresh" };
}
function physicalResult(reading, metric) {
    if (reading?.data) return reading.data[metric] || { value: null, status: "NO_DATA" };
    if (reading && Object.hasOwn(reading, "value")) return reading;
    if (metric === "soil_moisture_pct" && reading) return { value: reading.moisture_pct, status: reading.status, range: reading.range };
    return { value: null, status: "NO_DATA" };
}
function displayReading(reading, metric = "soil_moisture_pct", spec = DEFAULT_METRICS[metric]) {
    const result = physicalResult(reading, metric);
    const valid = ["OK", "PARTIAL", "CACHED"].includes(result.status) && typeof result.value === "number" && Number.isFinite(result.value);
    const text = valid ? result.value.toFixed(spec?.decimals ?? 2) : "—";
    return { text, moisture: text, unit: spec?.unit || "", status: result.status || "NO_DATA", valid, value: valid ? result.value : null };
}
function metricFreshness(result, spec, elapsed = 0) {
    const age = result?.age_ms == null ? null : Math.max(0, result.age_ms + elapsed);
    const stale = result?.status === "STALE" || (!result?.quality_frozen && result?.status === "OK" && age != null && age > spec.stale_ms);
    const valid = ["OK", "PARTIAL", "CACHED"].includes(result?.status) && !stale && typeof result.value === "number" && Number.isFinite(result.value);
    return { valid, stale, age, status: stale ? "STALE" : result?.status || "NO_DATA" };
}
function uniqueReadings(rows) {
    const seen = new Set();
    return rows.filter(row => { const key = (row.metric || "soil_moisture_pct") + ":" + row.id; if (seen.has(key)) return false; seen.add(key); return true; });
}
function windowInfo(reading, metric = "soil_moisture_pct") {
    const result = physicalResult(reading, metric);
    const status = result.status || "NO_DATA";
    const method = result.statistics?.method;
    const labels = { median: "短窗中位值", mean: "三分钟平均", circular_mean: "三分钟圆周平均", time_weighted_mean: "三分钟平均雨强", latest: "最近查询" };
    const states = { OK: "测量有效", PARTIAL: "部分有效", CACHED: "缓存有效", CALM: "静风", VARIABLE: "方向不稳定", STALE: "暂无新值", NO_DATA: "未采集", INVALID_VALUE: "数据无效", WINDOW_INSUFFICIENT: "样本不足" };
    let quality = states[status] || "采集失败";
    if (method) quality = (labels[method] || method) + " · " + quality;
    if (result.cached && result.latest_query_status !== "OK") quality += " · 最近查询 " + (result.latest_query_status || "失败");
    return { label: DEFAULT_METRICS[metric]?.name || metric, quality, warning: status !== "OK" };
}
function chartSegments(rows, device, gapMs, metric = "soil_moisture_pct") {
    const ordered = uniqueReadings(rows).filter(r => r.device_id === device && (!r.metric || r.metric === metric)).slice().sort((a, b) => Date.parse(a.received_at) - Date.parse(b.received_at) || a.id - b.id);
    const segments = []; let current = [], previous = null;
    for (const row of ordered) {
        const time = Date.parse(row.received_at), shown = displayReading(row, metric);
        const valid = shown.valid && Number.isFinite(time);
        if (!valid || (previous && (time - Date.parse(previous.received_at) > (row.statistics ? Math.max(gapMs, 540000) : gapMs) ||
            (metric === "wind_direction_deg" && Math.abs(shown.value - displayReading(previous, metric).value) > 180)))) {
            if (current.length) segments.push(current); current = [];
        }
        if (valid) current.push(row);
        previous = valid ? row : null;
    }
    if (current.length) segments.push(current);
    return segments;
}
function shortestAngle(previous, next) { return previous + ((next - previous + 540) % 360) - 180; }
function formatLocationTime(value) {
    const time = Date.parse(value || "");
    return Number.isFinite(time) ? new Intl.DateTimeFormat("zh-CN", { timeZone: "Asia/Shanghai", year: "numeric", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false }).format(new Date(time)) : "—";
}
function locationView(location, nowMs = Date.now(), staleSeconds = 540, apiError = false) {
    const fix = Date.parse(location?.fix_time_utc || ""), received = Date.parse(location?.received_at_utc || "");
    const lastReceived = Date.parse(location?.last_received_at_utc || "");
    const age = time => Number.isFinite(time) ? Math.max(0, (nowMs - time) / 1000) : null;
    const valid = typeof location?.longitude === "number" && Number.isFinite(location.longitude) && Math.abs(location.longitude) <= 180 &&
        typeof location?.latitude === "number" && Number.isFinite(location.latitude) && Math.abs(location.latitude) <= 90 &&
        !(location.longitude === 0 && location.latitude === 0) && Number.isFinite(fix) && Number.isFinite(received);
    const stale = valid && (age(fix) > staleSeconds || age(received) > staleSeconds || location.status === "STALE");
    const errors = { NO_FIX: "本次暂无有效定位", INVALID_COORDINATES: "坐标无效", INVALID_TIME: "定位时间无效", FUTURE_TIME: "定位时间异常", INVALID_COMMAND: "报文类型无效", FIX_CONFLICT: "同一定位时间的坐标冲突" };
    return {
        longitude: valid ? location.longitude.toFixed(6) : "—",
        latitude: valid ? location.latitude.toFixed(6) : "—",
        rawLng: valid ? location.longitude : null,
        rawLat: valid ? location.latitude : null,
        status: apiError ? "位置未刷新" : !valid ? "暂无定位" : stale ? "最近一次定位" : "位置正常",
        kind: apiError ? "error" : !valid ? "neutral" : location.latest_report_error ? "warning" : stale ? "neutral" : "fresh",
        fixTime: valid ? formatLocationTime(location.fix_time_utc) : "—",
        fixAge: valid ? age(fix) : null,
        receivedAge: age(lastReceived),
        source: valid ? "GPS" : "—",
        coordinateSystem: valid ? (location.coordinate_system === "UNKNOWN" ? "未确认" : location.coordinate_system || "未确认") : "—",
        error: location?.latest_report_error ? "最新定位报文异常 · " + (errors[location.last_report_status] || "数据无效") : ""
    };
}

// ---------- Leaflet 地图（懒加载） ----------
let _map = null, _marker = null, _mapReady = false;
function updateLocationMap(position) {
    const container = document.getElementById("location-map");
    if (!container) return;
    const valid = position.rawLng !== null && position.rawLat !== null;
    if (!valid) {
        container.classList.remove("has-data");
        return;
    }
    // 首次有有效坐标才初始化地图
    if (!_mapReady) {
        if (typeof L === "undefined") return; // Leaflet 还没加载好
        container.classList.add("has-data");
        _map = L.map("location-map", { zoomControl: true, attributionControl: true })
            .setView([position.rawLat, position.rawLng], 15);
        L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
            maxZoom: 19,
            attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>'
        }).addTo(_map);
        _marker = L.marker([position.rawLat, position.rawLng]).addTo(_map);
        _mapReady = true;
        // Leaflet 容器尺寸可能在初始化时还没稳定，强制重算
        setTimeout(() => _map && _map.invalidateSize(), 200);
        return;
    }
    // 已初始化：更新 marker 位置 + 视野居中
    const latlng = [position.rawLat, position.rawLng];
    _marker.setLatLng(latlng);
    const cur = _marker.getLatLng();
    _marker.bindPopup(
        `<b>设备 GPS</b><br>经度 ${position.longitude}<br>纬度 ${position.latitude}<br>${position.fixTime}`
    );
    // 如果坐标变化超过 ~50m 则平移视野；变化小则不打扰用户缩放
    const prevCenter = _map.getCenter();
    const dist = _map.distance(prevCenter, L.latLng(latlng));
    if (dist > 50) _map.panTo(latlng);
}
if (typeof module !== "undefined" && module.exports) module.exports = { chooseDevice, freshness, displayReading, chartSegments, isDemo, uniqueReadings, windowInfo, metricFreshness, shortestAngle, DEFAULT_METRICS, locationView, formatLocationTime };
if (typeof document !== "undefined") {
    const $ = id => document.getElementById(id);
    const state = {
        config: null, overview: null, rows: [], device: REAL_DEVICE, explicit: false, metric: "soil_moisture_pct", hours: 24, offset: 0,
        fetchedAt: 0, token: sessionStorage.getItem("tea_view_token") || "", ws: null, reconnect: null, loading: false, again: false, generation: 0,
        history: [], cursor: null, historyGeneration: 0, apiError: false, hasMore: false, angle: 0, page: "overview",
        viewStart: null, viewEnd: null, historyMetric: "soil_moisture_pct"
    };
    const clock = value => { const d = new Date(value); return value && Number.isFinite(d.getTime()) ? new Intl.DateTimeFormat("zh-CN", { timeZone: "Asia/Shanghai", month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false }).format(d) : "—"; };
    const spec = () => state.config?.metrics?.[state.metric] || DEFAULT_METRICS[state.metric];
    const selectedLatest = () => state.overview?.devices.find(d => d.device_id === state.device) || null;
    function ageText(age) { return age == null ? "—" : age < 60 ? Math.floor(age) + " 秒" : Math.floor(age / 60) + " 分 " + Math.floor(age % 60) + " 秒"; }
    function notice(message = "") { $("notice").textContent = message; $("notice").hidden = !message; }
    function wsBadge(text, kind = "neutral") { $("ws-state").textContent = "网页推送：" + text; $("ws-state").className = "badge " + kind; }
    async function api(path) {
        const headers = path !== "/api/config" && state.token ? { "X-View-Token": state.token } : {};
        const response = await fetch(path, { headers });
        if (response.status === 401) { $("auth-box").hidden = false; throw new Error("网页访问鉴权失败，请检查 VIEW_TOKEN。"); }
        if (!response.ok) throw new Error("服务请求失败（" + response.status + "）");
        $("auth-box").hidden = true; return response;
    }
    function query(limit) { return new URLSearchParams({ device_id: state.device, metric: state.metric, hours: state.hours, limit }); }
    const ICON_PATHS = {
        soil_moisture_pct: '<path d="M12 3s6 6.3 6 10a6 6 0 0 1-12 0c0-3.7 6-10 6-10z"/>',
        soil_ph: '<path d="M9 3h6M10 3v6.2L5.6 17A2.6 2.6 0 0 0 8 21h8a2.6 2.6 0 0 0 2.4-4L14 9.2V3"/><path d="M7.6 15h8.8"/>',
        wind_speed_mps: '<path d="M3 8h11a3 3 0 1 0-3-3"/><path d="M3 12h15a3 3 0 1 1-3 3"/><path d="M3 16h7"/>',
        wind_direction_deg: '<circle cx="12" cy="12" r="9"/><path d="M15.2 8.8l-1.8 4.6-4.6 1.8 1.8-4.6z"/>',
        rain_intensity_mm_min: '<path d="M7 15a4 4 0 0 1 .4-8 5 5 0 0 1 9.4 1.4A3.3 3.3 0 0 1 17 15"/><path d="M9 18l-1 2M13 18l-1 2M17 18l-1 2"/>',
        temperature_c: '<path d="M14 14.8V5a2 2 0 1 0-4 0v9.8a4 4 0 1 0 4 0z"/><circle cx="12" cy="18" r="1.1"/>',
        air_humidity_rh_pct: '<path d="M12 3s5.5 5.8 5.5 9.4a5.5 5.5 0 0 1-11 0C6.5 8.8 12 3 12 3z"/><path d="M9.6 13.4c1 .8 1.9.8 2.4.8s1.4 0 2.4-.8"/>',
        light_lux: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
        pressure_kpa: '<path d="M4 17a9 9 0 1 1 16 0"/><path d="M12 13l4-3.6"/><circle cx="12" cy="13" r="1.3"/>',
        pm25_ug_m3: '<circle cx="8" cy="9" r="1.9"/><circle cx="15.2" cy="7.2" r="1.2"/><circle cx="11.4" cy="15.2" r="1.5"/><circle cx="17" cy="14.6" r="1.9"/>',
        pm10_ug_m3: '<circle cx="8.4" cy="9" r="2.6"/><circle cx="16" cy="8" r="1.7"/><circle cx="12" cy="16" r="2.2"/>'
    };
    function metricIcon(key) {
        const path = ICON_PATHS[key] || '<circle cx="12" cy="12" r="8"/>';
        return '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + path + '</svg>';
    }
    function makeCards() {
        for (const [key, definition] of Object.entries(state.config?.metrics || DEFAULT_METRICS)) {
            const card = document.createElement("article"); card.className = "metric"; card.dataset.metric = key;
            card.setAttribute("role", "button"); card.setAttribute("tabindex", "0");
            const head = document.createElement("div"); head.className = "metric-head";
            const icon = document.createElement("span"); icon.className = "metric-icon"; icon.innerHTML = metricIcon(key);
            const title = document.createElement("h3"); title.textContent = definition.name;
            head.append(icon, title); card.append(head);
            const reading = document.createElement("div"); reading.className = "reading";
            const number = document.createElement("strong"); number.className = "number"; number.textContent = "—"; reading.append(number);
            const unit = document.createElement("span"); unit.className = "unit"; unit.textContent = definition.unit; reading.append(unit);
            if (key === "wind_direction_deg") { const compass = document.createElement("span"); compass.className = "compass"; compass.textContent = "↑"; compass.setAttribute("aria-label", "参考角度指针"); reading.append(compass); }
            card.append(reading);
            if (key === "wind_speed_mps") { const peak = document.createElement("div"); peak.className = "sample-max"; card.append(peak); }
            const quality = document.createElement("div"); quality.className = "quality"; card.append(quality);
            card.onclick = () => { state.metric = key; state.viewStart = null; state.viewEnd = null; $("metric").value = key; $("chart-title").textContent = definition.name + "趋势 · 加载中"; filterChanged(true); };
            card.onkeydown = e => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); card.click(); } };
            $(definition.group === "main" ? "main-cards" : "weather-cards").append(card);
        }
    }
    function updateFreshness() {
        const latest = selectedLatest(), upload = freshness(latest, Date.now() + state.offset, state.config?.online_seconds || 540, state.apiError);
        const online = upload.fresh;
        $("online-badge").className = "status-pill " + (online ? "online" : "offline");
        $("online-badge").querySelector(".label").textContent = !latest && !isDemo(state.device) ? "离线" : (online ? "在线" : "离线");
        $("fresh-state").textContent = !latest && !isDemo(state.device) ? "真实设备尚未接入" : upload.text;
        $("fresh-state").className = "badge " + upload.kind; $("last-seen").textContent = clock(latest?.received_at);
        document.querySelectorAll(".metric[data-metric]").forEach(card => {
            const key = card.dataset.metric, definition = state.config?.metrics?.[key] || DEFAULT_METRICS[key];
            const result = latest?.data?.[key];
            const age = result?.age_ms == null ? null : Math.max(0, result.age_ms + Date.now() - state.fetchedAt);
            const shown = displayReading(result, key, definition);
            const names = { soil_moisture_pct: "土壤湿度 · 短窗", soil_ph: "土壤 pH · 短窗", wind_speed_mps: "平均风速", wind_direction_deg: "平均风向", rain_intensity_mm_min: "平均雨强" };
            card.querySelector("h3").textContent = result?.statistics ? names[key] || definition.name : definition.name;
            if (key === "wind_speed_mps") {
                const peak = result?.wind_speed_sample_max_mps;
                const peakEl = card.querySelector(".sample-max");
                if (peakEl) peakEl.textContent = result?.statistics ? "采样最大风速：" + (shown.valid && typeof peak === "number" ? peak.toFixed(2) + " m/s" : "—") : "";
            }
            card.querySelector(".number").textContent = shown.text;
            card.className = "metric" + (shown.valid ? "" : " invalid");
            let label = windowInfo(result, key).quality;
            if (result?.showing_last_value) label = "上次有效值 · " + label;
            if (state.apiError) label = shown.valid ? "上次有效值 · 数据未刷新" : "数据未刷新";
            if (result?.range === "LOW") label += " · 偏低";
            if (result?.range === "HIGH") label += " · 偏高";
            card.querySelector(".quality").textContent = label + (age == null ? "" : " · " + ageText(age / 1000) + "前");
            if (key === "wind_direction_deg" && shown.valid) {
                state.angle = shortestAngle(state.angle, result.value);
                card.querySelector(".compass").style.transform = "rotate(" + state.angle + "deg)";
            }
        });
        const position = locationView(latest?.location, Date.now() + state.offset, state.config?.gps_stale_seconds || 540, state.apiError);
        $("location-longitude").textContent = position.longitude; $("location-latitude").textContent = position.latitude;
        $("location-state").textContent = position.status; $("location-state").className = "badge " + position.kind;
        $("location-fix-time").textContent = position.fixTime; $("location-fix-age").textContent = ageText(position.fixAge);
        $("location-received-age").textContent = ageText(position.receivedAge); $("location-source").textContent = position.source;
        $("location-coordinate-system").textContent = position.coordinateSystem; $("location-error").textContent = position.error;
        $("location-error").hidden = !position.error;
        updateLocationMap(position);
    }
    function rowsHtml(rows) {
        if (!rows.length) return '<tr><td colspan="5">当前设备 / 指标 / 范围暂无测量记录</td></tr>';
        return rows.map(row => {
            const shown = displayReading(row, row.metric || state.metric, state.config?.metrics?.[row.metric] || DEFAULT_METRICS[row.metric || state.metric]);
            return "<tr><td>" + escapeHtml(clock(row.received_at)) + "</td><td>" + escapeHtml(row.name || spec().name) + "</td><td>" + escapeHtml(shown.text + (shown.valid && shown.unit ? " " + shown.unit : "")) + "</td><td>" + escapeHtml(windowInfo(row, row.metric || state.metric).quality) + "</td><td>#" + escapeHtml(row.sample_seq) + " / " + escapeHtml(row.sample_t_ms) + " ms</td></tr>";
        }).join("");
    }
    function renderChart() {
        const definition = spec(), rows = state.rows, segments = chartSegments(rows, state.device, definition.stale_ms, state.metric);
        const points = segments.flat(); $("chart-title").textContent = definition.name + "趋势";
        const ordered = rows.slice().sort((a, b) => Date.parse(a.received_at) - Date.parse(b.received_at));
        const container = $("chart");
        if (!points.length) { container.innerHTML = '<div class="empty">当前范围没有有效测量点<br>失败和长时间缺测处断线</div>'; return; }
        const width = Math.max(280, container.clientWidth), height = container.clientHeight, left = 65, right = 25, top = 15, bottom = 36;
        let start = Date.parse(ordered[0].received_at), end = Date.parse(ordered.at(-1).received_at);
        if (start === end) { start -= 1500; end += 1500; }
        // 钳制缩放视图窗口到数据真实范围
        const viewStart = state.viewStart == null ? start : Math.max(start, state.viewStart);
        const viewEnd = state.viewEnd == null ? end : Math.min(end, state.viewEnd);
        state.viewStart = viewStart; state.viewEnd = viewEnd;
        $("chart-count").textContent = points.length + " 个有效点 / " + rows.length + " 次测量" + (state.hasMore ? " · 最近1000条上限" : "");
        $("chart-subtitle").textContent = state.device + " · " + definition.unit + " · 实际覆盖 " + clock(viewStart) + "—" + clock(viewEnd) + " · 服务器接收时间";
        const values = points.map(row => displayReading(row, state.metric).value);
        let low = Math.min(...values), high = Math.max(...values);
        if (definition.group === "main") { low = definition.minimum; high = definition.maximum; }
        else { const padding = Math.max((high - low) * .1, Math.abs(high) * .05, 1); low -= padding; high += padding; if (state.metric !== "temperature_c") low = Math.max(0, low); }
        const plotW = width - left - right;
        const x = row => left + Math.max(0, Math.min(1, (Date.parse(row.received_at) - viewStart) / (viewEnd - viewStart))) * plotW;
        const y = row => top + (high - displayReading(row, state.metric).value) / (high - low) * (height - top - bottom);
        let svg = "";
        // 裁剪区灰带
        if (viewStart > start) svg += '<rect x="0" y="0" width="' + left + '" height="' + height + '" fill="#f6f8f4"/><rect x="0" y="' + top + '" width="' + left + '" height="' + (height - top - bottom) + '" fill="#f1f4ef"/>';
        if (viewEnd < end) svg += '<rect x="' + (width - right) + '" y="' + top + '" width="' + right + '" height="' + (height - top - bottom) + '" fill="#f1f4ef"/>';
        for (let i = 0; i < 5; i++) {
            const value = low + (high - low) * i / 4, py = top + (4 - i) / 4 * (height - top - bottom);
            svg += '<line x1="' + left + '" y1="' + py + '" x2="' + (width - right) + '" y2="' + py + '" stroke="#e7ece5" stroke-dasharray="4 5"/><text x="' + (left - 10) + '" y="' + (py + 5) + '" text-anchor="end" fill="#71857a" font-size="12">' + escapeHtml(value.toFixed(definition.decimals ? 1 : 0)) + "</text>";
        }
        for (const segment of segments) {
            if (segment.length > 1) svg += '<polyline points="' + segment.map(row => x(row) + "," + y(row)).join(" ") + '" fill="none" stroke="#2f7a58" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>';
            for (const row of segment) svg += '<circle cx="' + x(row) + '" cy="' + y(row) + '" r="3" fill="#2f7a58"><title>' + escapeHtml(clock(row.received_at) + " · " + displayReading(row, state.metric).text + " " + definition.unit) + "</title></circle>";
        }
        // x 轴刻度（3 个点，带"—" 中间段标记）
        for (let i = 0; i < 3; i++) { const tx = left + i / 2 * plotW; svg += '<text x="' + tx + '" y="' + (height - 8) + '" text-anchor="' + (i === 0 ? "start" : i === 2 ? "end" : "middle") + '" fill="#71857a" font-size="12">' + escapeHtml(clock(new Date(viewStart + i / 2 * (viewEnd - viewStart)).toISOString())) + "</text>"; }
        container.innerHTML = '<svg viewBox="0 0 ' + width + " " + height + '" role="img" aria-label="' + escapeHtml(definition.name + "，单位" + definition.unit) + '">' + svg + "</svg>";
    }
    function render() {
        const devices = state.overview?.devices || [], ordered = devices.slice().sort((a, b) => Number(isDemo(a.device_id)) - Number(isDemo(b.device_id)));
        if (!ordered.some(d => d.device_id === state.device)) ordered.unshift({ device_id: state.device });
        $("device").replaceChildren(...ordered.map(d => { const option = document.createElement("option"); option.value = d.device_id; option.textContent = d.device_id + (isDemo(d.device_id) ? " · 模拟设备" : !d.received_at ? " · 尚未接入" : " · 真实设备"); return option; }));
        $("device").value = state.device; $("device-kind").textContent = isDemo(state.device) ? "模拟设备" : "真实设备"; $("current-device").textContent = state.device;
        $("total").textContent = state.overview?.counts.total ?? "—"; $("measurement-count").textContent = state.overview?.counts.measurements ?? "—";
        $("device-count").textContent = state.overview?.counts.devices ?? "—"; $("connections").textContent = state.overview?.connections ?? "—";
        $("updated").textContent = "服务器刷新 " + clock(state.overview?.server_time);
        $("recent-body").innerHTML = rowsHtml(state.rows.slice(0, 8));
        updateFreshness(); renderChart();
    }
    async function refreshMeasurements() {
        if (state.loading) { state.again = true; return; } state.loading = true;
        const generation = state.generation;
        try {
            const data = await (await api("/api/measurements?" + query(1000))).json();
            if (generation !== state.generation) return;
            state.rows = uniqueReadings(data.items); state.hasMore = data.has_more; state.apiError = false;
            notice(); updateFreshness(); renderChart();
            if (state.page === "overview") $("recent-body").innerHTML = rowsHtml(state.rows.slice(0, 8));
        } catch (error) { if (generation === state.generation) { state.apiError = true; notice(error.message); updateFreshness(); } }
        finally { state.loading = false; if (state.again) { state.again = false; refreshMeasurements(); } }
    }
    async function refresh() {
        if (state.loading) { state.again = true; return; } state.loading = true;
        const generation = state.generation;
        try {
            const overview = await (await api("/api/overview?hold_latest=true")).json();
            if (generation !== state.generation) return;
            state.device = chooseDevice(overview.devices, state.device, state.explicit);
            const data = await (await api("/api/measurements?" + query(1000))).json();
            if (generation !== state.generation) return;
            state.overview = overview; state.offset = Date.parse(overview.server_time) - Date.now(); state.fetchedAt = Date.now();
            state.rows = uniqueReadings(data.items); state.hasMore = data.has_more; state.apiError = false; notice(); render();
        } catch (error) { if (generation === state.generation) { state.apiError = true; notice(error.message); updateFreshness(); } }
        finally { state.loading = false; if (state.again) { state.again = false; refresh(); } }
    }
    async function loadHistory(reset = false) {
        if (reset) { state.history = []; state.cursor = null; state.historyGeneration++; }
        const generation = state.historyGeneration, savedMetric = state.metric;
        state.metric = state.historyMetric;
        const params = query(100);
        if (state.cursor) params.set("before_id", state.cursor);
        state.metric = savedMetric;
        $("history-more").disabled = true;
        try {
            const data = await (await api("/api/measurements?" + params)).json(); if (generation !== state.historyGeneration) return;
            state.history = uniqueReadings(state.history.concat(data.items)); state.cursor = data.next_before_id;
            $("history-body").innerHTML = rowsHtml(state.history); $("history-summary").textContent = state.history.length + " 条实际测量"; $("history-more").disabled = !data.has_more;
        } catch (error) { notice(error.message); }
    }
    function connectSocket() {
        if (state.ws) { state.ws.onclose = null; state.ws.close(); }
        clearTimeout(state.reconnect);
        const socket = new WebSocket((location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/ws"); state.ws = socket;
        socket.onopen = () => socket.send(JSON.stringify({ token: state.token }));
        socket.onmessage = event => {
            try {
                const message = JSON.parse(event.data);
                if (message.type === "ready") { wsBadge("已连接", "fresh"); refresh(); }
                else if (message.type === "reading" || message.type === "resync") { refresh(); if (state.page === "history") loadHistory(true); }
                else if (message.type === "location") { refresh(); }
            } catch { wsBadge("消息异常", "warning"); }
        };
        socket.onclose = () => { wsBadge("重连中", "warning"); state.reconnect = setTimeout(connectSocket, 3000); };
        socket.onerror = () => wsBadge("连接中断", "warning");
    }
    function page(name) {
        state.page = name; for (const key of ["overview", "history", "settings", "guide"]) $("page-" + key).hidden = key !== name;
        document.querySelectorAll("[data-page]").forEach(el => el.classList.toggle("active", el.dataset.page === name));
        if (name === "history") loadHistory(true); else if (name === "overview") { state.viewStart = state.viewEnd = null; renderChart(); }
    }
    function filterChanged(onlyMetric = false) {
        state.viewStart = null; state.viewEnd = null; state.generation++; state.historyGeneration++;
        if (onlyMetric) refreshMeasurements(); else refresh();
        if (state.page === "history") loadHistory(true);
    }
    $("device").onchange = () => { state.device = $("device").value; state.explicit = true; filterChanged(false); };
    $("metric").onchange = () => { state.metric = $("metric").value; $("chart-title").textContent = DEFAULT_METRICS[state.metric]?.name + "趋势 · 加载中"; filterChanged(true); };
    $("hours").onchange = () => { state.hours = Number($("hours").value); filterChanged(false); };
    document.querySelectorAll("[data-page]").forEach(el => el.onclick = () => page(el.dataset.page));
    $("all-records").onclick = () => page("history");
    $("history-refresh").onclick = () => loadHistory(true); $("history-more").onclick = () => loadHistory();
    $("settings").onclick = $("auth-open").onclick = () => { $("view-token").value = state.token; $("settings-dialog").showModal(); };
    $("settings-close").onclick = () => $("settings-dialog").close();
    $("settings-form").onsubmit = event => { event.preventDefault(); state.token = $("view-token").value; sessionStorage.setItem("tea_view_token", state.token); $("settings-dialog").close(); connectSocket(); filterChanged(); };
    $("fullscreen").onclick = async () => { const active = document.body.classList.toggle("presentation"); $("fullscreen").textContent = active ? "退出全屏" : "全屏"; try { if (active && !document.fullscreenElement) await document.documentElement.requestFullscreen(); else if (!active && document.fullscreenElement) await document.exitFullscreen(); } catch { } renderChart(); };
    document.addEventListener("fullscreenchange", () => { if (!document.fullscreenElement) { document.body.classList.remove("presentation"); $("fullscreen").textContent = "全屏"; renderChart(); } });
    $("export").onclick = async () => { try { const params = query(10000); params.delete("limit"); const blob = await (await api("/api/export.csv?" + params)).blob(); const url = URL.createObjectURL(blob), link = document.createElement("a"); link.href = url; link.download = "tea_sense_" + state.metric + ".csv"; link.click(); setTimeout(() => URL.revokeObjectURL(url), 1000); } catch (error) { notice(error.message); } };
    window.addEventListener("resize", renderChart);
    (async () => {
        try {
            state.config = await (await api("/api/config")).json();
            const viewAuthRequired = state.config.view_auth_required;
            $("settings").hidden = !viewAuthRequired;
            $("view-auth-setting").hidden = !viewAuthRequired;
            if (!viewAuthRequired) {
                state.token = "";
                sessionStorage.removeItem("tea_view_token");
            }
            makeCards();
            $("metric").replaceChildren(...Object.entries(state.config.metrics).map(([key, value]) => { const option = document.createElement("option"); option.value = key; option.textContent = value.name + " " + value.unit; return option; }));
            $("history-metric").replaceChildren(...Object.entries(state.config.metrics).map(([key, value]) => { const option = document.createElement("option"); option.value = key; option.textContent = value.name + " " + value.unit; return option; }));
            $("history-metric").value = state.historyMetric = state.metric;
            $("history-metric").onchange = () => { state.historyMetric = $("history-metric").value; loadHistory(true); };
            $("chart-reset").onclick = () => { state.viewStart = null; state.viewEnd = null; renderChart(); };
            $("chart").addEventListener("wheel", event => {
                if (state.page !== "overview") return;
                event.preventDefault();
                const container = $("chart");
                const ordered = state.rows.slice().sort((a, b) => Date.parse(a.received_at) - Date.parse(b.received_at));
                if (!ordered.length) return;
                const rect = container.getBoundingClientRect(), left = 65, right = 25;
                const clientX = event.clientX - rect.left;
                const start = Date.parse(ordered[0].received_at), end = Date.parse(ordered.at(-1).received_at);
                const base = state.viewStart == null ? start : state.viewStart;
                const baseEnd = state.viewEnd == null ? end : state.viewEnd;
                const duration = baseEnd - base;
                const pxToTime = (baseEnd - base) / (rect.width - left - right);
                const tAtCursor = base + (clientX - left) * pxToTime;
                const factor = Math.abs(event.deltaY);
                const shift = event.shiftKey || factor < 40;
                if (shift) {
                    // 平移：Shift+滚轮 或 触控板小幅滚动
                    const direction = event.deltaY > 0 ? 1 : -1;
                    const delta = direction * Math.min(duration * 0.1, Math.max(6000, factor * 40));
                    state.viewStart = Math.max(start, base - delta);
                    state.viewEnd = state.viewStart + duration;
                    if (state.viewEnd > end) { state.viewEnd = end; state.viewStart = end - duration; }
                } else {
                    // 缩放：以鼠标位置为中心
                    const zoom = event.deltaY < 0 ? 0.82 : 1.22;
                    const minDuration = Math.max(60000, (end - start) * 0.05);
                    const newDuration = Math.min(end - start, Math.max(minDuration, duration * zoom));
                    const ratio = (tAtCursor - base) / duration;
                    state.viewStart = Math.max(start, tAtCursor - ratio * newDuration);
                    state.viewEnd = state.viewStart + newDuration;
                    if (state.viewEnd > end) { state.viewEnd = end; state.viewStart = end - newDuration; }
                    if (state.viewStart < start) { state.viewStart = start; state.viewEnd = Math.min(end, start + newDuration); }
                }
                renderChart();
            }, { passive: false });
            if ($("example-json")) $("example-json").textContent = JSON.stringify(state.config.example, null, 2);
            await refresh(); connectSocket();
        } catch (error) { notice(error.message); }
    })();
    setInterval(updateFreshness, 1000); setInterval(refresh, 5000);
}
