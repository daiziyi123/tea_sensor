static void Test_RawSamplesAndFaults(void)
{
  BspAdcResult adc;
  const AppSampleRound *round;
  unsigned before;
  Test_Reset(0U); s_soil_duration_ms=2U;s_wind_duration_ms=3U;s_ad3_duration_ms=4U;s_ad4_duration_ms=5U;s_ad5_duration_ms=7U;
  Test_At(950U);assert(!s_console_length);before=s_config_calls;Test_At(1000U);round=&App_GetDiagnostics()->last_round;
  assert(before==5U && s_config_calls==before && s_wind_reads==1U && s_ad4_reads==1U);
  assert(round->wind_read_start_ms==952U && round->ad4_read_start_ms==959U && round->ad4_read_end_ms==964U);
  assert(!s_console_length && round->ad4.valid && round->ad4.raw==34567U);
  Test_At(1000U);assert(!s_console_length && s_config_calls==before);
  Test_Reset(0U);s_wind_raw=s_ad4_raw=0U;Test_At(1000U);
  assert(App_GetDiagnostics()->last_round.wind.adc.valid && App_GetDiagnostics()->last_round.ad4.valid);
  assert(App_GetDiagnostics()->last_round.soil_ph.valid && App_GetDiagnostics()->last_round.soil_ph.range==SENSOR_PH_RANGE_LOW);
  assert(!s_console_length && App_GetDiagnostics()->last_round.ad4.raw==0U);
  s_wind_raw=321U;s_ad4_poll_result=HAL_TIMEOUT;s_ad4_stop_result=HAL_ERROR;Test_At(2000U);round=&App_GetDiagnostics()->last_round;
  assert(round->wind.adc.valid && round->wind.adc.raw==321U && !round->ad4.valid && !round->soil_ph.valid);
  assert(round->ad4.stage==BSP_ADC_STAGE_POLL && round->ad4.hal_status==BSP_ADC_HAL_TIMEOUT && round->ad4.stop_status==BSP_ADC_HAL_ERROR);
  assert(!s_console_length && round->ad4.status==BSP_ADC_POLL_FAILED);
  s_ad4_poll_result=HAL_OK;Test_At(3000U);
  assert(App_GetDiagnostics()->last_round.ad4.stage==BSP_ADC_STAGE_STOP && !App_GetDiagnostics()->last_round.soil_ph.valid);
  assert(!strstr(s_console,"AD4_SAMPLE") && App_GetDiagnostics()->last_round.ad4.status==BSP_ADC_STOP_FAILED);
  s_ad4_stop_result=HAL_OK;s_ad4_config_result=HAL_BUSY;Test_At(4000U);
  assert(App_GetDiagnostics()->last_round.ad4.stage==BSP_ADC_STAGE_CONFIG && App_GetDiagnostics()->last_round.ad4.config_status==BSP_ADC_HAL_BUSY);
  assert(!s_console_length && App_GetDiagnostics()->last_round.ad4.status==BSP_ADC_START_FAILED);
  s_ad4_config_result=HAL_OK;s_ad4_start_result=HAL_ERROR;Test_At(5000U);
  assert(App_GetDiagnostics()->last_round.ad4.stage==BSP_ADC_STAGE_START);
  assert(!s_console_length && App_GetDiagnostics()->last_round.ad4.status==BSP_ADC_START_FAILED);
  s_ad4_start_result=HAL_OK;s_ad4_raw=65536U;Test_At(6000U);
  assert(App_GetDiagnostics()->last_round.ad4.status==BSP_ADC_OUT_OF_RANGE && !App_GetDiagnostics()->last_round.soil_ph.valid);
  assert(App_GetDiagnostics()->ad4_diagnostics.cleanup_errors==2U && App_GetDiagnostics()->ad4_diagnostics.config_errors==1U);
  assert(!s_console_length && !App_GetDiagnostics()->last_round.soil_ph.valid);
  s_ad4_raw=65535U;s_wind_poll_result=HAL_TIMEOUT;Test_At(7000U);
  assert(App_GetDiagnostics()->last_round.ad4.valid && !App_GetDiagnostics()->last_round.wind.adc.valid);
  assert(!s_console_length && App_GetDiagnostics()->last_round.ad4.raw==65535U);
  s_prepare_result=HAL_TIMEOUT;before=s_config_calls;Test_At(8000U);
  assert(s_config_calls==before && App_GetDiagnostics()->last_round.ad4.prepare_status==BSP_ADC_HAL_TIMEOUT);
  assert(!s_console_length && App_GetDiagnostics()->last_round.ad4.status==BSP_ADC_STOP_FAILED);
  s_cal_result=HAL_ERROR;BspAdc_Init();Test_At(9000U);
  adc=BspAdc_ReadRaw(BSP_ADC_INPUT_PH_AD4);
  assert(!adc.valid && adc.stage==BSP_ADC_STAGE_CALIBRATION && !adc.prepare_attempted && !adc.stop_attempted);
  assert(s_config_calls==before);
  assert(!strstr(s_console,"AD4_SAMPLE") && App_GetDiagnostics()->last_round.ad4.status==BSP_ADC_CAL_FAILED);

  /* A failed console is never used; repeat processing adds no ADC read. */
  Test_Reset(0U);s_console_tx_result=HAL_ERROR;Test_At(3000U);before=s_config_calls;
  assert(App_GetDiagnostics()->printf_errors==0U);
  s_console_tx_result=HAL_OK;Test_At(3000U);
  assert(!s_console_length && s_config_calls==before);
  Test_At(6000U);assert(App_GetDiagnostics()->printf_errors==0U);
  assert(!s_console_length && App_GetDiagnostics()->last_round.soil_ph.valid);
}

