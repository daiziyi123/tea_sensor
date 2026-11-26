#include "bsp_time.h"
#include "stm32h7xx_hal.h"

uint32_t BspTime_GetTickMs(void)
{
  return HAL_GetTick();
}
