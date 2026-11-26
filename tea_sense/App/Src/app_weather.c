#include "app_weather.h"
#include "app_config.h"
#include "bsp_config.h"
#include "bsp_time.h"
#include <string.h>

typedef struct {
  AppWeatherPortDiagnostics diagnostics;
  SensorWeatherParser parser;
  uint32_t tx_end_ms, rx_start_ms, rx_end_ms, rx_count, probe_last_ms;
  uint8_t rx_trace[32], trace_length, trace_truncated, ended_once;
  SensorWeatherItem cursor;
} AppWeatherPort;
static AppWeatherPort s_ports[BSP_WEATHER_RS485_PORT_COUNT];
static uint8_t s_attempted[SENSOR_WEATHER_ITEM_COUNT];
static uint32_t s_sequence;

uint32_t AppWeather_PeriodMs(SensorWeatherItem item)
{
  switch(item) {
    case SENSOR_WEATHER_TEMP_HUM:return APP_WEATHER_TEMP_HUM_PERIOD_MS;
    case SENSOR_WEATHER_LIGHT:return APP_WEATHER_LIGHT_PERIOD_MS;
    case SENSOR_WEATHER_PM:return APP_WEATHER_PM_PERIOD_MS;
    case SENSOR_WEATHER_PRESSURE:return APP_WEATHER_PRESSURE_PERIOD_MS;
    case SENSOR_WEATHER_CO2:return APP_WEATHER_CO2_PERIOD_MS;
    default:return APP_WEATHER_OXYGEN_PERIOD_MS;
  }
}
static SensorWeatherStatus AppWeather_UartStatus(BspWeatherRs485Status status)
{
  switch(status) {
    case BSP_WEATHER_RS485_UART_BUSY:return SENSOR_WEATHER_UART_BUSY;
    case BSP_WEATHER_RS485_UART_TIMEOUT:return SENSOR_WEATHER_UART_TIMEOUT;
    case BSP_WEATHER_RS485_RX_NOT_READY:return SENSOR_WEATHER_RX_NOT_READY;
    case BSP_WEATHER_RS485_IRQ_DISABLED:return SENSOR_WEATHER_IRQ_DISABLED;
    default:return SENSOR_WEATHER_UART_ERROR;
  }
}
/* Complete/cache diagnostics exactly once; failed fields never use old values. */
static void AppWeather_Finish(BspWeatherRs485Port port, SensorWeatherStatus status, uint32_t now_ms)
{
  AppWeatherPort *state=&s_ports[port];
  AppWeatherPortDiagnostics *diagnostics=&state->diagnostics;
  SensorWeatherReply *reply=&state->parser.reply;
  uint8_t legal=(status==SENSOR_WEATHER_OK || status==SENSOR_WEATHER_MODBUS_EXCEPTION);
  reply->status=status;
  SensorWeather_Complete(diagnostics->item,reply,state->rx_end_ms,now_ms);
  diagnostics->waiting=0U; diagnostics->last_status=status; diagnostics->last_complete_ms=now_ms;
  ++diagnostics->completions; diagnostics->echo_count+=state->parser.echo_count; state->ended_once=1U;
  if(legal) { diagnostics->no_response_streak=0U; diagnostics->backoff=0U; }
  else {
    if(diagnostics->no_response_streak<APP_WEATHER_NO_RESPONSE_LIMIT) { ++diagnostics->no_response_streak; }
    if(diagnostics->no_response_streak>=APP_WEATHER_NO_RESPONSE_LIMIT && !diagnostics->backoff) {
      diagnostics->backoff=1U; ++diagnostics->backoff_count; state->probe_last_ms=now_ms;
    }
  }
}
static uint8_t AppWeather_LineReady(BspWeatherRs485Port port, uint32_t now_ms)
{
  BspWeatherRs485Diagnostics diagnostics=BspWeatherRs485_GetDiagnostics(port);
  return diagnostics.rx_active && (!diagnostics.rx_seen || (uint32_t)(now_ms-diagnostics.last_rx_ms)>=BSP_WEATHER_RX_QUIET_MS);
}
static uint8_t AppWeather_GapReady(BspWeatherRs485Port port, uint32_t now_ms)
{ return !s_ports[port].ended_once || (uint32_t)(now_ms-s_ports[port].diagnostics.last_complete_ms)>=BSP_WEATHER_TRANSACTION_GAP_MS; }
static void AppWeather_Start(SensorWeatherItem item, uint32_t now_ms)
{
  BspWeatherRs485Port port=SensorWeather_Port(item);
  AppWeatherPort *state=&s_ports[port];
  BspWeatherRs485Status status;
  SensorWeather_ParserInit(&state->parser,item);
  state->trace_length=state->trace_truncated=0U; state->rx_count=0U; state->rx_start_ms=state->rx_end_ms=0U;
  state->diagnostics.item=item; state->diagnostics.query_sequence=++s_sequence;
  state->diagnostics.last_start_ms=now_ms; state->diagnostics.uart_error=0U;
  state->diagnostics.waiting=1U; ++state->diagnostics.queries; s_attempted[item]=1U;
  if(state->diagnostics.backoff) { ++state->diagnostics.probe_count; state->probe_last_ms=now_ms; }
  SensorWeather_RecordQuery(item,s_sequence,now_ms);
  state->cursor=(SensorWeatherItem)(item+1);
  if(port==BSP_WEATHER_RS485_MET && state->cursor>SENSOR_WEATHER_PRESSURE) { state->cursor=SENSOR_WEATHER_TEMP_HUM; }
  if(port==BSP_WEATHER_RS485_GAS && state->cursor>SENSOR_WEATHER_OXYGEN) { state->cursor=SENSOR_WEATHER_CO2; }
  status=BspWeatherRs485_Send(port,state->parser.request,8U); state->tx_end_ms=BspTime_GetTickMs();
  if(status!=BSP_WEATHER_RS485_OK) { AppWeather_Finish(port,AppWeather_UartStatus(status),state->tx_end_ms); }
}
static void AppWeather_Receive(BspWeatherRs485Port port)
{
  AppWeatherPort *state=&s_ports[port]; unsigned batch;
  BspWeatherRs485_Process(port);
  for(batch=0U;batch<4U;++batch) {
    BspWeatherRs485Byte bytes[16]; size_t index;
    BspWeatherRs485ReadResult received=BspWeatherRs485_Read(port,bytes,16U);
    if(received.data_lost && state->diagnostics.waiting) {
      state->diagnostics.uart_error=received.uart_error;
      AppWeather_Finish(port,received.lost_status!=BSP_WEATHER_RS485_OK?AppWeather_UartStatus(received.lost_status):SENSOR_WEATHER_RX_OVERFLOW,BspTime_GetTickMs());
    }
    for(index=0U;index<received.length;++index) {
      if(!state->diagnostics.waiting || (uint32_t)(bytes[index].t_ms-state->diagnostics.last_start_ms)>=
          (uint32_t)(state->tx_end_ms-state->diagnostics.last_start_ms)+BSP_WEATHER_RESPONSE_TIMEOUT_MS) {
        ++state->diagnostics.ignored_idle_bytes; continue;
      }
      if(!state->rx_count) { state->rx_start_ms=bytes[index].t_ms; }
      state->rx_end_ms=bytes[index].t_ms; ++state->rx_count;
      if(state->trace_length<sizeof(state->rx_trace)) { state->rx_trace[state->trace_length++]=bytes[index].byte; }
      else { state->trace_truncated=1U; }
      if(SensorWeather_ParseByte(&state->parser,bytes[index].byte,bytes[index].t_ms,BSP_WEATHER_RX_QUIET_MS)) {
        AppWeather_Finish(port,state->parser.reply.status,BspTime_GetTickMs());
      }
    }
    if(received.length<16U) { break; }
  }
  if(state->diagnostics.waiting && (uint32_t)(BspTime_GetTickMs()-state->tx_end_ms)>=BSP_WEATHER_RESPONSE_TIMEOUT_MS) {
    AppWeather_Finish(port,SensorWeather_ParserError(&state->parser),BspTime_GetTickMs());
  }
}
void AppWeather_Init(void)
{
  unsigned port;
  memset(s_ports,0,sizeof(s_ports)); memset(s_attempted,0,sizeof(s_attempted)); s_sequence=0U; SensorWeather_Init();
  s_ports[BSP_WEATHER_RS485_MET].cursor=SENSOR_WEATHER_TEMP_HUM;
  s_ports[BSP_WEATHER_RS485_GAS].cursor=SENSOR_WEATHER_CO2;
  for(port=0U;port<(APP_WEATHER_GAS_ENABLED?BSP_WEATHER_RS485_PORT_COUNT:1U);++port) {
    s_ports[port].diagnostics.init_status=BspWeatherRs485_Init((BspWeatherRs485Port)port);
  }
}
void AppWeather_Process(void)
{
  unsigned port,index; uint32_t now_ms;
  for(port=0U;port<(APP_WEATHER_GAS_ENABLED?BSP_WEATHER_RS485_PORT_COUNT:1U);++port) { AppWeather_Receive((BspWeatherRs485Port)port); }
  now_ms=BspTime_GetTickMs();
  /* Healthy startup has exactly the listed six-query order. A disabled/noisy
   * or backed-off route cannot prevent the other route from starting. */
  for(index=0U;index<(APP_WEATHER_GAS_ENABLED?SENSOR_WEATHER_ITEM_COUNT:4U);++index) {
    BspWeatherRs485Port route=SensorWeather_Port((SensorWeatherItem)index);
    if(s_attempted[index] || s_ports[route].diagnostics.backoff) { continue; }
    if(s_ports[0].diagnostics.waiting || s_ports[1].diagnostics.waiting || !AppWeather_GapReady(route,now_ms)) { return; }
    if(!AppWeather_LineReady(route,now_ms)) { continue; }
    AppWeather_Start((SensorWeatherItem)index,now_ms); return;
  }
  for(port=0U;port<(APP_WEATHER_GAS_ENABLED?BSP_WEATHER_RS485_PORT_COUNT:1U);++port) {
    AppWeatherPort *state=&s_ports[port];
    if(state->diagnostics.waiting || !AppWeather_GapReady((BspWeatherRs485Port)port,now_ms) || !AppWeather_LineReady((BspWeatherRs485Port)port,now_ms)) { continue; }
    if(state->diagnostics.backoff) {
      if((uint32_t)(now_ms-state->probe_last_ms)>=APP_WEATHER_PROBE_PERIOD_MS) {
        AppWeather_Start(port==BSP_WEATHER_RS485_MET?SENSOR_WEATHER_TEMP_HUM:SENSOR_WEATHER_CO2,now_ms);
      }
    } else {
      unsigned count=port==BSP_WEATHER_RS485_MET?4U:2U;
      for(index=0U;index<count;++index) {
        SensorWeatherItem item=(SensorWeatherItem)(port==BSP_WEATHER_RS485_MET?((unsigned)state->cursor+index)%4U:4U+((unsigned)state->cursor-4U+index)%2U);
        const SensorWeatherMeasurement *measurement=SensorWeather_GetMeasurement(item);
        if(!s_attempted[item] || (uint32_t)(now_ms-measurement->last_query_t_ms)>=AppWeather_PeriodMs(item)) {
          AppWeather_Start(item,now_ms); break;
        }
      }
    }
    now_ms=BspTime_GetTickMs();
  }
}
const AppWeatherPortDiagnostics *AppWeather_GetDiagnostics(BspWeatherRs485Port port)
{ return (unsigned)port<(APP_WEATHER_GAS_ENABLED?BSP_WEATHER_RS485_PORT_COUNT:1U)?&s_ports[port].diagnostics:NULL; }
