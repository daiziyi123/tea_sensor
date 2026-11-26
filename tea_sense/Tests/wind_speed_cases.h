static void Test_WindSpeedCalibration(void)
{
  static const struct
  {
    uint64_t sum;
    uint32_t count;
    int32_t input_mv;
    uint32_t speed_x100;
    SensorWindSpeedRange range;
  } cases[] = {
    {6779U,100U,40,0U,SENSOR_WIND_SPEED_RANGE_OK},
    {1579321U,100U,2540,1500U,SENSOR_WIND_SPEED_RANGE_OK},
    {3151863U,100U,5040,3000U,SENSOR_WIND_SPEED_RANGE_OK},
    {6778U,100U,40,0U,SENSOR_WIND_SPEED_RANGE_LOW},
    {6780U,100U,40,0U,SENSOR_WIND_SPEED_RANGE_OK},
    {3151862U,100U,5040,3000U,SENSOR_WIND_SPEED_RANGE_OK},
    {3151864U,100U,5040,3000U,SENSOR_WIND_SPEED_RANGE_HIGH},
    {0U,1U,29,0U,SENSOR_WIND_SPEED_RANGE_LOW},
    {65535U,1U,10448,3000U,SENSOR_WIND_SPEED_RANGE_HIGH},
    /* Rounded 41mV would yield 0.01m/s via a voltage-first conversion. */
    {73U,1U,41,0U,SENSOR_WIND_SPEED_RANGE_OK},
    /* Rounding the raw mean to 73.03 first would yield 0.00m/s. */
    {73032U,1000U,41,1U,SENSOR_WIND_SPEED_RANGE_OK},
    {12345U,1U,1992,1171U,SENSOR_WIND_SPEED_RANGE_OK},
    {93535U,30U,525,291U,SENSOR_WIND_SPEED_RANGE_OK},
    {(uint64_t)UINT32_MAX*65535U,UINT32_MAX,10448,3000U,SENSOR_WIND_SPEED_RANGE_HIGH},
    {(uint64_t)UINT32_MAX*32768U,UINT32_MAX,5239,3000U,SENSOR_WIND_SPEED_RANGE_HIGH},
    {(uint64_t)UINT32_MAX*67U+123456789U,UINT32_MAX,40,0U,SENSOR_WIND_SPEED_RANGE_LOW}
  };
  SensorWindSpeedValue value;
  unsigned i;
  uint32_t raw, last_speed=0U;
  int32_t last_mv=0;
  assert(SENSOR_WIND_SPEED_RAW_SPAN_X100==3145084U);
  for(i=0U;i<sizeof(cases)/sizeof(cases[0]);++i)
  {
    value=SensorWindSpeed_ConvertMean(cases[i].sum,cases[i].count);
    assert(value.valid && value.input_mv==cases[i].input_mv);
    assert(value.wind_speed_mps_x100==cases[i].speed_x100 && value.range==cases[i].range);
  }
  value=SensorWindSpeed_ConvertMean(0U,0U);
  assert(!value.valid && value.range==SENSOR_WIND_SPEED_RANGE_UNAVAILABLE);
  assert(!SensorWindSpeed_ConvertMean(65536U,1U).valid);
  assert(!SensorWindSpeed_ConvertMean(UINT64_MAX,UINT32_MAX).valid);
  for(raw=0U;raw<=UINT16_MAX;++raw)
  {
    value=SensorWindSpeed_ConvertMean(raw,1U);
    assert(value.valid && value.input_mv>=last_mv && value.wind_speed_mps_x100>=last_speed);
    assert(value.wind_speed_mps_x100<=3000U);
    last_mv=value.input_mv; last_speed=value.wind_speed_mps_x100;
  }
}

static void Test_AdcPairAndFailures(void)
{
  BspAdcResult adc; SensorWindSpeedData wind;
  unsigned before;
  Test_Reset(0U);
  adc=BspAdc_ReadRaw(BSP_ADC_INPUT_SOIL_AD0);
  assert(adc.valid && adc.raw==15791U && s_selected_channel==ADC_CHANNEL_5 && s_config_calls==1U);
  wind=SensorWindSpeed_Read();
  assert(wind.adc.valid && wind.adc.raw==12345U && s_selected_channel==ADC_CHANNEL_3 && s_config_calls==2U);
  assert(wind.value.valid && wind.value.input_mv==1992 && wind.value.wind_speed_mps_x100==1171U);
  s_raw=0U; adc=BspAdc_ReadRaw(BSP_ADC_INPUT_SOIL_AD0);
  assert(adc.valid && !adc.raw && s_selected_channel==ADC_CHANNEL_5 && s_config_calls==3U && s_cal_calls==1U);
  before=s_config_calls; adc=BspAdc_ReadRaw((BspAdcInput)99);
  assert(!adc.valid && adc.stage==BSP_ADC_STAGE_INPUT && !adc.prepare_attempted && s_config_calls==before);
  s_config_result=HAL_BUSY; adc=BspAdc_ReadRaw(BSP_ADC_INPUT_SOIL_AD0);
  assert(!adc.valid && adc.status==BSP_ADC_START_FAILED && adc.stage==BSP_ADC_STAGE_CONFIG);
  assert(adc.config_status==BSP_ADC_HAL_BUSY && adc.stop_attempted && adc.stop_status==BSP_ADC_HAL_OK);
  wind=SensorWindSpeed_Read(); assert(wind.adc.valid && wind.adc.raw==12345U);
  s_config_result=HAL_OK; s_start_result=HAL_ERROR; adc=BspAdc_ReadRaw(BSP_ADC_INPUT_SOIL_AD0);
  assert(!adc.valid && adc.stage==BSP_ADC_STAGE_START && adc.stop_attempted);
  s_start_result=HAL_OK; s_poll_result=HAL_TIMEOUT; s_stop_result=HAL_ERROR;
  adc=BspAdc_ReadRaw(BSP_ADC_INPUT_SOIL_AD0);
  assert(!adc.valid && adc.stage==BSP_ADC_STAGE_POLL && adc.hal_status==BSP_ADC_HAL_TIMEOUT && adc.stop_status==BSP_ADC_HAL_ERROR);
  /* Unrecoverable shared stop failure prevents both from configuring/starting. */
  s_prepare_result=HAL_TIMEOUT; before=s_config_calls;
  wind=SensorWindSpeed_Read(); adc=BspAdc_ReadRaw(BSP_ADC_INPUT_SOIL_AD0);
  assert(!wind.adc.valid && !adc.valid && s_config_calls==before);
  assert(wind.adc.stage==BSP_ADC_STAGE_PREPARE && adc.prepare_status==BSP_ADC_HAL_TIMEOUT);
  s_prepare_result=HAL_OK; s_poll_result=HAL_OK; s_stop_result=HAL_OK;
  s_wind_stop_result=HAL_ERROR; wind=SensorWindSpeed_Read();
  assert(!wind.adc.valid && wind.adc.stage==BSP_ADC_STAGE_STOP && !wind.value.valid);
  adc=BspAdc_ReadRaw(BSP_ADC_INPUT_SOIL_AD0); assert(adc.valid && !adc.raw);
  s_wind_stop_result=HAL_OK; s_wind_config_result=HAL_ERROR; wind=SensorWindSpeed_Read();
  assert(!wind.adc.valid && wind.adc.stage==BSP_ADC_STAGE_CONFIG && !wind.adc.raw);
  s_wind_config_result=HAL_OK; s_wind_start_result=HAL_BUSY; wind=SensorWindSpeed_Read();
  assert(!wind.adc.valid && wind.adc.stage==BSP_ADC_STAGE_START);
  s_wind_start_result=HAL_OK; s_wind_poll_result=HAL_TIMEOUT; wind=SensorWindSpeed_Read();
  assert(!wind.adc.valid && wind.adc.stage==BSP_ADC_STAGE_POLL);
  s_wind_poll_result=HAL_OK; s_wind_raw=65536U; wind=SensorWindSpeed_Read();
  assert(!wind.adc.valid && wind.adc.status==BSP_ADC_OUT_OF_RANGE);
  s_cal_result=HAL_ERROR; BspAdc_Init(); before=s_config_calls;
  wind=SensorWindSpeed_Read(); adc=BspAdc_ReadRaw(BSP_ADC_INPUT_SOIL_AD0);
  assert(!wind.adc.valid && !adc.valid && adc.stage==BSP_ADC_STAGE_CALIBRATION && s_config_calls==before);
}


