#include "app_config.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define APP_CONFIG_NAMESPACE "coproc"

#if defined(__has_include)
#  if __has_include("app_config_private.h")
#    include "app_config_private.h"
#  endif
#endif

#ifndef APP_DEFAULT_WIFI_SSID
#define APP_DEFAULT_WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef APP_DEFAULT_WIFI_PASSWORD
#define APP_DEFAULT_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif
#ifndef APP_DEFAULT_SERVER_HOST
#define APP_DEFAULT_SERVER_HOST "YOUR_SERVER_HOST_OR_IP"
#endif
#ifndef APP_DEFAULT_SERVER_PORT
#define APP_DEFAULT_SERVER_PORT 8080U
#endif
#ifndef APP_DEFAULT_NODE_ID
#define APP_DEFAULT_NODE_ID "STM32_RTOS_Device"
#endif
#ifndef APP_DEFAULT_NODE_LOCATION
#define APP_DEFAULT_NODE_LOCATION "EdgeWind Lab"
#endif
#ifndef APP_DEFAULT_HW_VERSION
#define APP_DEFAULT_HW_VERSION "ESP32-SPI-Coprocessor"
#endif

static SemaphoreHandle_t s_config_mutex;
static app_config_snapshot_t s_snapshot;
static app_runtime_state_t s_runtime_state;

static void copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static void lock_config(void)
{
    xSemaphoreTake(s_config_mutex, portMAX_DELAY);
}

static void unlock_config(void)
{
    xSemaphoreGive(s_config_mutex);
}

void app_config_sanitize_comm(app_comm_params_t *comm)
{
    if (comm == NULL) {
        return;
    }
    if (comm->heartbeat_ms < 200U) {
        comm->heartbeat_ms = 200U;
    }
    if (comm->http_timeout_ms < 1000U) {
        comm->http_timeout_ms = 1000U;
    }
    if (comm->reconnect_backoff_ms < 500U) {
        comm->reconnect_backoff_ms = 500U;
    }
    if (comm->downsample_step < 1U) {
        comm->downsample_step = 1U;
    }
    if (comm->downsample_step > 64U) {
        comm->downsample_step = 64U;
    }
    if (comm->upload_points < 256U) {
        comm->upload_points = 256U;
    }
    if (comm->upload_points > 4096U) {
        comm->upload_points = 4096U;
    }
    comm->upload_points = ((comm->upload_points + 128U) / 256U) * 256U;
    if (comm->upload_points < 256U) {
        comm->upload_points = 256U;
    }
    if (comm->upload_points > 4096U) {
        comm->upload_points = 4096U;
    }
    if (comm->hardreset_sec < 5U) {
        comm->hardreset_sec = 5U;
    }
    if (comm->hardreset_sec > 600U) {
        comm->hardreset_sec = 600U;
    }
    if (comm->chunk_kb > APP_COMM_CHUNK_KB_MAX) {
        comm->chunk_kb = APP_COMM_CHUNK_KB_MAX;
    }
    if (comm->chunk_delay_ms > APP_COMM_CHUNK_DELAY_MS_MAX) {
        comm->chunk_delay_ms = APP_COMM_CHUNK_DELAY_MS_MAX;
    }
}

void app_config_init_defaults(void)
{
    if (s_config_mutex == NULL) {
        s_config_mutex = xSemaphoreCreateMutex();
        configASSERT(s_config_mutex != NULL);
    }

    memset(&s_snapshot, 0, sizeof(s_snapshot));

    copy_string(s_snapshot.device.wifi_ssid, sizeof(s_snapshot.device.wifi_ssid), APP_DEFAULT_WIFI_SSID);
    copy_string(s_snapshot.device.wifi_password, sizeof(s_snapshot.device.wifi_password), APP_DEFAULT_WIFI_PASSWORD);
    copy_string(s_snapshot.device.server_host, sizeof(s_snapshot.device.server_host), APP_DEFAULT_SERVER_HOST);
    s_snapshot.device.server_port = APP_DEFAULT_SERVER_PORT;
    copy_string(s_snapshot.device.node_id, sizeof(s_snapshot.device.node_id), APP_DEFAULT_NODE_ID);
    copy_string(s_snapshot.device.node_location, sizeof(s_snapshot.device.node_location), APP_DEFAULT_NODE_LOCATION);
    copy_string(s_snapshot.device.hw_version, sizeof(s_snapshot.device.hw_version), APP_DEFAULT_HW_VERSION);

    s_snapshot.comm.heartbeat_ms = 5000U;
    s_snapshot.comm.min_interval_ms = 200U;
    s_snapshot.comm.http_timeout_ms = 15000U;
    s_snapshot.comm.reconnect_backoff_ms = 3000U;
    s_snapshot.comm.downsample_step = 1U;
    s_snapshot.comm.upload_points = 4096U;
    s_snapshot.comm.hardreset_sec = 6U;
    s_snapshot.comm.chunk_kb = 0U;
    s_snapshot.comm.chunk_delay_ms = 0U;
    app_config_sanitize_comm(&s_snapshot.comm);

    s_runtime_state.auto_reconnect_enabled = false;
    s_runtime_state.last_reporting = false;
    s_runtime_state.last_report_mode = REPORT_MODE_SUMMARY;
}

static esp_err_t read_string_from_nvs(nvs_handle_t nvs, const char *key, char *out_buf, size_t out_buf_len)
{
    size_t required = out_buf_len;
    esp_err_t err = nvs_get_str(nvs, key, out_buf, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_INVALID_LENGTH) {
        char *tmp = NULL;
        size_t actual = 0;
        err = nvs_get_str(nvs, key, NULL, &actual);
        if (err != ESP_OK) {
            return err;
        }
        tmp = (char *) malloc(actual);
        if (tmp == NULL) {
            return ESP_ERR_NO_MEM;
        }
        err = nvs_get_str(nvs, key, tmp, &actual);
        if (err == ESP_OK) {
            copy_string(out_buf, out_buf_len, tmp);
        }
        free(tmp);
        return err;
    }
    return err;
}

esp_err_t app_config_load_from_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_CONFIG_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    lock_config();
    (void) read_string_from_nvs(nvs, "wifi_ssid", s_snapshot.device.wifi_ssid, sizeof(s_snapshot.device.wifi_ssid));
    (void) read_string_from_nvs(nvs, "wifi_pass", s_snapshot.device.wifi_password, sizeof(s_snapshot.device.wifi_password));
    (void) read_string_from_nvs(nvs, "server_host", s_snapshot.device.server_host, sizeof(s_snapshot.device.server_host));
    (void) read_string_from_nvs(nvs, "node_id", s_snapshot.device.node_id, sizeof(s_snapshot.device.node_id));
    (void) read_string_from_nvs(nvs, "node_loc", s_snapshot.device.node_location, sizeof(s_snapshot.device.node_location));
    (void) read_string_from_nvs(nvs, "hw_ver", s_snapshot.device.hw_version, sizeof(s_snapshot.device.hw_version));

    (void) nvs_get_u16(nvs, "server_port", &s_snapshot.device.server_port);
    (void) nvs_get_u32(nvs, "hb_ms", &s_snapshot.comm.heartbeat_ms);
    (void) nvs_get_u32(nvs, "min_ms", &s_snapshot.comm.min_interval_ms);
    (void) nvs_get_u32(nvs, "http_ms", &s_snapshot.comm.http_timeout_ms);
    (void) nvs_get_u32(nvs, "reconn_ms", &s_snapshot.comm.reconnect_backoff_ms);
    (void) nvs_get_u32(nvs, "downstep", &s_snapshot.comm.downsample_step);
    (void) nvs_get_u32(nvs, "uppoints", &s_snapshot.comm.upload_points);
    (void) nvs_get_u32(nvs, "hard_sec", &s_snapshot.comm.hardreset_sec);
    (void) nvs_get_u32(nvs, "chunk_kb", &s_snapshot.comm.chunk_kb);
    (void) nvs_get_u32(nvs, "chunk_dly", &s_snapshot.comm.chunk_delay_ms);
    {
        uint8_t auto_reconnect = s_runtime_state.auto_reconnect_enabled ? 1U : 0U;
        uint8_t last_reporting = s_runtime_state.last_reporting ? 1U : 0U;
        uint8_t last_mode = (uint8_t) s_runtime_state.last_report_mode;
        (void) nvs_get_u8(nvs, "auto_rec", &auto_reconnect);
        (void) nvs_get_u8(nvs, "last_rep", &last_reporting);
        (void) nvs_get_u8(nvs, "last_mode", &last_mode);
        s_runtime_state.auto_reconnect_enabled = (auto_reconnect != 0U);
        s_runtime_state.last_reporting = (last_reporting != 0U);
        s_runtime_state.last_report_mode = (last_mode == REPORT_MODE_FULL) ? REPORT_MODE_FULL : REPORT_MODE_SUMMARY;
    }
    app_config_sanitize_comm(&s_snapshot.comm);
    unlock_config();

    nvs_close(nvs);
    return ESP_OK;
}

static esp_err_t persist_locked(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(APP_CONFIG_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, "wifi_ssid", s_snapshot.device.wifi_ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "wifi_pass", s_snapshot.device.wifi_password);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "server_host", s_snapshot.device.server_host);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "node_id", s_snapshot.device.node_id);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "node_loc", s_snapshot.device.node_location);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "hw_ver", s_snapshot.device.hw_version);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(nvs, "server_port", s_snapshot.device.server_port);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "hb_ms", s_snapshot.comm.heartbeat_ms);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "min_ms", s_snapshot.comm.min_interval_ms);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "http_ms", s_snapshot.comm.http_timeout_ms);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "reconn_ms", s_snapshot.comm.reconnect_backoff_ms);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "downstep", s_snapshot.comm.downsample_step);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "uppoints", s_snapshot.comm.upload_points);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "hard_sec", s_snapshot.comm.hardreset_sec);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "chunk_kb", s_snapshot.comm.chunk_kb);
    }
    if (err == ESP_OK) {
        err = nvs_set_u32(nvs, "chunk_dly", s_snapshot.comm.chunk_delay_ms);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "auto_rec", s_runtime_state.auto_reconnect_enabled ? 1U : 0U);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "last_rep", s_runtime_state.last_reporting ? 1U : 0U);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "last_mode", (uint8_t) s_runtime_state.last_report_mode);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

void app_config_get_snapshot(app_config_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return;
    }
    lock_config();
    *out_snapshot = s_snapshot;
    unlock_config();
}

void app_config_get_runtime_state(app_runtime_state_t *out_state)
{
    if (out_state == NULL) {
        return;
    }
    lock_config();
    *out_state = s_runtime_state;
    unlock_config();
}

esp_err_t app_config_update_device(const app_device_config_t *device, bool persist)
{
    esp_err_t err = ESP_OK;

    if (device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    lock_config();
    s_snapshot.device = *device;
    if (persist) {
        err = persist_locked();
    }
    unlock_config();
    return err;
}

esp_err_t app_config_update_comm(const app_comm_params_t *comm, bool persist)
{
    esp_err_t err = ESP_OK;
    app_comm_params_t sanitized;

    if (comm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sanitized = *comm;
    app_config_sanitize_comm(&sanitized);

    lock_config();
    s_snapshot.comm = sanitized;
    if (persist) {
        err = persist_locked();
    }
    unlock_config();
    return err;
}

esp_err_t app_config_update_runtime_state(const app_runtime_state_t *state, bool persist)
{
    esp_err_t err = ESP_OK;
    app_runtime_state_t sanitized;

    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sanitized = *state;
    sanitized.last_report_mode = (sanitized.last_report_mode == REPORT_MODE_FULL) ? REPORT_MODE_FULL : REPORT_MODE_SUMMARY;

    lock_config();
    s_runtime_state = sanitized;
    if (persist) {
        err = persist_locked();
    }
    unlock_config();
    return err;
}
