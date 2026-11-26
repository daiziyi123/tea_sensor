#ifndef TEST_HAL_H
#define TEST_HAL_H
#include <stddef.h>
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { unsigned unused; } ADC_HandleTypeDef;
typedef struct {
  uint32_t Channel, Rank, SamplingTime, SingleDiff, OffsetNumber, Offset, OffsetSignedSaturation;
} ADC_ChannelConfTypeDef;
typedef struct { uint32_t ErrorCode;
  uint32_t gState;
  uint16_t TxXferCount; } UART_HandleTypeDef;
typedef struct { unsigned unused; } GPIO_TypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
#define RESET 0U
#define UART_FLAG_TC 1U
#define HAL_UART_STATE_READY 0U
#define HAL_UART_ERROR_NONE 0U
#define HAL_UART_ERROR_PE 1U
#define HAL_UART_ERROR_NE 2U
#define HAL_UART_ERROR_FE 4U
#define HAL_UART_ERROR_ORE 8U
#define USART3_IRQn 39
#define UART4_IRQn 52
#define UART_RXDATA_FLUSH_REQUEST 1U
#define __HAL_UART_SEND_REQ(uart,request) ((void)(uart),(void)(request))
uint32_t NVIC_GetEnableIRQ(int irq);
#define ADC_CALIB_OFFSET_LINEARITY 2U
#define ADC_SINGLE_ENDED 0U
#define ADC_CHANNEL_5 5U
#define ADC_CHANNEL_3 3U
#define ADC_CHANNEL_19 19U
#define ADC_CHANNEL_18 18U
#define ADC_CHANNEL_15 15U
#define ADC_REGULAR_RANK_1 1U
#define ADC_SAMPLETIME_387CYCLES_5 3875U
#define ADC_OFFSET_NONE 0U
#define DISABLE 0U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
extern GPIO_TypeDef fake_gpio;
extern unsigned test_tc;
#define __HAL_UART_GET_FLAG(uart, flag) (test_tc)
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t delay);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *, uint32_t, uint32_t);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *, ADC_ChannelConfTypeDef *);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *, uint32_t);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *);
uint32_t HAL_UART_GetState(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *, const uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *, const uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
uint32_t HAL_UART_GetError(UART_HandleTypeDef *);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
void HAL_GPIO_Init(GPIO_TypeDef *, GPIO_InitTypeDef *);
void TestEnableGpioClock(void);
#endif

#ifndef UART_CLEAR_TCF
#define UART_CLEAR_TCF 1U
#define __HAL_UART_CLEAR_FLAG(uart,flag) ((void)(uart),(void)(flag))
#endif
