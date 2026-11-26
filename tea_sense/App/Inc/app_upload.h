#ifndef APP_UPLOAD_H
#define APP_UPLOAD_H
#include <stddef.h>
#include <stdint.h>
#include "app_data.h"
/* Public upper bound is independent of private credentials. */
#define APP_UPLOAD_FRAME_CAPACITY 6142U
#define APP_UPLOAD_ACK_CAPACITY 512U

typedef enum
{
  APP_UPLOAD_ACK_INVALID = 0,
  APP_UPLOAD_ACK_IGNORED,
  APP_UPLOAD_ACK_ACCEPTED
} AppUploadAckStatus;

typedef struct
{
  uint64_t id;
} AppUploadAck;

typedef enum {
  APP_UPLOAD_SUBMIT_ACCEPTED=0, APP_UPLOAD_SUBMIT_BUSY,
  APP_UPLOAD_SUBMIT_RATE_LIMITED, APP_UPLOAD_SUBMIT_DISABLED, APP_UPLOAD_SUBMIT_ERROR
} AppUploadSubmitStatus;
typedef struct {
  uint32_t ack_count, ack_sequence;
  uint64_t ack_id;
  uint32_t last_sequence, timeouts, line_drops, stream_gaps, format_errors, tx_errors;
  uint32_t skipped_busy, skipped_late, skipped_rate;
  uint8_t tx_pending, awaiting_ack;
  /* BspDtuStatus numeric value, without exposing private hardware state. */
  unsigned init_status;
} AppUploadDiagnostics;
void AppUpload_Init(void);
void AppUpload_Process(void);
/* Consumes synchronously; no stored pointer to caller's mutable snapshot. */
AppUploadSubmitStatus AppUpload_Submit(const AppSensorSnapshot *snapshot);
const AppUploadDiagnostics *AppUpload_GetDiagnostics(void);

uint8_t AppUpload_IsConfigured(void);
int AppUpload_Format(char *buffer, size_t capacity, uint32_t sequence,
                     uint32_t now_ms, const AppSensorSnapshot *snapshot);
AppUploadAckStatus AppUpload_ParseAck(const char *line, size_t length,
                                     uint32_t expected_sequence, AppUploadAck *ack);
#endif
