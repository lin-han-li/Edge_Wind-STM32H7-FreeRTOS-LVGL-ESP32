#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "app_types.h"

typedef enum {
    CLOUD_CLIENT_EVENT_REGISTER_RESULT = 1,
    CLOUD_CLIENT_EVENT_REPORT_RESULT = 2,
    CLOUD_CLIENT_EVENT_SERVER_COMMAND = 3,
    CLOUD_CLIENT_EVENT_ERROR = 4,
    CLOUD_CLIENT_EVENT_HEARTBEAT_RESULT = 5,
} cloud_client_event_id_t;

typedef struct {
    cloud_client_event_id_t id;
    esp_err_t error;
    int http_status;
    uint32_t ref_seq;
    uint32_t frame_id;
    server_command_event_t server_command;
    char message[64];
} cloud_client_event_t;

typedef void (*cloud_client_event_cb_t)(const cloud_client_event_t *event, void *ctx);

esp_err_t cloud_client_init(cloud_client_event_cb_t callback, void *ctx);
esp_err_t cloud_client_apply_snapshot(const app_config_snapshot_t *snapshot);
esp_err_t cloud_client_request_register(void);
esp_err_t cloud_client_set_reporting(bool enabled, report_mode_t mode);
esp_err_t cloud_client_submit_frame(report_frame_t *frame);
esp_err_t cloud_client_notify_wifi_state(bool connected);
size_t cloud_client_drop_pending_summary_frames(void);
bool cloud_client_is_registered(void);
