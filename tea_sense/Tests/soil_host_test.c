#include "app_main.h"
#include "app_diagnostics.h"
#include "app_upload.h"
#include "app_config.h"
#include "sensor_soil.h"
#include "bsp_console.h"
#include "bsp_config.h"
#include "adc.h"
#include "usart.h"
#include "rt_sys.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

ADC_HandleTypeDef hadc1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart6;
UART_HandleTypeDef huart3, huart4;
uint32_t NVIC_GetEnableIRQ(int irq) { (void)irq; return 0U; }
GPIO_TypeDef fake_gpio;
unsigned test_tc;
static uint32_t s_tick_ms, s_uart_state, s_raw;
static unsigned s_tick_advances, s_cal_calls, s_start_calls, s_poll_calls, s_read_calls;
static unsigned s_stop_calls, s_sends, s_gpio_inits, s_gpio_writes;
static GPIO_PinState s_gpio_level;
static HAL_StatusTypeDef s_cal_result, s_start_result, s_poll_result, s_stop_result, s_tx_result;
static char s_sent[2048];
static size_t s_sent_length;
static uint32_t s_irq_mask;
static uint8_t s_dtu_armed;
static uint32_t s_selected_channel;
static uint8_t s_prepare_phase=1U;

uint32_t __get_PRIMASK(void) { return s_irq_mask; }
void __disable_irq(void) { s_irq_mask = 1U; }
void __set_PRIMASK(uint32_t mask) { s_irq_mask = mask; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t length)
{
  assert(uart == &huart6 && data != NULL && length == 1U);
  s_dtu_armed = 1U;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart)
{ assert(uart == &huart6); s_dtu_armed = 0U; return HAL_OK; }
uint32_t HAL_UART_GetError(UART_HandleTypeDef *uart) { return uart->ErrorCode; }

uint32_t HAL_GetTick(void)
{
  uint32_t now_ms = s_tick_ms;
  s_tick_ms += s_tick_advances;
  return now_ms;
}
void HAL_Delay(uint32_t delay_ms) { s_tick_ms += delay_ms; }
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc,
                                            uint32_t calibration, uint32_t single_ended)
{
  assert(adc == &hadc1 && calibration == ADC_CALIB_OFFSET_LINEARITY && single_ended == ADC_SINGLE_ENDED);
  ++s_cal_calls;
  return s_cal_result;
}
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *adc, ADC_ChannelConfTypeDef *channel)
{
  assert(adc==&hadc1 && !s_prepare_phase);
  assert(channel->Channel==ADC_CHANNEL_5 || channel->Channel==ADC_CHANNEL_3 || channel->Channel==ADC_CHANNEL_19 || channel->Channel==ADC_CHANNEL_18 || channel->Channel==ADC_CHANNEL_15);
  assert(channel->Rank==ADC_REGULAR_RANK_1 && channel->SamplingTime==ADC_SAMPLETIME_387CYCLES_5);
  assert(channel->SingleDiff==ADC_SINGLE_ENDED && channel->OffsetNumber==ADC_OFFSET_NONE);
  assert(!channel->Offset && !channel->OffsetSignedSaturation);
  s_selected_channel=channel->Channel; return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc)
{
  assert(adc==&hadc1);
  if(s_selected_channel==ADC_CHANNEL_5) { ++s_start_calls; return s_start_result; }
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout_ms)
{
  assert(adc==&hadc1 && timeout_ms==BSP_ADC_POLL_TIMEOUT_MS);
  if(s_selected_channel==ADC_CHANNEL_5) { ++s_poll_calls; return s_poll_result; }
  return HAL_OK;
}
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc)
{
  assert(adc==&hadc1);
  if(s_selected_channel==ADC_CHANNEL_5) { ++s_read_calls; return s_raw; }
  if(s_selected_channel==ADC_CHANNEL_19) { return 23456U; }
  if(s_selected_channel==ADC_CHANNEL_18) { return 34567U; }
  if(s_selected_channel==ADC_CHANNEL_15) { return 15700U; }
  return 12345U; /* Independent AD2 fixture, never copied into AD0. */
}
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc)
{
  assert(adc==&hadc1);
  if(s_prepare_phase) { s_prepare_phase=0U; return HAL_OK; }
  s_prepare_phase=1U;
  if(s_selected_channel==ADC_CHANNEL_5) { ++s_stop_calls; return s_stop_result; }
  return HAL_OK;
}
uint32_t HAL_UART_GetState(UART_HandleTypeDef *uart)
{ assert(uart == &huart2); return s_uart_state; }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, const uint8_t *bytes,
                                   uint16_t length, uint32_t timeout_ms)
{
  if (uart == &huart6)
  {
    assert(s_dtu_armed && timeout_ms == BSP_DTU_TX_TIMEOUT_MS);
    assert(length >= 10U && bytes[0] == '{');
    return HAL_OK;
  }
  assert(uart == &huart2 && timeout_ms == BSP_CONSOLE_TX_TIMEOUT_MS);
  assert(length > 0U && memchr(bytes, '\0', length) == NULL);
#if BSP_RS485_DIRECTION_MODE == BSP_RS485_MANUAL
  assert(s_gpio_level == BSP_RS485_TX_LEVEL);
#endif
  ++s_sends;
  if (s_tx_result != HAL_OK) { return s_tx_result; }
  assert(s_sent_length + length < sizeof(s_sent));
  memcpy(s_sent + s_sent_length, bytes, length);
  s_sent_length += length;
  s_sent[s_sent_length] = '\0';
  return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,const uint8_t *bytes,uint16_t length)
{ assert(uart==&huart6 && bytes && length); huart6.gState=HAL_UART_STATE_READY; huart6.TxXferCount=0U; HAL_UART_TxCpltCallback(uart); return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart)
{ assert(uart==&huart6); return HAL_OK; }
void TestEnableGpioClock(void) {}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level)
{
  assert(port == &fake_gpio && pin == 1U);
  s_gpio_level = level;
  ++s_gpio_writes;
}
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *gpio)
{
  assert(port == &fake_gpio && gpio->Pin == 1U);
  assert(gpio->Mode == GPIO_MODE_OUTPUT_PP && s_gpio_level == GPIO_PIN_RESET);
  ++s_gpio_inits;
}

static void Test_ClearOutput(void) { s_sent_length = 0U; s_sent[0] = '\0'; }
static void Test_Reset(void)
{
  s_tick_ms = 0U; s_tick_advances = 0U; test_tc = 1U; s_uart_state = HAL_UART_STATE_READY;
  s_cal_result = s_start_result = s_poll_result = s_stop_result = s_tx_result = HAL_OK;
  s_prepare_phase=1U; s_selected_channel=0U;
  s_raw = 32768U; s_cal_calls = s_start_calls = s_poll_calls = s_read_calls = s_stop_calls = 0U;
  s_sends = s_gpio_inits = s_gpio_writes = 0U;
  Test_ClearOutput();
}
static void Test_Record(void)
{
  Test_ClearOutput();
  s_tick_ms += APP_SAMPLE_WINDOW_MS;
  App_Process();
}

int main(void)
{
  SensorSoilData data;
  uint32_t base, converted;
  unsigned sends_before;
  FILEHANDLE console;
  Test_Reset();
  /* Application startup and all processing remain silent. */
  base = UINT32_MAX - 500U; s_tick_ms = base; App_Init(); Test_ClearOutput(); s_sends=0U;
  assert(!BspConsole_GetDiagnostics()->stdout_unbuffered && !s_sends);
  assert(s_cal_calls == 1U);
  App_Process(); assert(s_start_calls == 1U && s_sends == 0U);
  s_tick_ms = base + 999U; App_Process(); assert(s_sends == 0U && s_start_calls == 2U);
  s_tick_ms = base + 1000U; App_Process();
  assert((App_GetDiagnostics()->last_round.round_id==2U && App_GetDiagnostics()->last_round.round_t_ms==498U));
  assert((App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.moisture_pct_x100==10000U && App_GetDiagnostics()->last_round.soil.range==SENSOR_SOIL_RANGE_HIGH));
  assert((App_GetDiagnostics()->last_round.wind.adc.raw==12345U && App_GetDiagnostics()->last_round.wind.value.valid && App_GetDiagnostics()->last_round.wind_direction.angle_deg_x100==24959U));
  assert(s_start_calls == 2U && s_poll_calls == 2U && s_read_calls == 2U && s_stop_calls == 2U);
  assert(s_sent_length==0U); /* No temporary raw output before the physical window. */
  /* A repeated call neither prints again nor advances the physical window. */
  sends_before = s_sends; App_Process(); assert(s_sends == sends_before);
  Test_ClearOutput(); s_tick_ms += 10000U; App_Process();
  assert(App_GetDiagnostics()->last_round.round_t_ms==10499U && (App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.moisture_pct_x100==10000U && App_GetDiagnostics()->last_round.soil.range==SENSOR_SOIL_RANGE_HIGH));
  assert(App_GetDiagnostics()->completed_windows==0U && AppUpload_GetDiagnostics()->last_sequence==0U);
  assert(App_GetDiagnostics()->last_window_end_ms == 0U && AppUpload_GetDiagnostics()->skipped_late == 0U);
  sends_before = s_sends; App_Process(); assert(s_sends == sends_before);
  s_raw = 0U; Test_Record();
  assert((App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.moisture_pct_x100==0U && App_GetDiagnostics()->last_round.soil.range==SENSOR_SOIL_RANGE_LOW));
  s_raw = 435U; Test_Record();
  assert((App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.moisture_pct_x100==0U && App_GetDiagnostics()->last_round.soil.range==SENSOR_SOIL_RANGE_LOW));
  s_raw = 436U; Test_Record();
  assert((App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.moisture_pct_x100==0U && App_GetDiagnostics()->last_round.soil.range==SENSOR_SOIL_RANGE_OK));
  data = SensorSoil_Read();
  assert(data.adc.valid && data.range == SENSOR_SOIL_RANGE_OK && data.moisture_pct_x100 == 0U);
  s_raw = 438U; Test_Record();
  assert((App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.moisture_pct_x100==1U && App_GetDiagnostics()->last_round.soil.range==SENSOR_SOIL_RANGE_OK));
  s_raw = 15791U; Test_Record();
  assert((App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.moisture_pct_x100==5000U && App_GetDiagnostics()->last_round.soil.range==SENSOR_SOIL_RANGE_OK));
  data = SensorSoil_Read();
  assert(data.adc.valid && data.range == SENSOR_SOIL_RANGE_OK && data.moisture_pct_x100 == 5000U);
  s_raw = 31146U; Test_Record();
  assert((App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.moisture_pct_x100==10000U && App_GetDiagnostics()->last_round.soil.range==SENSOR_SOIL_RANGE_OK));
  s_raw = 31147U; Test_Record();
  assert((App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.moisture_pct_x100==10000U && App_GetDiagnostics()->last_round.soil.range==SENSOR_SOIL_RANGE_HIGH));
  s_raw = 65535U; Test_Record();
  assert((App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.moisture_pct_x100==10000U && App_GetDiagnostics()->last_round.soil.range==SENSOR_SOIL_RANGE_HIGH));
  data = SensorSoil_Read();
  assert(data.adc.valid && data.range == SENSOR_SOIL_RANGE_HIGH && data.moisture_pct_x100 == 10000U);
  s_raw = 65536U; Test_Record();
  assert((!App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.adc.status==BSP_ADC_OUT_OF_RANGE));
  s_start_result = HAL_BUSY; Test_Record();
  assert((!App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.adc.status==BSP_ADC_START_FAILED));
  s_start_result = HAL_OK; s_poll_result = HAL_TIMEOUT; s_stop_result = HAL_ERROR;
  Test_Record(); assert((!App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.adc.status==BSP_ADC_POLL_FAILED));
  data = SensorSoil_Read();
  assert(!data.adc.valid && data.adc.hal_status == BSP_ADC_HAL_TIMEOUT && data.adc.stop_status == BSP_ADC_HAL_ERROR);
  assert(data.range == SENSOR_SOIL_RANGE_UNAVAILABLE && data.moisture_pct_x100 == 0U);
  s_poll_result = HAL_OK; s_raw = 0U;
  Test_Record(); assert((!App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.adc.status==BSP_ADC_STOP_FAILED));
  s_stop_result = HAL_OK; Test_Record();
  assert((App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.moisture_pct_x100==0U && App_GetDiagnostics()->last_round.soil.range==SENSOR_SOIL_RANGE_LOW));
  s_cal_result = HAL_ERROR; App_Init(); base = s_start_calls;
  Test_Record(); assert((!App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.adc.status==BSP_ADC_CAL_FAILED));
  assert(s_start_calls == base);
  data = SensorSoil_Read(); assert(!data.adc.valid && !data.adc.stop_attempted);

  /* An unavailable console has no effect on sampling/upload and is never used. */
  Test_Reset(); App_Init(); s_sends=0U; s_tx_result = HAL_ERROR;
  Test_Record(); assert(App_GetDiagnostics()->printf_errors == 0U);
  assert(s_sends==0U && BspConsole_GetDiagnostics()->tx_errors==0U);
  s_tx_result = HAL_OK; Test_Record();
  assert((App_GetDiagnostics()->last_round.round_id==2U && App_GetDiagnostics()->last_round.round_t_ms==2U*APP_SAMPLE_WINDOW_MS));
  assert((App_GetDiagnostics()->last_round.soil.adc.valid && App_GetDiagnostics()->last_round.soil.moisture_pct_x100==10000U && App_GetDiagnostics()->last_round.soil.range==SENSOR_SOIL_RANGE_HIGH));
  assert(App_GetDiagnostics()->printf_errors == 0U);

  /* Raw endpoint mapping, round-to-nearest, and full-domain bounds. */
  assert(BspAdc_PinMillivolts(0U) == 0U);
  assert(BspAdc_PinMillivolts(65535U) == BSP_ADC_VREF_MV);
  assert(SensorSoil_MoisturePercentX100(0U) == 0U);
  assert(SensorSoil_MoisturePercentX100(436U) == 0U);
  assert(SensorSoil_MoisturePercentX100(437U) == 0U);
  assert(SensorSoil_MoisturePercentX100(438U) == 1U);
  assert(SensorSoil_MoisturePercentX100(15789U) == 4999U);
  assert(SensorSoil_MoisturePercentX100(15790U) == 5000U);
  assert(SensorSoil_MoisturePercentX100(15791U) == 5000U);
  assert(SensorSoil_MoisturePercentX100(31145U) == 10000U);
  assert(SensorSoil_MoisturePercentX100(31146U) == 10000U);
  assert(SensorSoil_MoisturePercentX100(65535U) == 10000U);
  converted = 0U;
  for (base = 0U; base <= UINT16_MAX; ++base)
  {
    uint32_t current = SensorSoil_MoisturePercentX100((uint16_t)base);
    assert(current >= converted && current <= 10000U);
    converted = current;
  }

  /* Production C library contract, length bounds and transport outcomes. */
  Test_Reset(); BspConsole_Init();
  console = _sys_open(__stdout_name, OPEN_W);
  assert(console >= 0 && _sys_open("unsupported-file", OPEN_W) < 0);
  assert(_sys_write(-1, (const unsigned char *)"x", 1U, 0) < 0);
  assert(_sys_write(console, NULL, 0U, 0) == 0 && s_sends == 0U);
  assert(_sys_write(console, (const unsigned char *)"x", 65536U, 0) < 0);
  assert(BspConsole_Send(NULL, 1U) == BSP_CONSOLE_BAD_LENGTH);
  assert(BspConsole_Send("x", 0U) == BSP_CONSOLE_BAD_LENGTH);
  assert(BspConsole_Send("x", 65536U) == BSP_CONSOLE_BAD_LENGTH);
  s_uart_state = 1U; assert(BspConsole_Send("x", 1U) == BSP_CONSOLE_UART_BUSY);
  s_uart_state = HAL_UART_STATE_READY;
  s_tx_result = HAL_BUSY; assert(BspConsole_Send("x", 1U) == BSP_CONSOLE_UART_BUSY);
  s_tx_result = HAL_TIMEOUT; assert(BspConsole_Send("x", 1U) == BSP_CONSOLE_UART_TIMEOUT);
  s_tx_result = HAL_ERROR; assert(BspConsole_Send("x", 1U) == BSP_CONSOLE_UART_ERROR);
  test_tc = 0U; s_tick_advances = 1U;
  assert(BspConsole_Send("x", 1U) == BSP_CONSOLE_TC_TIMEOUT);
  sends_before = s_sends;
  assert(BspConsole_Send("x", 1U) == BSP_CONSOLE_DRAIN_PENDING && s_sends == sends_before);
#if BSP_RS485_DIRECTION_MODE == BSP_RS485_MANUAL
  assert(s_gpio_level == BSP_RS485_TX_LEVEL);
#else
  assert(s_gpio_inits == 0U && s_gpio_writes == 0U);
#endif
  test_tc = 1U; BspConsole_Process();
#if BSP_RS485_DIRECTION_MODE == BSP_RS485_MANUAL
  assert(s_gpio_level == BSP_RS485_RX_LEVEL);
#endif
  s_tx_result = HAL_OK;
  assert(BspConsole_Send("x", 1U) == BSP_CONSOLE_OK);
  assert(BspConsole_GetDiagnostics()->last_tx_status == BSP_CONSOLE_OK);
  assert(BspConsole_GetDiagnostics()->tx_errors == 10U);
  fprintf(stderr, "PASS: two-point full-domain bounds, ADC/printf/timeouts/direction\n");
  return 0;
}
