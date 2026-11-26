#ifndef BSP_RS485_DIR_H
#define BSP_RS485_DIR_H

void BspRs485Dir_Init(void);
void BspRs485Dir_Transmit(void);
/* Caller must have observed UART TC before changing to receive. */
void BspRs485Dir_Receive(void);
const char *BspRs485Dir_Name(void);

#endif
