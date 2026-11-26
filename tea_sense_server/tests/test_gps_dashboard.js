"use strict";
const assert=require("node:assert/strict");
const {locationView,formatLocationTime}=require("../web/app.js");
const now=Date.parse("2026-11-23T03:18:23Z");
const position={longitude:114.384038,latitude:30.491893,source:"GPS",coordinate_system:"UNKNOWN",
 fix_time_utc:"2026-11-23T03:17:21+00:00",received_at_utc:"2026-11-23T03:17:22+00:00",
 last_received_at_utc:"2026-11-23T03:17:22+00:00",status:"FRESH"};
const shown=locationView(position,now);
assert.equal(shown.longitude,"114.384038");assert.equal(shown.latitude,"30.491893");
assert.equal(shown.status,"位置正常");assert.equal(shown.coordinateSystem,"未确认");
assert.equal(shown.fixAge,62);assert.equal(shown.receivedAge,61);assert.equal(shown.source,"GPS");
assert.ok(shown.fixTime.includes("11:17:21"));assert.ok(formatLocationTime(position.fix_time_utc).includes("2026"));
assert.equal(locationView(position,Date.parse(position.fix_time_utc)+540000).status,"位置正常");
assert.equal(locationView(position,Date.parse(position.fix_time_utc)+540001).status,"最近一次定位");
const duplicate={...position,last_received_at_utc:new Date(now+600000).toISOString(),last_report_status:"DUPLICATE"};
assert.equal(locationView(duplicate,now+600000).status,"最近一次定位");
assert.equal(locationView(duplicate,now+600000).receivedAge,0);
assert.equal(locationView(duplicate,now+600000).longitude,"114.384038");
const failed={...position,last_report_status:"NO_FIX",latest_report_error:true};
assert.ok(locationView(failed,now).error.includes("最新定位报文异常"));
assert.equal(locationView(failed,now).longitude,"114.384038");
assert.equal(locationView(failed,now).kind,"warning");
assert.equal(locationView(failed,now+600000).status,"最近一次定位");
assert.equal(locationView(null,now).status,"暂无定位");assert.equal(locationView(null,now).longitude,"—");
assert.equal(locationView({...position,longitude:0},now).longitude,"0.000000");
assert.equal(locationView({...position,latitude:0},now).latitude,"0.000000");
for(const patch of [{longitude:0,latitude:0},{longitude:true},{latitude:"30"},{longitude:NaN},{fix_time_utc:null}]){
 assert.equal(locationView({...position,...patch},now).status,"暂无定位");
}
assert.equal(locationView(position,now,180,true).status,"位置未刷新");
assert.equal(locationView({...position,device_id:"demo_other",longitude:115},now).longitude,"115.000000");
assert.equal(locationView(null,now).fixTime,"—");
console.log("PASS: GPS named coordinates, six decimals, Beijing time, independent fix/receipt ages, stale cached values, latest errors and empty/device switch");
