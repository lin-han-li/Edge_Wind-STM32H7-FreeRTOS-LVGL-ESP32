#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "app_types.h"

typedef enum {
    WIFI_MANAGER_EVENT_CONNECTING = 0,
    WIFI_MANAGER_EVENT_CONNECTED = 1,
    WIFI_MANAGER_EVENT_DISCONNECTED = 2,
    WIFI_MANAGER_EVENT_FAILED = 3,
} wifi_manager_event_id_t;

typedef struct {
    wifi_manager_event_id_t id;
    esp_err_t error;
    int reason_code;
    int rssi_dbm;
    char ip_address[APP_MAX_IP_STR_LEN];
} wifi_manager_event_t;

typedef void (*wifi_manager_event_cb_t)(const wifi_manager_event_t *event, void *ctx);

esp_err_t wifi_manager_init(wifi_manager_event_cb_t callback, void *ctx);
esp_err_t wifi_manager_apply_config(const app_device_config_t *device);
esp_err_t wifi_manager_set_reconnect_backoff_ms(uint32_t backoff_ms);
esp_err_t wifi_manager_connect(void);
esp_err_t wifi_manager_disconnect(void);
bool wifi_manager_is_connected(void);
int wifi_manager_get_rssi_dbm(void);
void wifi_manager_get_ip_string(char *out, size_t out_len);
