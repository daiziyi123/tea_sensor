#ifndef SENSOR_PH_H
#define SENSOR_PH_H

#include <stdint.h>
#include "bsp_adc.h"

/* AD4 / PA4 / ADC1 channel 18: measured electrical endpoints, 0..10V version.
 * The manufacturer's nominal pH 3..9 relation has no soil-reference validation. */
#define SENSOR_PH_RAW_LOW_X100 5464U
#define SENSOR_PH_RAW_HIGH_X100 6274150U
#define SENSOR_PH_RAW_SPAN_X100 (SENSOR_PH_RAW_HIGH_X100 - SENSOR_PH_RAW_LOW_X100)
#define SENSOR_PH_INPUT_SPAN_MV 10000
#define SENSOR_PH_SOIL_PH_LOW_X100 300U
#define SENSOR_PH_SOIL_PH_HIGH_X100 900U
#define SENSOR_PH_SOIL_PH_SPAN_X100 (SENSOR_PH_SOIL_PH_HIGH_X100 - SENSOR_PH_SOIL_PH_LOW_X100)

typedef enum
{
  SENSOR_PH_RANGE_UNAVAILABLE = 0,
  SENSOR_PH_RANGE_OK,
  SENSOR_PH_RANGE_LOW,
  SENSOR_PH_RANGE_HIGH
} SensorPhRange;

typedef struct
{
  uint8_t valid;
  int32_t input_mv; /* AD4 terminal voltage, not the PA4 node; extrapolation retained. */
  uint32_t soil_ph_x100; /* Display limited to 3.00..9.00; no percent unit. */
  SensorPhRange range; /* From the unrounded, unclamped raw mean. */
} SensorPhValue;

/* Exact caller-selected raw fraction; no sampling or early rounding. */
SensorPhValue SensorPh_Convert(uint64_t raw_sum, uint32_t count);
const char *SensorPh_RangeName(SensorPhRange range);

#endif
