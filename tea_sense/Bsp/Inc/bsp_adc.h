#ifndef BSP_ADC_H
#define BSP_ADC_H

#include <stdint.h>

typedef enum
{
  BSP_ADC_INPUT_SOIL_AD0 = 0, /* PB1 -> ADC1 channel 5 */
  BSP_ADC_INPUT_WIND_SPEED_AD2, /* PA6 -> ADC1 channel 3, NOT ADC2 */
  BSP_ADC_INPUT_WIND_DIRECTION_AD3, /* PA5 -> ADC1 channel 19 */
  BSP_ADC_INPUT_PH_AD4, /* PA4 -> ADC1 channel 18; conversion belongs to Sensors */
  BSP_ADC_INPUT_RAIN_AD5 /* PA3 -> ADC1 channel 15; raw calibration capture only */
} BspAdcInput;

/* Detailed phase is local diagnostics; existing wire error classes stay stable. */
typedef enum
{
  BSP_ADC_STAGE_CALIBRATION = 0,
  BSP_ADC_STAGE_INPUT,
  BSP_ADC_STAGE_PREPARE,
  BSP_ADC_STAGE_CONFIG,
  BSP_ADC_STAGE_START,
  BSP_ADC_STAGE_POLL,
  BSP_ADC_STAGE_VALUE,
  BSP_ADC_STAGE_STOP,
  BSP_ADC_STAGE_COMPLETE
} BspAdcStage;

typedef enum
{
  BSP_ADC_OK = 0,
  BSP_ADC_CAL_FAILED,
  BSP_ADC_START_FAILED,
  BSP_ADC_POLL_FAILED,
  BSP_ADC_STOP_FAILED,
  BSP_ADC_OUT_OF_RANGE
} BspAdcStatus;

/* HAL outcome preserved without exposing HAL types above Bsp. */
typedef enum
{
  BSP_ADC_HAL_OK = 0,
  BSP_ADC_HAL_ERROR,
  BSP_ADC_HAL_BUSY,
  BSP_ADC_HAL_TIMEOUT
} BspAdcHalStatus;

typedef struct
{
  BspAdcStatus status;
  BspAdcHalStatus hal_status;
  BspAdcHalStatus stop_status;
  uint16_t raw; /* Unsigned ADC1 result, not ADS1115 signed data. */
  uint8_t valid;
  uint8_t stop_attempted;
  BspAdcStage stage;
  BspAdcHalStatus prepare_status;
  BspAdcHalStatus config_status;
  uint8_t prepare_attempted;
  uint8_t config_attempted;
} BspAdcResult;

typedef struct {
  uint32_t calibration_attempts, calibration_failures, recovery_attempts, recovery_successes, recovery_stop_errors;
  BspAdcHalStatus initial_calibration_status, last_calibration_status, last_recovery_stop_status;
  uint8_t calibrated;
} BspAdcDiagnostics;
/* CubeMX initializes ADC1; normal success calibrates once. Only initial failure
 * enables stopped-ADC recovery at the configured low frequency. */
void BspAdc_Init(void);
void BspAdc_Process(void);
const BspAdcDiagnostics *BspAdc_GetDiagnostics(void);
BspAdcResult BspAdc_ReadRaw(BspAdcInput input);
uint32_t BspAdc_PinMillivolts(uint32_t raw);
const char *BspAdc_StatusName(BspAdcStatus status);
const char *BspAdc_HalStatusName(BspAdcHalStatus status);
const char *BspAdc_StageName(BspAdcStage stage);

#endif
