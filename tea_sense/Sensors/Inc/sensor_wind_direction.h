#ifndef SENSOR_WIND_DIRECTION_H
#define SENSOR_WIND_DIRECTION_H

#include <stdint.h>

typedef enum
{
  SENSOR_WIND_DIRECTION_RANGE_UNAVAILABLE = 0,
  SENSOR_WIND_DIRECTION_RANGE_LOW,
  SENSOR_WIND_DIRECTION_RANGE_OK,
  SENSOR_WIND_DIRECTION_RANGE_HIGH
} SensorWindDirectionRange;

typedef struct
{
  uint8_t valid;
  int32_t input_mv; /* AD3 terminal voltage, extrapolated outside 1..5V. Not PA5. */
  uint32_t angle_deg_x100; /* Display limited to 0..36000; range is retained. */
  SensorWindDirectionRange range;
} SensorWindDirectionValue;

/* Pure conversion: raw_x100 is ADC raw * 100, including fractional calibration
 * observations. App calls only after successful acquisition. No ADC access or
 * dynamic directional averaging; invalid input (>65535*100) returns valid=0. */
SensorWindDirectionValue SensorWindDirection_Convert(uint32_t raw_x100);
const char *SensorWindDirection_RangeName(SensorWindDirectionRange range);

#endif
