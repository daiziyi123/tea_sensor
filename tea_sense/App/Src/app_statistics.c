#include "app_statistics.h"
#include "app_config.h"
#include <math.h>
#include <string.h>

typedef struct { uint32_t slot, seq, t_ms; uint16_t raw; uint8_t occupied, valid; } SlowSlot;
static SlowSlot s_soil[APP_SLOW_SLOT_COUNT], s_ph[APP_SLOW_SLOT_COUNT];
static uint32_t s_anchor, s_start, s_sequence, s_last_round;
static uint32_t s_wind_slot, s_wind_count, s_pair_count, s_direction_count;
static uint8_t s_have_round, s_have_wind_slot, s_have_rain;
static double s_wind_sum, s_wind_max, s_cos, s_sin, s_rain_weight;
static uint32_t s_rain_duration, s_rain_count, s_rain_prev_t;
static SensorRainData s_rain_prev;

static void ResetPeriod(void)
{
  s_have_wind_slot=0U; s_wind_count=s_pair_count=s_direction_count=0U;
  s_wind_sum=s_wind_max=s_cos=s_sin=s_rain_weight=0.0;
  s_rain_duration=s_rain_count=0U;
}
void AppStatistics_Init(uint32_t start_ms)
{
  memset(s_soil,0,sizeof(s_soil)); memset(s_ph,0,sizeof(s_ph));
  s_anchor=s_start=start_ms; s_sequence=s_last_round=0U;
  s_have_round=s_have_rain=0U; memset(&s_rain_prev,0,sizeof(s_rain_prev));
  ResetPeriod();
}
static void SlowAdd(SlowSlot *slots,const BspAdcResult *raw,uint32_t seq,uint32_t t,uint32_t now)
{
  uint32_t slot=(uint32_t)(t-s_anchor)/APP_SLOW_SLOT_PERIOD_MS;
  SlowSlot *entry=&slots[slot%APP_SLOW_SLOT_COUNT];
  if((uint32_t)(now-t)>APP_SAMPLE_ROUND_PERIOD_MS) { return; }
  if(entry->occupied && entry->slot==slot) { return; }
  entry->occupied=1U; entry->slot=slot; entry->seq=seq; entry->t_ms=t;
  entry->raw=raw->raw; entry->valid=raw->valid && raw->status==BSP_ADC_OK;
}
static AppSensorValue Base(const char *method,uint32_t start,uint32_t end,uint32_t expected)
{
  AppSensorValue v;
  memset(&v,0,sizeof(v)); v.method=method; v.has_sample=1U;
  v.status="WINDOW_INSUFFICIENT"; v.sample_seq=s_sequence; v.sample_t_ms=end;
  v.statistics_start_ms=start; v.statistics_end_ms=end;
  v.statistics_window_ms=(uint32_t)(end-start); v.expected_count=expected;
  return v;
}
static const char *Quality(uint32_t good,uint32_t expected)
{ return good==expected?"OK":"PARTIAL"; }
static uint8_t Covered(uint32_t good,uint32_t expected)
{ return expected && (uint64_t)good*100U>=(uint64_t)expected*APP_WINDOW_MIN_VALID_PERCENT; }
static AppSensorValue SlowFinish(SlowSlot *slots,uint32_t now,uint8_t ph)
{
  AppSensorValue v=Base("median",now-APP_SLOW_SLOT_COUNT*APP_SLOW_SLOT_PERIOD_MS,now,APP_SLOW_SLOT_COUNT);
  uint16_t sorted[APP_SLOW_SLOT_COUNT]; uint32_t n=0U,latest_age=UINT32_MAX;
  uint32_t i,j; uint64_t sum; uint32_t count;
  for(i=0U;i<APP_SLOW_SLOT_COUNT;++i) {
    SlowSlot *entry=&slots[i]; uint32_t age=(uint32_t)(now-entry->t_ms);
    if(!entry->occupied || age>=APP_SLOW_SLOT_COUNT*APP_SLOW_SLOT_PERIOD_MS || !entry->valid) { continue; }
    sorted[n++]=entry->raw;
    if(age<latest_age) { latest_age=age; v.sample_t_ms=entry->t_ms; v.sample_seq=entry->seq; v.has_sample=1U; }
  }
  v.valid_count=n;
  if(n<APP_SLOW_MIN_VALID_COUNT) { return v; }
  if(latest_age>APP_SLOW_LATEST_MAX_AGE_MS) { v.status="STALE"; return v; }
  for(i=1U;i<n;++i) { uint16_t a=sorted[i]; j=i; while(j && sorted[j-1U]>a) { sorted[j]=sorted[j-1U]; --j; } sorted[j]=a; }
  count=(n&1U)?1U:2U; sum=sorted[n/2U]; if(count==2U) { sum+=sorted[n/2U-1U]; }
  if(ph) { SensorPhValue p=SensorPh_Convert(sum,count); v.value=p.soil_ph_x100; v.range=SensorPh_RangeName(p.range); v.valid=p.valid; }
  else { SensorSoilRange range; v.value=SensorSoil_ConvertMean(sum,count,&range); v.range=range==SENSOR_SOIL_RANGE_LOW?"LOW":range==SENSOR_SOIL_RANGE_HIGH?"HIGH":"OK"; v.valid=1U; }
  v.used_count=count; v.status=Quality(n,APP_SLOW_SLOT_COUNT); return v;
}
static void WindAdd(const AppSampleRound *r)
{
  uint32_t offset=(uint32_t)(r->wind_read_end_ms-s_start),slot=offset/APP_SAMPLE_ROUND_PERIOD_MS;
  uint32_t pair_dt=(uint32_t)(r->ad3_read_end_ms-r->wind_read_end_ms);
  if(offset>=APP_SAMPLE_WINDOW_MS || (s_have_wind_slot && slot==s_wind_slot)) { return; }
  s_have_wind_slot=1U; s_wind_slot=slot;
  if(!r->wind.adc.valid || !r->wind.value.valid) { return; }
  ++s_wind_count; s_wind_sum+=r->wind.value.wind_speed_mps;
  if(r->wind.value.wind_speed_mps>s_wind_max) { s_wind_max=r->wind.value.wind_speed_mps; }
  if(!r->wind_direction.valid || pair_dt>APP_DIRECTION_PAIR_MAX_MS) { return; }
  ++s_pair_count;
  if(r->wind.value.wind_speed_mps*100.0>=APP_WIND_CALM_MPS_X100) {
    double radians=r->wind_direction.angle_deg_x100*(3.14159265358979323846/18000.0);
    s_cos+=cos(radians); s_sin+=sin(radians); ++s_direction_count;
  }
}
/* Both endpoints confirm an interval. Signed offsets are safe because every
 * accepted interval is <=300ms and window comparisons stay below half a tick. */
static void RainInterval(uint32_t a,uint32_t b,const SensorRainData *current,uint32_t end)
{
  uint32_t dt=(uint32_t)(b-a); int32_t left=(int32_t)(a-s_start),right=(int32_t)(b-s_start);
  uint32_t limit=(uint32_t)(end-s_start),duration;
  if(!s_have_rain || !s_rain_prev.valid || !current->valid || !dt || dt>APP_RAIN_MAX_GAP_MS) { return; }
  if(left<0) { left=0; } if(right>(int32_t)limit) { right=(int32_t)limit; }
  if(right<=left) { return; }
  duration=(uint32_t)(right-left); s_rain_weight+=s_rain_prev.intensity_mm_min*duration;
  s_rain_duration+=duration; ++s_rain_count;
}
static void Finish(uint32_t end,uint32_t now,AppSensorSnapshot *out)
{
  AppSensorValue *w,*d,*r; double mean=0.0,angle,R;
  memset(out,0,sizeof(*out)); out->window_seq=s_sequence; out->window_end_ms=end;
  out->soil_moisture_pct=SlowFinish(s_soil,now,0U); out->soil_ph=SlowFinish(s_ph,now,1U);
  w=&out->wind_speed_mps; d=&out->wind_direction_deg; r=&out->rain_intensity_mm_min;
  *w=Base("mean",s_start,end,APP_WINDOW_EXPECTED_ROUNDS); w->valid_count=w->used_count=s_wind_count;
  if(Covered(s_wind_count,APP_WINDOW_EXPECTED_ROUNDS)) {
    mean=s_wind_sum/s_wind_count; w->value=(int64_t)(mean*100.0+0.5); w->valid=1U; w->status=Quality(s_wind_count,APP_WINDOW_EXPECTED_ROUNDS);
    out->wind_speed_sample_max_valid=1U; out->wind_speed_sample_max_x100=(uint32_t)(s_wind_max*100.0+0.5);
  }
  *d=Base("circular_mean",s_start,end,APP_WINDOW_EXPECTED_ROUNDS); d->valid_count=s_pair_count; d->used_count=s_direction_count;
  if(Covered(s_pair_count,APP_WINDOW_EXPECTED_ROUNDS)) {
    if(mean*100.0<APP_WIND_CALM_MPS_X100) { d->status="CALM"; }
    else if(s_direction_count>=3U) {
      R=sqrt(s_cos*s_cos+s_sin*s_sin)/s_direction_count;
      if(R*1000.0<APP_DIRECTION_MIN_R_X1000) { d->status="VARIABLE"; }
      else {
        angle=atan2(s_sin,s_cos)*18000.0/3.14159265358979323846; if(angle<0.0) { angle+=36000.0; }
        d->value=(int64_t)(angle+0.5)%36000; d->valid=1U; d->status=Quality(s_pair_count,APP_WINDOW_EXPECTED_ROUNDS);
      }
    }
  }
  *r=Base("time_weighted_mean",s_start,end,APP_WINDOW_EXPECTED_ROUNDS);
  r->valid_count=r->used_count=s_rain_count; r->valid_duration_ms=s_rain_duration;
  if(Covered(s_rain_duration,APP_SAMPLE_WINDOW_MS)) {
    r->value=(int64_t)(s_rain_weight/s_rain_duration*100.0+0.5); r->valid=1U; r->status=Quality(s_rain_duration,APP_SAMPLE_WINDOW_MS);
  }
}
uint8_t AppStatistics_Update(const AppSampleRound *round,uint32_t now,AppSensorSnapshot *out)
{
  uint32_t windows=(uint32_t)(now-s_start)/APP_SAMPLE_WINDOW_MS,end;
  uint8_t fresh=round && (!s_have_round || round->round_id!=s_last_round),closed=0U;
  SensorRainData rain; memset(&rain,0,sizeof(rain));
  if(windows>1U) { s_start+=(windows-1U)*APP_SAMPLE_WINDOW_MS; s_sequence+=windows-1U; ResetPeriod(); }
  end=s_start+APP_SAMPLE_WINDOW_MS;
  if(fresh) {
    s_have_round=1U; s_last_round=round->round_id;
    SlowAdd(s_soil,&round->soil.adc,round->round_id,round->soil_read_end_ms,now);
    SlowAdd(s_ph,&round->ad4,round->round_id,round->ad4_read_end_ms,now);
    WindAdd(round);
    if(round->ad5.valid) { rain=SensorRain_Convert(round->ad5.raw,1U); }
    RainInterval(s_rain_prev_t,round->ad5_read_end_ms,&rain,windows?end:now);
  }
  if(windows) {
    ++s_sequence; Finish(end,now,out); closed=1U; s_start=end; ResetPeriod();
    if(fresh) { WindAdd(round); RainInterval(s_rain_prev_t,round->ad5_read_end_ms,&rain,now); }
  }
  if(fresh) { s_have_rain=1U; s_rain_prev=rain; s_rain_prev_t=round->ad5_read_end_ms; }
  return closed;
}
