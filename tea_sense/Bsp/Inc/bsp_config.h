#ifndef BSP_CONFIG_H
#define BSP_CONFIG_H

/* Nominal placeholder, NOT a measurement of VREF+ relative to VREF-. */
#ifndef BSP_ADC_VREF_MV
#define BSP_ADC_VREF_MV 3300U
#endif
#ifndef BSP_ADC_VREF_IS_MEASURED
#define BSP_ADC_VREF_IS_MEASURED 0
#endif
#define BSP_ADC_FULL_SCALE 65535U
#ifndef BSP_ADC_POLL_TIMEOUT_MS
#define BSP_ADC_POLL_TIMEOUT_MS 20U
#endif
#ifndef BSP_CONSOLE_TX_TIMEOUT_MS
#define BSP_CONSOLE_TX_TIMEOUT_MS 100U
#endif
#ifndef BSP_CONSOLE_TC_TIMEOUT_MS
#define BSP_CONSOLE_TC_TIMEOUT_MS 5U
#endif
#ifndef BSP_DTU_TX_TIMEOUT_MS
#define BSP_DTU_TX_TIMEOUT_MS 1000U
#endif
#define BSP_DTU_TX_BUFFER_CAPACITY 6144U
/* New engineering policy, pending real-board stability validation. */
#ifndef BSP_DTU_RECOVERY_PERIOD_MS
#define BSP_DTU_RECOVERY_PERIOD_MS 60000U
#endif
#ifndef BSP_ADC_CAL_RECOVERY_PERIOD_MS
#define BSP_ADC_CAL_RECOVERY_PERIOD_MS 60000U
#endif
#if BSP_DTU_RECOVERY_PERIOD_MS == 0 || BSP_DTU_RECOVERY_PERIOD_MS > 0x7FFFFFFFU || BSP_ADC_CAL_RECOVERY_PERIOD_MS == 0 || BSP_ADC_CAL_RECOVERY_PERIOD_MS > 0x7FFFFFFFU
#error Invalid bounded peripheral recovery interval
#endif
#define BSP_WEATHER_TX_TIMEOUT_MS 50U
#define BSP_WEATHER_RESPONSE_TIMEOUT_MS 1000U
#define BSP_WEATHER_TRANSACTION_GAP_MS 250U
/* 3.5 characters at 4800/8N1 rounded upward to milliseconds. */
#define BSP_WEATHER_RX_QUIET_MS 8U
#if BSP_WEATHER_TX_TIMEOUT_MS == 0 || BSP_WEATHER_TX_TIMEOUT_MS > 100U || BSP_WEATHER_RESPONSE_TIMEOUT_MS == 0 || BSP_WEATHER_RESPONSE_TIMEOUT_MS > 0x7FFFFFFFU || BSP_WEATHER_TRANSACTION_GAP_MS < 250U || BSP_WEATHER_TRANSACTION_GAP_MS > 0x7FFFFFFFU || BSP_WEATHER_RX_QUIET_MS == 0
#error Invalid weather UART timeout or spacing configuration
#endif

#define BSP_RS485_AUTO   0
#define BSP_RS485_MANUAL 1
#ifndef BSP_RS485_DIRECTION_MODE
#define BSP_RS485_DIRECTION_MODE BSP_RS485_AUTO
#endif
/* AUTO avoids GPIO access; it does not certify the physical assembly. */
#ifndef BSP_RS485_AUTO_ASSEMBLY_CONFIRMED
#define BSP_RS485_AUTO_ASSEMBLY_CONFIRMED 0
#endif
#ifndef BSP_RS485_MANUAL_WIRING_CONFIRMED
#define BSP_RS485_MANUAL_WIRING_CONFIRMED 0
#endif
/* Manual mode requires board-supplied BSP_RS485_GPIO_PORT, GPIO_PIN,
 * GPIO_CLOCK_ENABLE, TX_LEVEL and RX_LEVEL definitions with this prefix.
 * Hardware operations using them exist only in bsp_rs485_dir.c.
 * These settings apply ONLY to the USART2 console, never USART3/UART4. */
#ifndef BSP_RS485_SETUP_MS
#define BSP_RS485_SETUP_MS 0U
#endif
#ifndef BSP_RS485_HOLD_MS
#define BSP_RS485_HOLD_MS 0U
#endif

#if BSP_ADC_VREF_MV == 0 || BSP_ADC_VREF_MV > 3600U
#error Set a valid ADC reference in millivolts
#endif
#if BSP_ADC_POLL_TIMEOUT_MS == 0 || BSP_ADC_POLL_TIMEOUT_MS > 1000U
#error Invalid ADC polling timeout
#endif
#if BSP_CONSOLE_TX_TIMEOUT_MS == 0 || BSP_CONSOLE_TX_TIMEOUT_MS > 1000U || BSP_CONSOLE_TC_TIMEOUT_MS == 0 || BSP_CONSOLE_TC_TIMEOUT_MS > 1000U
#error Invalid UART timeout
#endif
#if BSP_DTU_TX_TIMEOUT_MS == 0 || BSP_DTU_TX_TIMEOUT_MS > 1000U
#error Invalid DTU transmit timeout
#endif
#if BSP_RS485_DIRECTION_MODE != BSP_RS485_AUTO && BSP_RS485_DIRECTION_MODE != BSP_RS485_MANUAL
#error Invalid BSP_RS485_DIRECTION_MODE
#endif
#if BSP_RS485_SETUP_MS > 1000U || BSP_RS485_HOLD_MS > 1000U
#error Invalid direction delay
#endif

#endif
