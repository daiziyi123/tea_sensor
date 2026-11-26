#ifndef BSP_DTU_H
#define BSP_DTU_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
  BSP_DTU_OK = 0,
  BSP_DTU_BAD_LENGTH,
  BSP_DTU_UART_ERROR,
  BSP_DTU_UART_BUSY,
  BSP_DTU_UART_TIMEOUT,
  BSP_DTU_RX_NOT_READY
} BspDtuStatus;

typedef struct
{
  size_t length;
  uint8_t data_lost; /* Discard any partial application line and resync at LF. */
} BspDtuReadResult;

typedef enum {
  BSP_DTU_TX_IDLE=0, BSP_DTU_TX_STARTING, BSP_DTU_TX_SENDING,
  BSP_DTU_TX_COMPLETE, BSP_DTU_TX_FAILED, BSP_DTU_TX_BLOCKED
} BspDtuTxState;
typedef struct
{
  uint32_t tx_attempts;
  uint32_t tx_errors;
  uint32_t rx_bytes;
  uint32_t rx_overflows;
  uint32_t rx_errors;
  uint32_t rx_start_errors;
  uint32_t last_uart_error;
  BspDtuStatus last_tx_status;
  BspDtuStatus last_rx_status;
  BspDtuStatus last_abort_status;
  uint8_t rx_active;
  uint32_t tx_completed, tx_abort_errors;
  uint8_t tx_active;
  BspDtuTxState tx_state;
  uint32_t recovery_attempts, recovery_successes;
  BspDtuStatus last_tx_abort_status;
} BspDtuDiagnostics;

typedef struct
{
  uint8_t busy, finished;
  BspDtuStatus status;
  uint32_t completed_ms;
} BspDtuTxResult;

/* Fixed USART6 / PC6-PC7 / A6-B6, AUTO direction; CubeMX init is prerequisite. */
BspDtuStatus BspDtu_Init(void);
/* Single owned buffer, writable only while idle; immutable from StartSend
 * until safely stopped; a failed completion can still leave TX BLOCKED. */
char *BspDtu_TxBuffer(size_t *capacity);
BspDtuStatus BspDtu_StartSend(size_t length);
BspDtuTxResult BspDtu_Process(void);
/* Main-loop consumer, at most 32 bytes per call; retry RX startup/recovery.
 * After loss, queued bytes are flushed and data_lost is reported once. */
BspDtuReadResult BspDtu_Read(uint8_t *data, size_t capacity);
BspDtuDiagnostics BspDtu_GetDiagnostics(void);
/* Called only by the fixed Bsp UART dispatcher. No HAL types cross layers. */
void BspDtu_RxCompleteIsr(void);
void BspDtu_TxCompleteIsr(void);
void BspDtu_ErrorIsr(uint32_t error);
const char *BspDtu_StatusName(BspDtuStatus status);

#endif
