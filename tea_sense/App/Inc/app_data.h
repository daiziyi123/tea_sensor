#ifndef APP_DATA_H
#define APP_DATA_H
#include <stdint.h>

#define APP_SENSOR_METRIC_COUNT 11U

/* Fixed eleven-metric V2 wire contract, not a sensor registry. Values use the field's fixed
 * scale (x100, x10 or integer), and source ticks are never UTC timestamps. */
typedef struct
{
  int64_t value;
  uint32_t sample_seq, sample_t_ms;
  /* Read-only strings must outlive submission, normally string literals. */
  const char *status, *range;
  uint8_t valid, has_sample;
  /* Null method denotes an older/unspecified result; live firmware sets it. */
  const char *method;
  uint32_t statistics_start_ms, statistics_end_ms, statistics_window_ms;
  uint32_t expected_count, valid_count, used_count, valid_duration_ms;
  const char *latest_query_status;
  uint32_t latest_query_seq, latest_query_t_ms;
  uint8_t cached;
} AppSensorValue;
typedef struct
{
  uint32_t window_seq, window_end_ms;
  /* x100: percent, m/s, degrees, dimensionless pH, respectively. */
  AppSensorValue soil_moisture_pct, wind_speed_mps, wind_direction_deg, soil_ph;
  AppSensorValue rain_intensity_mm_min; /* mm/min x100; not accumulated mm. */
  /* x10: deg C and %RH; integers: lux and both ug/m3; pressure is kPa x100. */
  AppSensorValue temperature_c, air_humidity_rh_pct, light_lux, pressure_kpa, pm25_ug_m3, pm10_ug_m3;
  uint32_t wind_speed_sample_max_x100;
  uint8_t wind_speed_sample_max_valid;
} AppSensorSnapshot;


#endif
