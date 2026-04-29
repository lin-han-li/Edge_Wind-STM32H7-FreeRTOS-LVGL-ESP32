#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_MAX_SSID_LEN 64
#define APP_MAX_PASSWORD_LEN 64
#define APP_MAX_HOST_LEN 64
#define APP_MAX_NODE_ID_LEN 64
#define APP_MAX_NODE_LOCATION_LEN 64
#define APP_MAX_HW_VERSION_LEN 32
#define APP_MAX_IP_STR_LEN 16
#define APP_MAX_ERROR_TEXT_LEN 64

#define REPORT_MAX_CHANNELS 4

typedef enum {
    REPORT_MODE_SUMMARY = 0,
    REPORT_MODE_FULL = 1,
} report_mode_t;

typedef enum {
    DEVICE_STATUS_OFFLINE = 0,
    DEVICE_STATUS_ONLINE = 1,
} device_status_t;

typedef struct {
    char wifi_ssid[APP_MAX_SSID_LEN];
    char wifi_password[APP_MAX_PASSWORD_LEN];
    char server_host[APP_MAX_HOST_LEN];
    uint16_t server_port;
    char node_id[APP_MAX_NODE_ID_LEN];
    char node_location[APP_MAX_NODE_LOCATION_LEN];
    char hw_version[APP_MAX_HW_VERSION_LEN];
} app_device_config_t;

typedef struct {
    uint32_t heartbeat_ms;
    uint32_t min_interval_ms;
    uint32_t http_timeout_ms;
    uint32_t reconnect_backoff_ms;
    uint32_t downsample_step;
    uint32_t upload_points;
    uint32_t hardreset_sec;
    uint32_t chunk_kb;
    uint32_t chunk_delay_ms;
} app_comm_params_t;

typedef struct {
    app_device_config_t device;
    app_comm_params_t comm;
} app_config_snapshot_t;

typedef struct {
    bool auto_reconnect_enabled;
    bool last_reporting;
    report_mode_t last_report_mode;
} app_runtime_state_t;

typedef struct {
    bool has_reset;
    bool has_report_mode;
    report_mode_t report_mode;
    bool has_downsample_step;
    uint32_t downsample_step;
    bool has_upload_points;
    uint32_t upload_points;
} server_command_event_t;

typedef struct {
    uint8_t channel_id;
    int32_t value_scaled;
    int32_t current_value_scaled;
    size_t waveform_count;
    int32_t *waveform_scaled;
    size_t fft_count;
    int16_t *fft_tenths;
} report_channel_data_t;

typedef struct {
    uint32_t frame_id;
    uint32_t ref_seq;
    uint64_t timestamp_ms;
    report_mode_t mode;
    device_status_t status;
    char fault_code[8];
    uint32_t downsample_step;
    uint32_t upload_points;
    size_t channel_count;
    report_channel_data_t channels[REPORT_MAX_CHANNELS];
} report_frame_t;

typedef struct {
    bool ready;
    bool wifi_connected;
    bool cloud_connected;
    bool registered_with_cloud;
    bool reporting_enabled;
    report_mode_t report_mode;
    uint32_t session_epoch;
    uint32_t last_frame_id;
    uint32_t downsample_step;
    uint32_t upload_points;
    int last_http_status;
    int rssi_dbm;
    char ip_address[APP_MAX_IP_STR_LEN];
    char node_id[APP_MAX_NODE_ID_LEN];
    char last_error[APP_MAX_ERROR_TEXT_LEN];
} app_status_snapshot_t;
