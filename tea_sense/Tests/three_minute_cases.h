static uint8_t Test_JsonField(const char *path,const char *expected)
{
  const char *value;size_t length;JSONTypes_t type;
  JSONStatus_t status=JSON_SearchConst(s_transmitted,s_transmitted_length,path,strlen(path),&value,&length,&type);
  if(!expected) { return status==JSONNotFound; }
  return status==JSONSuccess && length==strlen(expected) && !memcmp(value,expected,length);
}

static void Test_Through(uint32_t end)
{
  while ((uint32_t)(end - s_tick_ms) >= 100U) { Test_At(s_tick_ms + 100U); }
  if (s_tick_ms != end) { Test_At(end); }
}


static void Test_FirstWindow(uint32_t base)
{
  Test_Reset(base); Test_At(base); Test_Through(base+APP_SAMPLE_WINDOW_MS);
  assert(s_uart6_sends==1U);
}
static void Test_ThreeMinuteSchedule(void)
{
  uint32_t bases[2]={0U,UINT32_MAX-2000U};unsigned i;
  for(i=0U;i<2U;++i) {
    uint32_t base=bases[i]; unsigned reads;
    Test_Reset(base);Test_At(base);Test_Through(base+APP_SAMPLE_WINDOW_MS-1U);
    assert(!s_uart6_sends && !App_GetDiagnostics()->completed_windows);
    reads=s_config_calls;Test_At(base+APP_SAMPLE_WINDOW_MS);
    assert(s_uart6_sends==1U && s_config_calls==reads+5U);
    assert(App_GetDiagnostics()->last_window_end_ms==base+APP_SAMPLE_WINDOW_MS);
    assert(Test_JsonField("window_ms","180000") && Test_JsonField("data.wind_speed_mps.statistics.valid_count","1800"));
    assert(Test_JsonField("data.soil_moisture_pct.statistics.method","median"));
    assert(Test_JsonField("data.soil_moisture_pct.statistics.valid_count","5"));
    Test_Inject(s_ack,s_ack_length);Test_At(base+APP_SAMPLE_WINDOW_MS+1U);assert(Test_Acks());
    Test_Through(base+2U*APP_SAMPLE_WINDOW_MS);assert(s_uart6_sends==2U);
    Test_Inject(s_ack,s_ack_length);Test_At(base+2U*APP_SAMPLE_WINDOW_MS+1U);
    Test_At(base+6U*APP_SAMPLE_WINDOW_MS+500U);
    assert(s_uart6_sends==3U && App_GetDiagnostics()->completed_windows==6U);
    assert(Test_JsonField("data.wind_speed_mps.value","null") && Test_JsonField("data.wind_speed_mps.statistics.valid_count","0"));
    Test_At(base+6U*APP_SAMPLE_WINDOW_MS+501U);assert(s_uart6_sends==3U);
    assert(!s_console_length && App_GetDiagnostics()->sequence>3600U);
  }
}
static void Test_ThreeMinuteTransport(void)
{
  AppUploadAck ack;
  const char *invalid[]={
    "{\"ok\":\"true\",\"device_id\":\"tea_h743_001\",\"seq\":1,\"id\":1}",
    "{\"ok\":1,\"device_id\":\"tea_h743_001\",\"seq\":1,\"id\":1}",
    "{\"ok\":true,\"device_id\":\"tea_h743_001\",\"seq\":\"1\",\"id\":1}",
    "{\"ok\":true,\"device_id\":\"tea_h743_001\",\"seq\":1.0,\"id\":1}",
    "{\"ok\":false,\"ok\":true,\"device_id\":\"tea_h743_001\",\"seq\":1,\"id\":1}",
    "{\"ok\":true \"device_id\":\"tea_h743_001\",\"seq\":1,\"id\":1}",
    "{\"ok\":true,\"device_id\":\"tea_h743_001\",\"seq\":4294967296,\"id\":1}",
    "{\"ok\":true,\"device_id\":\"tea_h743_001\",\"seq\":1,\"id\":0}"};
  const char *ignored[]={"{\"ok\":false,\"device_id\":\"tea_h743_001\",\"seq\":1,\"id\":1}",
    "{\"ok\":true,\"device_id\":\"other\",\"seq\":1,\"id\":1}",
    "{\"ok\":true,\"device_id\":\"tea_h743_001\",\"seq\":2,\"id\":1}","{\"ok\":true}"};
  static const char reordered[]="{\"extra\":{\"ok\":false,\"nested\":[1,2]},\"id\":9223372036854775807,\"seq\":1,\"device_id\":\"tea_h743_001\",\"ok\":true}\r\n";
  unsigned index;
  for(index=0U;index<sizeof(invalid)/sizeof(invalid[0]);++index)
    assert(AppUpload_ParseAck(invalid[index],strlen(invalid[index]),1U,&ack)==APP_UPLOAD_ACK_INVALID);
  for(index=0U;index<sizeof(ignored)/sizeof(ignored[0]);++index)
    assert(AppUpload_ParseAck(ignored[index],strlen(ignored[index]),1U,&ack)==APP_UPLOAD_ACK_IGNORED);
  assert(AppUpload_ParseAck(reordered,sizeof(reordered)-1U,1U,&ack)==APP_UPLOAD_ACK_ACCEPTED && ack.id==INT64_MAX);

  Test_FirstWindow(0U);
  { size_t split=s_ack_length/2U;
    Test_Inject(s_ack,split);Test_At(APP_SAMPLE_WINDOW_MS+1U);assert(!Test_Acks());
    Test_Inject(s_ack+split,s_ack_length-split);Test_At(APP_SAMPLE_WINDOW_MS+2U);assert(Test_Acks()); }
  Test_FirstWindow(0U);
  { unsigned reads=s_soil_reads;
    Test_Through(APP_SAMPLE_WINDOW_MS+APP_DTU_TIMEOUT_MS-1U);
    assert(s_soil_reads>reads && !AppUpload_GetDiagnostics()->timeouts);
    Test_At(APP_SAMPLE_WINDOW_MS+APP_DTU_TIMEOUT_MS);
    assert(AppUpload_GetDiagnostics()->timeouts==1U && s_uart6_sends==1U);
    Test_Through(2U*APP_SAMPLE_WINDOW_MS);assert(s_uart6_sends==2U); }
  Test_Reset(0U);s_tx_auto_complete=0U;Test_At(0U);Test_Through(APP_SAMPLE_WINDOW_MS);
  { char frozen[APP_UPLOAD_FRAME_CAPACITY];unsigned reads=s_soil_reads;
    memcpy(frozen,s_active_tx,s_active_tx_length);
    Test_Inject(s_ack,s_ack_length);Test_At(APP_SAMPLE_WINDOW_MS+100U);
    assert(s_soil_reads>reads && !memcmp(frozen,s_active_tx,s_active_tx_length));
    assert(!AppUpload_GetDiagnostics()->ack_count);
    Test_TxComplete();Test_At(APP_SAMPLE_WINDOW_MS+101U);assert(AppUpload_GetDiagnostics()->ack_count==1U); }
  Test_Reset(0U);s_tx_result=HAL_ERROR;Test_At(0U);Test_Through(APP_SAMPLE_WINDOW_MS);
  { unsigned errors=AppUpload_GetDiagnostics()->tx_errors,seq=AppUpload_GetDiagnostics()->last_sequence;
    Test_At(APP_SAMPLE_WINDOW_MS+1U);Test_At(APP_SAMPLE_WINDOW_MS+2U);
    assert(errors && AppUpload_GetDiagnostics()->tx_errors==errors && AppUpload_GetDiagnostics()->last_sequence==seq);
    s_tx_result=HAL_OK;Test_Through(2U*APP_SAMPLE_WINDOW_MS);assert(AppUpload_GetDiagnostics()->last_sequence==seq+1U); }
  Test_Reset(0U);Test_At(0U);Test_Through(APP_SAMPLE_WINDOW_MS-100U);
  s_format_fault=1;Test_At(APP_SAMPLE_WINDOW_MS);s_format_fault=0;
  assert(AppUpload_GetDiagnostics()->format_errors==1U);
  Test_At(APP_SAMPLE_WINDOW_MS+1U);assert(AppUpload_GetDiagnostics()->format_errors==1U && !s_uart6_sends);
}
static void Test_WeatherCachedFrame(void)
{
  SensorWeatherReply reply={0};uint32_t p=APP_SAMPLE_WINDOW_MS;
  Test_Reset(0U);Test_At(0U);Test_Through(p-1000U);
  reply.status=SENSOR_WEATHER_OK;reply.valid_fields=3U;
  reply.value.temperature_c_x10=194;reply.value.humidity_rh_pct_x10=708U;
  SensorWeather_RecordQuery(0,500U,p-900U);SensorWeather_Complete(0,&reply,p-890U,p-889U);
  reply.status=SENSOR_WEATHER_CRC_ERROR;
  SensorWeather_RecordQuery(0,501U,p-500U);SensorWeather_Complete(0,&reply,0U,p-499U);
  Test_At(p);
  assert(Test_JsonField("data.temperature_c.value","19.4") && Test_JsonField("data.temperature_c.status","CACHED"));
  assert(Test_JsonField("data.temperature_c.sample_seq","500") && Test_JsonField("data.temperature_c.latest_query_status","CRC_ERROR"));
  Test_Inject(s_ack,s_ack_length);Test_At(p+1U);Test_At(2U*p);
  assert(Test_JsonField("data.temperature_c.value","null") && Test_JsonField("data.temperature_c.status","STALE"));
}

static void Test_OverflowAndLongLine(void)
{
  unsigned index; const char noise='x';
  Test_FirstWindow(0U); Test_Inject(s_ack,10U); Test_At((1U*APP_SAMPLE_WINDOW_MS+10U));
  for(index=0U;index<300U;++index) { Test_Inject(&noise,1U); }
  assert(BspDtu_GetDiagnostics().rx_overflows==45U);
  Test_At((1U*APP_SAMPLE_WINDOW_MS+20U)); assert(!Test_Acks() && AppUpload_GetDiagnostics()->stream_gaps);
  Test_Inject(s_ack,s_ack_length); Test_At((1U*APP_SAMPLE_WINDOW_MS+30U)); assert(!Test_Acks());
  Test_Inject(s_ack,s_ack_length); Test_At((1U*APP_SAMPLE_WINDOW_MS+40U)); assert(Test_Acks()==1U);
  Test_FirstWindow(0U);
  for(index=0U;index<513U;++index) { Test_Inject(&noise,1U); if(index%32U==31U) { Test_At((1U*APP_SAMPLE_WINDOW_MS+1U)); } }
  Test_Inject(s_ack,s_ack_length); Test_At((1U*APP_SAMPLE_WINDOW_MS+10U)); assert(!Test_Acks());
  Test_Inject(s_ack,s_ack_length); Test_At((1U*APP_SAMPLE_WINDOW_MS+20U)); assert(Test_Acks()==1U);
}

static void Test_ReceiveRecovery(void)
{
  uint8_t scratch[32];
  Test_FirstWindow(0U); Test_Inject(s_ack,10U); Test_At((1U*APP_SAMPLE_WINDOW_MS+10U));
  Test_Error(HAL_UART_ERROR_ORE|HAL_UART_ERROR_FE);
  assert(s_rx_armed && BspDtu_GetDiagnostics().rx_errors==1U);
  assert(BspDtu_GetDiagnostics().last_uart_error==(HAL_UART_ERROR_ORE|HAL_UART_ERROR_FE));
  Test_At((1U*APP_SAMPLE_WINDOW_MS+11U)); assert(!Test_Acks() && AppUpload_GetDiagnostics()->stream_gaps);
  Test_Inject(s_ack+10U,s_ack_length-10U); Test_Inject(s_ack,s_ack_length); Test_At((1U*APP_SAMPLE_WINDOW_MS+20U)); assert(Test_Acks()==1U);
  Test_Reset(0U); s_rx_result=HAL_ERROR; Test_Inject("a",1U);
  assert(!BspDtu_GetDiagnostics().rx_active && BspDtu_GetDiagnostics().rx_start_errors==1U);
  s_rx_result=HAL_OK; Test_At(1U); assert(s_rx_armed);
  Test_Inject("\n",1U); Test_At(2U); Test_Through((1U*APP_SAMPLE_WINDOW_MS+0U));
  Test_Inject(s_ack,s_ack_length); Test_At((1U*APP_SAMPLE_WINDOW_MS+1U)); assert(Test_Acks()==1U);
  Test_Reset(0U); s_abort_result=HAL_ERROR; Test_Error(HAL_UART_ERROR_PE);
  assert(BspDtu_GetDiagnostics().last_abort_status==BSP_DTU_UART_ERROR);
  Test_At(1U); assert(!BspDtu_GetDiagnostics().rx_active);
  s_abort_result=HAL_OK; Test_At(2U); assert(s_rx_armed);
  Test_Inject("\n",1U); Test_At(3U); Test_Through((1U*APP_SAMPLE_WINDOW_MS+0U));
  Test_Inject(s_ack,s_ack_length); Test_At((1U*APP_SAMPLE_WINDOW_MS+1U)); assert(Test_Acks()==1U);
  Test_Reset(0U); s_rx_result=HAL_ERROR; App_Init();
  assert(AppUpload_GetDiagnostics()->init_status==BSP_DTU_UART_ERROR);
  Test_Through((1U*APP_SAMPLE_WINDOW_MS+0U)); assert(!s_uart6_sends && BspDtu_GetDiagnostics().last_tx_status==BSP_DTU_RX_NOT_READY);
  s_rx_result=HAL_OK; Test_At((1U*APP_SAMPLE_WINDOW_MS+1U)); assert(s_rx_armed);
  Test_Inject("\n",1U); Test_At((1U*APP_SAMPLE_WINDOW_MS+2U)); Test_Through((2U*APP_SAMPLE_WINDOW_MS+0U));
  Test_Inject(s_ack,s_ack_length); Test_At((2U*APP_SAMPLE_WINDOW_MS+1U)); assert(Test_Acks()==1U);
  __disable_irq(); (void)BspDtu_Read(scratch,sizeof(scratch));
  (void)BspDtu_GetDiagnostics(); assert(s_irq_mask==1U); __set_PRIMASK(0U);
}

static void Test_SendFailuresAndLengthGuard(void)
{
  const HAL_StatusTypeDef failures[]={HAL_ERROR,HAL_BUSY,HAL_TIMEOUT};
  const BspDtuStatus statuses[]={BSP_DTU_UART_ERROR,BSP_DTU_UART_BUSY,BSP_DTU_UART_TIMEOUT};
  unsigned index,starts,writes;
  for(index=0U;index<3U;++index)
  {
    Test_Reset(0U); s_tx_result=failures[index]; Test_At(0U); Test_Through((1U*APP_SAMPLE_WINDOW_MS+0U));
    assert(BspDtu_GetDiagnostics().last_tx_status==statuses[index] && AppUpload_GetDiagnostics()->last_sequence==1U);
    assert(BspDtu_GetDiagnostics().tx_errors==1U);
    Test_Inject(s_ack,s_ack_length); Test_At((1U*APP_SAMPLE_WINDOW_MS+10U)); assert(!Test_Acks());
    s_tx_result=HAL_OK; Test_Through((2U*APP_SAMPLE_WINDOW_MS+0U)); assert(s_uart6_sends==2U);
    Test_Inject(s_ack,s_ack_length); Test_At((2U*APP_SAMPLE_WINDOW_MS+1U)); assert(Test_Acks()==1U);
  }
  Test_Reset(0U); s_format_fault=1; Test_At(0U); Test_Through((1U*APP_SAMPLE_WINDOW_MS+0U));
  assert(!s_uart6_sends && (AppUpload_GetDiagnostics()->format_errors==1U && AppUpload_GetDiagnostics()->last_sequence==1U));
  s_format_fault=2; Test_Through((2U*APP_SAMPLE_WINDOW_MS+0U)); assert(!s_uart6_sends);
  s_format_fault=0; Test_Through((3U*APP_SAMPLE_WINDOW_MS+0U)); assert(s_uart6_sends==1U);
  Test_At((3U*APP_SAMPLE_WINDOW_MS+1U));
  assert(BspDtu_TxBuffer(NULL)==NULL);
  assert(BspDtu_StartSend(0U)==BSP_DTU_BAD_LENGTH);
  assert(BspDtu_StartSend(65536U)==BSP_DTU_BAD_LENGTH);
  starts=s_rx_starts; writes=s_gpio_writes;
  HAL_UART_RxCpltCallback(&huart2); HAL_UART_ErrorCallback(&huart2);
  assert(s_rx_starts==starts && s_gpio_writes==writes);
  Test_Reset(0U); s_echo_during_tx=1U; Test_At(0U); Test_Through((1U*APP_SAMPLE_WINDOW_MS+0U));
  Test_At((1U*APP_SAMPLE_WINDOW_MS+1U)); assert(Test_Acks()==1U);
}

