#ifndef SENSOR_SOIL_H
#define SENSOR_SOIL_H

#include "bsp_adc.h"

#define SENSOR_SOIL_CHANNEL_NAME "AD0"
#define SENSOR_SOIL_MODEL "RE-SD-V05-TR"
/* Rounded means supplied by the user: 90 air-dry / 37 fully immersed samples.
 * These endpoints define relative wetness, not volumetric soil water content. */
#define SENSOR_SOIL_RAW_DRY 436U
#define SENSOR_SOIL_RAW_WET 31146U
#if SENSOR_SOIL_RAW_WET <= SENSOR_SOIL_RAW_DRY
#error SENSOR_SOIL_RAW_WET must be greater than SENSOR_SOIL_RAW_DRY
#endif
#if SENSOR_SOIL_RAW_WET > UINT16_MAX
#error Soil calibration endpoints must fit the unsigned 16-bit ADC
#endif

typedef enum
{
  SENSOR_SOIL_RANGE_UNAVAILABLE = 0,
  SENSOR_SOIL_RANGE_OK,
  SENSOR_SOIL_RANGE_LOW,
  SENSOR_SOIL_RANGE_HIGH
} SensorSoilRange;

typedef struct
{
  BspAdcResult adc;
  /* 0.01% relative wetness, valid only when adc.valid is set. */
  uint32_t moisture_pct_x100;
  SensorSoilRange range;
} SensorSoilData;

/* One AD0 acquisition, then conversion only on a valid ADC result. */
SensorSoilData SensorSoil_Read(void);
/* Unique raw-to-percent conversion: clamped, rounded, no VREF/gain factor. */
uint32_t SensorSoil_MoisturePercentX100(uint16_t raw);
uint32_t SensorSoil_ConvertMean(uint64_t raw_sum, uint32_t count, SensorSoilRange *range);

#endif
