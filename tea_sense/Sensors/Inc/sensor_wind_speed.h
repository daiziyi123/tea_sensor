#ifndef SENSOR_WIND_SPEED_H
#define SENSOR_WIND_SPEED_H

#include "bsp_adc.h"

#define SENSOR_WIND_SPEED_CHANNEL_NAME "AD2"
/* Measured AD2 terminal-voltage endpoints; speed uses the nominal 0..30m/s relation. */
#define SENSOR_WIND_SPEED_RAW_ZERO_X100 6779U
#define SENSOR_WIND_SPEED_RAW_FULL_X100 3151863U
#define SENSOR_WIND_SPEED_RAW_SPAN_X100 (SENSOR_WIND_SPEED_RAW_FULL_X100 - SENSOR_WIND_SPEED_RAW_ZERO_X100)
#define SENSOR_WIND_SPEED_INPUT_ZERO_MV 40
#define SENSOR_WIND_SPEED_INPUT_SPAN_MV 5000
#define SENSOR_WIND_SPEED_FULL_SCALE_MPS_X100 3000U

typedef enum
{
  SENSOR_WIND_SPEED_RANGE_UNAVAILABLE = 0,
  SENSOR_WIND_SPEED_RANGE_OK,
  SENSOR_WIND_SPEED_RANGE_LOW,
  SENSOR_WIND_SPEED_RANGE_HIGH
} SensorWindSpeedRange;

typedef struct
{
  uint8_t valid; /* Availability of converted values, independent of ADC status. */
  int32_t input_mv; /* AD2 terminal voltage, not the MCU PA6 node. */
  uint32_t wind_speed_mps_x100;
  double wind_speed_mps; /* Unrounded physical value for App accumulation. */
  SensorWindSpeedRange range; /* Unrounded raw mean vs measured zero/full endpoints. */
} SensorWindSpeedValue;

typedef struct
{
  BspAdcResult adc;
  SensorWindSpeedValue value;
} SensorWindSpeedData;

typedef struct
{
  uint32_t error_counts[BSP_ADC_OUT_OF_RANGE + 1U];
  uint32_t cleanup_error_count;
  uint32_t prepare_error_count;
  uint32_t config_error_count;
  BspAdcResult last_error;
} SensorWindSpeedDiagnostics;

SensorWindSpeedData SensorWindSpeed_Read(void);
/* One fixed zero-compensated mapping for a sample or the exact raw sum/count.
 * No acquisition, pre-rounded raw/mV, VREF, divider guess or runtime mode. */
SensorWindSpeedValue SensorWindSpeed_ConvertMean(uint64_t raw_sum, uint32_t count);
const char *SensorWindSpeed_RangeName(SensorWindSpeedRange range);

#endif
