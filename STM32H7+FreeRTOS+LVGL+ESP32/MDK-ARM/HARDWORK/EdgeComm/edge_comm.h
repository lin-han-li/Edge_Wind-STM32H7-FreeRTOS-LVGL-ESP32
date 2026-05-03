#ifndef __EDGE_COMM_H
#define __EDGE_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>
#include "sd_config.h"
#include "edge_comm_config.h"

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif
#ifndef SERVER_IP
#define SERVER_IP "192.168.10.43"
#endif
#ifndef SERVER_PORT
#define SERVER_PORT 5000
#endif
#ifndef NODE_ID
#define NODE_ID "STM32_H7_Node"
#endif
#ifndef NODE_LOCATION
#define NODE_LOCATION "Lab_Test"
#endif

#ifndef ESP_DEBUG
#define ESP_DEBUG 1
#endif

/* Debug console is USART1. ESP32 communication is SPI only. */
#ifndef ESP_LOG_UART_PORT
#define ESP_LOG_UART_PORT 1
#endif

#ifndef ESP_CONSOLE_ENABLE
#define ESP_CONSOLE_ENABLE 1
#endif

#ifndef ESP_NO_SERVER_RESPONSE_RECOVER_SEC
#define ESP_NO_SERVER_RESPONSE_RECOVER_SEC 6
#endif

#ifndef ESP_HEARTBEAT_INTERVAL_MS
#define ESP_HEARTBEAT_INTERVAL_MS 5000
#endif

#ifndef ESP_MIN_SEND_INTERVAL_MS
#define ESP_MIN_SEND_INTERVAL_MS 200
#endif

#ifndef ESP_HTTP_TIMEOUT_MS_DEFAULT
#define ESP_HTTP_TIMEOUT_MS_DEFAULT 1200
#endif

#ifndef ESP_CHUNK_KB_DEFAULT
#define ESP_CHUNK_KB_DEFAULT 4
#endif

#ifndef ESP_CHUNK_DELAY_MS_DEFAULT
#define ESP_CHUNK_DELAY_MS_DEFAULT 10
#endif

typedef struct {
    uint32_t heartbeat_ms;
    uint32_t min_interval_ms;
    uint32_t http_timeout_ms;
    uint32_t hardreset_sec;
    uint32_t wave_step;
    uint32_t upload_points;
    uint32_t chunk_kb;
    uint32_t chunk_delay_ms;
} ESP_CommParams_t;

void ESP_CommParams_Get(ESP_CommParams_t *out);
void ESP_CommParams_Apply(const ESP_CommParams_t *p);
bool ESP_CommParams_LoadFromSD(void);
bool ESP_CommParams_SaveRuntimeToSD(void);
uint32_t ESP_CommParams_HeartbeatMs(void);
uint32_t ESP_CommParams_MinIntervalMs(void);
uint32_t ESP_CommParams_HttpTimeoutMs(void);
uint32_t ESP_CommParams_HardResetSec(void);
uint32_t ESP_CommParams_WaveStep(void);
uint32_t ESP_CommParams_UploadPoints(void);
uint32_t ESP_CommParams_ChunkKb(void);
uint32_t ESP_CommParams_ChunkDelayMs(void);

bool ESP_AutoReconnect_Read(bool *auto_reconnect_en, bool *last_reporting);
bool ESP_AutoReconnect_SetEnabled(bool auto_reconnect_en);
bool ESP_AutoReconnect_SetLastReporting(bool last_reporting);

bool ESP_Config_LoadFromSD_UIFiles(void);
bool ESP_Config_LoadRuntimeFromSD(void);

#ifndef WAVEFORM_POINTS
#ifdef AD_ACQ_POINTS
#define WAVEFORM_POINTS AD_ACQ_POINTS
#else
#define WAVEFORM_POINTS 1024
#endif
#endif

#ifndef WAVEFORM_SEND_STEP
#define WAVEFORM_SEND_STEP 1
#endif

#ifndef FFT_POINTS
#define FFT_POINTS (WAVEFORM_POINTS / 2)
#endif

typedef struct {
    uint8_t id;
    char label[32];
    char unit[8];
    char type[16];
    float current_value;
    float waveform[WAVEFORM_POINTS];
    float fft_data[FFT_POINTS];
} Channel_Data_t;

extern Channel_Data_t node_channels[4];
extern volatile uint8_t g_esp_ready;

void ESP_Update_Data_And_FFT(void);
void ESP_Post_Data(void);
void ESP_Post_Summary(void);
void ESP_Console_Init(void);
void ESP_Console_Poll(void);
const SystemConfig_t *ESP_Config_Get(void);
void ESP_Config_Apply(const SystemConfig_t *cfg);

typedef enum {
    ESP_UI_CMD_WIFI = 0,
    ESP_UI_CMD_TCP,
    ESP_UI_CMD_REG,
    ESP_UI_CMD_REPORT_TOGGLE,
    ESP_UI_CMD_AUTO_CONNECT,
    ESP_UI_CMD_APPLY_CONFIG,
} esp_ui_cmd_t;

typedef void (*esp_ui_log_hook_t)(const char *line, void *ctx);
typedef void (*esp_ui_step_hook_t)(esp_ui_cmd_t step, bool ok, void *ctx);

void ESP_UI_SetHooks(esp_ui_log_hook_t log_hook, void *log_ctx,
                     esp_ui_step_hook_t step_hook, void *step_ctx);
void ESP_UI_TaskInit(void);
bool ESP_UI_SendCmd(esp_ui_cmd_t cmd);
void ESP_UI_TaskPoll(void);
bool ESP_UI_IsReporting(void);
bool ESP_ServerReportFull(void);
bool ESP_UI_IsWiFiOk(void);
bool ESP_UI_IsTcpOk(void);
bool ESP_UI_IsRegOk(void);
const char *ESP_UI_NodeId(void);
void ESP_UI_InvalidateReg(void);

#ifdef __cplusplus
}
#endif

#endif /* __EDGE_COMM_H */
