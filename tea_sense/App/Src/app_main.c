#include "app_main.h"
#include "app_diagnostics.h"
#include "app_config.h"
#include "app_data.h"
#include "app_weather.h"
#include "app_statistics.h"
#include "sensor_soil.h"
#include "app_upload.h"
#include "bsp_time.h"
#include <string.h>
static uint32_t s_last_sample_tick_ms;
static uint8_t s_sampling_started, s_window_ready;
static AppSensorSnapshot s_snapshot;
static AppDiagnostics s_diagnostics;
static AppSensorValue App_WeatherResult(SensorWeatherItem item, uint16_t field, int64_t value, uint32_t now_ms)
{
  AppSensorValue result;
  const SensorWeatherMeasurement *measurement=SensorWeather_GetMeasurement(item);
  memset(&result,0,sizeof(result)); result.status="NO_DATA"; result.method="latest";
  if(!measurement->has_completed) { return result; }
  result.latest_query_seq=measurement->completed_sequence;
  result.latest_query_t_ms=measurement->completed_t_ms;
  result.latest_query_status=SensorWeather_StatusName(measurement->completed_status);
  if(measurement->completed_status==SENSOR_WEATHER_OK && !(measurement->valid_fields&field)) { result.latest_query_status="INVALID_VALUE"; }
  if(!(measurement->cached_fields&field)) { result.status=result.latest_query_status; result.has_sample=1U; result.sample_seq=measurement->completed_sequence; result.sample_t_ms=measurement->sample_t_ms; return result; }
  { unsigned index=0U; uint16_t mask=field; while(mask>1U) { mask>>=1U; ++index; }
    result.sample_seq=measurement->field_sequence[index]; result.sample_t_ms=measurement->field_t_ms[index]; }
  result.has_sample=1U; result.value=value; result.cached=result.sample_seq!=measurement->completed_sequence;
  if((uint32_t)(now_ms-result.sample_t_ms)>3U*AppWeather_PeriodMs(item)) { result.status="STALE"; }
  else { result.valid=1U; result.status=result.cached?"CACHED":"OK"; }
  return result;
}

static void App_FreezeWeather(uint32_t now_ms)
{
  const SensorWeatherValue *temp=&SensorWeather_GetMeasurement(SENSOR_WEATHER_TEMP_HUM)->value;
  const SensorWeatherValue *light=&SensorWeather_GetMeasurement(SENSOR_WEATHER_LIGHT)->value;
  const SensorWeatherValue *pm=&SensorWeather_GetMeasurement(SENSOR_WEATHER_PM)->value;
  const SensorWeatherValue *pressure=&SensorWeather_GetMeasurement(SENSOR_WEATHER_PRESSURE)->value;
  s_snapshot.temperature_c=App_WeatherResult(SENSOR_WEATHER_TEMP_HUM,SENSOR_WEATHER_FIELD_TEMPERATURE,temp->temperature_c_x10,now_ms);
  s_snapshot.air_humidity_rh_pct=App_WeatherResult(SENSOR_WEATHER_TEMP_HUM,SENSOR_WEATHER_FIELD_HUMIDITY,temp->humidity_rh_pct_x10,now_ms);
  s_snapshot.light_lux=App_WeatherResult(SENSOR_WEATHER_LIGHT,SENSOR_WEATHER_FIELD_LIGHT,light->light_lux,now_ms);
  s_snapshot.pm25_ug_m3=App_WeatherResult(SENSOR_WEATHER_PM,SENSOR_WEATHER_FIELD_PM25,pm->pm25_ug_m3,now_ms);
  s_snapshot.pm10_ug_m3=App_WeatherResult(SENSOR_WEATHER_PM,SENSOR_WEATHER_FIELD_PM10,pm->pm10_ug_m3,now_ms);
  s_snapshot.pressure_kpa=App_WeatherResult(SENSOR_WEATHER_PRESSURE,SENSOR_WEATHER_FIELD_PRESSURE,pressure->pressure_kpa_x100,now_ms);
}

void App_Init(void)
{
  memset(&s_diagnostics,0,sizeof(s_diagnostics));
  AppUpload_Init(); BspAdc_Init(); AppWeather_Init();
  s_last_sample_tick_ms=BspTime_GetTickMs(); AppStatistics_Init(s_last_sample_tick_ms);
  memset(&s_snapshot,0,sizeof(s_snapshot)); s_sampling_started=s_window_ready=0U;
}

static void App_RecordAdcError(AppAdcDiagnostics *d,const BspAdcResult *a)
{
  if(a->valid) { return; }
  if((unsigned)a->status<=BSP_ADC_OUT_OF_RANGE) { ++d->error_counts[a->status]; }
  if(a->stop_attempted && a->stop_status!=BSP_ADC_HAL_OK) { ++d->cleanup_errors; }
  if(a->prepare_attempted && a->prepare_status!=BSP_ADC_HAL_OK) { ++d->prepare_errors; }
  if(a->config_attempted && a->config_status!=BSP_ADC_HAL_OK) { ++d->config_errors; }
  d->last_error=*a;
}

/* Keep the round snapshot frame off the receive/parser call paths. */
static void App_AcquireRound(uint32_t now_ms)
{
  AppSampleRound round;
  memset(&round, 0, sizeof(round));
  s_sampling_started = 1U;
  s_last_sample_tick_ms = now_ms;
  round.round_id = ++s_diagnostics.sequence;
  round.round_t_ms = now_ms;
  round.soil_read_start_ms = BspTime_GetTickMs();
  round.soil = SensorSoil_Read();
  round.soil_read_end_ms = BspTime_GetTickMs();
  round.wind_read_start_ms = BspTime_GetTickMs();
  round.wind = SensorWindSpeed_Read();
  round.wind_read_end_ms = BspTime_GetTickMs();
  /* Preserve the existing AD0/AD2 pair duration. */
  round.pair_span_ms = (uint32_t)(BspTime_GetTickMs() - round.round_t_ms);
  round.ad3_read_start_ms = BspTime_GetTickMs();
  round.ad3 = BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_DIRECTION_AD3);
  round.ad3_read_end_ms = BspTime_GetTickMs();
  round.ad4_read_start_ms = BspTime_GetTickMs();
  round.ad4 = BspAdc_ReadRaw(BSP_ADC_INPUT_PH_AD4);
  round.ad4_read_end_ms = BspTime_GetTickMs();
  ++s_diagnostics.ad5_sequence;
  s_diagnostics.last_ad5 = BspAdc_ReadRaw(BSP_ADC_INPUT_RAIN_AD5);
  s_diagnostics.ad5_sample_t_ms = BspTime_GetTickMs();
  round.ad5=s_diagnostics.last_ad5; round.ad5_read_end_ms=s_diagnostics.ad5_sample_t_ms;
  if(!s_diagnostics.last_ad5.valid) { ++s_diagnostics.ad5_sample_errors; }
  if (round.ad4.valid && round.ad4.status == BSP_ADC_OK)
  {
    round.soil_ph = SensorPh_Convert(round.ad4.raw, 1U);
  }
  if (round.ad3.valid && round.ad3.status == BSP_ADC_OK)
  {
    round.wind_direction = SensorWindDirection_Convert((uint32_t)round.ad3.raw * 100U);
  }
  if (!round.ad3.valid) { ++s_diagnostics.ad3_sample_errors; }
  if (!round.ad4.valid) { ++s_diagnostics.ad4_sample_errors; }
  App_RecordAdcError(&s_diagnostics.ad3_diagnostics,&round.ad3);
  App_RecordAdcError(&s_diagnostics.ad4_diagnostics,&round.ad4);
  App_RecordAdcError(&s_diagnostics.ad5_diagnostics,&round.ad5);
  round.round_span_ms = (uint32_t)(BspTime_GetTickMs() - round.round_t_ms);
  s_diagnostics.last_round = round;
  if (!round.soil.adc.valid)
  {
    ++s_diagnostics.sample_errors;
    if ((unsigned)round.soil.adc.status <= BSP_ADC_OUT_OF_RANGE)
    {
      ++s_diagnostics.sample_error_counts[round.soil.adc.status];
    }
    s_diagnostics.last_adc_error = round.soil.adc;
    if (round.soil.adc.stop_attempted && round.soil.adc.stop_status != BSP_ADC_HAL_OK)
    {
      ++s_diagnostics.cleanup_errors;
    }
    if(round.soil.adc.prepare_attempted && round.soil.adc.prepare_status!=BSP_ADC_HAL_OK) { ++s_diagnostics.prepare_errors; }
    if(round.soil.adc.config_attempted && round.soil.adc.config_status!=BSP_ADC_HAL_OK) { ++s_diagnostics.config_errors; }
  }
  if (!round.wind.adc.valid)
  {
    const BspAdcResult *error = &round.wind.adc;
    SensorWindSpeedDiagnostics *diagnostics = &s_diagnostics.wind_diagnostics;
    ++s_diagnostics.wind_sample_errors;
    if ((unsigned)error->status <= BSP_ADC_OUT_OF_RANGE) { ++diagnostics->error_counts[error->status]; }
    if (error->stop_attempted && error->stop_status != BSP_ADC_HAL_OK) { ++diagnostics->cleanup_error_count; }
    if (error->prepare_attempted && error->prepare_status != BSP_ADC_HAL_OK) { ++diagnostics->prepare_error_count; }
    if (error->config_attempted && error->config_status != BSP_ADC_HAL_OK) { ++diagnostics->config_error_count; }
    diagnostics->last_error = *error;
  }
}

void App_Process(void)
{
  uint32_t now_ms; uint8_t acquired=0U;
  AppUpload_Process(); BspAdc_Process(); now_ms=BspTime_GetTickMs();
  if(!s_sampling_started || (uint32_t)(now_ms-s_last_sample_tick_ms)>=APP_SAMPLE_ROUND_PERIOD_MS) {
    App_AcquireRound(now_ms); acquired=1U;
  }
  AppWeather_Process(); now_ms=BspTime_GetTickMs();
  if(AppStatistics_Update(acquired?&s_diagnostics.last_round:NULL,now_ms,&s_snapshot)) {
    s_diagnostics.completed_windows=s_snapshot.window_seq; s_diagnostics.last_window_end_ms=s_snapshot.window_end_ms;
    App_FreezeWeather(now_ms); s_window_ready=1U;
  }
  if(s_window_ready && AppUpload_Submit(&s_snapshot)!=APP_UPLOAD_SUBMIT_RATE_LIMITED) { s_window_ready=0U; }
}
const AppDiagnostics *App_GetDiagnostics(void) { return &s_diagnostics; }
