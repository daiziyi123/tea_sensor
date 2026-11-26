#include "bsp_weather_rs485.h"
#include "bsp_config.h"
#include "usart.h"
#include <string.h>

#define BSP_WEATHER_RS485_RX_SIZE 64U
typedef struct {
  volatile uint8_t bytes[BSP_WEATHER_RS485_RX_SIZE];
  volatile uint32_t ticks[BSP_WEATHER_RS485_RX_SIZE];
  volatile uint16_t head, tail;
  uint8_t rx_byte;
  volatile uint8_t initialized, recover, lost;
  volatile uint32_t error_event;
  volatile BspWeatherRs485Status lost_status;
  volatile BspWeatherRs485Diagnostics diagnostics;
} BspWeatherRs485Context;
static BspWeatherRs485Context s_ports[BSP_WEATHER_RS485_PORT_COUNT];

static UART_HandleTypeDef *BspWeatherRs485_Uart(BspWeatherRs485Port port)
{ return port == BSP_WEATHER_RS485_MET ? &huart3 : &huart4; }
static uint8_t BspWeatherRs485_IrqEnabled(BspWeatherRs485Port port)
{ return NVIC_GetEnableIRQ(port == BSP_WEATHER_RS485_MET ? USART3_IRQn : UART4_IRQn) ? 1U : 0U; }
static BspWeatherRs485Status BspWeatherRs485_MapHal(HAL_StatusTypeDef status)
{
  switch(status) {
    case HAL_OK: return BSP_WEATHER_RS485_OK;
    case HAL_BUSY: return BSP_WEATHER_RS485_UART_BUSY;
    case HAL_TIMEOUT: return BSP_WEATHER_RS485_UART_TIMEOUT;
    default: return BSP_WEATHER_RS485_UART_ERROR;
  }
}
/* Main loop with IRQs masked, or RX ISR. Abort/recovery is main-loop only. */
static BspWeatherRs485Status BspWeatherRs485_Arm(BspWeatherRs485Port port)
{
  BspWeatherRs485Context *context=&s_ports[port];
  BspWeatherRs485Status status=BspWeatherRs485_MapHal(
    HAL_UART_Receive_IT(BspWeatherRs485_Uart(port), &context->rx_byte, 1U));
  context->diagnostics.last_rx_status=status;
  context->diagnostics.rx_active=status==BSP_WEATHER_RS485_OK;
  if(status!=BSP_WEATHER_RS485_OK) {
    ++context->diagnostics.rx_start_errors; context->recover=1U; context->lost=1U;
    context->lost_status=status;
  }
  return status;
}
BspWeatherRs485Status BspWeatherRs485_Init(BspWeatherRs485Port port)
{
  uint32_t mask;
  BspWeatherRs485Status status;
  uint8_t was_initialized;
  if((unsigned)port>=BSP_WEATHER_RS485_PORT_COUNT) { return BSP_WEATHER_RS485_BAD_ARGUMENT; }
  mask=__get_PRIMASK(); __disable_irq();
  was_initialized=s_ports[port].initialized;
  memset(&s_ports[port],0,sizeof(s_ports[port])); s_ports[port].initialized=1U;
  if(!BspWeatherRs485_IrqEnabled(port)) { status=BSP_WEATHER_RS485_IRQ_DISABLED; }
  else {
    status=was_initialized ? BspWeatherRs485_MapHal(HAL_UART_AbortReceive(BspWeatherRs485_Uart(port))) : BSP_WEATHER_RS485_OK;
    if(status==BSP_WEATHER_RS485_OK) { status=BspWeatherRs485_Arm(port); }
    else { s_ports[port].recover=1U; }
  }
  s_ports[port].diagnostics.last_rx_status=status;
  __set_PRIMASK(mask); return status;
}
void BspWeatherRs485_Process(BspWeatherRs485Port port)
{
  uint32_t mask;
  BspWeatherRs485Context *context;
  if((unsigned)port>=BSP_WEATHER_RS485_PORT_COUNT) { return; }
  context=&s_ports[port];
  if(!context->initialized || !BspWeatherRs485_IrqEnabled(port)) { return; }
  mask=__get_PRIMASK(); __disable_irq();
  if(context->recover || !context->diagnostics.rx_active) {
    BspWeatherRs485Status status=BspWeatherRs485_MapHal(HAL_UART_AbortReceive(BspWeatherRs485_Uart(port)));
    if(status==BSP_WEATHER_RS485_OK) {
      context->recover=0U;
      __HAL_UART_SEND_REQ(BspWeatherRs485_Uart(port), UART_RXDATA_FLUSH_REQUEST);
      (void)BspWeatherRs485_Arm(port);
    } else { context->diagnostics.last_rx_status=status; }
  }
  __set_PRIMASK(mask);
}
BspWeatherRs485Status BspWeatherRs485_Send(BspWeatherRs485Port port, const uint8_t *data, size_t length)
{
  BspWeatherRs485Status status;
  BspWeatherRs485Context *context;
  uint32_t mask;
  if((unsigned)port>=BSP_WEATHER_RS485_PORT_COUNT || !data || !length || length>8U) { return BSP_WEATHER_RS485_BAD_ARGUMENT; }
  context=&s_ports[port]; mask=__get_PRIMASK(); __disable_irq();
  if(!BspWeatherRs485_IrqEnabled(port)) { status=BSP_WEATHER_RS485_IRQ_DISABLED; }
  else if(!context->initialized) { status=BSP_WEATHER_RS485_RX_NOT_READY; }
  else {
    /* Remove residual hardware/software bytes before the new request, not a
     * guessed number of bytes after it. RX is already armed when TX begins. */
    status=BspWeatherRs485_MapHal(HAL_UART_AbortReceive(BspWeatherRs485_Uart(port)));
    context->diagnostics.rx_active=0U;
    if(status==BSP_WEATHER_RS485_OK) {
      context->head=context->tail=0U; context->lost=0U; context->error_event=0U; context->recover=0U;
      context->lost_status=BSP_WEATHER_RS485_OK;
      __HAL_UART_SEND_REQ(BspWeatherRs485_Uart(port), UART_RXDATA_FLUSH_REQUEST);
      status=BspWeatherRs485_Arm(port);
    } else { context->recover=1U; }
  }
  __set_PRIMASK(mask);
  if(status==BSP_WEATHER_RS485_OK) {
    status=BspWeatherRs485_MapHal(HAL_UART_Transmit(BspWeatherRs485_Uart(port),data,(uint16_t)length,BSP_WEATHER_TX_TIMEOUT_MS));
  }
  mask=__get_PRIMASK(); __disable_irq();
  ++context->diagnostics.tx_attempts; context->diagnostics.last_tx_status=status;
  if(status!=BSP_WEATHER_RS485_OK) { ++context->diagnostics.tx_errors; }
  __set_PRIMASK(mask); return status;
}
BspWeatherRs485ReadResult BspWeatherRs485_Read(BspWeatherRs485Port port, BspWeatherRs485Byte *data, size_t capacity)
{
  BspWeatherRs485ReadResult result={0U,0U,0U,BSP_WEATHER_RS485_OK};
  BspWeatherRs485Context *context;
  uint32_t mask;
  if((unsigned)port>=BSP_WEATHER_RS485_PORT_COUNT || !data || !capacity) { return result; }
  if(capacity>16U) { capacity=16U; }
  context=&s_ports[port]; mask=__get_PRIMASK(); __disable_irq();
  result.data_lost=context->lost; result.uart_error=context->error_event;
  result.lost_status=context->lost_status; context->lost_status=BSP_WEATHER_RS485_OK;
  context->lost=0U; context->error_event=0U;
  if(result.data_lost) { context->tail=context->head; }
  else while(result.length<capacity && context->tail!=context->head) {
    data[result.length].byte=context->bytes[context->tail]; data[result.length].t_ms=context->ticks[context->tail];
    ++result.length; context->tail=(uint16_t)((context->tail+1U)%BSP_WEATHER_RS485_RX_SIZE);
  }
  __set_PRIMASK(mask); return result;
}
BspWeatherRs485Diagnostics BspWeatherRs485_GetDiagnostics(BspWeatherRs485Port port)
{
  BspWeatherRs485Diagnostics result={0}; uint32_t mask;
  if((unsigned)port>=BSP_WEATHER_RS485_PORT_COUNT) { return result; }
  mask=__get_PRIMASK(); __disable_irq(); result=s_ports[port].diagnostics; __set_PRIMASK(mask); return result;
}
void BspWeatherRs485_RxCompleteIsr(BspWeatherRs485Port port)
{
  BspWeatherRs485Context *context; uint16_t next;
  if((unsigned)port>=BSP_WEATHER_RS485_PORT_COUNT) { return; }
  context=&s_ports[port]; if(!context->initialized || !context->diagnostics.rx_active) { return; }
  context->diagnostics.rx_active=0U; ++context->diagnostics.rx_bytes;
  context->diagnostics.last_rx_ms=HAL_GetTick(); context->diagnostics.rx_seen=1U;
  next=(uint16_t)((context->head+1U)%BSP_WEATHER_RS485_RX_SIZE);
  if(next==context->tail) { context->lost=1U; ++context->diagnostics.rx_overflows; }
  else { context->bytes[context->head]=context->rx_byte; context->ticks[context->head]=context->diagnostics.last_rx_ms; context->head=next; }
  (void)BspWeatherRs485_Arm(port);
}
void BspWeatherRs485_ErrorIsr(BspWeatherRs485Port port, uint32_t error)
{
  BspWeatherRs485Context *context;
  if((unsigned)port>=BSP_WEATHER_RS485_PORT_COUNT) { return; }
  context=&s_ports[port]; if(!context->initialized) { return; }
  ++context->diagnostics.rx_errors; context->diagnostics.last_uart_error=error;
  context->error_event|=error; context->lost=1U; context->recover=1U; context->diagnostics.rx_active=0U;
  context->lost_status=BSP_WEATHER_RS485_UART_ERROR;
}
const char *BspWeatherRs485_StatusName(BspWeatherRs485Status status)
{
  switch(status) {
    case BSP_WEATHER_RS485_OK:return "OK";
    case BSP_WEATHER_RS485_UART_BUSY:return "UART_BUSY";
    case BSP_WEATHER_RS485_UART_TIMEOUT:return "UART_TIMEOUT";
    case BSP_WEATHER_RS485_RX_NOT_READY:return "RX_NOT_READY";
    case BSP_WEATHER_RS485_IRQ_DISABLED:return "IRQ_DISABLED";
    case BSP_WEATHER_RS485_BAD_ARGUMENT:return "BAD_ARGUMENT";
    default:return "UART_ERROR";
  }
}
