#include "wifi_manager.h"

#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "lwip/ip4_addr.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT BIT1
#define WIFI_MAX_RETRY_COUNT 10
#define WIFI_SCAN_LOG_MAX_AP 12

static const char *TAG = "wifi_mgr";

static SemaphoreHandle_t s_mutex;
static EventGroupHandle_t s_wifi_event_group;
static wifi_manager_event_cb_t s_callback;
static void *s_callback_ctx;
static esp_netif_t *s_wifi_netif;
static app_device_config_t s_device_cfg;
static bool s_initialized;
static bool s_connected;
static bool s_manual_disconnect;
static int s_retry_count;
static int s_last_disconnect_reason;
static int s_rssi_dbm;
static char s_ip_address[APP_MAX_IP_STR_LEN];
static TimerHandle_t s_reconnect_timer;
static uint32_t s_reconnect_backoff_ms = 3000U;

static void lock_state(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void unlock_state(void)
{
    xSemaphoreGive(s_mutex);
}

static void post_event(wifi_manager_event_id_t id, esp_err_t error, int reason)
{
    wifi_manager_event_t event = {
        .id = id,
        .error = error,
        .reason_code = reason,
        .rssi_dbm = s_rssi_dbm,
    };

    strncpy(event.ip_address, s_ip_address, sizeof(event.ip_address) - 1U);
    event.ip_address[sizeof(event.ip_address) - 1U] = '\0';

    if (s_callback != NULL) {
        s_callback(&event, s_callback_ctx);
    }
}

static void update_rssi_locked(void)
{
    wifi_ap_record_t ap_info;
    if (s_connected && esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        s_rssi_dbm = ap_info.rssi;
    } else if (!s_connected) {
        s_rssi_dbm = 0;
    }
}

static void reconnect_timer_cb(TimerHandle_t timer)
{
    (void) timer;
    (void) esp_wifi_connect();
}

static void log_scan_results(const char *target_ssid)
{
    wifi_scan_config_t scan_cfg = {
        .show_hidden = true,
    };
    wifi_ap_record_t *ap_records = NULL;
    uint16_t ap_count = 0;
    uint16_t ap_count_to_fetch = 0;
    uint16_t ap_log_count = 0;
    esp_err_t err;
    bool found = false;
    uint8_t found_channel = 0;
    int found_rssi = 0;
    wifi_auth_mode_t found_auth = WIFI_AUTH_OPEN;

    err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan start failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan get ap num failed: %s", esp_err_to_name(err));
        return;
    }

    if (ap_count == 0U) {
        ESP_LOGI(TAG, "scan complete, no AP found");
        if (target_ssid != NULL) {
            ESP_LOGI(TAG, "target ssid '%s' NOT FOUND in full scan list", target_ssid);
        }
        return;
    }

    ap_records = calloc(ap_count, sizeof(*ap_records));
    if (ap_records == NULL) {
        ESP_LOGW(TAG, "scan alloc failed for %u AP(s)", (unsigned int)ap_count);
        return;
    }

    ap_count_to_fetch = ap_count;
    err = esp_wifi_scan_get_ap_records(&ap_count_to_fetch, ap_records);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan get records failed: %s", esp_err_to_name(err));
        free(ap_records);
        return;
    }

    ESP_LOGI(TAG, "scan complete, %u AP(s) total", (unsigned int)ap_count_to_fetch);
    ap_log_count = ap_count_to_fetch > WIFI_SCAN_LOG_MAX_AP ? WIFI_SCAN_LOG_MAX_AP : ap_count_to_fetch;
    for (uint16_t i = 0; i < ap_count_to_fetch; ++i) {
        const wifi_ap_record_t *ap = &ap_records[i];
        if (i < ap_log_count) {
            ESP_LOGI(TAG,
                     "AP[%u] ssid=%s ch=%u rssi=%d auth=%u",
                     (unsigned int)i,
                     (const char *)ap->ssid,
                     (unsigned int)ap->primary,
                     (int)ap->rssi,
                     (unsigned int)ap->authmode);
        }
        if (target_ssid != NULL && strcmp((const char *)ap->ssid, target_ssid) == 0) {
            found = true;
            found_channel = ap->primary;
            found_rssi = ap->rssi;
            found_auth = ap->authmode;
        }
    }
    if (ap_count_to_fetch > ap_log_count) {
        ESP_LOGI(TAG, "... truncated log, %u more AP(s) not printed", (unsigned int)(ap_count_to_fetch - ap_log_count));
    }

    if (target_ssid != NULL) {
        if (found) {
            ESP_LOGI(TAG,
                     "target ssid '%s' FOUND in full scan list: ch=%u rssi=%d auth=%u",
                     target_ssid,
                     (unsigned int)found_channel,
                     found_rssi,
                     (unsigned int)found_auth);
        } else {
            ESP_LOGI(TAG, "target ssid '%s' NOT FOUND in full scan list", target_ssid);
        }
    }

    free(ap_records);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        lock_state();
        s_retry_count = 0;
        unlock_state();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disc = (const wifi_event_sta_disconnected_t *) event_data;
        bool reconnect = false;

        lock_state();
        s_connected = false;
        s_last_disconnect_reason = disc != NULL ? disc->reason : 0;
        s_ip_address[0] = '\0';

        if (!s_manual_disconnect && s_retry_count < WIFI_MAX_RETRY_COUNT) {
            s_retry_count++;
            reconnect = true;
        }
        unlock_state();

        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        ESP_LOGW(TAG,
                 "STA disconnected, reason=%d retry=%d manual=%d",
                 s_last_disconnect_reason,
                 reconnect ? 1 : 0,
                 s_manual_disconnect ? 1 : 0);

        if (reconnect) {
            post_event(WIFI_MANAGER_EVENT_CONNECTING, ESP_OK, s_last_disconnect_reason);
            if (s_reconnect_timer != NULL) {
                TickType_t delay_ticks = pdMS_TO_TICKS(s_reconnect_backoff_ms);
                if (delay_ticks == 0U) {
                    delay_ticks = 1U;
                }
                (void) xTimerStop(s_reconnect_timer, 0);
                (void) xTimerChangePeriod(s_reconnect_timer, delay_ticks, 0);
                (void) xTimerStart(s_reconnect_timer, 0);
            }
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAILED_BIT);
            post_event(s_manual_disconnect ? WIFI_MANAGER_EVENT_DISCONNECTED : WIFI_MANAGER_EVENT_FAILED,
                       ESP_FAIL,
                       s_last_disconnect_reason);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *) event_data;

        lock_state();
        s_connected = true;
        s_retry_count = 0;
        s_manual_disconnect = false;
        if (evt != NULL) {
            ip4addr_ntoa_r((const ip4_addr_t *) &evt->ip_info.ip, s_ip_address, sizeof(s_ip_address));
        }
        update_rssi_locked();
        unlock_state();
        if (s_reconnect_timer != NULL) {
            (void) xTimerStop(s_reconnect_timer, 0);
        }

        ESP_LOGI(TAG, "STA got IP %s, RSSI=%d", s_ip_address, s_rssi_dbm);

        xEventGroupClearBits(s_wifi_event_group, WIFI_FAILED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        post_event(WIFI_MANAGER_EVENT_CONNECTED, ESP_OK, 0);
    }
}

esp_err_t wifi_manager_init(wifi_manager_event_cb_t callback, void *ctx)
{
    esp_err_t err;
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_event_handler_instance_t any_id;
    esp_event_handler_instance_t got_ip;

    if (s_initialized) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    s_wifi_event_group = xEventGroupCreate();
    s_reconnect_timer = xTimerCreate("wifi_reconnect",
                                     pdMS_TO_TICKS(s_reconnect_backoff_ms),
                                     pdFALSE,
                                     NULL,
                                     reconnect_timer_cb);
    if (s_mutex == NULL || s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (s_reconnect_timer == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_callback = callback;
    s_callback_ctx = ctx;

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_wifi_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi_netif == NULL) {
        return ESP_FAIL;
    }

    err = esp_wifi_init(&wifi_init_cfg);
    if (err != ESP_OK) {
        return err;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_manager_apply_config(const app_device_config_t *device)
{
    if (!s_initialized || device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    lock_state();
    s_device_cfg = *device;
    unlock_state();
    return ESP_OK;
}

esp_err_t wifi_manager_connect(void)
{
    wifi_config_t cfg = { 0 };
    esp_err_t err = ESP_OK;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    lock_state();
    if (s_device_cfg.wifi_ssid[0] == '\0') {
        unlock_state();
        return ESP_ERR_INVALID_ARG;
    }

    strncpy((char *) cfg.sta.ssid, s_device_cfg.wifi_ssid, sizeof(cfg.sta.ssid) - 1U);
    strncpy((char *) cfg.sta.password, s_device_cfg.wifi_password, sizeof(cfg.sta.password) - 1U);
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    s_manual_disconnect = false;
    s_retry_count = 0;
    unlock_state();
    if (s_reconnect_timer != NULL) {
        (void) xTimerStop(s_reconnect_timer, 0);
    }

    ESP_LOGI(TAG, "connect request ssid='%s'", (const char *)cfg.sta.ssid);
    log_scan_results((const char *)cfg.sta.ssid);

    for (int attempt = 0; attempt < 3; ++attempt) {
        err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
        if (err != ESP_ERR_WIFI_STATE) {
            break;
        }
        ESP_LOGW(TAG, "set_config while connecting, forcing disconnect and retry");
        (void) esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config failed: %s", esp_err_to_name(err));
        post_event(WIFI_MANAGER_EVENT_FAILED, err, 0);
        return err;
    }

    post_event(WIFI_MANAGER_EVENT_CONNECTING, ESP_OK, 0);
    err = esp_wifi_connect();
    ESP_LOGI(TAG, "esp_wifi_connect -> %s", esp_err_to_name(err));
    if (err == ESP_ERR_WIFI_CONN) {
        return ESP_OK;
    }
    return err;
}

esp_err_t wifi_manager_disconnect(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    lock_state();
    s_manual_disconnect = true;
    unlock_state();
    if (s_reconnect_timer != NULL) {
        (void) xTimerStop(s_reconnect_timer, 0);
    }
    return esp_wifi_disconnect();
}

esp_err_t wifi_manager_set_reconnect_backoff_ms(uint32_t backoff_ms)
{
    TickType_t delay_ticks;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (backoff_ms < 500U) {
        backoff_ms = 500U;
    }
    s_reconnect_backoff_ms = backoff_ms;
    delay_ticks = pdMS_TO_TICKS(s_reconnect_backoff_ms);
    if (delay_ticks == 0U) {
        delay_ticks = 1U;
    }
    return (s_reconnect_timer != NULL && xTimerChangePeriod(s_reconnect_timer, delay_ticks, pdMS_TO_TICKS(100)) == pdPASS)
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

bool wifi_manager_is_connected(void)
{
    bool connected;

    if (!s_initialized) {
        return false;
    }
    lock_state();
    connected = s_connected;
    unlock_state();
    return connected;
}

int wifi_manager_get_rssi_dbm(void)
{
    int rssi;

    if (!s_initialized) {
        return 0;
    }
    lock_state();
    update_rssi_locked();
    rssi = s_rssi_dbm;
    unlock_state();
    return rssi;
}

void wifi_manager_get_ip_string(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0U) {
        return;
    }

    lock_state();
    strncpy(out, s_ip_address, out_len - 1U);
    out[out_len - 1U] = '\0';
    unlock_state();
}
