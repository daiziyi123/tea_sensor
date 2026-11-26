static void Test_Ad5RawSelectionAndFaults(void)
{
  BspAdcResult adc;
  const AppDiagnostics *d;
  unsigned configs,stops;
  static const uint32_t order[10]={ADC_CHANNEL_5,ADC_CHANNEL_3,ADC_CHANNEL_19,ADC_CHANNEL_18,ADC_CHANNEL_15,
    ADC_CHANNEL_5,ADC_CHANNEL_3,ADC_CHANNEL_19,ADC_CHANNEL_18,ADC_CHANNEL_15};
  unsigned i;
  assert(BSP_ADC_INPUT_SOIL_AD0==0 && BSP_ADC_INPUT_WIND_SPEED_AD2==1 &&
         BSP_ADC_INPUT_WIND_DIRECTION_AD3==2 && BSP_ADC_INPUT_PH_AD4==3 && BSP_ADC_INPUT_RAIN_AD5==4);
  Test_Reset(0U);s_ad5_raw=0U;
  assert(!App_GetDiagnostics()->ad5_sequence && !strstr(s_console,"AD5_SAMPLE"));
  Test_At(0U);d=App_GetDiagnostics();
  assert(d->ad5_sequence==1U && d->ad5_sample_t_ms==0U && d->last_ad5.valid && d->last_ad5.raw==0U);
  assert(d->last_ad5.status==BSP_ADC_OK && !s_adc_active);
  s_ad5_raw=65535U;Test_At(100U);
  assert(d->last_ad5.valid && d->last_ad5.raw==65535U && d->ad5_sequence==2U);
  assert(s_config_calls==10U && s_adc_stop_calls==20U && s_cal_calls==1U);
  for(i=0U;i<10U;++i) { assert(s_channel_history[i]==order[i]); }
  assert(d->last_round.soil.adc.raw==15791U && d->last_round.wind.adc.raw==12345U);
  assert(d->last_round.ad3.raw==23456U && d->last_round.ad4.raw==34567U);
  s_ad5_raw=65536U;Test_At(200U);
  assert(!d->last_ad5.valid && d->last_ad5.status==BSP_ADC_OUT_OF_RANGE && d->last_ad5.stage==BSP_ADC_STAGE_VALUE);
  assert(d->last_ad5.hal_status==BSP_ADC_HAL_OK && d->last_ad5.stop_status==BSP_ADC_HAL_OK);
  s_ad5_raw=15700U;s_ad5_config_result=HAL_BUSY;s_ad5_stop_result=HAL_ERROR;Test_At(300U);
  assert(!d->last_ad5.valid && d->last_ad5.status==BSP_ADC_START_FAILED && d->last_ad5.stage==BSP_ADC_STAGE_CONFIG);
  assert(d->last_ad5.config_attempted && d->last_ad5.config_status==BSP_ADC_HAL_BUSY &&
    d->last_ad5.hal_status==BSP_ADC_HAL_BUSY && d->last_ad5.stop_status==BSP_ADC_HAL_ERROR);
  s_ad5_config_result=HAL_OK;s_ad5_start_result=HAL_ERROR;s_ad5_stop_result=HAL_TIMEOUT;Test_At(400U);
  assert(!d->last_ad5.valid && d->last_ad5.stage==BSP_ADC_STAGE_START &&
    d->last_ad5.hal_status==BSP_ADC_HAL_ERROR && d->last_ad5.stop_status==BSP_ADC_HAL_TIMEOUT);
  s_ad5_start_result=HAL_OK;s_ad5_poll_result=HAL_TIMEOUT;s_ad5_stop_result=HAL_ERROR;Test_At(500U);
  assert(!d->last_ad5.valid && d->last_ad5.status==BSP_ADC_POLL_FAILED &&
    d->last_ad5.stage==BSP_ADC_STAGE_POLL && d->last_ad5.hal_status==BSP_ADC_HAL_TIMEOUT &&
    d->last_ad5.stop_status==BSP_ADC_HAL_ERROR);
  s_ad5_poll_result=HAL_OK;Test_At(600U);
  assert(!d->last_ad5.valid && d->last_ad5.status==BSP_ADC_STOP_FAILED &&
    d->last_ad5.stage==BSP_ADC_STAGE_STOP && d->last_ad5.hal_status==BSP_ADC_HAL_ERROR);
  assert(d->ad5_sequence==7U && d->ad5_sample_errors==5U && !d->sample_errors &&
    d->last_round.soil.adc.valid && d->last_round.wind.adc.valid && d->last_round.ad3.valid && d->last_round.ad4.valid);
  configs=s_config_calls;stops=s_adc_stop_calls;s_prepare_result=HAL_TIMEOUT;
  adc=BspAdc_ReadRaw(BSP_ADC_INPUT_RAIN_AD5);
  assert(!adc.valid && adc.stage==BSP_ADC_STAGE_PREPARE && adc.prepare_status==BSP_ADC_HAL_TIMEOUT);
  assert(adc.prepare_attempted && !adc.config_attempted && !adc.stop_attempted &&
    s_config_calls==configs && s_adc_stop_calls==stops+1U);
  s_prepare_result=HAL_OK;s_ad5_stop_result=HAL_OK;
  adc=BspAdc_ReadRaw(BSP_ADC_INPUT_RAIN_AD5);assert(adc.valid && !s_adc_active);
  configs=s_config_calls;adc=BspAdc_ReadRaw(BSP_ADC_INPUT_SOIL_AD0);
  assert(adc.valid && adc.raw==15791U && s_selected_channel==ADC_CHANNEL_5 && s_config_calls==configs+1U);
  s_cal_result=HAL_BUSY;BspAdc_Init();configs=s_config_calls;Test_At(700U);
  assert(!d->last_ad5.valid && d->last_ad5.status==BSP_ADC_CAL_FAILED &&
    d->last_ad5.hal_status==BSP_ADC_HAL_BUSY && d->ad5_sequence==8U && s_config_calls==configs);
}

