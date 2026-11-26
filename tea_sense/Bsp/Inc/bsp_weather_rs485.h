#ifndef BSP_WEATHER_RS485_H
#define BSP_WEATHER_RS485_H
#include <stddef.h>
#include <stdint.h>

typedef enum { BSP_WEATHER_RS485_MET = 0, BSP_WEATHER_RS485_GAS, BSP_WEATHER_RS485_PORT_COUNT } BspWeatherRs485Port;
typedef enum {
  BSP_WEATHER_RS485_OK = 0, BSP_WEATHER_RS485_UART_ERROR, BSP_WEATHER_RS485_UART_BUSY,
  BSP_WEATHER_RS485_UART_TIMEOUT, BSP_WEATHER_RS485_RX_NOT_READY,
  BSP_WEATHER_RS485_IRQ_DISABLED, BSP_WEATHER_RS485_BAD_ARGUMENT
} BspWeatherRs485Status;
typedef struct { uint8_t byte; uint32_t t_ms; } BspWeatherRs485Byte;
typedef struct {
  size_t length;
  uint8_t data_lost;
  uint32_t uart_error;
  BspWeatherRs485Status lost_status;
} BspWeatherRs485ReadResult;
typedef struct {
  uint32_t rx_bytes, rx_overflows, rx_errors, rx_start_errors, tx_attempts, tx_errors;
  uint32_t last_uart_error, last_rx_ms;
  uint8_t rx_seen, rx_active;
  BspWeatherRs485Status last_rx_status, last_tx_status;
} BspWeatherRs485Diagnostics;

BspWeatherRs485Status BspWeatherRs485_Init(BspWeatherRs485Port port);
void BspWeatherRs485_Process(BspWeatherRs485Port port);
/* Flush old RX and arm one-byte IT receive before bounded binary transmit. */
BspWeatherRs485Status BspWeatherRs485_Send(BspWeatherRs485Port port, const uint8_t *data, size_t length);
BspWeatherRs485ReadResult BspWeatherRs485_Read(BspWeatherRs485Port port, BspWeatherRs485Byte *data, size_t capacity);
BspWeatherRs485Diagnostics BspWeatherRs485_GetDiagnostics(BspWeatherRs485Port port);
/* ISR entries dispatched from the single HAL callbacks in bsp_uart_irq.c. */
void BspWeatherRs485_RxCompleteIsr(BspWeatherRs485Port port);
void BspWeatherRs485_ErrorIsr(BspWeatherRs485Port port, uint32_t error);
const char *BspWeatherRs485_StatusName(BspWeatherRs485Status status);
#endif
