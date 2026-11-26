static void Test_Ad3SelectionAndFaults(void)
{
  BspAdcResult adc;
  unsigned before;
  Test_Reset(0U); Test_At(0U); Test_At(100U);
  assert(s_config_calls==10U && s_cal_calls==1U);
  assert(s_channel_history[0]==ADC_CHANNEL_5 && s_channel_history[1]==ADC_CHANNEL_3);
  assert(s_channel_history[2]==ADC_CHANNEL_19 && s_channel_history[3]==ADC_CHANNEL_18);
  assert(s_channel_history[4]==ADC_CHANNEL_15 && s_channel_history[5]==ADC_CHANNEL_5);
  assert(s_channel_history[6]==ADC_CHANNEL_3 && s_channel_history[7]==ADC_CHANNEL_19);
  assert(s_channel_history[8]==ADC_CHANNEL_18 && s_channel_history[9]==ADC_CHANNEL_15);
  assert(App_GetDiagnostics()->last_round.soil.adc.raw==15791U);
  assert(App_GetDiagnostics()->last_round.wind.adc.raw==12345U);
  assert(App_GetDiagnostics()->last_round.ad3.raw==23456U);
  s_ad3_config_result=HAL_BUSY; adc=BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_DIRECTION_AD3);
  assert(!adc.valid && !adc.raw && adc.status==BSP_ADC_START_FAILED && adc.stage==BSP_ADC_STAGE_CONFIG);
  assert(adc.config_status==BSP_ADC_HAL_BUSY && adc.stop_attempted && adc.stop_status==BSP_ADC_HAL_OK);
  s_ad3_config_result=HAL_OK; s_ad3_start_result=HAL_ERROR;
  adc=BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_DIRECTION_AD3);
  assert(!adc.valid && adc.stage==BSP_ADC_STAGE_START && adc.hal_status==BSP_ADC_HAL_ERROR);
  s_ad3_start_result=HAL_OK; s_ad3_poll_result=HAL_TIMEOUT; s_ad3_stop_result=HAL_ERROR;
  adc=BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_DIRECTION_AD3);
  assert(!adc.valid && adc.stage==BSP_ADC_STAGE_POLL && adc.hal_status==BSP_ADC_HAL_TIMEOUT);
  assert(adc.stop_status==BSP_ADC_HAL_ERROR && adc.stop_attempted);
  /* Shared hardware unavailable: all three return the actual prepare failure. */
  s_prepare_result=HAL_TIMEOUT; before=s_config_calls; Test_At(1000U);
  assert(!App_GetDiagnostics()->last_round.soil.adc.valid && !App_GetDiagnostics()->last_round.wind.adc.valid);
  assert(!App_GetDiagnostics()->last_round.ad3.valid && s_config_calls==before);
  assert((!App_GetDiagnostics()->last_round.wind_direction.valid && App_GetDiagnostics()->last_round.ad3.stage==BSP_ADC_STAGE_PREPARE && App_GetDiagnostics()->last_round.ad3.prepare_status==BSP_ADC_HAL_TIMEOUT));
  s_prepare_result=HAL_OK; s_ad3_poll_result=HAL_OK;
  adc=BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_DIRECTION_AD3);
  assert(!adc.valid && adc.status==BSP_ADC_STOP_FAILED && adc.stage==BSP_ADC_STAGE_STOP);
  s_ad3_stop_result=HAL_OK; s_ad3_raw=65536U;
  adc=BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_DIRECTION_AD3);
  assert(!adc.valid && adc.status==BSP_ADC_OUT_OF_RANGE && adc.stage==BSP_ADC_STAGE_VALUE);
  s_ad3_raw=0U; adc=BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_DIRECTION_AD3);
  assert(adc.valid && adc.raw==0U && adc.status==BSP_ADC_OK);
  s_cal_result=HAL_ERROR; BspAdc_Init(); before=s_config_calls;
  adc=BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_DIRECTION_AD3);
  assert(!adc.valid && adc.stage==BSP_ADC_STAGE_CALIBRATION && s_config_calls==before);
  /* A newly read value with failed cleanup is still NA, never the prior success. */
  Test_Reset(0U); Test_At(0U); s_ad3_stop_result=HAL_ERROR; s_ad3_raw=999U; Test_At(1000U);
  assert((!App_GetDiagnostics()->last_round.wind_direction.valid && App_GetDiagnostics()->last_round.ad3.stage==BSP_ADC_STAGE_STOP && App_GetDiagnostics()->last_round.ad3.stop_status==BSP_ADC_HAL_ERROR));
  assert(App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.wind.adc.valid);
}


