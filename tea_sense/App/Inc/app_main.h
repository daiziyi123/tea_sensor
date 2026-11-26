#ifndef APP_MAIN_H
#define APP_MAIN_H
typedef struct AppDiagnostics AppDiagnostics;
void App_Init(void);
void App_Process(void);
/* Read-only sampling/console debugger view; definition in app_diagnostics.h. */
const AppDiagnostics *App_GetDiagnostics(void);
#endif
