#include "app_main.h"
#include "app_diagnostics.h"
#include "app_upload.h"
#include "app_weather.h"
#include "app_config.h"
#include "app_version.h"
#include "bsp_dtu.h"
#include "core_json.h"
#include <stdlib.h>
#include "bsp_config.h"
#include "adc.h"
#include "usart.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

ADC_HandleTypeDef hadc1;
UART_HandleTypeDef huart2, huart6;
UART_HandleTypeDef huart3, huart4;
static uint8_t s_weather_irq[2], s_weather_armed[2], *s_weather_destination[2];
static uint8_t s_weather_requests[2][8];
static unsigned s_weather_sends[2], s_weather_aborts[2], s_weather_starts[2];
static HAL_StatusTypeDef s_weather_tx_result[2], s_weather_rx_result[2], s_weather_abort_result[2];
static uint8_t s_weather_auto_reply[2], s_weather_echo[2];
static uint32_t s_weather_tx_duration_ms;
static void Test_WeatherInject(unsigned port,const uint8_t *data,size_t length);
static void Test_WeatherAutoReply(unsigned port);
uint32_t NVIC_GetEnableIRQ(int irq) { return s_weather_irq[irq==USART3_IRQn?0:1]; }
GPIO_TypeDef fake_gpio;
unsigned test_tc = 1U;
static uint32_t s_tick_ms, s_irq_mask;
static uint32_t s_acks_before_process, s_timeouts_before_process;
static uint8_t *s_rx_destination;
static uint8_t s_rx_armed, s_in_callback, s_echo_during_tx;
static unsigned s_uart6_sends, s_rx_starts, s_aborts, s_soil_reads, s_gpio_writes;
static HAL_StatusTypeDef s_tx_result, s_rx_result, s_abort_result;
static int s_format_fault;
static HAL_StatusTypeDef s_console_tx_result=HAL_OK;
static uint8_t s_tx_auto_complete=1U;
static uint8_t s_start_error_injection, s_tc_during_tick;
static HAL_StatusTypeDef s_abort_tx_result=HAL_OK;
static unsigned s_tx_aborts;
static const uint8_t *s_active_tx;
static uint16_t s_active_tx_length;
static char s_console[4096], s_transmitted[APP_UPLOAD_FRAME_CAPACITY];
static size_t s_console_length, s_transmitted_length;
static char s_ack[160];
static size_t s_ack_length;
static uint32_t s_raw = 15791U;
static HAL_StatusTypeDef s_poll_result=HAL_OK, s_stop_result=HAL_OK;
static HAL_StatusTypeDef s_cal_result=HAL_OK, s_start_result=HAL_OK, s_config_result=HAL_OK;
static HAL_StatusTypeDef s_wind_poll_result=HAL_OK, s_wind_stop_result=HAL_OK;
static HAL_StatusTypeDef s_wind_start_result=HAL_OK, s_wind_config_result=HAL_OK, s_prepare_result=HAL_OK;
static uint32_t s_selected_channel, s_wind_raw=12345U, s_soil_duration_ms, s_wind_duration_ms;
static unsigned s_cal_calls, s_config_calls, s_wind_reads;
static HAL_StatusTypeDef s_ad3_poll_result, s_ad3_stop_result, s_ad3_start_result, s_ad3_config_result;
static uint32_t s_ad3_raw, s_ad3_duration_ms;
static unsigned s_ad3_reads;
static HAL_StatusTypeDef s_ad4_poll_result, s_ad4_stop_result, s_ad4_start_result, s_ad4_config_result;
static uint32_t s_ad4_raw, s_ad4_duration_ms;
static unsigned s_ad4_reads;
static HAL_StatusTypeDef s_ad5_poll_result,s_ad5_stop_result,s_ad5_start_result,s_ad5_config_result;
static uint32_t s_ad5_raw,s_ad5_duration_ms;
static unsigned s_ad5_reads,s_adc_stop_calls;
static uint32_t s_channel_history[512];
static uint8_t s_prepare_phase=1U, s_adc_active, s_inject_ack_in_wind;
static void Test_Inject(const void *data, size_t length);

static void Test_TxComplete(void)
{ huart6.gState=HAL_UART_STATE_READY; huart6.TxXferCount=0U; HAL_UART_TxCpltCallback(&huart6); }
uint32_t __get_PRIMASK(void) { return s_irq_mask; }
void __disable_irq(void) { s_irq_mask = 1U; }
void __set_PRIMASK(uint32_t mask) { s_irq_mask = mask; }
uint32_t HAL_GetTick(void) {
  uint32_t old=s_tick_ms;
  if(s_tc_during_tick) { s_tc_during_tick=0U; ++s_tick_ms; Test_TxComplete(); }
  return old;
}
void HAL_Delay(uint32_t delay_ms) { assert(!s_in_callback); s_tick_ms += delay_ms; }
void TestEnableGpioClock(void) {}
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *gpio) { (void)port; (void)gpio; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level)
{ (void)port; (void)pin; (void)level; ++s_gpio_writes; }
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc, uint32_t calibration, uint32_t single_ended)
{
  assert(adc==&hadc1 && calibration==ADC_CALIB_OFFSET_LINEARITY && single_ended==ADC_SINGLE_ENDED);
  ++s_cal_calls; s_prepare_phase=1U; return s_cal_result;
}
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *adc, ADC_ChannelConfTypeDef *channel)
{
  assert(adc==&hadc1 && !s_prepare_phase && !s_adc_active);
  assert(channel->Channel==ADC_CHANNEL_5 || channel->Channel==ADC_CHANNEL_3 || channel->Channel==ADC_CHANNEL_19 || channel->Channel==ADC_CHANNEL_18 || channel->Channel==ADC_CHANNEL_15);
  assert(channel->Rank==ADC_REGULAR_RANK_1 && channel->SamplingTime==ADC_SAMPLETIME_387CYCLES_5);
  assert(channel->SingleDiff==ADC_SINGLE_ENDED && channel->OffsetNumber==ADC_OFFSET_NONE);
  assert(!channel->Offset && !channel->OffsetSignedSaturation);
  if(s_config_calls<sizeof(s_channel_history)/sizeof(s_channel_history[0])) { s_channel_history[s_config_calls]=channel->Channel; }
  ++s_config_calls; s_selected_channel=channel->Channel;
  if(s_selected_channel==ADC_CHANNEL_19) { return s_ad3_config_result; }
  if(s_selected_channel==ADC_CHANNEL_18) { return s_ad4_config_result; }
  if(s_selected_channel==ADC_CHANNEL_15) { return s_ad5_config_result; }
  return s_selected_channel==ADC_CHANNEL_5?s_config_result:s_wind_config_result;
}
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc)
{
  HAL_StatusTypeDef result;
  assert(adc==&hadc1 && !s_prepare_phase && !s_adc_active);
  result=s_selected_channel==ADC_CHANNEL_5?s_start_result:s_wind_start_result;
  if(s_selected_channel==ADC_CHANNEL_19) { result=s_ad3_start_result; }
  if(s_selected_channel==ADC_CHANNEL_18) { result=s_ad4_start_result; }
  if(s_selected_channel==ADC_CHANNEL_15) { result=s_ad5_start_result; }
  s_adc_active=result==HAL_OK; return result;
}
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout_ms)
{
  assert(adc==&hadc1 && s_adc_active && timeout_ms==BSP_ADC_POLL_TIMEOUT_MS);
  s_tick_ms+=s_selected_channel==ADC_CHANNEL_5?s_soil_duration_ms:
    s_selected_channel==ADC_CHANNEL_3?s_wind_duration_ms:
    s_selected_channel==ADC_CHANNEL_19?s_ad3_duration_ms:
    s_selected_channel==ADC_CHANNEL_18?s_ad4_duration_ms:s_ad5_duration_ms;
  if(s_selected_channel==ADC_CHANNEL_3 && s_inject_ack_in_wind)
  { s_inject_ack_in_wind=0U; Test_Inject(s_ack,s_ack_length); }
  if(s_selected_channel==ADC_CHANNEL_19) { return s_ad3_poll_result; }
  if(s_selected_channel==ADC_CHANNEL_18) { return s_ad4_poll_result; }
  if(s_selected_channel==ADC_CHANNEL_15) { return s_ad5_poll_result; }
  return s_selected_channel==ADC_CHANNEL_5?s_poll_result:s_wind_poll_result;
}
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc)
{
  assert(adc==&hadc1 && s_adc_active);
  if(s_selected_channel==ADC_CHANNEL_5) { ++s_soil_reads; return s_raw; }
  if(s_selected_channel==ADC_CHANNEL_19) { ++s_ad3_reads; return s_ad3_raw; }
  if(s_selected_channel==ADC_CHANNEL_18) { ++s_ad4_reads; return s_ad4_raw; }
  if(s_selected_channel==ADC_CHANNEL_15) { ++s_ad5_reads; return s_ad5_raw; }
  ++s_wind_reads; return s_wind_raw;
}
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc)
{
  HAL_StatusTypeDef result;
  assert(adc==&hadc1); ++s_adc_stop_calls;
  if(s_prepare_phase)
  {
    if(s_prepare_result==HAL_OK) { s_prepare_phase=0U; s_adc_active=0U; }
    return s_prepare_result;
  }
  s_prepare_phase=1U;
  result=s_selected_channel==ADC_CHANNEL_5?s_stop_result:s_wind_stop_result;
  if(s_selected_channel==ADC_CHANNEL_19) { result=s_ad3_stop_result; }
  if(s_selected_channel==ADC_CHANNEL_18) { result=s_ad4_stop_result; }
  if(s_selected_channel==ADC_CHANNEL_15) { result=s_ad5_stop_result; }
  if(result==HAL_OK) { s_adc_active=0U; }
  return result;
}
uint32_t HAL_UART_GetState(UART_HandleTypeDef *uart) { assert(uart == &huart2); return HAL_UART_STATE_READY; }
uint32_t HAL_UART_GetError(UART_HandleTypeDef *uart) { return uart->ErrorCode; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *destination, uint16_t length)
{
  if(uart==&huart3 || uart==&huart4) {
    unsigned port=uart==&huart3?0U:1U; assert(destination && length==1U); ++s_weather_starts[port];
    if(s_weather_rx_result[port]!=HAL_OK) { return s_weather_rx_result[port]; }
    if(s_weather_armed[port]) { return HAL_BUSY; }
    s_weather_destination[port]=destination; s_weather_armed[port]=1U; return HAL_OK;
  }
  assert(uart == &huart6 && destination && length == 1U);
  ++s_rx_starts;
  if (s_rx_result != HAL_OK) { return s_rx_result; }
  if (s_rx_armed) { return HAL_BUSY; }
  s_rx_destination = destination;
  s_rx_armed = 1U;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart)
{
  if(uart==&huart3 || uart==&huart4) {
    unsigned port=uart==&huart3?0U:1U; ++s_weather_aborts[port];
    if(s_weather_abort_result[port]!=HAL_OK) { return s_weather_abort_result[port]; }
    s_weather_armed[port]=0U; uart->ErrorCode=0U; return HAL_OK;
  }
  assert(uart == &huart6);
  ++s_aborts;
  if (s_abort_result != HAL_OK) { return s_abort_result; }
  s_rx_armed = 0U;
  huart6.ErrorCode = HAL_UART_ERROR_NONE;
  return HAL_OK;
}
static void Test_Inject(const void *data, size_t length)
{
  const uint8_t *bytes = data;
  size_t index;
  for (index = 0U; index < length; ++index)
  {
    assert(s_rx_armed && !s_irq_mask);
    *s_rx_destination = bytes[index];
    s_rx_armed = 0U;
    s_in_callback = 1U;
    HAL_UART_RxCpltCallback(&huart6);
    s_in_callback = 0U;
  }
}
static void Test_Error(uint32_t error)
{
  huart6.ErrorCode = error;
  s_in_callback = 1U;
  HAL_UART_ErrorCallback(&huart6);
  s_in_callback = 0U;
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, const uint8_t *bytes,
                                   uint16_t length, uint32_t timeout_ms)
{
  assert(!s_in_callback && !s_irq_mask);
  if(uart==&huart3 || uart==&huart4) {
    unsigned port=uart==&huart3?0U:1U;
    assert(s_weather_armed[port] && length==8U && timeout_ms==BSP_WEATHER_TX_TIMEOUT_MS);
    memcpy(s_weather_requests[port],bytes,8U); ++s_weather_sends[port];
    s_tick_ms+=s_weather_tx_duration_ms;
    if(s_weather_tx_result[port]==HAL_OK) {
      if(s_weather_echo[port]) { Test_WeatherInject(port,bytes,length); }
      if(s_weather_auto_reply[port]) { Test_WeatherAutoReply(port); }
    }
    return s_weather_tx_result[port];
  }
  assert(uart == &huart2 && timeout_ms == BSP_CONSOLE_TX_TIMEOUT_MS);
  assert(!"Application USART2 transmission must remain disabled");
  if(s_console_tx_result!=HAL_OK) { return s_console_tx_result; }
  assert(s_console_length + length < sizeof(s_console));
  memcpy(s_console + s_console_length, bytes, length);
  s_console_length += length; s_console[s_console_length] = '\0';
  return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,const uint8_t *bytes,uint16_t length)
{
  assert(!s_irq_mask);
  huart6.gState=0x21U; huart6.TxXferCount=length;
  s_active_tx=bytes; s_active_tx_length=length;
    assert(s_rx_armed && uart==&huart6);
    assert(length > 0U && length < sizeof(s_transmitted));
    memcpy(s_transmitted, bytes, length); s_transmitted[length] = '\0';
    s_transmitted_length = length;
    ++s_uart6_sends;
    {
      const char *value; size_t size; JSONTypes_t type;
      assert(JSON_Validate(s_transmitted, length) == JSONSuccess);
      assert(JSON_SearchConst(s_transmitted, length, "seq", 3U, &value, &size, &type) == JSONSuccess);
      s_ack_length = (size_t)snprintf(s_ack, sizeof(s_ack),
        "{\"ok\":true,\"device_id\":\"tea_h743_001\",\"seq\":%lu,\"id\":14,\"duplicate\":false}\r\n", strtoul(value, NULL, 10));
    }
    if (s_echo_during_tx && s_tx_result == HAL_OK) { Test_Inject(s_ack, s_ack_length); }
    if(s_start_error_injection) { s_start_error_injection=0U; Test_Error(1U); }
    if(s_tx_auto_complete && s_tx_result==HAL_OK) { Test_TxComplete(); }
    return s_tx_result;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart)
{ assert(uart==&huart6); ++s_tx_aborts; if(s_abort_tx_result==HAL_OK) { huart6.gState=HAL_UART_STATE_READY;huart6.TxXferCount=0U; } return s_abort_tx_result; }
/* Fault injection for the production DTU snprintf length guard. */
int snprintf(char *buffer, size_t capacity, const char *format, ...)
{
  int length;
  va_list arguments;
  if (s_format_fault) { return s_format_fault == 1 ? -1 : (int)capacity; }
  va_start(arguments, format);
  length = vsnprintf(buffer, capacity, format, arguments);
  va_end(arguments);
  return length;
}
static void Test_ClearConsole(void) { s_console_length = 0U; s_console[0] = '\0'; }
static void Test_At(uint32_t now_ms)
{
  s_acks_before_process=AppUpload_GetDiagnostics()->ack_count;
  s_timeouts_before_process=AppUpload_GetDiagnostics()->timeouts;
  Test_ClearConsole(); s_tick_ms = now_ms; App_Process(); assert(!s_irq_mask && !s_console_length);
}
static void Test_Reset(uint32_t now_ms)
{
  s_console_tx_result=HAL_OK;
  s_start_error_injection=s_tc_during_tick=0U;
  s_tx_auto_complete=1U; s_abort_tx_result=HAL_OK; s_tx_aborts=0U;
  s_weather_irq[0]=s_weather_irq[1]=0U;
  s_cal_result=s_start_result=s_config_result=s_prepare_result=HAL_OK;
  s_wind_poll_result=s_wind_stop_result=s_wind_start_result=s_wind_config_result=HAL_OK;
  s_ad3_poll_result=s_ad3_stop_result=s_ad3_start_result=s_ad3_config_result=HAL_OK;
  s_ad3_raw=23456U; s_ad3_duration_ms=0U; s_ad3_reads=0U;
  s_ad4_poll_result=s_ad4_stop_result=s_ad4_start_result=s_ad4_config_result=HAL_OK;
  s_ad4_raw=34567U; s_ad4_duration_ms=0U; s_ad4_reads=0U;
  s_ad5_poll_result=s_ad5_stop_result=s_ad5_start_result=s_ad5_config_result=HAL_OK;
  s_ad5_raw=15723U; s_ad5_duration_ms=0U; s_ad5_reads=s_adc_stop_calls=0U;
  s_prepare_phase=1U; s_adc_active=0U; s_selected_channel=0U; s_inject_ack_in_wind=0U;
  s_cal_calls=s_config_calls=s_wind_reads=0U; s_wind_raw=12345U; s_soil_duration_ms=s_wind_duration_ms=0U;
  s_tick_ms = now_ms; s_irq_mask = 0U; s_rx_armed = 0U; s_in_callback = 0U;
  s_echo_during_tx = 0U; s_uart6_sends = s_rx_starts = s_aborts = s_soil_reads = s_gpio_writes = 0U;
  s_tx_result = s_rx_result = s_abort_result = HAL_OK; s_format_fault = 0;
  s_raw = 15791U; s_poll_result = s_stop_result = HAL_OK; s_transmitted_length = 0U; s_transmitted[0] = '\0'; Test_ClearConsole(); App_Init();
  assert(s_rx_armed && BspDtu_GetDiagnostics().rx_active);
  assert(!s_console_length && !App_GetDiagnostics()->printf_errors);
  Test_ClearConsole();
}
static unsigned Test_Acks(void)
{
  return AppUpload_GetDiagnostics()->ack_count-s_acks_before_process;
}

static void Test_RingOrder(void)
{
  uint8_t sent[20], received[32];
  unsigned round, index;
  Test_Reset(0U);
  for (round = 0U; round < 40U; ++round)
  {
    BspDtuReadResult result;
    for (index = 0U; index < sizeof(sent); ++index) { sent[index] = (uint8_t)(round + index); }
    Test_Inject(sent, sizeof(sent)); result = BspDtu_Read(received, sizeof(received));
    assert(result.length == sizeof(sent) && !result.data_lost);
    assert(memcmp(sent, received, sizeof(sent)) == 0);
  }
  assert(!BspDtu_GetDiagnostics().rx_overflows);
  assert(BspDtu_Read(NULL, 1U).length == 0U);
  assert(BspDtu_Read(received, 0U).length == 0U);
}


#include "three_minute_cases.h"
#include "wind_speed_cases.h"
#include "ad3_cases.h"
#include "adc_raw_cases.h"
#include "ph_cases.h"
#include "wind_direction_cases.h"
#include "weather_cases.h"
#include "stabilization_cases.h"
#include "ad5_cases.h"
#include "rain_cases.h"

int main(void)
{
  Test_Ad5RawSelectionAndFaults(); Test_RainConversionAndWindow();
  Test_DtuControlledRecovery(); Test_AdcCalibrationRecovery();
  Test_RingOrder(); Test_WindSpeedCalibration(); Test_AdcPairAndFailures();
  Test_Ad3SelectionAndFaults(); Test_RawSamplesAndFaults();
  Test_SoilPhConversion(); Test_WindDirectionConversion();
  Test_WeatherProtocol(); Test_WeatherTransactions();
  Test_WeatherScheduleAndBackoff(); Test_WeatherHardwareErrors();
  Test_WeatherQualityAndCompletion();
  Test_OverflowAndLongLine(); Test_ReceiveRecovery(); Test_SendFailuresAndLengthGuard();
  Test_ThreeMinuteSchedule(); Test_ThreeMinuteTransport(); Test_WeatherCachedFrame();
  fprintf(stderr,"PASS: real App/HAL 180s scheduling, ADC diagnostics/calibration, ACK/async/error/wrap, weather and silent console\n");
  return 0;
}
