#ifndef APP_WEATHER_H
#define APP_WEATHER_H
#include "sensor_weather.h"
typedef struct {
  uint32_t query_sequence, queries, completions, last_start_ms, last_complete_ms;
  uint32_t no_response_streak, backoff_count, probe_count, ignored_idle_bytes, echo_count, uart_error;
  uint32_t printf_errors;
  uint8_t waiting, backoff;
  SensorWeatherItem item;
  SensorWeatherStatus last_status;
  BspWeatherRs485Status init_status;
} AppWeatherPortDiagnostics;
void AppWeather_Init(void);
void AppWeather_Process(void);
const AppWeatherPortDiagnostics *AppWeather_GetDiagnostics(BspWeatherRs485Port port);
uint32_t AppWeather_PeriodMs(SensorWeatherItem item);
#endif
