#include "sensor_rain.h"
#include <string.h>
#if SENSOR_RAIN_RAW_FULL_X100 <= SENSOR_RAIN_RAW_ZERO_X100 || SENSOR_RAIN_RAW_FULL_X100 > 6553500U
#error Invalid rain electrical endpoint span
#endif
SensorRainData SensorRain_Convert(uint64_t raw_sum,uint32_t count)
{
  SensorRainData result={0};
  int64_t delta,denominator,scaled,whole,remainder;
  const int64_t span=SENSOR_RAIN_RAW_SPAN_X100;
  if(!count || raw_sum>(uint64_t)UINT16_MAX*count) { return result; }
  delta=(int64_t)raw_sum*100-(int64_t)SENSOR_RAIN_RAW_ZERO_X100*count;
  denominator=span*count;
  result.intensity_mm_min=delta<=0?0.0:delta>=denominator?
    SENSOR_RAIN_INTENSITY_MAX_X100/100.0:
    (SENSOR_RAIN_INTENSITY_MAX_X100/100.0)*(double)delta/(double)denominator;
  result.range=delta<0?SENSOR_RAIN_RANGE_LOW:
    delta>denominator?SENSOR_RAIN_RANGE_HIGH:SENSOR_RAIN_RANGE_OK;
  if(delta<=0) { result.rain_intensity_mm_min_x100=0U; }
  else if(delta>=denominator) { result.rain_intensity_mm_min_x100=SENSOR_RAIN_INTENSITY_MAX_X100; }
  else {
    /* Preserve both quotient and remainder. Avoid delta*800 overflow even
     * for UINT32_MAX samples; only the final fixed-point value is rounded. */
    scaled=(delta/count)*SENSOR_RAIN_INTENSITY_MAX_X100;
    whole=scaled/span;
    remainder=(scaled%span)*count+(delta%count)*SENSOR_RAIN_INTENSITY_MAX_X100;
    whole+=remainder/denominator; remainder%=denominator;
    result.rain_intensity_mm_min_x100=(uint32_t)(whole+(remainder+denominator/2)/denominator);
  }
  result.valid=1U;return result;
}
const char *SensorRain_RangeName(SensorRainRange range)
{
  switch(range) {
    case SENSOR_RAIN_RANGE_OK:return "OK";
    case SENSOR_RAIN_RANGE_LOW:return "LOW";
    case SENSOR_RAIN_RANGE_HIGH:return "HIGH";
    default:return "NA";
  }
}
