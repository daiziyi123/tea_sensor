"use strict";
const REAL_DEVICE="tea_h743_001";
const isDemo=id=>/^demo(?:_|$)/i.test(id||"");
const escapeHtml=value=>String(value??"—").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const DEFAULT_METRICS={
 soil_moisture_pct:{name:"土壤湿度",unit:"%",decimals:2,period_ms:3000,stale_ms:9000,minimum:0,maximum:100,group:"main"},
 soil_ph:{name:"土壤pH",unit:"",decimals:2,period_ms:3000,stale_ms:9000,minimum:3,maximum:9,group:"main"},
 wind_speed_mps:{name:"风速",unit:"m/s",decimals:2,period_ms:3000,stale_ms:9000,minimum:0,maximum:30,group:"main"},
 wind_direction_deg:{name:"风向角度",unit:"°",decimals:2,period_ms:3000,stale_ms:9000,minimum:0,maximum:360,group:"main"},
 rain_intensity_mm_min:{name:"降雨强度",unit:"mm/min",decimals:2,period_ms:3000,stale_ms:9000,minimum:0,maximum:8,group:"main"},
 temperature_c:{name:"气温",unit:"℃",decimals:1,period_ms:10000,stale_ms:30000,group:"weather"},
 air_humidity_rh_pct:{name:"空气湿度",unit:"%RH",decimals:1,period_ms:10000,stale_ms:30000,group:"weather"},
 light_lux:{name:"光照",unit:"lx",decimals:0,period_ms:10000,stale_ms:30000,group:"weather"},
 pressure_kpa:{name:"气压",unit:"kPa",decimals:2,period_ms:60000,stale_ms:180000,group:"weather"},
 pm25_ug_m3:{name:"PM2.5",unit:"μg/m³",decimals:0,period_ms:30000,stale_ms:90000,group:"weather"},
 pm10_ug_m3:{name:"PM10",unit:"μg/m³",decimals:0,period_ms:30000,stale_ms:90000,group:"weather"}
};
function chooseDevice(devices,current="",explicit=false){
 if(explicit&&current)return current;
 if(devices.some(d=>d.device_id===REAL_DEVICE))return REAL_DEVICE;
 return devices.find(d=>!isDemo(d.device_id))?.device_id||REAL_DEVICE;
}
function freshness(reading,nowMs,onlineSeconds,apiError=false){
 const received=Date.parse(reading?.received_at||"");
 if(!Number.isFinite(received))return {age:null,fresh:false,text:"尚未收到数据",kind:"neutral"};
 const elapsed=Math.max(0,nowMs-received),age=Math.floor(elapsed/1000);
 if(apiError)return {age,fresh:false,text:"数据未刷新 · 历史最后值",kind:"error"};
 return elapsed>onlineSeconds*1000?{age,fresh:false,text:"数据超时 · 历史最后值",kind:"warning"}:{age,fresh:true,text:"近期收到数据",kind:"fresh"};
}
function physicalResult(reading,metric){
 if(reading?.data)return reading.data[metric]||{value:null,status:"NO_DATA"};
 if(reading&&Object.hasOwn(reading,"value"))return reading;
 if(metric==="soil_moisture_pct"&&reading)return {value:reading.moisture_pct,status:reading.status,range:reading.range};
 return {value:null,status:"NO_DATA"};
}
function displayReading(reading,metric="soil_moisture_pct",spec=DEFAULT_METRICS[metric]){
 const result=physicalResult(reading,metric);
 const valid=result.status==="OK"&&typeof result.value==="number"&&Number.isFinite(result.value);
 const text=valid?result.value.toFixed(spec?.decimals??2):"—";
 return {text,moisture:text,unit:spec?.unit||"",status:result.status||"NO_DATA",valid,value:valid?result.value:null};
}
function metricFreshness(result,spec,elapsed=0){
 const age=result?.age_ms==null?null:Math.max(0,result.age_ms+elapsed);
 const stale=result?.status==="STALE"||(result?.status==="OK"&&age!=null&&age>spec.stale_ms);
 const valid=result?.status==="OK"&&!stale&&typeof result.value==="number"&&Number.isFinite(result.value);
 return {valid,stale,age,status:stale?"STALE":result?.status||"NO_DATA"};
}
function uniqueReadings(rows){
 const seen=new Set();
 return rows.filter(row=>{const key=(row.metric||"soil_moisture_pct")+":"+row.id;if(seen.has(key))return false;seen.add(key);return true;});
}
function windowInfo(reading,metric="soil_moisture_pct"){
 const status=physicalResult(reading,metric).status||"NO_DATA";
 const quality=status==="OK"?"测量有效":status==="STALE"?"过期":status==="NO_DATA"?"未采集":status==="INVALID_VALUE"?"数据无效":"采集失败";
 return {label:DEFAULT_METRICS[metric]?.name||metric,quality,warning:status!=="OK"};
}
function chartSegments(rows,device,gapMs,metric="soil_moisture_pct"){
 const ordered=uniqueReadings(rows).filter(r=>r.device_id===device&&(!r.metric||r.metric===metric)).slice().sort((a,b)=>Date.parse(a.received_at)-Date.parse(b.received_at)||a.id-b.id);
 const segments=[];let current=[],previous=null;
 for(const row of ordered){
  const time=Date.parse(row.received_at),shown=displayReading(row,metric);
  const valid=shown.valid&&Number.isFinite(time);
  if(!valid||(previous&&(time-Date.parse(previous.received_at)>gapMs||
      (metric==="wind_direction_deg"&&Math.abs(shown.value-displayReading(previous,metric).value)>180)))){
   if(current.length)segments.push(current);current=[];
  }
  if(valid)current.push(row);
  previous=valid?row:null;
 }
 if(current.length)segments.push(current);
 return segments;
}
function shortestAngle(previous,next){return previous+((next-previous+540)%360)-180;}
if(typeof module!=="undefined"&&module.exports)module.exports={chooseDevice,freshness,displayReading,chartSegments,isDemo,uniqueReadings,windowInfo,metricFreshness,shortestAngle,DEFAULT_METRICS};
if(typeof document!=="undefined"){
 const $=id=>document.getElementById(id);
 const state={config:null,overview:null,rows:[],device:REAL_DEVICE,explicit:false,metric:"soil_moisture_pct",hours:24,offset:0,
  fetchedAt:0,token:sessionStorage.getItem("tea_view_token")||"",ws:null,reconnect:null,loading:false,again:false,generation:0,
  history:[],cursor:null,historyGeneration:0,apiError:false,hasMore:false,angle:0,page:"overview"};
 const clock=value=>{const d=new Date(value);return value&&Number.isFinite(d.getTime())?new Intl.DateTimeFormat("zh-CN",{timeZone:"Asia/Shanghai",month:"2-digit",day:"2-digit",hour:"2-digit",minute:"2-digit",second:"2-digit",hour12:false}).format(d):"—";};
 const spec=()=>state.config?.metrics?.[state.metric]||DEFAULT_METRICS[state.metric];
 const selectedLatest=()=>state.overview?.devices.find(d=>d.device_id===state.device)||null;
 function notice(message=""){$("notice").textContent=message;$("notice").hidden=!message;}
 function wsBadge(text,kind="neutral"){$("ws-state").textContent="网页推送："+text;$("ws-state").className="badge "+kind;}
 async function api(path){
  const response=await fetch(path,{headers:{"X-View-Token":state.token}});
  if(response.status===401){$("auth-box").hidden=false;throw new Error("网页访问鉴权失败，请检查 VIEW_TOKEN。");}
  if(!response.ok)throw new Error("服务请求失败（"+response.status+"）");
  $("auth-box").hidden=true;return response;
 }
 function query(limit){return new URLSearchParams({device_id:state.device,metric:state.metric,hours:state.hours,limit});}
 function makeCards(){
  for(const [key,definition] of Object.entries(state.config?.metrics||DEFAULT_METRICS)){
   const card=document.createElement("article");card.className="metric";card.dataset.metric=key;
   const title=document.createElement("h3");title.textContent=definition.name;card.append(title);
   const reading=document.createElement("div");reading.className="reading";
   const number=document.createElement("strong");number.className="number";number.textContent="—";reading.append(number);
   const unit=document.createElement("span");unit.className="unit";unit.textContent=definition.unit;reading.append(unit);
   if(key==="wind_direction_deg"){const compass=document.createElement("span");compass.className="compass";compass.textContent="↑";compass.setAttribute("aria-label","参考角度指针");reading.append(compass);}
   card.append(reading);const quality=document.createElement("div");quality.className="quality";card.append(quality);
   $(definition.group==="main"?"main-cards":"weather-cards").append(card);
  }
 }
 function updateFreshness(){
  const latest=selectedLatest(),upload=freshness(latest,Date.now()+state.offset,state.config?.online_seconds||60,state.apiError);
  $("fresh-state").textContent=!latest&&!isDemo(state.device)?"真实设备尚未接入":upload.text;
  $("fresh-state").className="badge "+upload.kind;$("last-seen").textContent=clock(latest?.received_at);
  document.querySelectorAll(".metric[data-metric]").forEach(card=>{
   const key=card.dataset.metric,definition=state.config?.metrics?.[key]||DEFAULT_METRICS[key];
   const result=latest?.data?.[key],quality=metricFreshness(result,definition,Math.max(0,Date.now()-state.fetchedAt));
   const shown=displayReading(result,key,definition);
   card.querySelector(".number").textContent=quality.valid&&!state.apiError?shown.text:"—";
   card.className="metric"+(quality.stale?" stale":!quality.valid||state.apiError?" invalid":"");
   let label=windowInfo({value:result?.value,status:quality.status},key).quality;
   if(state.apiError)label="数据未刷新";
   if(result?.range==="LOW")label+=" · 偏低";
   if(result?.range==="HIGH")label+=" · 偏高";
   card.querySelector(".quality").textContent=label+(quality.age==null?"":" · "+Math.floor(quality.age/1000)+"秒前");
   if(key==="wind_direction_deg"&&quality.valid){
    state.angle=shortestAngle(state.angle,result.value);
    card.querySelector(".compass").style.transform="rotate("+state.angle+"deg)";
   }
  });
 }
 function rowsHtml(rows){
  if(!rows.length)return '<tr><td colspan="5">当前设备 / 指标 / 范围暂无测量记录</td></tr>';
  return rows.map(row=>{
   const shown=displayReading(row,row.metric||state.metric,state.config?.metrics?.[row.metric]||DEFAULT_METRICS[row.metric||state.metric]);
   return "<tr><td>"+escapeHtml(clock(row.received_at))+"</td><td>"+escapeHtml(row.name||spec().name)+"</td><td>"+escapeHtml(shown.text+(shown.valid&&shown.unit?" "+shown.unit:""))+"</td><td>"+escapeHtml(windowInfo(row,row.metric||state.metric).quality)+"</td><td>#"+escapeHtml(row.sample_seq)+" / "+escapeHtml(row.sample_t_ms)+" ms</td></tr>";
  }).join("");
 }
 function renderChart(){
  const definition=spec(),rows=state.rows,segments=chartSegments(rows,state.device,definition.stale_ms,state.metric);
  const points=segments.flat();$("chart-title").textContent=definition.name+"趋势";
  const ordered=rows.slice().sort((a,b)=>Date.parse(a.received_at)-Date.parse(b.received_at));
  $("chart-count").textContent=points.length+" 个有效点 / "+rows.length+" 次测量"+(state.hasMore?" · 最近1000条上限":"");
  $("chart-subtitle").textContent=state.device+" · "+definition.unit+" · 实际覆盖 "+(ordered.length?clock(ordered[0].received_at)+"—"+clock(ordered.at(-1).received_at):"无数据")+" · 服务器接收时间";
  const container=$("chart");
  if(!points.length){container.innerHTML='<div class="empty">当前范围没有有效测量点<br>失败和长时间缺测处断线</div>';return;}
  const width=Math.max(280,container.clientWidth),height=container.clientHeight,left=65,right=25,top=15,bottom=36;
  let start=Date.parse(ordered[0].received_at),end=Date.parse(ordered.at(-1).received_at);
  if(start===end){start-=1500;end+=1500;}
  const values=points.map(row=>displayReading(row,state.metric).value);
  let low=Math.min(...values),high=Math.max(...values);
  if(definition.group==="main"){low=definition.minimum;high=definition.maximum;}
  else {const padding=Math.max((high-low)*.1,Math.abs(high)*.05,1);low-=padding;high+=padding;if(state.metric!=="temperature_c")low=Math.max(0,low);}
  const x=row=>left+(Date.parse(row.received_at)-start)/(end-start)*(width-left-right);
  const y=row=>top+(high-displayReading(row,state.metric).value)/(high-low)*(height-top-bottom);
  let svg="";
  for(let i=0;i<5;i++){const value=low+(high-low)*i/4,py=top+(4-i)/4*(height-top-bottom);
   svg+='<line x1="'+left+'" y1="'+py+'" x2="'+(width-right)+'" y2="'+py+'" stroke="#dce5d9" stroke-dasharray="4 5"/><text x="'+(left-10)+'" y="'+(py+5)+'" text-anchor="end" fill="#6d7e72" font-size="12">'+escapeHtml(value.toFixed(definition.decimals?1:0))+"</text>";
  }
  for(const segment of segments){
   if(segment.length>1)svg+='<polyline points="'+segment.map(row=>x(row)+","+y(row)).join(" ")+'" fill="none" stroke="#28674c" stroke-width="2.5"/>';
   for(const row of segment)svg+='<circle cx="'+x(row)+'" cy="'+y(row)+'" r="3" fill="#28674c"><title>'+escapeHtml(clock(row.received_at)+" · "+displayReading(row,state.metric).text+" "+definition.unit)+"</title></circle>";
  }
  for(let i=0;i<3;i++){const tx=left+i/2*(width-left-right);svg+='<text x="'+tx+'" y="'+(height-8)+'" text-anchor="'+(i===0?"start":i===2?"end":"middle")+'" fill="#6d7e72" font-size="12">'+escapeHtml(clock(new Date(start+i/2*(end-start)).toISOString()))+"</text>";}
  container.innerHTML='<svg viewBox="0 0 '+width+" "+height+'" role="img" aria-label="'+escapeHtml(definition.name+"，单位"+definition.unit)+'">'+svg+"</svg>";
 }
 function render(){
  const devices=state.overview?.devices||[],ordered=devices.slice().sort((a,b)=>Number(isDemo(a.device_id))-Number(isDemo(b.device_id)));
  if(!ordered.some(d=>d.device_id===state.device))ordered.unshift({device_id:state.device});
  $("device").replaceChildren(...ordered.map(d=>{const option=document.createElement("option");option.value=d.device_id;option.textContent=d.device_id+(isDemo(d.device_id)?" · 模拟设备":!d.received_at?" · 尚未接入":" · 真实设备");return option;}));
  $("device").value=state.device;$("device-kind").textContent=isDemo(state.device)?"模拟设备":"真实设备";$("current-device").textContent=state.device;
  $("total").textContent=state.overview?.counts.total??"—";$("measurement-count").textContent=state.overview?.counts.measurements??"—";
  $("device-count").textContent=state.overview?.counts.devices??"—";$("connections").textContent=state.overview?.connections??"—";
  $("updated").textContent="服务器刷新 "+clock(state.overview?.server_time);
  $("recent-body").innerHTML=rowsHtml(state.rows.slice(0,8));updateFreshness();renderChart();
 }
 async function refresh(){
  if(state.loading){state.again=true;return;}state.loading=true;
  const generation=state.generation;
  try{
   const overview=await (await api("/api/overview")).json();
   if(generation!==state.generation)return;
   state.device=chooseDevice(overview.devices,state.device,state.explicit);
   const data=await (await api("/api/measurements?"+query(1000))).json();
   if(generation!==state.generation)return;
   state.overview=overview;state.offset=Date.parse(overview.server_time)-Date.now();state.fetchedAt=Date.now();
   state.rows=uniqueReadings(data.items);state.hasMore=data.has_more;state.apiError=false;notice();render();
  }catch(error){if(generation===state.generation){state.apiError=true;notice(error.message);updateFreshness();}}
  finally{state.loading=false;if(state.again){state.again=false;refresh();}}
 }
 async function loadHistory(reset=false){
  if(reset){state.history=[];state.cursor=null;state.historyGeneration++;}
  const generation=state.historyGeneration,params=query(100);
  if(state.cursor)params.set("before_id",state.cursor);
  $("history-more").disabled=true;
  try{const data=await (await api("/api/measurements?"+params)).json();if(generation!==state.historyGeneration)return;
   state.history=uniqueReadings(state.history.concat(data.items));state.cursor=data.next_before_id;
   $("history-body").innerHTML=rowsHtml(state.history);$("history-summary").textContent=state.history.length+" 条实际测量";$("history-more").disabled=!data.has_more;
  }catch(error){notice(error.message);}
 }
 function connectSocket(){
  if(state.ws){state.ws.onclose=null;state.ws.close();}
  clearTimeout(state.reconnect);
  const socket=new WebSocket((location.protocol==="https:"?"wss://":"ws://")+location.host+"/ws");state.ws=socket;
  socket.onopen=()=>socket.send(JSON.stringify({token:state.token}));
  socket.onmessage=event=>{try{const message=JSON.parse(event.data);
   if(message.type==="ready"){wsBadge("已连接","fresh");refresh();}
   else if(message.type==="reading"||message.type==="resync"){refresh();if(state.page==="history")loadHistory(true);}
  }catch{wsBadge("消息异常","warning");}};
  socket.onclose=()=>{wsBadge("重连中","warning");state.reconnect=setTimeout(connectSocket,3000);};
  socket.onerror=()=>wsBadge("连接中断","warning");
 }
 function page(name){state.page=name;for(const key of ["overview","history","guide"])$("page-"+key).hidden=key!==name;document.querySelectorAll("[data-page]").forEach(el=>el.classList.toggle("active",el.dataset.page===name));if(name==="history")loadHistory(true);}
 function filterChanged(){state.generation++;state.historyGeneration++;refresh();if(state.page==="history")loadHistory(true);}
 $("device").onchange=()=>{state.device=$("device").value;state.explicit=true;filterChanged();};
 $("metric").onchange=()=>{state.metric=$("metric").value;filterChanged();};
 $("hours").onchange=()=>{state.hours=Number($("hours").value);filterChanged();};
 document.querySelectorAll("[data-page]").forEach(el=>el.onclick=()=>page(el.dataset.page));
 $("all-records").onclick=()=>page("history");$("history-refresh").onclick=()=>loadHistory(true);$("history-more").onclick=()=>loadHistory();
 $("settings").onclick=$("auth-open").onclick=()=>{$("view-token").value=state.token;$("settings-dialog").showModal();};
 $("settings-close").onclick=()=>$("settings-dialog").close();
 $("settings-form").onsubmit=event=>{event.preventDefault();state.token=$("view-token").value;sessionStorage.setItem("tea_view_token",state.token);$("settings-dialog").close();connectSocket();filterChanged();};
 $("fullscreen").onclick=async()=>{const active=document.body.classList.toggle("presentation");$("fullscreen").textContent=active?"退出全屏":"全屏";try{if(active&&!document.fullscreenElement)await document.documentElement.requestFullscreen();else if(!active&&document.fullscreenElement)await document.exitFullscreen();}catch{}renderChart();};
 document.addEventListener("fullscreenchange",()=>{if(!document.fullscreenElement){document.body.classList.remove("presentation");$("fullscreen").textContent="全屏";renderChart();}});
 $("export").onclick=async()=>{try{const params=query(10000);params.delete("limit");const blob=await(await api("/api/export.csv?"+params)).blob();const url=URL.createObjectURL(blob),link=document.createElement("a");link.href=url;link.download="tea_sense_"+state.metric+".csv";link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}catch(error){notice(error.message);}};
 window.addEventListener("resize",renderChart);
 (async()=>{try{state.config=await(await api("/api/config")).json();makeCards();
  $("metric").replaceChildren(...Object.entries(state.config.metrics).map(([key,value])=>{const option=document.createElement("option");option.value=key;option.textContent=value.name+" "+value.unit;return option;}));
  $("example-json").textContent=JSON.stringify(state.config.example,null,2);await refresh();connectSocket();
 }catch(error){notice(error.message);}})();
 setInterval(updateFreshness,1000);setInterval(refresh,5000);
}
