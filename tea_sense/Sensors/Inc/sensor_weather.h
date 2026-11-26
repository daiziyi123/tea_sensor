#ifndef SENSOR_WEATHER_H
#define SENSOR_WEATHER_H
#include "bsp_weather_rs485.h"

#define SENSOR_WEATHER_ADDRESS 1U
typedef enum {
  SENSOR_WEATHER_TEMP_HUM=0, SENSOR_WEATHER_LIGHT, SENSOR_WEATHER_PM,
  SENSOR_WEATHER_PRESSURE, SENSOR_WEATHER_CO2, SENSOR_WEATHER_OXYGEN, SENSOR_WEATHER_ITEM_COUNT
} SensorWeatherItem;
typedef enum {
  SENSOR_WEATHER_NO_DATA=0, SENSOR_WEATHER_PENDING, SENSOR_WEATHER_OK, SENSOR_WEATHER_TIMEOUT,
  SENSOR_WEATHER_CRC_ERROR, SENSOR_WEATHER_FRAME_ERROR, SENSOR_WEATHER_MODBUS_EXCEPTION,
  SENSOR_WEATHER_UART_ERROR, SENSOR_WEATHER_UART_BUSY, SENSOR_WEATHER_UART_TIMEOUT,
  SENSOR_WEATHER_RX_NOT_READY, SENSOR_WEATHER_IRQ_DISABLED, SENSOR_WEATHER_RX_OVERFLOW
} SensorWeatherStatus;

#define SENSOR_WEATHER_FIELD_TEMPERATURE 1U
#define SENSOR_WEATHER_FIELD_HUMIDITY 2U
#define SENSOR_WEATHER_FIELD_LIGHT 4U
#define SENSOR_WEATHER_FIELD_PM25 8U
#define SENSOR_WEATHER_FIELD_PM10 16U
#define SENSOR_WEATHER_FIELD_PRESSURE 32U
#define SENSOR_WEATHER_FIELD_CO2 64U
#define SENSOR_WEATHER_FIELD_OXYGEN 128U
typedef enum { SENSOR_WEATHER_FRESHNESS_NO_DATA=0, SENSOR_WEATHER_FRESH, SENSOR_WEATHER_STALE } SensorWeatherFreshness;
typedef struct {
  int16_t temperature_c_x10;
  uint16_t humidity_rh_pct_x10;
  uint32_t light_lux;
  uint16_t pm25_ug_m3, pm10_ug_m3, pressure_kpa_x100, co2_ppm, oxygen_pct_x10;
} SensorWeatherValue;
typedef struct { SensorWeatherStatus status; uint8_t exception_code; SensorWeatherValue value; uint16_t valid_fields; } SensorWeatherReply;
typedef struct {
  SensorWeatherValue value;
  uint32_t valid_t_ms, valid_sequence, last_query_t_ms, query_sequence;
  SensorWeatherStatus last_status;
  uint8_t has_value, exception_code;
  SensorWeatherValue completed_value;
  uint32_t completed_sequence, completed_t_ms, sample_t_ms;
  SensorWeatherStatus completed_status;
  uint16_t valid_fields, cached_fields;
  uint32_t field_sequence[8], field_t_ms[8];
  uint8_t has_completed, in_progress;
} SensorWeatherMeasurement;
typedef struct {
  SensorWeatherItem item;
  uint8_t request[8], bytes[16], length;
  uint32_t last_byte_ms, echo_count, crc_errors, frame_errors;
  uint8_t byte_seen;
  SensorWeatherReply reply;
} SensorWeatherParser;

uint16_t SensorWeather_Crc16(const uint8_t *bytes, size_t length);
uint8_t SensorWeather_BuildRequest(SensorWeatherItem item, uint8_t request[8]);
BspWeatherRs485Port SensorWeather_Port(SensorWeatherItem item);
const char *SensorWeather_ItemName(SensorWeatherItem item);
const char *SensorWeather_StatusName(SensorWeatherStatus status);
void SensorWeather_ParserInit(SensorWeatherParser *parser, SensorWeatherItem item);
/* Returns 1 only for a complete address/function/count/CRC-valid normal or
 * exception reply. Invalid candidates are resynchronized within bounded storage. */
uint8_t SensorWeather_ParseByte(SensorWeatherParser *parser, uint8_t byte, uint32_t t_ms, uint32_t quiet_ms);
SensorWeatherStatus SensorWeather_ParserError(const SensorWeatherParser *parser);
void SensorWeather_Init(void);
void SensorWeather_RecordQuery(SensorWeatherItem item, uint32_t sequence, uint32_t t_ms);
void SensorWeather_Complete(SensorWeatherItem item, const SensorWeatherReply *reply,
  uint32_t response_t_ms, uint32_t completed_t_ms);
const SensorWeatherMeasurement *SensorWeather_GetMeasurement(SensorWeatherItem item);
SensorWeatherFreshness SensorWeather_GetFreshness(SensorWeatherItem item, uint32_t now_ms, uint32_t period_ms);
#endif
