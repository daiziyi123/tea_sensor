/* Fixed UART routing. Protocol state and rings remain private to each Bsp. */
#include "bsp_dtu.h"
#include "bsp_weather_rs485.h"
#include "usart.h"
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
  if(uart==&huart3) { BspWeatherRs485_RxCompleteIsr(BSP_WEATHER_RS485_MET); }
  else if(uart==&huart4) { BspWeatherRs485_RxCompleteIsr(BSP_WEATHER_RS485_GAS); }
  else if(uart==&huart6) { BspDtu_RxCompleteIsr(); }
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
  if(uart==&huart6) { BspDtu_TxCompleteIsr(); }
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
  if(uart==&huart3) { BspWeatherRs485_ErrorIsr(BSP_WEATHER_RS485_MET,HAL_UART_GetError(uart)); }
  else if(uart==&huart4) { BspWeatherRs485_ErrorIsr(BSP_WEATHER_RS485_GAS,HAL_UART_GetError(uart)); }
  else if(uart==&huart6) { BspDtu_ErrorIsr(HAL_UART_GetError(uart)); }
}
