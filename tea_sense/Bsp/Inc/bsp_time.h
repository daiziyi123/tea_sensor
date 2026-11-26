#ifndef BSP_TIME_H
#define BSP_TIME_H

#include <stdint.h>

/* Hardware tick boundary for the application's wrap-safe period scheduler. */
uint32_t BspTime_GetTickMs(void);

#endif
