#include "app_statistics.h"
#include "app_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
BspAdcResult BspAdc_ReadRaw(BspAdcInput input)
{ BspAdcResult r={0};(void)input;assert(!"Statistics cannot acquire ADC");return r; }
static AppSampleRound Round(uint32_t seq,uint32_t t,double speed,uint32_t angle,uint16_t soil,uint16_t ph,uint16_t rain)
{
  AppSampleRound r;memset(&r,0,sizeof(r));r.round_id=seq;r.round_t_ms=t;
  r.soil_read_end_ms=r.wind_read_end_ms=r.ad3_read_end_ms=r.ad4_read_end_ms=r.ad5_read_end_ms=t;
  r.soil.adc.valid=r.wind.adc.valid=r.ad3.valid=r.ad4.valid=r.ad5.valid=1U;
  r.soil.adc.raw=soil;r.ad4.raw=ph;r.ad5.raw=rain;
  r.wind.value.valid=1U;r.wind.value.wind_speed_mps=speed;
  r.wind_direction.valid=1U;r.wind_direction.angle_deg_x100=angle;
  return r;
}
static void SlowCases(void)
{
  AppSampleRound r;AppSensorSnapshot out;uint32_t i;
  AppStatistics_Init(0U);
  for(i=0U;i<5U;++i) {
    r=Round(i+1U,176000U+1000U*i,1.0,0U,i==4U?28075U:6578U,31398U,0U);
    assert(AppStatistics_Update(&r,r.round_t_ms,&out)==(i==4U));
    assert(!AppStatistics_Update(&r,r.round_t_ms,&out));
  }
  assert(out.soil_moisture_pct.value==2000 && out.soil_moisture_pct.valid_count==5U);
  assert(out.soil_ph.value==600 && out.soil_ph.sample_t_ms==180000U);
  AppStatistics_Init(0U);
  for(i=0U;i<5U;++i) {
    r=Round(i+1U,176000U+1000U*i,1.0,0U,(uint16_t)(436U+6142U*i),0U,0U);
    if(i==4U) { r.soil.adc.valid=r.ad4.valid=0U; }
    AppStatistics_Update(&r,r.round_t_ms,&out);
  }
  assert(out.soil_moisture_pct.value==3000 && out.soil_moisture_pct.used_count==2U);
  assert(!strcmp(out.soil_moisture_pct.status,"PARTIAL") && out.soil_moisture_pct.sample_t_ms==179000U);
  assert(out.soil_ph.valid && out.soil_ph.value==300 && !strcmp(out.soil_ph.range,"LOW"));
  AppStatistics_Init(0U);
  for(i=0U;i<5U;++i) {
    r=Round(i+1U,176000U+1000U*i,1.0,0U,0U,0U,0U);
    if(i>=3U) { r.ad4.valid=0U; }
    AppStatistics_Update(&r,r.round_t_ms,&out);
  }
  assert(!out.soil_ph.valid && out.soil_ph.valid_count==3U && out.soil_moisture_pct.valid && out.soil_moisture_pct.value==0);
  /* Half-raw medians straddle a final rounding boundary. Rounding the raw
   * median first would incorrectly give soil0.01 and pH3.01 here. */
  AppStatistics_Init(0U);
  for(i=0U;i<5U;++i) {
    const uint16_t soil[5]={0U,437U,438U,65535U,0U},ph[5]={0U,106U,107U,1000U,0U};
    r=Round(i+1U,176000U+1000U*i,1.0,0U,soil[i],ph[i],0U);
    if(i==4U) { r.soil.adc.valid=r.ad4.valid=0U; }
    AppStatistics_Update(&r,r.round_t_ms,&out);
  }
  assert(out.soil_moisture_pct.valid && out.soil_moisture_pct.value==0 && out.soil_ph.valid && out.soil_ph.value==300);
  AppStatistics_Init(0U);r=Round(1U,176000U,1.0,0U,0U,0U,0U);
  AppStatistics_Update(&r,176000U,&out);
  for(i=177000U;i<=180000U;i+=1000U) { AppStatistics_Update(&r,i,&out); }
  assert(!out.soil_ph.valid && out.soil_ph.valid_count==1U); /* one cache cannot fill five slots */
  AppStatistics_Init(0U);r=Round(1U,1000U,1.0,0U,0U,0U,0U);AppStatistics_Update(&r,1000U,&out);
  AppStatistics_Update(NULL,180000U,&out);assert(!out.soil_ph.valid && !out.soil_ph.valid_count);
}
static void PeriodCases(void)
{
  uint32_t scenario,i;AppSensorSnapshot out;AppSampleRound r;
  for(scenario=0U;scenario<9U;++scenario) {
    AppStatistics_Init(0U);
    for(i=0U;i<=1800U;++i) {
      double speed=scenario==2U?0.1:i<900U?2.0:4.0;
      uint32_t angle=scenario==1U?(i&1U?9000U:27000U):(i&1U?35900U:100U);
      uint16_t rain=i<300U?7933U:0U;
      r=Round(i+1U,i*100U,speed,angle,6578U,31398U,rain);
      if(scenario==3U && i<361U) { r.wind.adc.valid=0U; }
      if(scenario==4U && i<360U) { r.wind.adc.valid=0U; }
      if(scenario==5U && i<400U) { r.ad5.valid=0U; }
      if(scenario==6U) { r.ad3_read_end_ms+=101U; }
      if(scenario==7U && i<360U) { r.ad5.valid=0U; }
      if(scenario==8U && i<361U) { r.ad5.valid=0U; }
      AppStatistics_Update(&r,r.round_t_ms,&out);
      /* Same source and same planned slot never inflate coverage. */
      AppStatistics_Update(&r,r.round_t_ms,&out);
    }
    if(scenario==3U) { assert(!out.wind_speed_mps.valid && !out.wind_speed_sample_max_valid); }
    else {
      assert(out.wind_speed_mps.valid && out.wind_speed_sample_max_valid);
      if(scenario==4U) { assert(!strcmp(out.wind_speed_mps.status,"PARTIAL") && out.wind_speed_mps.valid_count==1440U); }
      else if(scenario!=2U) { assert(out.wind_speed_mps.value==300 && out.wind_speed_sample_max_x100==400U && out.wind_speed_mps.valid_count==1800U); }
    }
    if(scenario==0U) { assert(out.wind_direction_deg.valid && out.wind_direction_deg.value==0); }
    if(scenario==1U) { assert(!out.wind_direction_deg.valid && !strcmp(out.wind_direction_deg.status,"VARIABLE")); }
    if(scenario==2U) { assert(!out.wind_direction_deg.valid && !strcmp(out.wind_direction_deg.status,"CALM")); }
    if(scenario==6U) { assert(!out.wind_direction_deg.valid && out.wind_speed_mps.valid); }
    if(scenario==5U || scenario==8U) { assert(!out.rain_intensity_mm_min.valid); }
    else if(scenario==7U) { assert(out.rain_intensity_mm_min.valid && !strcmp(out.rain_intensity_mm_min.status,"PARTIAL") && out.rain_intensity_mm_min.valid_duration_ms==144000U && out.rain_intensity_mm_min.value==0); }
    else { assert(out.rain_intensity_mm_min.valid && out.rain_intensity_mm_min.value==33 && out.rain_intensity_mm_min.valid_duration_ms==180000U); }
  }
}
static void IrregularAndBoundary(void)
{
  AppSampleRound r;AppSensorSnapshot out;uint32_t t=0U,seq=0U,base=UINT32_MAX-1000U;
  AppStatistics_Init(base);
  while(t<180000U) {
    r=Round(++seq,base+t,2.0,35900U,0U,0U,7933U);AppStatistics_Update(&r,base+t,&out);
    t+=(t%200U==0U)?150U:50U;
  }
  r=Round(++seq,base+180050U,30.0,0U,0U,0U,0U);assert(AppStatistics_Update(&r,base+180050U,&out));
  assert(out.window_end_ms==base+180000U && out.wind_speed_mps.value==200 && out.wind_speed_sample_max_x100==200U);
  assert(out.rain_intensity_mm_min.value==200 && out.rain_intensity_mm_min.valid_duration_ms==180000U);
  /* Cross-boundary 50ms tail retains the previous confirmed intensity. */
  t=180100U;
  while(t<=360000U) { r=Round(++seq,base+t,4.0,100U,0U,0U,0U);AppStatistics_Update(&r,base+t,&out);t+=100U; }
  assert(out.window_seq==2U && out.wind_speed_mps.valid_count==1800U && out.wind_speed_sample_max_x100==3000U);
  assert(out.rain_intensity_mm_min.valid_duration_ms==180000U);
  /* A long gap is wholly missing, never old-value holding or zero fill. */
  AppStatistics_Init(0U);r=Round(1U,0U,2.0,0U,0U,0U,7933U);AppStatistics_Update(&r,0U,&out);
  r=Round(2U,400U,2.0,0U,0U,0U,0U);AppStatistics_Update(&r,400U,&out);
  r=Round(3U,500U,2.0,0U,0U,0U,0U);AppStatistics_Update(&r,500U,&out);
  AppStatistics_Update(NULL,180000U,&out);assert(out.rain_intensity_mm_min.valid_duration_ms==100U && !out.rain_intensity_mm_min.valid);
  AppStatistics_Update(NULL,900000U,&out);assert(out.window_seq==5U && !out.wind_speed_mps.valid_count && !out.rain_intensity_mm_min.valid_duration_ms);
}
int main(void)
{
  SlowCases();PeriodCases();IrregularAndBoundary();
  puts("PASS: App median/fraction/zero/dedup, 180s mean/max/coverage, circular calm/variable/pairing, rain real-time/gaps/boundary/wrap/no backlog");return 0;
}
