#include "sensor_wind_direction.h"
#include <math.h>
#include <string.h>

/* Current board electrical calibration, in chronological record order:
 * 5V:31000.43, 1V:6400.67, 3.5V:21920.14, 2.5V:15614.18.
 * Only 1V and 5V define the line; the other two are independent checks.
 * No soil/AD2 divider, VREF, orientation offset or automatic calibration. */
#define SENSOR_WIND_DIRECTION_RAW_1V_X100 640067L
#define SENSOR_WIND_DIRECTION_RAW_5V_X100 3100043L
#define SENSOR_WIND_DIRECTION_RAW_SPAN_X100 \
  (SENSOR_WIND_DIRECTION_RAW_5V_X100 - SENSOR_WIND_DIRECTION_RAW_1V_X100)
#define SENSOR_WIND_DIRECTION_INPUT_LOW_MV 1000L
#define SENSOR_WIND_DIRECTION_INPUT_SPAN_MV 4000L
#define SENSOR_WIND_DIRECTION_FULL_ANGLE_X100 36000L

#if SENSOR_WIND_DIRECTION_RAW_SPAN_X100 <= 0
#error Wind direction calibration raw span must be positive
#endif

static int32_t SensorWindDirection_RoundDivide(int64_t numerator, int64_t denominator)
{
  /* All products are bounded by a 16-bit ADC value times 100 and 36000.
   * Symmetric nearest rounding also handles negative voltage extrapolation. */
  return numerator >= 0 ? (int32_t)((numerator + denominator / 2) / denominator) :
                         -(int32_t)((-numerator + denominator / 2) / denominator);
}

SensorWindDirectionValue SensorWindDirection_Convert(uint32_t raw_x100)
{
  SensorWindDirectionValue value = {0};
  int64_t delta, span, voltage_numerator;
  if (raw_x100 > (uint32_t)UINT16_MAX * 100U) { return value; }
  span = SENSOR_WIND_DIRECTION_RAW_SPAN_X100;
  delta = (int64_t)raw_x100 - SENSOR_WIND_DIRECTION_RAW_1V_X100;
  voltage_numerator = SENSOR_WIND_DIRECTION_INPUT_LOW_MV * span +
                      delta * SENSOR_WIND_DIRECTION_INPUT_SPAN_MV;
  value.input_mv = SensorWindDirection_RoundDivide(voltage_numerator, span);
  /* Compare the unrounded raw relationship; rounded display endpoints do not
   * erase a small LOW/HIGH excursion. Equal endpoints are within range. */
  value.range = delta < 0 ? SENSOR_WIND_DIRECTION_RANGE_LOW :
                delta > span ? SENSOR_WIND_DIRECTION_RANGE_HIGH : SENSOR_WIND_DIRECTION_RANGE_OK;
  if (delta <= 0) { value.angle_deg_x100 = 0U; }
  else if (delta >= span) { value.angle_deg_x100 = SENSOR_WIND_DIRECTION_FULL_ANGLE_X100; }
  else
  {
    /* Compute angle directly from raw, never from already-rounded mV. */
    value.angle_deg_x100 = (uint32_t)SensorWindDirection_RoundDivide(
      delta * SENSOR_WIND_DIRECTION_FULL_ANGLE_X100, span);
  }
  value.valid = 1U;
  return value;
}

const char *SensorWindDirection_RangeName(SensorWindDirectionRange range)
{
  switch (range)
  {
    case SENSOR_WIND_DIRECTION_RANGE_LOW: return "LOW";
    case SENSOR_WIND_DIRECTION_RANGE_OK: return "OK";
    case SENSOR_WIND_DIRECTION_RANGE_HIGH: return "HIGH";
    default: return "NA";
  }
}

