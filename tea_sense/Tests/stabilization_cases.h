static void Test_DtuControlledRecovery(void)
{
  uint32_t bases[2]={0U,UINT32_MAX-5000U};unsigned i;
  for(i=0U;i<2U;++i) {
    char frozen[APP_UPLOAD_FRAME_CAPACITY];size_t capacity;
    unsigned aborts,reads;uint32_t base=bases[i],locked=base+APP_SAMPLE_WINDOW_MS+1002U;
    Test_Reset(base);s_tx_auto_complete=0U;Test_At(base);Test_Through(base+APP_SAMPLE_WINDOW_MS);
    s_abort_tx_result=HAL_ERROR;Test_At(base+APP_SAMPLE_WINDOW_MS+1000U);Test_At(base+APP_SAMPLE_WINDOW_MS+1001U);Test_At(locked);
    assert(BspDtu_GetDiagnostics().tx_state==BSP_DTU_TX_BLOCKED);
    memcpy(frozen,s_active_tx,s_active_tx_length);aborts=s_tx_aborts;reads=s_soil_reads;
    assert(BspDtu_Init()==BSP_DTU_UART_ERROR);
    assert(BspDtu_TxBuffer(&capacity)==NULL && s_tx_aborts==aborts+1U);
    aborts=s_tx_aborts;
    Test_At(locked+100U);Test_At(locked+BSP_DTU_RECOVERY_PERIOD_MS-1U);
    assert(s_tx_aborts==aborts && s_soil_reads>reads && s_rx_armed);
    assert(!memcmp(frozen,s_active_tx,s_active_tx_length));
    Test_At(locked+BSP_DTU_RECOVERY_PERIOD_MS);
    assert(s_tx_aborts==aborts+1U && BspDtu_GetDiagnostics().recovery_attempts==1U);
    assert(BspDtu_TxBuffer(&capacity)==NULL && !memcmp(frozen,s_active_tx,s_active_tx_length));
    Test_At(locked+2U*BSP_DTU_RECOVERY_PERIOD_MS-1U);assert(s_tx_aborts==aborts+1U);
    s_abort_tx_result=HAL_OK;s_tx_auto_complete=1U;
    Test_At(locked+2U*BSP_DTU_RECOVERY_PERIOD_MS);
    assert(BspDtu_GetDiagnostics().recovery_successes==1U && BspDtu_GetDiagnostics().recovery_attempts==2U);
    assert(BspDtu_GetDiagnostics().tx_errors==1U && AppUpload_GetDiagnostics()->tx_errors==1U);
    /* Next available new window, never replay the failed 3000ms snapshot. */
    if(s_uart6_sends==1U) { Test_Through(base+2U*APP_SAMPLE_WINDOW_MS); }
    assert(s_uart6_sends==2U && s_active_tx[0]=='\r' && s_active_tx[1]=='\n');
    assert(s_rx_armed && BspDtu_GetDiagnostics().tx_abort_errors==4U);
  }
}

static void Test_AdcCalibrationRecovery(void)
{
  unsigned i,cal;uint32_t bases[2]={0U,UINT32_MAX-1000U};
  for(i=0U;i<2U;++i) {
    uint32_t base=bases[i];BspAdcResult raw;
    Test_Reset(base);s_cal_result=HAL_TIMEOUT;BspAdc_Init();cal=s_cal_calls;
    assert(BspAdc_GetDiagnostics()->initial_calibration_status==BSP_ADC_HAL_TIMEOUT);
    raw=BspAdc_ReadRaw(BSP_ADC_INPUT_PH_AD4);assert(!raw.valid && raw.status==BSP_ADC_CAL_FAILED);
    s_cal_result=HAL_OK;s_tick_ms=base+BSP_ADC_CAL_RECOVERY_PERIOD_MS-1U;BspAdc_Process();
    assert(s_cal_calls==cal && !BspAdc_GetDiagnostics()->recovery_attempts);
    s_prepare_result=HAL_ERROR;s_tick_ms=base+BSP_ADC_CAL_RECOVERY_PERIOD_MS;BspAdc_Process();
    assert(s_cal_calls==cal && BspAdc_GetDiagnostics()->recovery_stop_errors==1U);
    s_prepare_result=HAL_OK;s_tick_ms=base+2U*BSP_ADC_CAL_RECOVERY_PERIOD_MS-1U;BspAdc_Process();
    assert(s_cal_calls==cal);
    s_tick_ms=base+2U*BSP_ADC_CAL_RECOVERY_PERIOD_MS;BspAdc_Process();
    assert(s_cal_calls==cal+1U && BspAdc_GetDiagnostics()->recovery_successes==1U);
    assert(BspAdc_GetDiagnostics()->calibration_failures==1U);
    assert(BspAdc_GetDiagnostics()->initial_calibration_status==BSP_ADC_HAL_TIMEOUT);
    s_raw=0U;raw=BspAdc_ReadRaw(BSP_ADC_INPUT_SOIL_AD0);assert(raw.valid && raw.raw==0U);
    s_tick_ms+=10U*BSP_ADC_CAL_RECOVERY_PERIOD_MS;BspAdc_Process();assert(s_cal_calls==cal+1U);
    Test_Reset(base);s_cal_result=HAL_ERROR;BspAdc_Init();cal=s_cal_calls;
    s_tick_ms=base+BSP_ADC_CAL_RECOVERY_PERIOD_MS;BspAdc_Process();
    assert(s_cal_calls==cal+1U && !BspAdc_GetDiagnostics()->calibrated);
    s_tick_ms=base+2U*BSP_ADC_CAL_RECOVERY_PERIOD_MS-1U;BspAdc_Process();assert(s_cal_calls==cal+1U);
    s_tick_ms++;BspAdc_Process();assert(s_cal_calls==cal+2U && BspAdc_GetDiagnostics()->calibration_failures==3U);
    raw=BspAdc_ReadRaw(BSP_ADC_INPUT_WIND_SPEED_AD2);assert(!raw.valid && raw.status==BSP_ADC_CAL_FAILED);
  }
}

