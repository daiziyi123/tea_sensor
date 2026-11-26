#include "sensor_weather.h"
#include <string.h>
static SensorWeatherMeasurement s_measurements[SENSOR_WEATHER_ITEM_COUNT];

static uint16_t SensorWeather_Register(SensorWeatherItem item)
{
  switch(item) {
    case SENSOR_WEATHER_LIGHT:return 2U;
    case SENSOR_WEATHER_PM:return 20U;
    case SENSOR_WEATHER_PRESSURE:return 11U;
    case SENSOR_WEATHER_CO2:return 7U;
    case SENSOR_WEATHER_OXYGEN:return 8U;
    default:return 0U;
  }
}
static uint8_t SensorWeather_Count(SensorWeatherItem item)
{ return item<=SENSOR_WEATHER_PM ? 2U : 1U; }
static uint16_t SensorWeather_Word(const uint8_t *bytes)
{ return (uint16_t)(((uint16_t)bytes[0]<<8)|bytes[1]); }
uint16_t SensorWeather_Crc16(const uint8_t *bytes, size_t length)
{
  uint16_t crc=0xFFFFU; size_t index; unsigned bit;
  for(index=0U;index<length;++index) {
    crc^=bytes[index];
    for(bit=0U;bit<8U;++bit) { crc=(crc&1U)?(uint16_t)((crc>>1)^0xA001U):(uint16_t)(crc>>1); }
  }
  return crc;
}
uint8_t SensorWeather_BuildRequest(SensorWeatherItem item, uint8_t request[8])
{
  uint16_t address,crc;
  if((unsigned)item>=SENSOR_WEATHER_ITEM_COUNT || !request) { return 0U; }
  address=SensorWeather_Register(item);
  request[0]=SENSOR_WEATHER_ADDRESS; request[1]=3U;
  request[2]=(uint8_t)(address>>8); request[3]=(uint8_t)address;
  request[4]=0U; request[5]=SensorWeather_Count(item);
  crc=SensorWeather_Crc16(request,6U); request[6]=(uint8_t)crc; request[7]=(uint8_t)(crc>>8); return 1U;
}
BspWeatherRs485Port SensorWeather_Port(SensorWeatherItem item)
{ return item>=SENSOR_WEATHER_CO2 ? BSP_WEATHER_RS485_GAS : BSP_WEATHER_RS485_MET; }
const char *SensorWeather_ItemName(SensorWeatherItem item)
{
  switch(item) {
    case SENSOR_WEATHER_TEMP_HUM:return "TEMP_HUM";
    case SENSOR_WEATHER_LIGHT:return "LIGHT";
    case SENSOR_WEATHER_PM:return "PM";
    case SENSOR_WEATHER_PRESSURE:return "PRESSURE";
    case SENSOR_WEATHER_CO2:return "CO2";
    case SENSOR_WEATHER_OXYGEN:return "OXYGEN";
    default:return "INVALID";
  }
}
const char *SensorWeather_StatusName(SensorWeatherStatus status)
{
  switch(status) {
    case SENSOR_WEATHER_NO_DATA:return "NO_DATA";
    case SENSOR_WEATHER_PENDING:return "PENDING";
    case SENSOR_WEATHER_OK:return "OK";
    case SENSOR_WEATHER_TIMEOUT:return "TIMEOUT";
    case SENSOR_WEATHER_CRC_ERROR:return "CRC_ERROR";
    case SENSOR_WEATHER_FRAME_ERROR:return "FRAME_ERROR";
    case SENSOR_WEATHER_MODBUS_EXCEPTION:return "MODBUS_EXCEPTION";
    case SENSOR_WEATHER_UART_BUSY:return "UART_BUSY";
    case SENSOR_WEATHER_UART_TIMEOUT:return "UART_TIMEOUT";
    case SENSOR_WEATHER_RX_NOT_READY:return "RX_NOT_READY";
    case SENSOR_WEATHER_IRQ_DISABLED:return "IRQ_DISABLED";
    case SENSOR_WEATHER_RX_OVERFLOW:return "RX_OVERFLOW";
    default:return "UART_ERROR";
  }
}
void SensorWeather_ParserInit(SensorWeatherParser *parser, SensorWeatherItem item)
{
  memset(parser,0,sizeof(*parser)); parser->item=item;
  (void)SensorWeather_BuildRequest(item,parser->request);
}
static void SensorWeather_Drop(SensorWeatherParser *parser, uint8_t count)
{
  parser->length=(uint8_t)(parser->length-count);
  memmove(parser->bytes,parser->bytes+count,parser->length);
}
uint8_t SensorWeather_ParseByte(SensorWeatherParser *parser, uint8_t byte, uint32_t t_ms, uint32_t quiet_ms)
{
  uint8_t expected=(uint8_t)(SensorWeather_Count(parser->item)*2U);
  if(parser->byte_seen && (uint32_t)(t_ms-parser->last_byte_ms)>=quiet_ms && parser->length) {
    ++parser->frame_errors; parser->length=0U;
  }
  parser->last_byte_ms=t_ms; parser->byte_seen=1U;
  if(parser->length==sizeof(parser->bytes)) { ++parser->frame_errors; SensorWeather_Drop(parser,1U); }
  parser->bytes[parser->length++]=byte;
  while(parser->length) {
    uint8_t length; uint16_t crc; uint16_t first,second;
    if(parser->length<=8U && !memcmp(parser->bytes,parser->request,parser->length)) {
      if(parser->length<8U) { return 0U; }
      ++parser->echo_count; SensorWeather_Drop(parser,8U); continue;
    }
    if(parser->bytes[0]!=SENSOR_WEATHER_ADDRESS) { ++parser->frame_errors; SensorWeather_Drop(parser,1U); continue; }
    if(parser->length<2U) { return 0U; }
    if(parser->bytes[1]==0x83U) { length=5U; }
    else if(parser->bytes[1]==3U) {
      if(parser->length<3U) { return 0U; }
      if(parser->bytes[2]!=expected) { ++parser->frame_errors; SensorWeather_Drop(parser,1U); continue; }
      length=(uint8_t)(expected+5U);
    } else { ++parser->frame_errors; SensorWeather_Drop(parser,1U); continue; }
    if(parser->length<length) { return 0U; }
    crc=SensorWeather_Crc16(parser->bytes,length-2U);
    if(parser->bytes[length-2U]!=(uint8_t)crc || parser->bytes[length-1U]!=(uint8_t)(crc>>8)) {
      ++parser->crc_errors; SensorWeather_Drop(parser,1U); continue;
    }
    memset(&parser->reply,0,sizeof(parser->reply));
    if(parser->bytes[1]==0x83U) {
      parser->reply.status=SENSOR_WEATHER_MODBUS_EXCEPTION;
      parser->reply.exception_code=parser->bytes[2]; return 1U;
    }
    first=SensorWeather_Word(parser->bytes+3U);
    second=expected==4U ? SensorWeather_Word(parser->bytes+5U) : 0U;
    switch(parser->item) {
      case SENSOR_WEATHER_TEMP_HUM:
        parser->reply.value.humidity_rh_pct_x10=first;
        parser->reply.value.temperature_c_x10=(int16_t)(second<=INT16_MAX ? (int32_t)second : (int32_t)second-65536L); break;
      case SENSOR_WEATHER_LIGHT:parser->reply.value.light_lux=((uint32_t)first<<16)|second;break;
      case SENSOR_WEATHER_PM:parser->reply.value.pm25_ug_m3=first;parser->reply.value.pm10_ug_m3=second;break;
      case SENSOR_WEATHER_PRESSURE:parser->reply.value.pressure_kpa_x100=first;break;
      case SENSOR_WEATHER_CO2:parser->reply.value.co2_ppm=first;break;
      case SENSOR_WEATHER_OXYGEN:parser->reply.value.oxygen_pct_x10=first;break;
      default:++parser->frame_errors;return 0U;
    }
    /* A legal CRC frame remains a legal response even if a physical field is
     * unusable. Independent bits prevent one bad field erasing its sibling. */
    switch(parser->item) {
      case SENSOR_WEATHER_TEMP_HUM:
        if(first<=1000U) { parser->reply.valid_fields|=SENSOR_WEATHER_FIELD_HUMIDITY; }
        if(parser->reply.value.temperature_c_x10>=-400 && parser->reply.value.temperature_c_x10<=800)
        { parser->reply.valid_fields|=SENSOR_WEATHER_FIELD_TEMPERATURE; } break;
      case SENSOR_WEATHER_LIGHT:
        if(parser->reply.value.light_lux!=UINT32_MAX) { parser->reply.valid_fields=SENSOR_WEATHER_FIELD_LIGHT; } break;
      case SENSOR_WEATHER_PM:
        if(first!=UINT16_MAX) { parser->reply.valid_fields|=SENSOR_WEATHER_FIELD_PM25; }
        if(second!=UINT16_MAX) { parser->reply.valid_fields|=SENSOR_WEATHER_FIELD_PM10; } break;
      case SENSOR_WEATHER_PRESSURE:
        if(first>=3000U && first<=11000U) { parser->reply.valid_fields=SENSOR_WEATHER_FIELD_PRESSURE; } break;
      case SENSOR_WEATHER_CO2:parser->reply.valid_fields=SENSOR_WEATHER_FIELD_CO2;break;
      case SENSOR_WEATHER_OXYGEN:parser->reply.valid_fields=SENSOR_WEATHER_FIELD_OXYGEN;break;
      default:break;
    }
    parser->reply.status=SENSOR_WEATHER_OK; return 1U;
  }
  return 0U;
}
SensorWeatherStatus SensorWeather_ParserError(const SensorWeatherParser *parser)
{ return parser->crc_errors ? SENSOR_WEATHER_CRC_ERROR : (parser->frame_errors || parser->length) ? SENSOR_WEATHER_FRAME_ERROR : SENSOR_WEATHER_TIMEOUT; }
void SensorWeather_Init(void) { memset(s_measurements,0,sizeof(s_measurements)); }
void SensorWeather_RecordQuery(SensorWeatherItem item, uint32_t sequence, uint32_t t_ms)
{
  if((unsigned)item>=SENSOR_WEATHER_ITEM_COUNT) { return; }
  s_measurements[item].last_query_t_ms=t_ms; s_measurements[item].query_sequence=sequence;
  s_measurements[item].last_status=SENSOR_WEATHER_PENDING;
  s_measurements[item].in_progress=1U;
}
void SensorWeather_Complete(SensorWeatherItem item, const SensorWeatherReply *reply,
  uint32_t response_t_ms, uint32_t completed_t_ms)
{
  SensorWeatherMeasurement *measurement;
  if((unsigned)item>=SENSOR_WEATHER_ITEM_COUNT || !reply) { return; }
  measurement=&s_measurements[item];
  if(measurement->has_completed && !measurement->in_progress) { return; }
  measurement->last_status=reply->status; measurement->exception_code=reply->exception_code;
  measurement->completed_sequence=measurement->query_sequence;
  measurement->completed_t_ms=completed_t_ms;
  measurement->sample_t_ms=reply->status==SENSOR_WEATHER_OK?response_t_ms:completed_t_ms;
  measurement->completed_status=reply->status; measurement->completed_value=reply->value;
  measurement->valid_fields=reply->status==SENSOR_WEATHER_OK?reply->valid_fields:0U;
  measurement->has_completed=1U; measurement->in_progress=0U;
  if(reply->status==SENSOR_WEATHER_OK && reply->valid_fields) {
    unsigned index;
    for(index=0U;index<8U;++index) { if(reply->valid_fields&(1U<<index)) {
      measurement->field_sequence[index]=measurement->query_sequence;
      measurement->field_t_ms[index]=response_t_ms;
    } }
    measurement->cached_fields|=reply->valid_fields;
    if(reply->valid_fields&1U) { measurement->value.temperature_c_x10=reply->value.temperature_c_x10; }
    if(reply->valid_fields&2U) { measurement->value.humidity_rh_pct_x10=reply->value.humidity_rh_pct_x10; }
    if(reply->valid_fields&4U) { measurement->value.light_lux=reply->value.light_lux; }
    if(reply->valid_fields&8U) { measurement->value.pm25_ug_m3=reply->value.pm25_ug_m3; }
    if(reply->valid_fields&16U) { measurement->value.pm10_ug_m3=reply->value.pm10_ug_m3; }
    if(reply->valid_fields&32U) { measurement->value.pressure_kpa_x100=reply->value.pressure_kpa_x100; }
    if(reply->valid_fields&64U) { measurement->value.co2_ppm=reply->value.co2_ppm; }
    if(reply->valid_fields&128U) { measurement->value.oxygen_pct_x10=reply->value.oxygen_pct_x10; }
    measurement->valid_t_ms=response_t_ms;
    measurement->valid_sequence=measurement->query_sequence; measurement->has_value=1U;
  }
}
const SensorWeatherMeasurement *SensorWeather_GetMeasurement(SensorWeatherItem item)
{ return (unsigned)item<SENSOR_WEATHER_ITEM_COUNT ? &s_measurements[item] : NULL; }
SensorWeatherFreshness SensorWeather_GetFreshness(SensorWeatherItem item, uint32_t now_ms, uint32_t period_ms)
{
  const SensorWeatherMeasurement *measurement=SensorWeather_GetMeasurement(item);
  if(!measurement || !measurement->has_value) { return SENSOR_WEATHER_FRESHNESS_NO_DATA; }
  if(!period_ms || period_ms>0x7FFFFFFFU/3U || (uint32_t)(now_ms-measurement->valid_t_ms)>period_ms*3U) { return SENSOR_WEATHER_STALE; }
  return SENSOR_WEATHER_FRESH;
}
