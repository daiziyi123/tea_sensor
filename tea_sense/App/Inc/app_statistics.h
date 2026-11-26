#ifndef APP_STATISTICS_H
#define APP_STATISTICS_H
#include "app_data.h"
#include "app_diagnostics.h"
void AppStatistics_Init(uint32_t start_ms);
/* Consume only a newly completed real round, or NULL when no acquisition ran.
 * Returns one immutable latest completed window, never a catch-up queue. */
uint8_t AppStatistics_Update(const AppSampleRound *round, uint32_t now_ms, AppSensorSnapshot *snapshot);
#endif
