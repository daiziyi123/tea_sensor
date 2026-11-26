#include "sensor_soil.h"
#include <string.h>

/* One conversion path for single readings and unrounded window means. */
uint32_t SensorSoil_ConvertMean(uint64_t raw_sum, uint32_t count, SensorSoilRange *range)
{
  const uint64_t dry = (uint64_t)SENSOR_SOIL_RAW_DRY * count;
  const uint64_t wet = (uint64_t)SENSOR_SOIL_RAW_WET * count;
  const uint64_t denominator = wet - dry;
  int64_t delta;
  if (!count || raw_sum > (uint64_t)UINT16_MAX * count) { *range=SENSOR_SOIL_RANGE_UNAVAILABLE; return 0U; }
  *range = raw_sum < dry ? SENSOR_SOIL_RANGE_LOW :
           raw_sum > wet ? SENSOR_SOIL_RANGE_HIGH : SENSOR_SOIL_RANGE_OK;
  if (raw_sum <= dry)
  {
    return 0U;
  }
  if (raw_sum >= wet)
  {
    return 10000U;
  }
  /* Bounds precede subtraction. uint32 count * 16-bit raw * 10000 fits int64. */
  delta = (int64_t)raw_sum - (int64_t)dry;
  return (uint32_t)(((uint64_t)delta * 10000U + denominator / 2U) / denominator);
}

uint32_t SensorSoil_MoisturePercentX100(uint16_t raw)
{
  SensorSoilRange range;
  return SensorSoil_ConvertMean(raw, 1U, &range);
}

SensorSoilData SensorSoil_Read(void)
{
  SensorSoilData data;
  data.adc = BspAdc_ReadRaw(BSP_ADC_INPUT_SOIL_AD0);
  data.moisture_pct_x100 = 0U;
  data.range = SENSOR_SOIL_RANGE_UNAVAILABLE;
  if (data.adc.valid == 0U)
  {
    return data;
  }
  data.moisture_pct_x100 = SensorSoil_ConvertMean(data.adc.raw, 1U, &data.range);
  return data;
}

