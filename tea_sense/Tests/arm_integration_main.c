/* CPU-only integration entry; never included by MDK production target. */
#include <stdint.h>
#include "app_main.h"
#include "app_diagnostics.h"
#include "app_upload.h"
#include "app_weather.h"
#include "app_config.h"
#include "usart.h"
volatile uint32_t g_test_tick_ms,g_test_raw,g_test_tx_error,g_test_poll_error,g_test_ad5_poll_error;
volatile uint32_t g_test_soil_channel,g_test_wind_channel,g_test_ad3_channel,g_test_ad4_channel,g_test_ad5_channel;
volatile uint32_t g_test_wind_raw,g_test_ad3_raw,g_test_ad4_raw,g_test_ad5_raw;
volatile uint8_t *g_test_dtu_rx_byte,*g_test_weather_rx[2];
uint8_t g_test_ack[512],g_test_weather_reply[2][9];
volatile uint32_t g_test_ack_length,g_test_ack_ready,g_test_tx_pending,g_test_tx_due;
volatile uint32_t g_test_weather_length[2],g_test_stage;
volatile uint32_t g_test_acks,g_test_printf_errors,g_test_soil_valid,g_test_wind_valid,g_test_wind_used;
volatile uint32_t g_test_direction,g_test_soil_ph,g_test_rounds;
volatile uint32_t g_test_rain_valid,g_test_rain_used,g_test_rain_errors;
__attribute__((noinline)) void TestArm_Observe(uint32_t stage) { g_test_stage=stage; }
__attribute__((noinline)) void TestArm_Done(void) { for(;;){} }
void Error_Handler(void) { for(;;){} }
static void TestArm_CompleteTx(void)
{
  if(g_test_tx_pending && (int32_t)(g_test_tick_ms-g_test_tx_due)>=0) {
    g_test_tx_pending=0U;huart6.gState=HAL_UART_STATE_READY;huart6.TxXferCount=0U;HAL_UART_TxCpltCallback(&huart6);
  }
}
static void TestArm_Feed(void)
{
  uint32_t port,index;
  for(port=0U;port<2U;++port) {
    for(index=0U;index<g_test_weather_length[port];++index) {
      *g_test_weather_rx[port]=g_test_weather_reply[port][index];
      HAL_UART_RxCpltCallback(port?&huart4:&huart3);
    }
    g_test_weather_length[port]=0U;
  }
  if(g_test_ack_ready) {
    g_test_ack_ready=0U;
    for(index=0U;index<g_test_ack_length;++index) {
      *g_test_dtu_rx_byte=g_test_ack[index];HAL_UART_RxCpltCallback(&huart6);
    }
  }
}
static void TestArm_Check(uint32_t stage)
{
  const AppDiagnostics *d=App_GetDiagnostics();
  g_test_acks=AppUpload_GetDiagnostics()->ack_count;g_test_printf_errors=d->printf_errors;
  g_test_soil_valid=d->last_round.soil.adc.valid;
  g_test_wind_valid=d->last_round.wind.value.valid;
  g_test_direction=d->last_round.wind_direction.angle_deg_x100;
  g_test_soil_ph=d->last_round.soil_ph.soil_ph_x100;g_test_rounds=d->sequence;
  g_test_rain_errors=d->ad5_sample_errors;
  TestArm_Observe(stage);
}
int main(void)
{
  uint32_t tick;
  huart3.Instance=USART3;huart4.Instance=UART4;
  NVIC->ISER[(uint32_t)USART3_IRQn/32U]|=1UL<<((uint32_t)USART3_IRQn%32U);
  g_test_soil_channel=ADC_CHANNEL_5;g_test_wind_channel=ADC_CHANNEL_3;
  g_test_ad3_channel=ADC_CHANNEL_19;g_test_ad4_channel=ADC_CHANNEL_18;g_test_ad5_channel=ADC_CHANNEL_15;
  g_test_wind_raw=15793U;g_test_ad3_raw=15614U;g_test_ad4_raw=31398U;
  App_Init();
  for(tick=0U;tick<=6U*APP_SAMPLE_WINDOW_MS;tick+=100U) {
    g_test_tick_ms=tick;g_test_raw=tick>=APP_SAMPLE_WINDOW_MS && tick<2U*APP_SAMPLE_WINDOW_MS?0U:19122U;
    g_test_ad5_raw=15723U;
    TestArm_CompleteTx();App_Process();TestArm_Feed();App_Process();
    if(tick && tick%APP_SAMPLE_WINDOW_MS==0U) {TestArm_Check(tick/APP_SAMPLE_WINDOW_MS);}
  }
  g_test_tick_ms=6U*APP_SAMPLE_WINDOW_MS+600U;
  TestArm_CompleteTx();App_Process();TestArm_Feed();App_Process();
  TestArm_Check(7U);TestArm_Done();return 0;
}
