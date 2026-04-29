#include "cloud_client.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "report_buffer.h"
#include "report_codec.h"
#include "wifi_manager.h"

static const char *TAG = "cloud_client";

#define CLOUD_TASK_STACK_SIZE 12288
#define CLOUD_TASK_PRIORITY 5
#define CLOUD_QUEUE_LENGTH 32
#define CLOUD_JSON_SCRATCH_LEN 2048
#define CLOUD_RESPONSE_MAX_LEN 1024
#define CLOUD_LOOP_POLL_MS 200
#define CLOUD_SUBMIT_QUEUE_TIMEOUT_MS 20
#define CLOUD_SUMMARY_COALESCE_THRESHOLD 4
#define CLOUD_FULL_HTTP_TIMEOUT_MS 3000U
#define CLOUD_FULL_HTTP_TOTAL_BUDGET_MS 6000U
#define CLOUD_REPORT_REREGISTER_FAIL_STREAK 3U
#define CLOUD_REPORT_WIFI_RECOVER_FAIL_STREAK 3U
#define CLOUD_REPORT_WIFI_RECOVER_COOLDOWN_MS 45000U
#define CLOUD_REPORT_WIFI_RECONNECT_SETTLE_MS 500U

typedef enum {
    CLOUD_MSG_APPLY_SNAPSHOT = 1,
    CLOUD_MSG_REGISTER = 2,
    CLOUD_MSG_SET_REPORTING = 3,
    CLOUD_MSG_SUBMIT_FRAME = 4,
    CLOUD_MSG_NOTIFY_WIFI = 5,
} cloud_msg_type_t;

typedef struct {
    cloud_msg_type_t type;
    union {
        app_config_snapshot_t snapshot;
        struct {
            bool enabled;
            report_mode_t mode;
        } reporting;
        report_frame_t *frame;
        bool wifi_connected;
    } data;
} cloud_msg_t;

static QueueHandle_t s_queue;
static cloud_client_event_cb_t s_callback;
static void *s_callback_ctx;
static bool s_registered;
static int64_t s_last_request_us;
static uint32_t s_report_transport_fail_streak;
static int64_t s_last_wifi_recover_us;

static void post_event(cloud_client_event_id_t id,
                       esp_err_t error,
                       int http_status,
                       uint32_t ref_seq,
                       uint32_t frame_id,
                       const server_command_event_t *server_command,
                       const char *message)
{
    cloud_client_event_t event = {
        .id = id,
        .error = error,
        .http_status = http_status,
        .ref_seq = ref_seq,
        .frame_id = frame_id,
    };

    if (server_command != NULL) {
        event.server_command = *server_command;
    }
    if (message != NULL) {
        strncpy(event.message, message, sizeof(event.message) - 1U);
    }
    if (s_callback != NULL) {
        s_callback(&event, s_callback_ctx);
    }
}

static void format_err_message(char *buffer, size_t buffer_len, const char *prefix, esp_err_t err)
{
    if (buffer == NULL || buffer_len == 0U) {
        return;
    }
    snprintf(buffer, buffer_len, "%s:%s(0x%x)", prefix, esp_err_to_name(err), (unsigned int) err);
}

static esp_err_t queue_cloud_msg(const cloud_msg_t *msg, TickType_t timeout)
{
    if (s_queue == NULL || msg == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return (xQueueSend(s_queue, msg, timeout) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t read_response_body(esp_http_client_handle_t client, char *buffer, size_t buffer_len, int *out_http_status)
{
    size_t total = 0;
    int read_len;

    if (buffer == NULL || buffer_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    while ((read_len = esp_http_client_read(client, buffer + total, (int) (buffer_len - total - 1U))) > 0) {
        total += (size_t) read_len;
        if (total >= buffer_len - 1U) {
            break;
        }
    }
    if (read_len < 0) {
        buffer[total] = '\0';
        return ESP_FAIL;
    }
    buffer[total] = '\0';

    if (out_http_status != NULL) {
        *out_http_status = esp_http_client_get_status_code(client);
    }
    return ESP_OK;
}

static void touch_request_timestamp(void)
{
    s_last_request_us = esp_timer_get_time();
}

static void clear_request_timestamp(void)
{
    s_last_request_us = 0;
}

static void apply_request_interval(const app_config_snapshot_t *snapshot)
{
    int64_t now_us;
    int64_t delta_us;
    int64_t min_interval_us;

    if (snapshot == NULL || snapshot->comm.min_interval_ms == 0U || s_last_request_us <= 0) {
        return;
    }

    now_us = esp_timer_get_time();
    delta_us = now_us - s_last_request_us;
    min_interval_us = (int64_t) snapshot->comm.min_interval_ms * 1000LL;
    if (delta_us < min_interval_us) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t) ((min_interval_us - delta_us) / 1000LL)));
    }
}

static esp_err_t post_register_request(const app_config_snapshot_t *snapshot)
{
    esp_err_t err;
    char *body = NULL;
    size_t body_len = 0;
    char url[160];
    char response[CLOUD_RESPONSE_MAX_LEN];
    int http_status = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = (int) snapshot->comm.http_timeout_ms,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client;

    if (!wifi_manager_is_connected()) {
        post_event(CLOUD_CLIENT_EVENT_REGISTER_RESULT, ESP_ERR_INVALID_STATE, 0, 0, 0, NULL, "wifi_not_connected");
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(url, sizeof(url), "http://%s:%u/api/register", snapshot->device.server_host, snapshot->device.server_port);

    err = report_codec_build_register_json(snapshot, &body, &body_len);
    if (err != ESP_OK) {
        post_event(CLOUD_CLIENT_EVENT_ERROR, err, 0, 0, 0, NULL, "build_register_json_failed");
        return err;
    }

    client = esp_http_client_init(&cfg);
    if (client == NULL) {
        free(body);
        post_event(CLOUD_CLIENT_EVENT_ERROR, ESP_ERR_NO_MEM, 0, 0, 0, NULL, "http_client_init_failed");
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int) body_len);
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        err = read_response_body(client, response, sizeof(response), &http_status);
        if (err == ESP_OK) {
            s_registered = (http_status >= 200 && http_status < 300);
            touch_request_timestamp();
            post_event(CLOUD_CLIENT_EVENT_REGISTER_RESULT,
                       ESP_OK,
                       http_status,
                       0,
                       0,
                       NULL,
                       s_registered ? "register_ok" : "register_http_fail");
        } else {
            char detail[64];
            format_err_message(detail, sizeof(detail), "register_read_fail", err);
            s_registered = false;
            post_event(CLOUD_CLIENT_EVENT_REGISTER_RESULT, err, 0, 0, 0, NULL, detail);
        }
    } else {
        char detail[64];
        format_err_message(detail, sizeof(detail), "register_transport_fail", err);
        s_registered = false;
        post_event(CLOUD_CLIENT_EVENT_REGISTER_RESULT, err, 0, 0, 0, NULL, detail);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(body);
    return err;
}

static uint32_t report_request_timeout_ms(const app_config_snapshot_t *snapshot, const report_frame_t *frame)
{
    uint32_t timeout_ms = snapshot != NULL ? snapshot->comm.http_timeout_ms : 0U;

    if (frame != NULL && frame->mode == REPORT_MODE_FULL) {
        /* Full snapshots are best-effort streaming frames.  A stalled TCP write
         * must not block the whole upload pipeline for 8-10 seconds; drop the
         * stale full frame and let STM32 send a newer snapshot instead. */
        if (timeout_ms == 0U || timeout_ms > CLOUD_FULL_HTTP_TIMEOUT_MS) {
            timeout_ms = CLOUD_FULL_HTTP_TIMEOUT_MS;
        }
        return timeout_ms;
    }

    if (timeout_ms == 0U) {
        timeout_ms = CLOUD_FULL_HTTP_TIMEOUT_MS;
    }
    return timeout_ms;
}

static void log_report_request_result(const char *stage,
                                      const report_frame_t *frame,
                                      size_t payload_len,
                                      esp_err_t err,
                                      int http_status,
                                      int64_t total_ms,
                                      int64_t open_ms,
                                      int64_t stream_ms,
                                      int64_t fetch_ms,
                                      int64_t read_ms)
{
    ESP_LOGI(TAG,
             "report stage=%s frame=%" PRIu32 " ref=%" PRIu32 " mode=%u len=%u err=%s(0x%x) http=%d ms total=%lld open=%lld stream=%lld fetch=%lld read=%lld heap=%u min=%u largest=%u stack=%u q=%u fail_streak=%u",
             stage != NULL ? stage : "unknown",
             frame != NULL ? frame->frame_id : 0U,
             frame != NULL ? frame->ref_seq : 0U,
             frame != NULL ? (unsigned int) frame->mode : 0U,
             (unsigned int) payload_len,
             esp_err_to_name(err),
             (unsigned int) err,
             http_status,
             (long long) total_ms,
             (long long) open_ms,
             (long long) stream_ms,
             (long long) fetch_ms,
             (long long) read_ms,
             (unsigned int) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int) uxTaskGetStackHighWaterMark(NULL),
              (unsigned int) (s_queue != NULL ? uxQueueMessagesWaiting(s_queue) : 0U),
              (unsigned int) s_report_transport_fail_streak);
}

static void maybe_force_wifi_recover(const report_frame_t *frame, esp_err_t err, int http_status)
{
    int64_t now_us;
    int64_t cooldown_us;
    esp_err_t reconnect_err;

    if (s_report_transport_fail_streak < CLOUD_REPORT_WIFI_RECOVER_FAIL_STREAK) {
        return;
    }
    if (!wifi_manager_is_connected()) {
        return;
    }

    now_us = esp_timer_get_time();
    cooldown_us = (int64_t) CLOUD_REPORT_WIFI_RECOVER_COOLDOWN_MS * 1000LL;
    if (s_last_wifi_recover_us > 0 && (now_us - s_last_wifi_recover_us) < cooldown_us) {
        return;
    }
    s_last_wifi_recover_us = now_us;
    s_registered = false;

    ESP_LOGW(TAG,
             "forcing WiFi/cloud recovery after report failures streak=%u frame=%" PRIu32 " ref=%" PRIu32 " err=%s http=%d",
             (unsigned int) s_report_transport_fail_streak,
             frame != NULL ? frame->frame_id : 0U,
             frame != NULL ? frame->ref_seq : 0U,
             esp_err_to_name(err),
             http_status);

    post_event(CLOUD_CLIENT_EVENT_ERROR,
               err != ESP_OK ? err : ESP_ERR_INVALID_STATE,
               http_status,
               frame != NULL ? frame->ref_seq : 0U,
               frame != NULL ? frame->frame_id : 0U,
               NULL,
               "report_force_wifi_reconnect");

    reconnect_err = wifi_manager_force_reconnect(CLOUD_REPORT_WIFI_RECONNECT_SETTLE_MS);
    if (reconnect_err != ESP_OK) {
        ESP_LOGW(TAG, "force WiFi reconnect request returned %s", esp_err_to_name(reconnect_err));
    }
}

static esp_err_t post_report_request(const app_config_snapshot_t *snapshot,
                                     const report_frame_t *frame,
                                     bool reporting_enabled)
{
    esp_err_t err;
    size_t payload_len = 0;
    char url[160];
    char scratch[CLOUD_JSON_SCRATCH_LEN];
    char response[CLOUD_RESPONSE_MAX_LEN];
    int http_status = 0;
    server_command_event_t server_command;
    esp_http_client_handle_t client = NULL;
    const char *stage = "init";
    uint32_t timeout_ms;
    uint32_t total_budget_ms = 0U;
    int64_t t0_us;
    int64_t t_stage_us;
    int64_t open_ms = 0;
    int64_t stream_ms = 0;
    int64_t fetch_ms = 0;
    int64_t read_ms = 0;

    if (snapshot == NULL || frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    timeout_ms = report_request_timeout_ms(snapshot, frame);
    if (frame->mode == REPORT_MODE_FULL) {
        total_budget_ms = CLOUD_FULL_HTTP_TOTAL_BUDGET_MS;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = (int) timeout_ms,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .keep_alive_enable = false,
    };

    if (!reporting_enabled) {
        post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, ESP_ERR_INVALID_STATE, 0, frame->ref_seq, frame->frame_id, NULL, "reporting_disabled");
        return ESP_ERR_INVALID_STATE;
    }
    if (!wifi_manager_is_connected()) {
        post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, ESP_ERR_INVALID_STATE, 0, frame->ref_seq, frame->frame_id, NULL, "wifi_not_connected");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_registered) {
        err = post_register_request(snapshot);
        if (err != ESP_OK || !s_registered) {
            esp_err_t report_err = (err != ESP_OK) ? err : ESP_FAIL;
            post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT,
                       report_err,
                       0,
                       frame->ref_seq,
                       frame->frame_id,
                       NULL,
                       (err != ESP_OK) ? "register_before_report_failed" : "register_before_report_not_ok");
            return report_err;
        }
    }
    apply_request_interval(snapshot);

    snprintf(url, sizeof(url), "http://%s:%u/api/node/heartbeat", snapshot->device.server_host, snapshot->device.server_port);

    err = report_codec_measure_heartbeat_json(snapshot, frame, &payload_len);
    if (err != ESP_OK) {
        post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, err, 0, frame->ref_seq, frame->frame_id, NULL, "measure_json_failed");
        return err;
    }

    t0_us = esp_timer_get_time();
    ESP_LOGI(TAG,
             "report start frame=%" PRIu32 " ref=%" PRIu32 " mode=%u len=%u timeout=%ums budget=%ums heap=%u largest=%u q=%u",
             frame->frame_id,
             frame->ref_seq,
             (unsigned int) frame->mode,
             (unsigned int) payload_len,
             (unsigned int) timeout_ms,
             (unsigned int) total_budget_ms,
             (unsigned int) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int) (s_queue != NULL ? uxQueueMessagesWaiting(s_queue) : 0U));

    client = esp_http_client_init(&cfg);
    if (client == NULL) {
        post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, ESP_ERR_NO_MEM, 0, frame->ref_seq, frame->frame_id, NULL, "http_client_init_failed");
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Connection", "close");

    stage = "open";
    t_stage_us = esp_timer_get_time();
    err = esp_http_client_open(client, (int) payload_len);
    open_ms = (esp_timer_get_time() - t_stage_us) / 1000LL;
    if (err != ESP_OK) {
        char detail[64];
        format_err_message(detail, sizeof(detail), "report_open_fail", err);
        post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, err, 0, frame->ref_seq, frame->frame_id, NULL, detail);
        goto cleanup;
    }

    stage = "stream";
    t_stage_us = esp_timer_get_time();
    err = report_codec_stream_heartbeat_json(snapshot, frame, scratch, sizeof(scratch), client, total_budget_ms);
    stream_ms = (esp_timer_get_time() - t_stage_us) / 1000LL;
    if (err != ESP_OK) {
        char detail[64];
        format_err_message(detail, sizeof(detail), "report_stream_fail", err);
        post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, err, 0, frame->ref_seq, frame->frame_id, NULL, detail);
        goto cleanup;
    }

    stage = "fetch";
    t_stage_us = esp_timer_get_time();
    err = (esp_err_t) esp_http_client_fetch_headers(client);
    fetch_ms = (esp_timer_get_time() - t_stage_us) / 1000LL;
    if (err < 0) {
        char detail[64];
        format_err_message(detail, sizeof(detail), "report_fetch_fail", err);
        post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, err, 0, frame->ref_seq, frame->frame_id, NULL, detail);
        goto cleanup;
    }
    err = ESP_OK;

    stage = "read";
    t_stage_us = esp_timer_get_time();
    err = read_response_body(client, response, sizeof(response), &http_status);
    read_ms = (esp_timer_get_time() - t_stage_us) / 1000LL;
    if (err == ESP_OK) {
        post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT,
                   ESP_OK,
                   http_status,
                   frame->ref_seq,
                   frame->frame_id,
                   NULL,
                   (http_status >= 200 && http_status < 300) ? "report_ok" : "report_http_fail");

        if (report_codec_parse_server_command(response, &server_command)) {
            post_event(CLOUD_CLIENT_EVENT_SERVER_COMMAND,
                       ESP_OK,
                       http_status,
                       frame->ref_seq,
                       frame->frame_id,
                       &server_command,
                       "server_command");
        }
        if (http_status == 401 || http_status == 404) {
            s_registered = false;
        }
    } else {
        char detail[64];
        format_err_message(detail, sizeof(detail), "report_read_fail", err);
        post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, err, 0, frame->ref_seq, frame->frame_id, NULL, detail);
    }

cleanup:
    if (client != NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    if (err == ESP_OK && http_status >= 200 && http_status < 300) {
        s_report_transport_fail_streak = 0;
        touch_request_timestamp();
    } else if (err != ESP_OK || http_status == 0 || http_status >= 500) {
        /*
         * Do not let a failed full POST suppress all cloud traffic until the
         * next full frame.  Clearing the timestamp lets the idle branch send a
         * lightweight register/heartbeat during STM32's recovery holdoff, so
         * the node remains online even while full snapshots are being skipped.
         */
        clear_request_timestamp();
        if (s_report_transport_fail_streak < 1000000U) {
            ++s_report_transport_fail_streak;
        }
        if (s_report_transport_fail_streak >= CLOUD_REPORT_REREGISTER_FAIL_STREAK) {
            s_registered = false;
            ESP_LOGW(TAG,
                     "forcing re-register after report failures streak=%u err=%s http=%d",
                     (unsigned int) s_report_transport_fail_streak,
                      esp_err_to_name(err),
                      http_status);
        }
        maybe_force_wifi_recover(frame, err, http_status);
    } else {
        touch_request_timestamp();
    }

    log_report_request_result(stage,
                              frame,
                              payload_len,
                              err,
                              http_status,
                              (esp_timer_get_time() - t0_us) / 1000LL,
                              open_ms,
                              stream_ms,
                              fetch_ms,
                              read_ms);
    return err;
}

static esp_err_t post_empty_heartbeat_request(const app_config_snapshot_t *snapshot,
                                              device_status_t status,
                                              const char *fault_code)
{
    esp_err_t err;
    char *body = NULL;
    size_t body_len = 0;
    char url[160];
    char response[CLOUD_RESPONSE_MAX_LEN];
    int http_status = 0;
    server_command_event_t server_command;
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = (int) snapshot->comm.http_timeout_ms,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client;

    if (!wifi_manager_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_registered) {
        err = post_register_request(snapshot);
        if (err != ESP_OK || !s_registered) {
            return err != ESP_OK ? err : ESP_FAIL;
        }
    }

    apply_request_interval(snapshot);
    snprintf(url, sizeof(url), "http://%s:%u/api/node/heartbeat", snapshot->device.server_host, snapshot->device.server_port);

    err = report_codec_build_empty_heartbeat_json(snapshot, status, fault_code, &body, &body_len);
    if (err != ESP_OK) {
        post_event(CLOUD_CLIENT_EVENT_ERROR, err, 0, 0, 0, NULL, "build_empty_heartbeat_failed");
        return err;
    }

    client = esp_http_client_init(&cfg);
    if (client == NULL) {
        free(body);
        post_event(CLOUD_CLIENT_EVENT_ERROR, ESP_ERR_NO_MEM, 0, 0, 0, NULL, "http_client_init_failed");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int) body_len);
    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        err = read_response_body(client, response, sizeof(response), &http_status);
        if (err == ESP_OK) {
            if (report_codec_parse_server_command(response, &server_command)) {
                post_event(CLOUD_CLIENT_EVENT_SERVER_COMMAND, ESP_OK, http_status, 0, 0, &server_command, "server_command");
            }
            if (http_status == 401 || http_status == 404) {
                s_registered = false;
            }
            if (http_status >= 200 && http_status < 300) {
                post_event(CLOUD_CLIENT_EVENT_HEARTBEAT_RESULT, ESP_OK, http_status, 0, 0, NULL, "heartbeat_ok");
            } else {
                post_event(CLOUD_CLIENT_EVENT_ERROR, ESP_OK, http_status, 0, 0, NULL, "heartbeat_http_fail");
            }
        } else {
            char detail[64];
            format_err_message(detail, sizeof(detail), "heartbeat_read_fail", err);
            post_event(CLOUD_CLIENT_EVENT_ERROR, err, 0, 0, 0, NULL, detail);
        }
    } else {
        char detail[64];
        format_err_message(detail, sizeof(detail), "heartbeat_transport_fail", err);
        post_event(CLOUD_CLIENT_EVENT_ERROR, err, 0, 0, 0, NULL, detail);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(body);
    touch_request_timestamp();
    return err;
}

static void cloud_task(void *arg)
{
    cloud_msg_t msg;
    app_config_snapshot_t snapshot = { 0 };
    bool reporting_enabled = false;
    report_mode_t report_mode = REPORT_MODE_SUMMARY;
    bool wifi_connected = false;
    device_status_t last_status = DEVICE_STATUS_ONLINE;
    char last_fault_code[8] = "E00";
    BaseType_t received;

    for (;;) {
        received = xQueueReceive(s_queue, &msg, pdMS_TO_TICKS(CLOUD_LOOP_POLL_MS));
        if (received != pdTRUE) {
            if (reporting_enabled &&
                report_mode != REPORT_MODE_FULL &&
                wifi_connected &&
                snapshot.comm.heartbeat_ms > 0U &&
                (s_last_request_us == 0 || (esp_timer_get_time() - s_last_request_us) >= ((int64_t) snapshot.comm.heartbeat_ms * 1000LL))) {
                (void) post_empty_heartbeat_request(&snapshot, last_status, last_fault_code);
            }
            continue;
        }

        switch (msg.type) {
        case CLOUD_MSG_APPLY_SNAPSHOT:
            snapshot = msg.data.snapshot;
            break;

        case CLOUD_MSG_NOTIFY_WIFI:
            wifi_connected = msg.data.wifi_connected;
            if (!wifi_connected) {
                s_registered = false;
            } else if (reporting_enabled && !s_registered) {
                (void) post_register_request(&snapshot);
            }
            break;

        case CLOUD_MSG_REGISTER:
            if (wifi_connected) {
                (void) post_register_request(&snapshot);
            } else {
                post_event(CLOUD_CLIENT_EVENT_REGISTER_RESULT, ESP_ERR_INVALID_STATE, 0, 0, 0, NULL, "register_before_wifi");
            }
            break;

        case CLOUD_MSG_SET_REPORTING:
            reporting_enabled = msg.data.reporting.enabled;
            report_mode = msg.data.reporting.mode;
            if (reporting_enabled && s_last_request_us == 0) {
                touch_request_timestamp();
            }
            if (reporting_enabled && wifi_connected && !s_registered) {
                (void) post_register_request(&snapshot);
            }
            break;

        case CLOUD_MSG_SUBMIT_FRAME:
            if (msg.data.frame != NULL) {
                last_status = msg.data.frame->status;
                strncpy(last_fault_code, msg.data.frame->fault_code, sizeof(last_fault_code) - 1U);
                last_fault_code[sizeof(last_fault_code) - 1U] = '\0';
                (void) post_report_request(&snapshot, msg.data.frame, reporting_enabled);
                report_frame_free(msg.data.frame);
            }
            break;

        default:
            break;
        }
    }
}

esp_err_t cloud_client_init(cloud_client_event_cb_t callback, void *ctx)
{
    s_callback = callback;
    s_callback_ctx = ctx;
    s_queue = xQueueCreate(CLOUD_QUEUE_LENGTH, sizeof(cloud_msg_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return (xTaskCreate(cloud_task, "cloud_task", CLOUD_TASK_STACK_SIZE, NULL, CLOUD_TASK_PRIORITY, NULL) == pdPASS)
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

esp_err_t cloud_client_apply_snapshot(const app_config_snapshot_t *snapshot)
{
    cloud_msg_t msg = {
        .type = CLOUD_MSG_APPLY_SNAPSHOT,
    };

    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    msg.data.snapshot = *snapshot;
    return queue_cloud_msg(&msg, pdMS_TO_TICKS(1000));
}

esp_err_t cloud_client_request_register(void)
{
    cloud_msg_t msg = {
        .type = CLOUD_MSG_REGISTER,
    };
    return queue_cloud_msg(&msg, pdMS_TO_TICKS(1000));
}

esp_err_t cloud_client_set_reporting(bool enabled, report_mode_t mode)
{
    cloud_msg_t msg = {
        .type = CLOUD_MSG_SET_REPORTING,
    };
    esp_err_t err;

    msg.data.reporting.enabled = enabled;
    msg.data.reporting.mode = mode;
    if (!enabled) {
        (void) cloud_client_drop_pending_summary_frames();
    }
    err = queue_cloud_msg(&msg, pdMS_TO_TICKS(1000));
    if (err != ESP_OK && !enabled) {
        (void) cloud_client_drop_pending_summary_frames();
        err = queue_cloud_msg(&msg, pdMS_TO_TICKS(1000));
    }
    return err;
}

esp_err_t cloud_client_submit_frame(report_frame_t *frame)
{
    cloud_msg_t msg = {
        .type = CLOUD_MSG_SUBMIT_FRAME,
    };
    esp_err_t err;
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    msg.data.frame = frame;

    if (frame->mode == REPORT_MODE_SUMMARY &&
        s_queue != NULL &&
        uxQueueMessagesWaiting(s_queue) > CLOUD_SUMMARY_COALESCE_THRESHOLD) {
        (void) cloud_client_drop_pending_summary_frames();
    }

    err = queue_cloud_msg(&msg, pdMS_TO_TICKS(CLOUD_SUBMIT_QUEUE_TIMEOUT_MS));
    if (err != ESP_OK && frame->mode == REPORT_MODE_SUMMARY) {
        (void) cloud_client_drop_pending_summary_frames();
        err = queue_cloud_msg(&msg, pdMS_TO_TICKS(CLOUD_SUBMIT_QUEUE_TIMEOUT_MS));
    }
    if (err != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

size_t cloud_client_drop_pending_summary_frames(void)
{
    cloud_msg_t msg;
    cloud_msg_t *keep;
    UBaseType_t queued;
    size_t keep_count = 0U;
    size_t dropped = 0U;

    if (s_queue == NULL) {
        return 0U;
    }

    queued = uxQueueMessagesWaiting(s_queue);
    if (queued == 0U) {
        return 0U;
    }

    keep = (cloud_msg_t *) calloc((size_t) queued, sizeof(*keep));
    if (keep == NULL) {
        return 0U;
    }

    for (UBaseType_t i = 0; i < queued; ++i) {
        if (xQueueReceive(s_queue, &msg, 0) != pdTRUE) {
            break;
        }
        if (msg.type == CLOUD_MSG_SUBMIT_FRAME &&
            msg.data.frame != NULL &&
            msg.data.frame->mode == REPORT_MODE_SUMMARY) {
            report_frame_free(msg.data.frame);
            dropped++;
            continue;
        }
        keep[keep_count++] = msg;
    }

    for (size_t i = 0; i < keep_count; ++i) {
        (void) xQueueSendToBack(s_queue, &keep[i], 0);
    }
    free(keep);

    if (dropped > 0U) {
        ESP_LOGI(TAG, "Dropped %u queued summary frames to keep latest report", (unsigned) dropped);
    }
    return dropped;
}

esp_err_t cloud_client_notify_wifi_state(bool connected)
{
    cloud_msg_t msg = {
        .type = CLOUD_MSG_NOTIFY_WIFI,
    };
    msg.data.wifi_connected = connected;
    return queue_cloud_msg(&msg, pdMS_TO_TICKS(250));
}

bool cloud_client_is_registered(void)
{
    return s_registered;
}
