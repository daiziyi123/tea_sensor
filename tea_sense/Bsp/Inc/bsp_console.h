#ifndef BSP_CONSOLE_H
#define BSP_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
  BSP_CONSOLE_OK = 0,
  BSP_CONSOLE_BAD_LENGTH,
  BSP_CONSOLE_UART_ERROR,
  BSP_CONSOLE_UART_BUSY,
  BSP_CONSOLE_UART_TIMEOUT,
  BSP_CONSOLE_TC_TIMEOUT,
  BSP_CONSOLE_DRAIN_PENDING
} BspConsoleStatus;

typedef struct
{
  uint32_t tx_attempts;
  uint32_t tx_errors;
  BspConsoleStatus last_tx_status;
  uint8_t stdout_unbuffered;
} BspConsoleDiagnostics;

/* Fixed USART2 computer console, PD5/PD6, 115200 8N1. */
void BspConsole_Init(void);
/* Nonblocking release after a failed transmit whose TC has not yet arrived. */
void BspConsole_Process(void);
BspConsoleStatus BspConsole_Send(const char *data, size_t length);
/* Counts BspConsole sends, including C library stdout writes. */
const BspConsoleDiagnostics *BspConsole_GetDiagnostics(void);
const char *BspConsole_StatusName(BspConsoleStatus status);

#endif
