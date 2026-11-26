#include "sensor_wind_speed.h"
#include <string.h>

/* Exactly round offset + delta*scale/(span*count), without delta*scale overflow
 * for UINT32_MAX samples. Both division remainders are retained: this does not
 * round the raw mean first. With these fixed endpoints every intermediate fits
 * int64_t (largest residual < 1.36e16). Halfway values round away from zero. */
static int32_t SensorWindSpeed_ScaleMean(int64_t delta, uint32_t count, int32_t scale, int32_t offset)
{
  int64_t span = SENSOR_WIND_SPEED_RAW_SPAN_X100;
  int64_t denominator = span * count;
  int64_t scaled = (delta / count) * scale + (int64_t)offset * span;
  int64_t whole = scaled / span;
  int64_t remainder = (scaled % span) * count + (delta % count) * scale;
  whole += remainder / denominator;
  remainder %= denominator;
  /* Normalize opposite signs before applying symmetric final rounding. */
  if (whole > 0 && remainder < 0) { --whole; remainder += denominator; }
  else if (whole < 0 && remainder > 0) { ++whole; remainder -= denominator; }
  return (int32_t)(whole + (remainder >= 0 ? (remainder + denominator / 2) / denominator :
                                            -((-remainder + denominator / 2) / denominator)));
}

SensorWindSpeedValue SensorWindSpeed_ConvertMean(uint64_t raw_sum, uint32_t count)
{
  SensorWindSpeedValue value;
  int64_t denominator, delta;
  memset(&value, 0, sizeof(value));
  if (!count || raw_sum > (uint64_t)UINT16_MAX * count) { return value; }
  denominator = (int64_t)SENSOR_WIND_SPEED_RAW_SPAN_X100 * count;
  delta = (int64_t)raw_sum * 100 - (int64_t)SENSOR_WIND_SPEED_RAW_ZERO_X100 * count;
  value.wind_speed_mps = delta <= 0 ? 0.0 : delta >= denominator ?
    SENSOR_WIND_SPEED_FULL_SCALE_MPS_X100 / 100.0 :
    (SENSOR_WIND_SPEED_FULL_SCALE_MPS_X100 / 100.0) * (double)delta / (double)denominator;
  value.input_mv = SensorWindSpeed_ScaleMean(delta, count,
    SENSOR_WIND_SPEED_INPUT_SPAN_MV, SENSOR_WIND_SPEED_INPUT_ZERO_MV);
  value.range = delta < 0 ? SENSOR_WIND_SPEED_RANGE_LOW :
                delta > denominator ?
                SENSOR_WIND_SPEED_RANGE_HIGH : SENSOR_WIND_SPEED_RANGE_OK;
  if (delta <= 0) { value.wind_speed_mps_x100 = 0U; }
  else if (delta >= denominator)
  { value.wind_speed_mps_x100 = SENSOR_WIND_SPEED_FULL_SCALE_MPS_X100; }
  else
  {
    /* Nominal speed from raw independently of rounded terminal millivolts.
     * Electrical mapping measured; no reference-anemometer accuracy claim. */
    value.wind_speed_mps_x100 = (uint32_t)SensorWindSpeed_ScaleMean(delta, count,
      SENSOR_WIND_SPEED_FULL_SCALE_MPS_X100, 0);
  }
  value.valid = 1U;
  return value;
}

SensorWindSpeedData SensorWindSpeed_Read(void)
{
  SensorWindSpeedData data;
  memset(&data, 0, sizeof(data));
  data.adc = BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_SPEED_AD2);
  data.value = SensorWindSpeed_ConvertMean(data.adc.raw, data.adc.valid ? 1U : 0U);
  return data;
}

const char *SensorWindSpeed_RangeName(SensorWindSpeedRange range)
{
  switch (range)
  {
    case SENSOR_WIND_SPEED_RANGE_OK: return "OK";
    case SENSOR_WIND_SPEED_RANGE_LOW: return "LOW";
    case SENSOR_WIND_SPEED_RANGE_HIGH: return "HIGH";
    default: return "NA";
  }
}
