#ifndef WEATHER_CASES_H
#define WEATHER_CASES_H

static void Test_WeatherInject(unsigned port,const uint8_t *data,size_t length)
{
  size_t index;
  for(index=0U;index<length;++index) {
    assert(s_weather_armed[port] && !s_irq_mask);
    *s_weather_destination[port]=data[index]; s_weather_armed[port]=0U;
    s_in_callback=1U; HAL_UART_RxCpltCallback(port?&huart4:&huart3); s_in_callback=0U;
  }
}
static size_t Test_WeatherFrame(uint8_t *frame,uint16_t first,uint16_t second,uint8_t words)
{
  size_t length=(size_t)words*2U+5U; uint16_t crc;
  frame[0]=1U;frame[1]=3U;frame[2]=(uint8_t)(words*2U);
  frame[3]=(uint8_t)(first>>8);frame[4]=(uint8_t)first;
  if(words==2U) {frame[5]=(uint8_t)(second>>8);frame[6]=(uint8_t)second;}
  crc=SensorWeather_Crc16(frame,length-2U);frame[length-2U]=(uint8_t)crc;frame[length-1U]=(uint8_t)(crc>>8);return length;
}
static void Test_WeatherAutoReply(unsigned port)
{
  uint8_t frame[9];uint16_t first=0U,second=0U;size_t length;
  switch(s_weather_requests[port][3]) {
    case 0U:first=456U;second=0xFFC9U;break;
    case 2U:first=0x1234U;second=0x5678U;break;
    case 20U:first=12U;second=34U;break;
    case 11U:first=10132U;break;
    case 7U:first=600U;break;
    case 8U:first=209U;break;
    default:assert(0);
  }
  length=Test_WeatherFrame(frame,first,second,s_weather_requests[port][5]); Test_WeatherInject(port,frame,length);
}
static void Test_WeatherReset(uint32_t now_ms,uint8_t automatic)
{
  unsigned port;
  Test_Reset(now_ms);
  for(port=0U;port<2U;++port) {
    s_weather_irq[port]=1U;s_weather_armed[port]=0U;
    s_weather_sends[port]=s_weather_aborts[port]=s_weather_starts[port]=0U;
    s_weather_tx_result[port]=s_weather_rx_result[port]=s_weather_abort_result[port]=HAL_OK;
    s_weather_auto_reply[port]=automatic;s_weather_echo[port]=0U;
  }
  s_weather_tx_duration_ms=0U;Test_ClearConsole();App_Init();
  assert(AppWeather_GetDiagnostics(0)->init_status==BSP_WEATHER_RS485_OK);
  assert(APP_WEATHER_GAS_ENABLED || s_weather_starts[1]==0U);
  assert(!s_console_length && !App_GetDiagnostics()->printf_errors); /* Startup is silent. */
  Test_ClearConsole();
}
static uint8_t Test_WeatherParse(SensorWeatherParser *parser,const uint8_t *frame,size_t length,uint32_t start)
{
  size_t index;uint8_t complete=0U;
  for(index=0U;index<length;++index) {complete=SensorWeather_ParseByte(parser,frame[index],start+(uint32_t)index*2U,8U);if(index+1U<length){assert(!complete);}}
  return complete;
}
static void Test_WeatherProtocol(void)
{
  static const uint8_t golden[6][8]={
    {1,3,0,0,0,2,0xC4,0x0B},{1,3,0,2,0,2,0x65,0xCB},
    {1,3,0,20,0,2,0x84,0x0F},{1,3,0,11,0,1,0xF5,0xC8},
    {1,3,0,7,0,1,0x35,0xCB},{1,3,0,8,0,1,0x05,0xC8}};
  SensorWeatherParser parser;uint8_t frame[9],request[8];size_t length;unsigned item,index;
  const SensorWeatherMeasurement *cache;
  for(item=0U;item<6U;++item) {
    assert(SensorWeather_BuildRequest((SensorWeatherItem)item,request)); assert(!memcmp(request,golden[item],8U));
    assert(SensorWeather_Port((SensorWeatherItem)item)==(item<4U?BSP_WEATHER_RS485_MET:BSP_WEATHER_RS485_GAS));
  }
  assert(!SensorWeather_BuildRequest(SENSOR_WEATHER_ITEM_COUNT,request));
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_TEMP_HUM);length=Test_WeatherFrame(frame,456U,0xFFC9U,2U);
  for(index=0U;index<4U;++index) {assert(!SensorWeather_ParseByte(&parser,frame[index],index*2U,8U));}
  for(;index<length;++index) {assert(SensorWeather_ParseByte(&parser,frame[index],index*2U,8U)==(index+1U==length));}
  assert(parser.reply.status==SENSOR_WEATHER_OK && parser.reply.value.humidity_rh_pct_x10==456U && parser.reply.value.temperature_c_x10==-55);
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_TEMP_HUM);length=Test_WeatherFrame(frame,0U,0x8000U,2U);
  assert(Test_WeatherParse(&parser,frame,length,UINT32_MAX-5U));assert(parser.reply.value.temperature_c_x10==INT16_MIN);
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_LIGHT);length=Test_WeatherFrame(frame,0xFFFFU,0xFFFFU,2U);
  assert(Test_WeatherParse(&parser,frame,length,0U));assert(parser.reply.value.light_lux==UINT32_MAX);
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_PM);length=Test_WeatherFrame(frame,12U,34U,2U);
  assert(Test_WeatherParse(&parser,frame,length,0U));assert(parser.reply.value.pm25_ug_m3==12U && parser.reply.value.pm10_ug_m3==34U);
  for(item=3U;item<6U;++item) {
    SensorWeather_ParserInit(&parser,(SensorWeatherItem)item);length=Test_WeatherFrame(frame,10132U,0U,1U);
    assert(Test_WeatherParse(&parser,frame,length,0U));
    assert(item!=3U || parser.reply.value.pressure_kpa_x100==10132U);
    assert(item!=4U || parser.reply.value.co2_ppm==10132U);
    assert(item!=5U || parser.reply.value.oxygen_pct_x10==10132U);
  }
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_TEMP_HUM);length=Test_WeatherFrame(frame,456U,0xFFC9U,2U);
  frame[length-1U]^=1U;assert(!Test_WeatherParse(&parser,frame,length,0U));assert(SensorWeather_ParserError(&parser)==SENSOR_WEATHER_CRC_ERROR);
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_CO2);length=Test_WeatherFrame(frame,0U,0U,1U);
  frame[0]=2U;assert(!Test_WeatherParse(&parser,frame,length,0U));assert(SensorWeather_ParserError(&parser)==SENSOR_WEATHER_FRAME_ERROR);
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_TEMP_HUM);length=Test_WeatherFrame(frame,0U,0U,1U);
  assert(!Test_WeatherParse(&parser,frame,length,0U));assert(SensorWeather_ParserError(&parser)==SENSOR_WEATHER_FRAME_ERROR);
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_CO2);
  for(index=0U;index<8U;++index) {assert(!SensorWeather_ParseByte(&parser,golden[4][index],index*2U,8U));}
  assert(parser.echo_count==1U && SensorWeather_ParserError(&parser)==SENSOR_WEATHER_TIMEOUT);
  length=Test_WeatherFrame(frame,0U,0U,1U);assert(Test_WeatherParse(&parser,frame,length,16U));assert(parser.reply.value.co2_ppm==0U);
  SensorWeather_Init();assert(SensorWeather_GetFreshness(SENSOR_WEATHER_CO2,0U,30000U)==SENSOR_WEATHER_FRESHNESS_NO_DATA);
  SensorWeather_RecordQuery(SENSOR_WEATHER_CO2,1U,90U);SensorWeather_Complete(SENSOR_WEATHER_CO2,&parser.reply,100U,100U);
  cache=SensorWeather_GetMeasurement(SENSOR_WEATHER_CO2);assert(cache->has_value && cache->value.co2_ppm==0U && cache->valid_sequence==1U);
  parser.reply.status=SENSOR_WEATHER_TIMEOUT;SensorWeather_RecordQuery(SENSOR_WEATHER_CO2,2U,1000U);SensorWeather_Complete(SENSOR_WEATHER_CO2,&parser.reply,2000U,2000U);
  assert(cache->valid_t_ms==100U && cache->valid_sequence==1U && cache->last_query_t_ms==1000U && cache->last_status==SENSOR_WEATHER_TIMEOUT);
  assert(SensorWeather_GetFreshness(SENSOR_WEATHER_CO2,90100U,30000U)==SENSOR_WEATHER_FRESH);
  assert(SensorWeather_GetFreshness(SENSOR_WEATHER_CO2,90101U,30000U)==SENSOR_WEATHER_STALE);
  parser.reply.status=SENSOR_WEATHER_OK;SensorWeather_RecordQuery(SENSOR_WEATHER_CO2,3U,UINT32_MAX-1000U);SensorWeather_Complete(SENSOR_WEATHER_CO2,&parser.reply,UINT32_MAX-1000U,UINT32_MAX-1000U);
  assert(SensorWeather_GetFreshness(SENSOR_WEATHER_CO2,2000U,30000U)==SENSOR_WEATHER_FRESH);
}
static void Test_WeatherTransactions(void)
{
  uint8_t frame[9];size_t length,split;unsigned before,printed=0U;
  const SensorWeatherMeasurement *cache;
  Test_WeatherReset(0U,1U);s_weather_echo[0]=1U;Test_At(0U);
  assert(s_weather_sends[0]==1U && !s_weather_sends[1] && AppWeather_GetDiagnostics(0)->waiting);
  Test_At(1U);cache=SensorWeather_GetMeasurement(SENSOR_WEATHER_TEMP_HUM);
  assert(cache->has_value && cache->value.temperature_c_x10==-55 && cache->value.humidity_rh_pct_x10==456U);
  assert(!s_console_length && cache->completed_value.temperature_c_x10==-55 && cache->completed_value.humidity_rh_pct_x10==456U);
  assert(AppWeather_GetDiagnostics(0)->echo_count==1U);
  Test_At(250U);assert(s_weather_sends[0]==1U);Test_At(251U);assert(s_weather_sends[0]==2U && s_weather_requests[0][3]==2U);
  Test_At(252U);assert(!s_console_length && SensorWeather_GetMeasurement(SENSOR_WEATHER_LIGHT)->completed_value.light_lux==305419896U);
  for(before=253U;before<=1100U;++before) {
    Test_At(before);
    if(SensorWeather_GetMeasurement(SENSOR_WEATHER_PM)->has_completed && SensorWeather_GetMeasurement(SENSOR_WEATHER_PM)->completed_value.pm25_ug_m3==12U && SensorWeather_GetMeasurement(SENSOR_WEATHER_PM)->completed_value.pm10_ug_m3==34U) {printed|=1U;}
    if(SensorWeather_GetMeasurement(SENSOR_WEATHER_PRESSURE)->has_completed && SensorWeather_GetMeasurement(SENSOR_WEATHER_PRESSURE)->completed_value.pressure_kpa_x100==10132U) {printed|=2U;}
    if((APP_WEATHER_GAS_ENABLED && SensorWeather_GetMeasurement(SENSOR_WEATHER_CO2)->has_value)) {printed|=4U;}
    if((APP_WEATHER_GAS_ENABLED && SensorWeather_GetMeasurement(SENSOR_WEATHER_OXYGEN)->has_value)) {printed|=8U;}
  }
  assert(printed==(APP_WEATHER_GAS_ENABLED?15U:3U));
  assert(s_weather_sends[0]==4U && s_weather_sends[1]==(APP_WEATHER_GAS_ENABLED?2U:0U));
  assert(SensorWeather_GetMeasurement(SENSOR_WEATHER_PM)->value.pm25_ug_m3==12U);
  assert(SensorWeather_GetMeasurement(SENSOR_WEATHER_PRESSURE)->value.pressure_kpa_x100==10132U);
  assert(APP_WEATHER_GAS_ENABLED ? SensorWeather_GetMeasurement(SENSOR_WEATHER_CO2)->value.co2_ppm==600U : !SensorWeather_GetMeasurement(SENSOR_WEATHER_CO2)->has_completed);
  assert(APP_WEATHER_GAS_ENABLED ? SensorWeather_GetMeasurement(SENSOR_WEATHER_OXYGEN)->value.oxygen_pct_x10==209U : !SensorWeather_GetMeasurement(SENSOR_WEATHER_OXYGEN)->has_completed);
  /* Fragmented response, every possible split, immediate completion on last byte. */
  length=Test_WeatherFrame(frame,456U,0xFFC9U,2U);
  for(split=1U;split<length;++split) {
    Test_WeatherReset(0U,0U);Test_At(0U);s_tick_ms=10U;Test_WeatherInject(0U,frame,split);Test_At(10U);
    assert(AppWeather_GetDiagnostics(0)->waiting);
    s_tick_ms=11U;Test_WeatherInject(0U,frame+split,length-split);Test_At(11U);
    assert(!AppWeather_GetDiagnostics(0)->waiting && AppWeather_GetDiagnostics(0)->last_status==SENSOR_WEATHER_OK);
  }
  Test_WeatherReset(0U,0U);Test_At(0U);frame[length-1U]^=1U;s_tick_ms=10U;Test_WeatherInject(0U,frame,length);Test_At(10U);Test_At(1000U);
  assert(AppWeather_GetDiagnostics(0)->last_status==SENSOR_WEATHER_CRC_ERROR && !s_console_length && SensorWeather_GetMeasurement(SENSOR_WEATHER_TEMP_HUM)->completed_status==SENSOR_WEATHER_CRC_ERROR);
  Test_WeatherReset(0U,0U);Test_At(0U);length=Test_WeatherFrame(frame,456U,0xFFC9U,2U);frame[1]=4U;
  s_tick_ms=10U;Test_WeatherInject(0U,frame,length);Test_At(10U);Test_At(1000U);
  assert(AppWeather_GetDiagnostics(0)->last_status==SENSOR_WEATHER_FRAME_ERROR && !s_console_length && SensorWeather_GetMeasurement(SENSOR_WEATHER_TEMP_HUM)->completed_status==SENSOR_WEATHER_FRAME_ERROR);
  /* A bad candidate may precede a valid frame in the same bounded stream. */
  Test_WeatherReset(0U,0U);Test_At(0U);length=Test_WeatherFrame(frame,456U,0xFFC9U,2U);frame[length-1U]^=1U;
  s_tick_ms=5U;Test_WeatherInject(0U,frame,length);frame[length-1U]^=1U;Test_WeatherInject(0U,frame,length);Test_At(5U);
  assert(AppWeather_GetDiagnostics(0)->last_status==SENSOR_WEATHER_OK);
  Test_WeatherReset(0U,0U);Test_At(0U);Test_At(999U);assert(AppWeather_GetDiagnostics(0)->waiting);Test_At(1000U);
  assert((SensorWeather_GetMeasurement(0)->completed_status==SENSOR_WEATHER_TIMEOUT && SensorWeather_GetMeasurement(0)->sample_t_ms==1000U) && SensorWeather_GetMeasurement(0)->last_query_t_ms==0U);
  before=s_weather_sends[0];s_tick_ms=1001U;Test_WeatherInject(0U,frame,length);Test_At(1001U);
  assert(s_weather_sends[0]==before && !SensorWeather_GetMeasurement(0)->has_value && AppWeather_GetDiagnostics(0)->ignored_idle_bytes==length);
  Test_WeatherReset(UINT32_MAX-500U,0U);Test_At(UINT32_MAX-500U);s_tick_ms=(uint32_t)(UINT32_MAX-500U+999U);Test_WeatherInject(0U,frame,length);
  Test_At((uint32_t)(UINT32_MAX-500U+1001U));assert(SensorWeather_GetMeasurement(0)->has_value && SensorWeather_GetMeasurement(0)->valid_t_ms==498U);
  Test_WeatherReset(UINT32_MAX-500U,0U);Test_At(UINT32_MAX-500U);Test_At(498U);assert(AppWeather_GetDiagnostics(0)->waiting);Test_At(499U);assert(AppWeather_GetDiagnostics(0)->last_status==SENSOR_WEATHER_TIMEOUT);
  Test_WeatherReset(0U,0U);s_weather_tx_duration_ms=17U;Test_At(0U);Test_At(1016U);assert(AppWeather_GetDiagnostics(0)->waiting);Test_At(1017U);assert(AppWeather_GetDiagnostics(0)->last_status==SENSOR_WEATHER_TIMEOUT);
}
static void Test_WeatherScheduleAndBackoff(void)
{
  uint8_t exception[5]={1U,0x83U,2U,0U,0U};uint16_t crc;
  unsigned met,gas,index;uint32_t last[6];
  Test_WeatherReset(0U,1U);s_echo_during_tx=1U;Test_At(0U);Test_Through(6000U);
  assert(App_GetDiagnostics()->sequence==61U && App_GetDiagnostics()->completed_windows==0U && s_uart6_sends==0U);
  assert(App_GetDiagnostics()->last_round.wind.value.valid && App_GetDiagnostics()->last_round.ad3.valid);
  for(index=0U;index<(APP_WEATHER_GAS_ENABLED?6U:4U);++index) {last[index]=SensorWeather_GetMeasurement((SensorWeatherItem)index)->last_query_t_ms;}
  Test_Through(9999U);assert(SensorWeather_GetMeasurement(SENSOR_WEATHER_TEMP_HUM)->last_query_t_ms==0U);
  Test_At(10000U);assert(SensorWeather_GetMeasurement(SENSOR_WEATHER_TEMP_HUM)->last_query_t_ms==10000U);
  for(index=0U;index<(APP_WEATHER_GAS_ENABLED?6U:4U);++index) {if(index){assert(SensorWeather_GetMeasurement((SensorWeatherItem)index)->last_query_t_ms==last[index]);}}
  Test_Through(62000U);
  for(index=0U;index<(APP_WEATHER_GAS_ENABLED?6U:4U);++index) {
    const SensorWeatherMeasurement *cache=SensorWeather_GetMeasurement((SensorWeatherItem)index);
    assert((uint32_t)(cache->last_query_t_ms-last[index])>=AppWeather_PeriodMs((SensorWeatherItem)index));
  }
  /* Long scheduling delay takes one new task, never a burst of missed periods. */
  met=s_weather_sends[0];gas=s_weather_sends[1];Test_At(1000000U);Test_At(1000000U);assert(s_weather_sends[0]<=met+1U && s_weather_sends[1]<=gas+1U);
  Test_WeatherReset(0U,0U);s_weather_auto_reply[1]=1U;s_weather_tx_result[0]=HAL_TIMEOUT;
  Test_At(0U);Test_At(250U);Test_At(500U);
  assert(s_weather_sends[0]==3U && AppWeather_GetDiagnostics(0)->backoff && AppWeather_GetDiagnostics(0)->no_response_streak==3U);
  assert(SensorWeather_GetMeasurement(SENSOR_WEATHER_PM)->last_query_t_ms==500U);
  Test_At(501U);Test_At(502U);assert(APP_WEATHER_GAS_ENABLED?s_weather_sends[1]>0U:s_weather_sends[1]==0U);
  Test_At(60499U);assert(s_weather_sends[0]==3U);
  s_weather_tx_result[0]=HAL_OK;Test_At(60500U);assert(s_weather_sends[0]==4U && s_weather_requests[0][3]==0U && AppWeather_GetDiagnostics(0)->probe_count==1U);
  crc=SensorWeather_Crc16(exception,3U);exception[3]=(uint8_t)crc;exception[4]=(uint8_t)(crc>>8);
  s_tick_ms=60501U;Test_WeatherInject(0U,exception,5U);Test_At(60501U);
  assert(!AppWeather_GetDiagnostics(0)->backoff && !AppWeather_GetDiagnostics(0)->no_response_streak);
  assert(SensorWeather_GetMeasurement(0)->last_status==SENSOR_WEATHER_MODBUS_EXCEPTION && !SensorWeather_GetMeasurement(0)->has_value);
  assert(SensorWeather_GetMeasurement(0)->exception_code==2U && !s_console_length && SensorWeather_GetMeasurement(0)->completed_status==SENSOR_WEATHER_MODBUS_EXCEPTION);
  Test_At(60750U);assert(s_weather_sends[0]==4U);Test_At(60751U);assert(s_weather_sends[0]==5U && s_weather_requests[0][3]==11U);
#if APP_WEATHER_GAS_ENABLED
  /* UART4 has its own three-failure limit and uses CO2 as probe. */
  Test_WeatherReset(0U,1U);Test_At(0U);Test_Through(3000U);s_weather_auto_reply[1]=0U;s_weather_tx_result[1]=HAL_ERROR;
  Test_Through(121000U);assert(AppWeather_GetDiagnostics(1)->backoff);
  gas=s_weather_sends[1];Test_At(181000U);assert(s_weather_sends[1]<=gas+1U && s_weather_requests[1][3]==7U);
#endif
}
static void Test_WeatherHardwareErrors(void)
{
  uint8_t noise[80]={0};unsigned gas_aborts,dt_aborts;
  const SensorWeatherMeasurement *cache;
  Test_WeatherReset(0U,1U);Test_At(0U);Test_Through(3000U);cache=SensorWeather_GetMeasurement(0);
  assert(cache->has_value);s_weather_auto_reply[0]=0U;Test_At(10000U);gas_aborts=s_weather_aborts[1];dt_aborts=s_aborts;
  huart3.ErrorCode=HAL_UART_ERROR_ORE;s_in_callback=1U;HAL_UART_ErrorCallback(&huart3);s_in_callback=0U;Test_At(10001U);
  assert(AppWeather_GetDiagnostics(0)->last_status==SENSOR_WEATHER_UART_ERROR && cache->valid_t_ms==0U);
  assert(cache->value.temperature_c_x10==-55 && cache->value.humidity_rh_pct_x10==456U && cache->valid_sequence==1U && cache->last_query_t_ms==10000U);
  assert(s_weather_aborts[1]==gas_aborts && s_aborts==dt_aborts && BspDtu_GetDiagnostics().rx_active && s_weather_armed[0]);
  Test_WeatherReset(0U,0U);Test_At(0U);Test_WeatherInject(0U,noise,sizeof(noise));Test_At(1U);
  assert(AppWeather_GetDiagnostics(0)->last_status==SENSOR_WEATHER_RX_OVERFLOW && BspWeatherRs485_GetDiagnostics(0).rx_overflows==17U);
  Test_WeatherReset(0U,0U);Test_At(0U);s_weather_rx_result[0]=HAL_ERROR;Test_WeatherInject(0U,noise,1U);s_weather_rx_result[0]=HAL_OK;Test_At(1U);
  assert(AppWeather_GetDiagnostics(0)->last_status==SENSOR_WEATHER_UART_ERROR && s_weather_armed[0] && BspWeatherRs485_GetDiagnostics(0).rx_start_errors==1U);
  Test_WeatherReset(0U,0U);Test_At(0U);s_weather_abort_result[0]=HAL_ERROR;huart3.ErrorCode=HAL_UART_ERROR_FE;HAL_UART_ErrorCallback(&huart3);Test_At(1U);
  assert(!s_weather_armed[0] || !BspWeatherRs485_GetDiagnostics(0).rx_active);
  s_weather_abort_result[0]=HAL_OK;Test_At(2U);assert(s_weather_armed[0] && BspWeatherRs485_GetDiagnostics(0).rx_active);
  Test_WeatherReset(0U,0U);s_weather_tx_result[0]=HAL_BUSY;Test_At(0U);
  assert(SensorWeather_GetMeasurement(0)->last_status==SENSOR_WEATHER_UART_BUSY && SensorWeather_GetMeasurement(0)->query_sequence==1U);
  Test_WeatherReset(0U,0U);s_weather_tx_result[0]=HAL_ERROR;Test_At(0U);assert(SensorWeather_GetMeasurement(0)->last_status==SENSOR_WEATHER_UART_ERROR);
  fprintf(stderr,"PASS: weather CRC/units/splits/echo/CRC+frame+exception/timeouts/wrap, MET four periods/optional gas/gap/backoff/completed-cache/STALE, per-UART recovery and ADC/DTU coexistence\n");
}
static void Test_WeatherQualityAndCompletion(void)
{
  SensorWeatherParser parser;
  uint8_t frame[9];size_t length;
  const SensorWeatherMeasurement *m;
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_TEMP_HUM);
  length=Test_WeatherFrame(frame,708U,194U,2U);assert(Test_WeatherParse(&parser,frame,length,0U));
  assert(parser.reply.value.humidity_rh_pct_x10==708U && parser.reply.value.temperature_c_x10==194 && parser.reply.valid_fields==3U);
  SensorWeather_Init();SensorWeather_RecordQuery(0,4U,100U);SensorWeather_Complete(0,&parser.reply,110U,111U);
  m=SensorWeather_GetMeasurement(0);assert(m->completed_sequence==4U && m->sample_t_ms==110U);
  SensorWeather_RecordQuery(0,5U,200U);
  assert(m->in_progress && m->completed_sequence==4U && m->completed_status==SENSOR_WEATHER_OK);
  parser.reply.status=SENSOR_WEATHER_TIMEOUT;SensorWeather_Complete(0,&parser.reply,0U,1200U);
  assert(m->completed_sequence==5U && m->sample_t_ms==1200U && !m->valid_fields && m->valid_sequence==4U);
  SensorWeather_Complete(0,&parser.reply,0U,1300U);assert(m->completed_t_ms==1200U);
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_TEMP_HUM);
  length=Test_WeatherFrame(frame,UINT16_MAX,194U,2U);assert(Test_WeatherParse(&parser,frame,length,0U));
  assert(parser.reply.status==SENSOR_WEATHER_OK && parser.reply.valid_fields==SENSOR_WEATHER_FIELD_TEMPERATURE);
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_TEMP_HUM);
  length=Test_WeatherFrame(frame,708U,0xFFFFU,2U);assert(Test_WeatherParse(&parser,frame,length,0U));
  assert(parser.reply.valid_fields==3U && parser.reply.value.temperature_c_x10==-1);
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_PM);
  length=Test_WeatherFrame(frame,UINT16_MAX,23U,2U);assert(Test_WeatherParse(&parser,frame,length,0U));
  assert(parser.reply.valid_fields==SENSOR_WEATHER_FIELD_PM10 && parser.reply.value.pm10_ug_m3==23U);
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_LIGHT);
  length=Test_WeatherFrame(frame,UINT16_MAX,UINT16_MAX,2U);assert(Test_WeatherParse(&parser,frame,length,0U));
  assert(!parser.reply.valid_fields && parser.reply.status==SENSOR_WEATHER_OK);
  SensorWeather_ParserInit(&parser,SENSOR_WEATHER_PRESSURE);
  length=Test_WeatherFrame(frame,10373U,0U,1U);assert(Test_WeatherParse(&parser,frame,length,0U));
  assert(parser.reply.value.pressure_kpa_x100==10373U && parser.reply.valid_fields==SENSOR_WEATHER_FIELD_PRESSURE);
  Test_WeatherReset(0U,0U);Test_At(0U);
  length=Test_WeatherFrame(frame,UINT16_MAX,194U,2U);s_tick_ms=11U;Test_WeatherInject(0U,frame,length);Test_At(11U);
  assert(!AppWeather_GetDiagnostics(0)->no_response_streak);
  assert(!s_console_length && SensorWeather_GetMeasurement(0)->completed_value.temperature_c_x10==194 && SensorWeather_GetMeasurement(0)->valid_fields==SENSOR_WEATHER_FIELD_TEMPERATURE);
  assert(SensorWeather_GetMeasurement(0)->field_sequence[0]==1U &&
    SensorWeather_GetMeasurement(0)->field_t_ms[0]==11U);
}
#endif
