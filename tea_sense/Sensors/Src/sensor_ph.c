#include "sensor_ph.h"
#include <string.h>

/* Exact signed-64 evaluation of offset + delta*scale/(span*count).
 * Preserve division remainders to avoid pre-rounding the mean, and avoid
 * delta*10000 overflow even for UINT32_MAX valid 16-bit samples. */
static int32_t SensorPh_ScaleMean(int64_t delta, uint32_t count, int32_t scale, int32_t offset)
{
  int64_t span = SENSOR_PH_RAW_SPAN_X100;
  int64_t denominator = span * count;
  int64_t scaled = (delta / count) * scale + (int64_t)offset * span;
  int64_t whole = scaled / span;
  int64_t remainder = (scaled % span) * count + (delta % count) * scale;
  whole += remainder / denominator;
  remainder %= denominator;
  if (whole > 0 && remainder < 0) { --whole; remainder += denominator; }
  else if (whole < 0 && remainder > 0) { ++whole; remainder -= denominator; }
  /* One final round to nearest, with ties away from zero. */
  return (int32_t)(whole + (remainder >= 0 ? (remainder + denominator / 2) / denominator :
                                            -((-remainder + denominator / 2) / denominator)));
}

SensorPhValue SensorPh_Convert(uint64_t raw_sum, uint32_t count)
{
  SensorPhValue soil_ph;
  int64_t delta, denominator;
  memset(&soil_ph, 0, sizeof(soil_ph));
  if (!count || raw_sum > (uint64_t)UINT16_MAX * count) { return soil_ph; }
  denominator = (int64_t)SENSOR_PH_RAW_SPAN_X100 * count;
  delta = (int64_t)raw_sum * 100 - (int64_t)SENSOR_PH_RAW_LOW_X100 * count;
  soil_ph.input_mv = SensorPh_ScaleMean(delta, count, SENSOR_PH_INPUT_SPAN_MV, 0);
  soil_ph.range = delta < 0 ? SENSOR_PH_RANGE_LOW :
                  delta > denominator ? SENSOR_PH_RANGE_HIGH : SENSOR_PH_RANGE_OK;
  if (delta <= 0) { soil_ph.soil_ph_x100 = SENSOR_PH_SOIL_PH_LOW_X100; }
  else if (delta >= denominator) { soil_ph.soil_ph_x100 = SENSOR_PH_SOIL_PH_HIGH_X100; }
  else
  {
    /* pH independently from raw, never from rounded terminal millivolts. */
    soil_ph.soil_ph_x100 = (uint32_t)SensorPh_ScaleMean(delta, count,
      SENSOR_PH_SOIL_PH_SPAN_X100, SENSOR_PH_SOIL_PH_LOW_X100);
  }
  soil_ph.valid = 1U;
  return soil_ph;
}

const char *SensorPh_RangeName(SensorPhRange range)
{
  switch (range)
  {
    case SENSOR_PH_RANGE_OK: return "OK";
    case SENSOR_PH_RANGE_LOW: return "LOW";
    case SENSOR_PH_RANGE_HIGH: return "HIGH";
    default: return "NA";
  }
}

