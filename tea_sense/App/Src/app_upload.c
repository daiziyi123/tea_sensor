#include "app_upload.h"
#include "app_config.h"
#include "app_version.h"
#include <app_secrets.h>
#include "bsp_dtu.h"
#include "bsp_time.h"
#include "bsp_config.h"
#include "core_json.h"
#include <stdio.h>
#include <string.h>

uint8_t AppUpload_IsConfigured(void)
{
  return (APP_INGEST_TOKEN_CONFIGURED == 1 && sizeof(APP_INGEST_TOKEN_JSON) > 3U) ? 1U : 0U;
}

/* 4500 bytes bound eleven fields, statistics and latest-query diagnostics.
 * Escaped token and device sizes are checked privately; Bsp owns the only TX. */
typedef char AppUploadFrameBudget[(sizeof(APP_INGEST_TOKEN_JSON)+sizeof(APP_DEVICE_ID)+4500U
                                  <=APP_UPLOAD_FRAME_CAPACITY)?1:-1];
typedef char AppUploadHardwareCapacity[(APP_UPLOAD_FRAME_CAPACITY+2U<=BSP_DTU_TX_BUFFER_CAPACITY)?1:-1];

/* Keep each literal format visible to ARMCC's integer-only printf selection. */
static uint8_t AppUpload_Commit(size_t capacity, size_t *offset, int length)
{
  if(length<0 || (size_t)length>=capacity-*offset) { return 0U; }
  *offset+=(size_t)length; return 1U;
}
#define APP_UPLOAD_APPEND(...) do { \
  int written=snprintf(buffer+offset,capacity-offset,__VA_ARGS__); \
  if(!AppUpload_Commit(capacity,&offset,written)) { return -1; } \
} while(0)

int AppUpload_Format(char *buffer, size_t capacity, uint32_t sequence,
                     uint32_t now_ms, const AppSensorSnapshot *snapshot)
{
  size_t offset=0U;
  unsigned index;
  const char *names[APP_SENSOR_METRIC_COUNT]={"soil_moisture_pct","wind_speed_mps","wind_direction_deg","soil_ph",
    "temperature_c","air_humidity_rh_pct","light_lux","pressure_kpa","pm25_ug_m3","pm10_ug_m3","rain_intensity_mm_min"};
  const AppSensorValue *values[APP_SENSOR_METRIC_COUNT];
  const uint32_t scales[APP_SENSOR_METRIC_COUNT]={100U,100U,100U,100U,10U,10U,1U,100U,1U,1U,100U};
  if(!buffer || !snapshot || !capacity || !AppUpload_IsConfigured()) { return -1; }
  values[0]=&snapshot->soil_moisture_pct; values[1]=&snapshot->wind_speed_mps;
  values[2]=&snapshot->wind_direction_deg; values[3]=&snapshot->soil_ph;
  values[4]=&snapshot->temperature_c; values[5]=&snapshot->air_humidity_rh_pct;
  values[6]=&snapshot->light_lux; values[7]=&snapshot->pressure_kpa;
  values[8]=&snapshot->pm25_ug_m3; values[9]=&snapshot->pm10_ug_m3; values[10]=&snapshot->rain_intensity_mm_min;
  APP_UPLOAD_APPEND(
    "{\"schema_version\":%lu,\"device_id\":\"%s\",\"seq\":%lu,\"t_ms\":%lu,\"window_ms\":%lu,\"data\":{",
    (unsigned long)APP_PROTOCOL_SCHEMA_VERSION,APP_DEVICE_ID,(unsigned long)sequence,(unsigned long)snapshot->window_end_ms,(unsigned long)APP_SAMPLE_WINDOW_MS);
  for(index=0U;index<APP_SENSOR_METRIC_COUNT;++index)
  {
    const AppSensorValue *value=values[index];
    const char *status=value->status?value->status:"NO_DATA";
    if(strlen(status)>40U || (value->valid && (!value->has_sample ||
       (strcmp(status,"OK") && strcmp(status,"PARTIAL") && strcmp(status,"CACHED"))))) { return -1; }
    APP_UPLOAD_APPEND("%s\"%s\":{\"value\":",index?",":"",names[index]);
    if(value->valid)
    {
      uint64_t magnitude=value->value<0?(uint64_t)(-(value->value+1))+1U:(uint64_t)value->value;
      const char *sign=value->value<0?"-":"";
      if(magnitude>UINT32_MAX) { return -1; }
      if(scales[index]==1U) {
        APP_UPLOAD_APPEND("%s%lu",sign,(unsigned long)magnitude);
      } else {
        if(scales[index]==100U) {
          APP_UPLOAD_APPEND("%s%lu.%02lu",sign,(unsigned long)(magnitude/100U),(unsigned long)(magnitude%100U));
        } else {
          APP_UPLOAD_APPEND("%s%lu.%lu",sign,(unsigned long)(magnitude/10U),(unsigned long)(magnitude%10U));
        }
      }
    }
    else APP_UPLOAD_APPEND("null");
    APP_UPLOAD_APPEND(",\"status\":\"%s\",",status);
    if(value->has_sample) {
      APP_UPLOAD_APPEND("\"sample_seq\":%lu,\"sample_t_ms\":%lu,\"age_ms\":%lu",
        (unsigned long)value->sample_seq,(unsigned long)value->sample_t_ms,
        (unsigned long)(uint32_t)(now_ms-value->sample_t_ms));
    } else APP_UPLOAD_APPEND("\"sample_seq\":null,\"sample_t_ms\":null,\"age_ms\":null");
    if(value->valid && value->range) { APP_UPLOAD_APPEND(",\"range\":\"%s\"",value->range); }
    if(value->method) {
      APP_UPLOAD_APPEND(",\"statistics\":{\"method\":\"%s\"",value->method);
      if(strcmp(value->method,"latest")) {
        APP_UPLOAD_APPEND(",\"window_ms\":%lu,\"start_ms\":%lu,\"end_ms\":%lu,\"expected_count\":%lu,\"valid_count\":%lu,\"used_count\":%lu",
          (unsigned long)value->statistics_window_ms,(unsigned long)value->statistics_start_ms,
          (unsigned long)value->statistics_end_ms,(unsigned long)value->expected_count,
          (unsigned long)value->valid_count,(unsigned long)value->used_count);
        if(!strcmp(value->method,"time_weighted_mean")) {
          APP_UPLOAD_APPEND(",\"valid_duration_ms\":%lu",(unsigned long)value->valid_duration_ms);
        }
      }
      APP_UPLOAD_APPEND("}");
    }
    if(index==1U && value->method) {
      if(snapshot->wind_speed_sample_max_valid) {
        APP_UPLOAD_APPEND(",\"wind_speed_sample_max_mps\":%lu.%02lu",
          (unsigned long)(snapshot->wind_speed_sample_max_x100/100U),
          (unsigned long)(snapshot->wind_speed_sample_max_x100%100U));
      } else APP_UPLOAD_APPEND(",\"wind_speed_sample_max_mps\":null");
    }
    if(value->latest_query_status) {
      APP_UPLOAD_APPEND(",\"cached\":%s,\"latest_query_status\":\"%s\",\"latest_query_seq\":%lu,\"latest_query_t_ms\":%lu",
        value->cached?"true":"false",value->latest_query_status,
        (unsigned long)value->latest_query_seq,(unsigned long)value->latest_query_t_ms);
    }
    APP_UPLOAD_APPEND("}");
  }
  APP_UPLOAD_APPEND("},\"token\":%s}\r\n",APP_INGEST_TOKEN_JSON);
  return (int)offset;
}

static int AppUpload_HexDigit(char character)
{
  if (character >= '0' && character <= '9') { return character - '0'; }
  if (character >= 'a' && character <= 'f') { return character - 'a' + 10; }
  if (character >= 'A' && character <= 'F') { return character - 'A' + 10; }
  return -1;
}

/* Match ASCII protocol identifiers, including equivalent JSON escapes. */
static uint8_t AppUpload_StringEquals(const char *text, size_t length, const char *expected)
{
  size_t index = 0U;
  while (index < length)
  {
    unsigned character = (unsigned char)text[index++];
    if (character == '\\')
    {
      if (index == length) { return 0U; }
      character = (unsigned char)text[index++];
      if (character == 'u')
      {
        unsigned digit;
        character = 0U;
        if (length - index < 4U) { return 0U; }
        for (digit = 0U; digit < 4U; ++digit)
        {
          int value = AppUpload_HexDigit(text[index++]);
          if (value < 0) { return 0U; }
          character = character * 16U + (unsigned)value;
        }
      }
      else if (character != '\\' && character != '"' && character != '/') { return 0U; }
    }
    if (*expected == '\0' || character != (unsigned char)*expected++) { return 0U; }
  }
  return *expected == '\0' ? 1U : 0U;
}

static uint8_t AppUpload_ParseUnsigned(const char *text, size_t length, uint64_t maximum, uint64_t *value)
{
  size_t index;
  uint64_t number = 0U;
  if (!length) { return 0U; }
  for (index = 0U; index < length; ++index)
  {
    unsigned digit = (unsigned char)text[index] - (unsigned)'0';
    if (digit > 9U || number > (maximum - digit) / 10U) { return 0U; }
    number = number * 10U + digit;
  }
  *value = number;
  return 1U;
}

AppUploadAckStatus AppUpload_ParseAck(const char *line, size_t length,
                                     uint32_t expected_sequence, AppUploadAck *ack)
{
  size_t start = 0U, next = 0U, leading = 0U;
  unsigned seen = 0U;
  uint8_t ok = 0U, device_matches = 0U;
  uint64_t sequence = 0U, id = 0U;
  JSONPair_t pair;
  JSONStatus_t result;
  if (!line || !ack || JSON_Validate(line, length) != JSONSuccess) { return APP_UPLOAD_ACK_INVALID; }
  while (leading < length && (line[leading] == ' ' || line[leading] == '\t' || line[leading] == '\r' || line[leading] == '\n')) { ++leading; }
  if (leading == length || line[leading] != '{') { return APP_UPLOAD_ACK_INVALID; }
  while ((result = JSON_Iterate(line, length, &start, &next, &pair)) == JSONSuccess)
  {
    unsigned bit = 0U;
    if (AppUpload_StringEquals(pair.key, pair.keyLength, "ok"))
    {
      bit = 1U;
      if (pair.jsonType != JSONTrue && pair.jsonType != JSONFalse) { return APP_UPLOAD_ACK_INVALID; }
      ok = pair.jsonType == JSONTrue ? 1U : 0U;
    }
    else if (AppUpload_StringEquals(pair.key, pair.keyLength, "device_id"))
    {
      bit = 2U;
      if (pair.jsonType != JSONString) { return APP_UPLOAD_ACK_INVALID; }
      device_matches = AppUpload_StringEquals(pair.value, pair.valueLength, APP_DEVICE_ID);
    }
    else if (AppUpload_StringEquals(pair.key, pair.keyLength, "seq"))
    {
      bit = 4U;
      if (pair.jsonType != JSONNumber || !AppUpload_ParseUnsigned(pair.value, pair.valueLength, UINT32_MAX, &sequence)) { return APP_UPLOAD_ACK_INVALID; }
    }
    else if (AppUpload_StringEquals(pair.key, pair.keyLength, "id"))
    {
      bit = 8U;
      if (pair.jsonType != JSONNumber || !AppUpload_ParseUnsigned(pair.value, pair.valueLength, INT64_MAX, &id) || !id) { return APP_UPLOAD_ACK_INVALID; }
    }
    if (bit && (seen & bit)) { return APP_UPLOAD_ACK_INVALID; }
    seen |= bit;
  }
  if (result != JSONNotFound) { return APP_UPLOAD_ACK_INVALID; }
  if (seen != 15U || !ok || !device_matches || sequence != expected_sequence) { return APP_UPLOAD_ACK_IGNORED; }
  ack->id = id;
  return APP_UPLOAD_ACK_ACCEPTED;
}

/* App owns one transaction and one ACK line, never the hardware TX bytes. */
static AppUploadDiagnostics s_upload;
static uint8_t s_early_ack, s_discard_line, s_attempted, s_window_seen, s_rate_deferred;
static uint64_t s_early_id;
static uint32_t s_sent_ms, s_last_attempt_ms, s_last_window_seq;
static uint8_t s_rx_line[APP_UPLOAD_ACK_CAPACITY], s_rx_chunk[32];
static size_t s_rx_length;

void AppUpload_Init(void)
{
  memset(&s_upload,0,sizeof(s_upload));
  s_early_ack=s_discard_line=s_attempted=s_window_seen=s_rate_deferred=0U;
  s_rx_length=0U; s_sent_ms=s_last_attempt_ms=s_last_window_seq=0U;
  s_upload.init_status=(unsigned)BspDtu_Init();
}
static void AppUpload_ConsumeByte(uint8_t byte)
{
  uint32_t now_ms;
  AppUploadAck ack;
  if(s_discard_line) { if(byte=='\n') { s_discard_line=0U; } return; }
  if(s_rx_length==sizeof(s_rx_line)) {
    s_rx_length=0U; s_discard_line=byte!='\n'; ++s_upload.line_drops; return;
  }
  s_rx_line[s_rx_length++]=byte;
  if(byte!='\n') { return; }
  now_ms=BspTime_GetTickMs();
  if((s_upload.tx_pending || (s_upload.awaiting_ack &&
      (uint32_t)(now_ms-s_sent_ms)<APP_DTU_TIMEOUT_MS)) &&
      AppUpload_ParseAck((const char *)s_rx_line,s_rx_length,s_upload.last_sequence,&ack)==APP_UPLOAD_ACK_ACCEPTED) {
    if(s_upload.tx_pending) { s_early_ack=1U; s_early_id=ack.id; }
    else { s_upload.awaiting_ack=0U; ++s_upload.ack_count; s_upload.ack_sequence=s_upload.last_sequence; s_upload.ack_id=ack.id; }
  }
  s_rx_length=0U;
}
void AppUpload_Process(void)
{
  BspDtuTxResult transmitted=BspDtu_Process();
  uint32_t now_ms;
  unsigned batch;
  if(s_upload.tx_pending && transmitted.finished) {
    s_upload.tx_pending=0U;
    if(transmitted.status==BSP_DTU_OK) {
      s_sent_ms=transmitted.completed_ms; s_upload.awaiting_ack=1U;
      if(s_early_ack) {
        s_upload.awaiting_ack=0U; ++s_upload.ack_count;
        s_upload.ack_sequence=s_upload.last_sequence; s_upload.ack_id=s_early_id;
      }
    } else { ++s_upload.tx_errors; }
    s_early_ack=0U;
  }
  for(batch=0U;batch<8U;++batch) {
    size_t index;
    BspDtuReadResult received=BspDtu_Read(s_rx_chunk,sizeof(s_rx_chunk));
    if(received.data_lost) {
      if(!s_discard_line) { ++s_upload.stream_gaps; }
      s_discard_line=1U; s_rx_length=0U;
    }
    for(index=0U;index<received.length;++index) { AppUpload_ConsumeByte(s_rx_chunk[index]); }
    if(!received.length) { break; }
  }
  /* A TC ISR may have advanced time during Bsp processing: never pair an old
   * caller tick with a newer completed_ms. Always refresh after consumption. */
  now_ms=BspTime_GetTickMs();
  if(s_upload.awaiting_ack && (uint32_t)(now_ms-s_sent_ms)>=APP_DTU_TIMEOUT_MS) {
    ++s_upload.timeouts; s_upload.awaiting_ack=0U;
  }
}
AppUploadSubmitStatus AppUpload_Submit(const AppSensorSnapshot *snapshot)
{
  char *buffer;
  size_t capacity;
  uint32_t now_ms=BspTime_GetTickMs();
  int length;
  if(!snapshot) { ++s_upload.format_errors; return APP_UPLOAD_SUBMIT_ERROR; }
  if(!s_window_seen || snapshot->window_seq!=s_last_window_seq) {
    uint32_t gap=snapshot->window_seq-s_last_window_seq;
    if(gap>1U) { s_upload.skipped_late+=gap-1U; }
    if(s_rate_deferred) { ++s_upload.skipped_rate; s_rate_deferred=0U; }
    s_last_window_seq=snapshot->window_seq; s_window_seen=1U;
  }
  if(s_upload.awaiting_ack || s_upload.tx_pending) { ++s_upload.skipped_busy; return APP_UPLOAD_SUBMIT_BUSY; }
  if(!AppUpload_IsConfigured()) { return APP_UPLOAD_SUBMIT_DISABLED; }
  if(s_attempted && (uint32_t)(now_ms-s_last_attempt_ms)<APP_DTU_PERIOD_MS) {
    s_rate_deferred=1U; return APP_UPLOAD_SUBMIT_RATE_LIMITED;
  }
  buffer=BspDtu_TxBuffer(&capacity);
  if(!buffer) { ++s_upload.skipped_busy; s_rate_deferred=0U; return APP_UPLOAD_SUBMIT_BUSY; }
  s_rate_deferred=0U; s_last_attempt_ms=snapshot->window_end_ms; s_attempted=1U; ++s_upload.last_sequence;
  if(capacity>APP_UPLOAD_FRAME_CAPACITY) { capacity=APP_UPLOAD_FRAME_CAPACITY; }
  length=AppUpload_Format(buffer,capacity,s_upload.last_sequence,now_ms,snapshot);
  if(length<0) { ++s_upload.format_errors; return APP_UPLOAD_SUBMIT_ERROR; }
  s_early_ack=0U; s_upload.tx_pending=1U;
  if(BspDtu_StartSend((size_t)length)!=BSP_DTU_OK) { s_upload.tx_pending=0U; ++s_upload.tx_errors; return APP_UPLOAD_SUBMIT_ERROR; }
  return APP_UPLOAD_SUBMIT_ACCEPTED;
}
const AppUploadDiagnostics *AppUpload_GetDiagnostics(void) { return &s_upload; }
