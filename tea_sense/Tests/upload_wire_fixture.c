/* Isolated V2 formatter/ACK fixture. Does not access ADC, production config or DTU. */
#include "app_upload.h"
#include "app_statistics.h"
#include "app_config.h"
#include "sensor_soil.h"
#include "bsp_dtu.h"
#include "sensor_wind_speed.h"
#include "sensor_wind_direction.h"
#include "sensor_ph.h"
#include "sensor_rain.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

BspAdcResult BspAdc_ReadRaw(BspAdcInput input)
{ BspAdcResult result={0}; (void)input; assert(!"Fixture must not start ADC"); return result; }

BspDtuStatus BspDtu_Init(void) { assert(!"pure fixture"); return BSP_DTU_UART_ERROR; }
BspDtuTxResult BspDtu_Process(void) { BspDtuTxResult r={0};assert(!"pure fixture");return r; }
BspDtuReadResult BspDtu_Read(uint8_t *p,size_t n) { BspDtuReadResult r={0};(void)p;(void)n;assert(!"pure fixture");return r; }
char *BspDtu_TxBuffer(size_t *n) { (void)n;assert(!"pure fixture");return NULL; }
BspDtuStatus BspDtu_StartSend(size_t n) { (void)n;assert(!"pure fixture");return BSP_DTU_UART_ERROR; }
uint32_t BspTime_GetTickMs(void) { assert(!"pure fixture");return 0U; }


int main(int argc,char **argv)
{
  AppSensorSnapshot snapshot;AppSampleRound round;AppUploadAck ack;
  AppSensorValue *weather[6];char buffer[APP_UPLOAD_FRAME_CAPACITY];
  uint32_t sequence=1U,now,start,tick;unsigned i;int length;
  memset(&snapshot,0,sizeof(snapshot));
  if(argc>1 && !strcmp(argv[1],"missing")) {
    assert(!AppUpload_IsConfigured());
    assert(AppUpload_Format(buffer,sizeof(buffer),1U,180000U,&snapshot)<0);return 0;
  }
  if(argc>1 && !strncmp(argv[1],"ack",3U)) {
    uint32_t expected=strtoul(argv[1]+3,NULL,10);if(!expected)expected=1U;
    assert(fgets(buffer,sizeof(buffer),stdin));
    assert(AppUpload_ParseAck(buffer,strlen(buffer),expected,&ack)==APP_UPLOAD_ACK_ACCEPTED && ack.id>0U);
    puts("ACK_ACCEPTED");return 0;
  }
  if(argc>1) {
    if(!strcmp(argv[1],"failure"))sequence=2U;
    else if(!strcmp(argv[1],"configuration"))sequence=3U;
    else sequence=(uint32_t)strtoul(argv[1],NULL,10);
  }
  assert(sequence>0U);now=sequence*APP_SAMPLE_WINDOW_MS;start=now-APP_SAMPLE_WINDOW_MS;
  AppStatistics_Init(start);
  for(tick=0U;tick<=APP_SAMPLE_WINDOW_MS;tick+=100U) {
    memset(&round,0,sizeof(round));round.round_id=tick/100U+1U;round.round_t_ms=start+tick;
    round.soil_read_end_ms=round.wind_read_end_ms=round.ad3_read_end_ms=round.ad4_read_end_ms=round.ad5_read_end_ms=start+tick;
    round.soil.adc.valid=round.wind.adc.valid=round.ad3.valid=round.ad4.valid=round.ad5.valid=1U;
    round.soil.adc.raw=19122U;round.wind.value=SensorWindSpeed_ConvertMean(15793U,1U);
    round.wind_direction=SensorWindDirection_Convert(1561400U);round.ad4.raw=31398U;round.ad5.raw=15723U;
    if(argc>1 && ((!strcmp(argv[1],"failure") && tick>=140000U) || !strcmp(argv[1],"configuration"))) {
      round.soil.adc.valid=round.ad5.valid=0U;round.soil.adc.status=round.ad5.status=BSP_ADC_POLL_FAILED;
    }
    AppStatistics_Update(&round,start+tick,&snapshot);
  }
  snapshot.window_seq=sequence;
  weather[0]=&snapshot.temperature_c;weather[1]=&snapshot.air_humidity_rh_pct;
  weather[2]=&snapshot.light_lux;weather[3]=&snapshot.pressure_kpa;weather[4]=&snapshot.pm25_ug_m3;weather[5]=&snapshot.pm10_ug_m3;
  for(i=0U;i<6U;++i) {
    weather[i]->method="latest";weather[i]->sample_t_ms=now-1000U;
    weather[i]->sample_seq=sequence*100U+i;
    weather[i]->valid=weather[i]->has_sample=1U;weather[i]->status="OK";
    weather[i]->latest_query_status="OK";weather[i]->latest_query_seq=weather[i]->sample_seq;weather[i]->latest_query_t_ms=weather[i]->sample_t_ms;
  }
  weather[0]->value=194;weather[1]->value=708;weather[2]->value=1249;
  weather[3]->value=10373;weather[4]->value=22;weather[5]->value=23;
  if(argc>1 && !strcmp(argv[1],"configuration")) {
    weather[0]->valid=0U;weather[0]->status="TIMEOUT";weather[0]->sample_t_ms=now;
    weather[0]->latest_query_status="TIMEOUT";weather[0]->latest_query_t_ms=now;
  }
  length=AppUpload_Format(buffer,sizeof(buffer),sequence,now,&snapshot);
  assert(length>0 && (size_t)length<sizeof(buffer));
  assert(buffer[length-2]=='\r' && buffer[length-1]=='\n');
  fwrite(buffer,1U,(size_t)length,stdout);return 0;
}
