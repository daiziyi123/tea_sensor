#include "bsp_adc.h"
#include "bsp_config.h"
#include "adc.h"
#include <string.h>

static HAL_StatusTypeDef s_calibration_status = HAL_ERROR;
static BspAdcDiagnostics s_diagnostics;
static uint32_t s_recovery_last_ms;
static uint8_t s_reading;

static BspAdcHalStatus BspAdc_MapHalStatus(HAL_StatusTypeDef status)
{
  switch (status)
  {
    case HAL_OK:      return BSP_ADC_HAL_OK;
    case HAL_BUSY:    return BSP_ADC_HAL_BUSY;
    case HAL_TIMEOUT: return BSP_ADC_HAL_TIMEOUT;
    default:          return BSP_ADC_HAL_ERROR;
  }
}

static void BspAdc_Calibrate(void)
{
  ++s_diagnostics.calibration_attempts;
  s_calibration_status=HAL_ADCEx_Calibration_Start(&hadc1,ADC_CALIB_OFFSET_LINEARITY,ADC_SINGLE_ENDED);
  s_diagnostics.last_calibration_status=BspAdc_MapHalStatus(s_calibration_status);
  s_diagnostics.calibrated=s_calibration_status==HAL_OK;
  if(s_calibration_status!=HAL_OK) { ++s_diagnostics.calibration_failures; }
}
void BspAdc_Init(void)
{
  if(s_reading) { return; }
  memset(&s_diagnostics,0,sizeof(s_diagnostics));
  BspAdc_Calibrate();
  s_diagnostics.initial_calibration_status=s_diagnostics.last_calibration_status;
  s_recovery_last_ms=HAL_GetTick();
}
void BspAdc_Process(void)
{
  uint32_t now_ms;
  if(s_calibration_status==HAL_OK || s_reading) { return; }
  now_ms=HAL_GetTick();
  if((uint32_t)(now_ms-s_recovery_last_ms)<BSP_ADC_CAL_RECOVERY_PERIOD_MS) { return; }
  s_recovery_last_ms=now_ms; ++s_diagnostics.recovery_attempts;
  s_diagnostics.last_recovery_stop_status=BspAdc_MapHalStatus(HAL_ADC_Stop(&hadc1));
  if(s_diagnostics.last_recovery_stop_status!=BSP_ADC_HAL_OK) {
    ++s_diagnostics.recovery_stop_errors; return;
  }
  BspAdc_Calibrate();
  if(s_calibration_status==HAL_OK) { ++s_diagnostics.recovery_successes; }
}
const BspAdcDiagnostics *BspAdc_GetDiagnostics(void) { return &s_diagnostics; }

BspAdcResult BspAdc_ReadRaw(BspAdcInput input)
{
  BspAdcResult result;
  ADC_ChannelConfTypeDef channel = {0};
  uint32_t converted;
  memset(&result, 0, sizeof(result));
  result.status = BSP_ADC_CAL_FAILED;
  result.hal_status = BSP_ADC_HAL_ERROR;
  result.stage = BSP_ADC_STAGE_CALIBRATION;
  switch (input)
  {
    case BSP_ADC_INPUT_SOIL_AD0: channel.Channel = ADC_CHANNEL_5; break;
    case BSP_ADC_INPUT_WIND_SPEED_AD2: channel.Channel = ADC_CHANNEL_3; break;
    case BSP_ADC_INPUT_WIND_DIRECTION_AD3: channel.Channel = ADC_CHANNEL_19; break;
    case BSP_ADC_INPUT_PH_AD4: channel.Channel = ADC_CHANNEL_18; break;
    case BSP_ADC_INPUT_RAIN_AD5: channel.Channel = ADC_CHANNEL_15; break;
    default:
      result.status = BSP_ADC_START_FAILED;
      result.stage = BSP_ADC_STAGE_INPUT;
      return result;
  }
  if (s_calibration_status != HAL_OK)
  {
    result.hal_status = BspAdc_MapHalStatus(s_calibration_status);
    return result;
  }

  if(s_reading) {
    result.status=BSP_ADC_START_FAILED; result.stage=BSP_ADC_STAGE_START;
    result.hal_status=BSP_ADC_HAL_BUSY; return result;
  }
  s_reading=1U;
  result.prepare_attempted = 1U;
  result.prepare_status = BspAdc_MapHalStatus(HAL_ADC_Stop(&hadc1));
  if (result.prepare_status != BSP_ADC_HAL_OK)
  {
    result.status = BSP_ADC_STOP_FAILED;
    result.stage = BSP_ADC_STAGE_PREPARE;
    result.hal_status = result.prepare_status;
    s_reading=0U;
    return result; /* Do not configure a shared ADC that could not be stopped. */
  }
  channel.Rank = ADC_REGULAR_RANK_1;
  channel.SamplingTime = ADC_SAMPLETIME_387CYCLES_5;
  channel.SingleDiff = ADC_SINGLE_ENDED;
  channel.OffsetNumber = ADC_OFFSET_NONE;
  channel.Offset = 0U;
  channel.OffsetSignedSaturation = DISABLE;
  result.config_attempted = 1U;
  result.config_status = BspAdc_MapHalStatus(HAL_ADC_ConfigChannel(&hadc1, &channel));
  result.hal_status = result.config_status;
  result.stage = BSP_ADC_STAGE_CONFIG;
  if (result.config_status != BSP_ADC_HAL_OK)
  {
    /* The existing START_FAILED wire class includes pre-start configuration.
     * stage/config_status retain the precise local failure, without a new wire schema. */
    result.status = BSP_ADC_START_FAILED;
  }
  else
  {
    result.stage = BSP_ADC_STAGE_START;
    result.hal_status = BspAdc_MapHalStatus(HAL_ADC_Start(&hadc1));
    if (result.hal_status != BSP_ADC_HAL_OK)
    {
      result.status = BSP_ADC_START_FAILED;
    }
    else
    {
      result.stage = BSP_ADC_STAGE_POLL;
      result.hal_status = BspAdc_MapHalStatus(
        HAL_ADC_PollForConversion(&hadc1, BSP_ADC_POLL_TIMEOUT_MS));
      if (result.hal_status != BSP_ADC_HAL_OK)
      {
        result.status = BSP_ADC_POLL_FAILED;
      }
      else
      {
        result.stage = BSP_ADC_STAGE_VALUE;
        converted = HAL_ADC_GetValue(&hadc1);
        /* Validate before narrowing so a wider result cannot wrap to zero. */
        if (converted <= BSP_ADC_FULL_SCALE)
        {
          result.raw = (uint16_t)converted;
          result.status = BSP_ADC_OK;
        }
        else
        {
          result.status = BSP_ADC_OUT_OF_RANGE;
        }
      }
    }
  }
  /* Preserve both acquisition and cleanup failures, including failed start. */
  result.stop_attempted = 1U;
  result.stop_status = BspAdc_MapHalStatus(HAL_ADC_Stop(&hadc1));
  if ((result.status == BSP_ADC_OK) && (result.stop_status != BSP_ADC_HAL_OK))
  {
    result.status = BSP_ADC_STOP_FAILED;
    result.hal_status = result.stop_status;
    result.stage = BSP_ADC_STAGE_STOP;
  }
  result.valid = (result.status == BSP_ADC_OK) ? 1U : 0U;
  if (result.valid) { result.stage = BSP_ADC_STAGE_COMPLETE; }
  s_reading=0U;
  return result;
}

uint32_t BspAdc_PinMillivolts(uint32_t raw)
{
  return (uint32_t)(((uint64_t)raw * BSP_ADC_VREF_MV +
                    BSP_ADC_FULL_SCALE / 2U) / BSP_ADC_FULL_SCALE);
}

const char *BspAdc_StatusName(BspAdcStatus status)
{
  switch (status)
  {
    case BSP_ADC_OK:           return "OK";
    case BSP_ADC_CAL_FAILED:   return "ADC_CAL_FAILED";
    case BSP_ADC_START_FAILED: return "ADC_START_FAILED";
    case BSP_ADC_POLL_FAILED:  return "ADC_POLL_FAILED";
    case BSP_ADC_STOP_FAILED:  return "ADC_STOP_FAILED";
    default:                  return "ADC_OUT_OF_RANGE";
  }
}

const char *BspAdc_HalStatusName(BspAdcHalStatus status)
{
  switch (status)
  {
    case BSP_ADC_HAL_OK:      return "HAL_OK";
    case BSP_ADC_HAL_BUSY:    return "HAL_BUSY";
    case BSP_ADC_HAL_TIMEOUT: return "HAL_TIMEOUT";
    default:                 return "HAL_ERROR";
  }
}

const char *BspAdc_StageName(BspAdcStage stage)
{
  switch (stage)
  {
    case BSP_ADC_STAGE_CALIBRATION: return "CALIBRATION";
    case BSP_ADC_STAGE_INPUT: return "INPUT";
    case BSP_ADC_STAGE_PREPARE: return "PREPARE";
    case BSP_ADC_STAGE_CONFIG: return "CONFIG";
    case BSP_ADC_STAGE_START: return "START";
    case BSP_ADC_STAGE_POLL: return "POLL";
    case BSP_ADC_STAGE_VALUE: return "VALUE";
    case BSP_ADC_STAGE_STOP: return "STOP";
    case BSP_ADC_STAGE_COMPLETE: return "COMPLETE";
    default: return "UNKNOWN";
  }
}
