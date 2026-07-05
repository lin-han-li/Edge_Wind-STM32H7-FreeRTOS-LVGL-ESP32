#include "cloud_client.h"

#include <inttypes.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"

#include "report_buffer.h"
#include "report_codec.h"
#include "wifi_manager.h"

static const char *TAG = "cloud_client";

#define CLOUD_TASK_STACK_SIZE 12288
#define CLOUD_TASK_PRIORITY 5
#define CLOUD_QUEUE_LENGTH 32
#define CLOUD_JSON_SCRATCH_LEN 2048
#define CLOUD_RESPONSE_MAX_LEN 1024
#define CLOUD_FAULT_RESPONSE_MAX_LEN 4096
#define CLOUD_LOOP_POLL_MS 200
#define CLOUD_FAULT_SYNC_INTERVAL_MS 2000U
#define CLOUD_TIME_SYNC_PUSH_INTERVAL_MS 60000U
#define CLOUD_SUBMIT_QUEUE_TIMEOUT_MS 20
#define CLOUD_SUMMARY_COALESCE_THRESHOLD 4
#define CLOUD_FULL_HTTP_TIMEOUT_MIN_MS 2500U
#define CLOUD_FULL_HTTP_TIMEOUT_MAX_MS 3000U
#define CLOUD_FULL_HTTP_TOTAL_BUDGET_MS 5000U
#define CLOUD_REPORT_REREGISTER_FAIL_STREAK 8U
#define CLOUD_REPORT_WIFI_RECOVER_FAIL_STREAK 6U
#define CLOUD_REPORT_FULL_WIFI_RECOVER_FAIL_STREAK 4U
#define CLOUD_REPORT_WIFI_RECOVER_COOLDOWN_MS 15000U
#define CLOUD_REPORT_WIFI_RECONNECT_SETTLE_MS 500U
#define CLOUD_FULL_LOW_HEAP_FREE_BYTES 20000U
#define CLOUD_FULL_LOW_HEAP_LARGEST_BYTES 4096U
#define CLOUD_FULL_RAW_KEEPALIVE_ENABLE 1
#define CLOUD_FULL_RAW_HEADER_MAX_LEN 512U
#define CLOUD_FULL_RAW_RESPONSE_HEADER_MAX_LEN 768U
#define CLOUD_FULL_RAW_IO_TIMEOUT_MS 1200U
#ifndef CLOUD_FULL_RAW_MAX_REUSE
#define CLOUD_FULL_RAW_MAX_REUSE 96U
#endif
#define CLOUD_FULL_RAW_WRITE_BLOCK_ABORT_MS 1200U
#define CLOUD_FULL_RAW_WRITE_RETRY_MAX 0U
#define CLOUD_FULL_RAW_WRITE_RETRY_DELAY_MS 20U

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
static int64_t s_last_fault_sync_us;
static uint32_t s_fault_since_rev;
static uint32_t s_last_pushed_fault_rev;
static bool s_fault_summary_sent_once;
static protocol_fault_summary_payload_t s_latest_fault_summary;
static bool s_latest_fault_summary_valid;
static portMUX_TYPE s_fault_summary_mux = portMUX_INITIALIZER_UNLOCKED;
static protocol_time_sync_payload_t s_latest_time_sync;
static bool s_latest_time_sync_valid;
static portMUX_TYPE s_time_sync_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t s_last_time_sync_push_us;
static int s_full_raw_sock = -1;
static char s_full_raw_host[96];
static uint16_t s_full_raw_port;
static uint32_t s_full_raw_reuse_count;

static uint32_t report_request_timeout_ms(const app_config_snapshot_t *snapshot, const report_frame_t *frame);

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

static void copy_fixed_string(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0U) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return;
    }
    strncpy(dst, src, dst_len - 1U);
    dst[dst_len - 1U] = '\0';
}

static void store_fault_summary(const protocol_fault_summary_payload_t *summary)
{
    if (summary == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_fault_summary_mux);
    s_latest_fault_summary = *summary;
    s_latest_fault_summary_valid = true;
    portEXIT_CRITICAL(&s_fault_summary_mux);
}

bool cloud_client_get_fault_summary(protocol_fault_summary_payload_t *out_summary)
{
    bool valid;

    if (out_summary == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_fault_summary_mux);
    valid = s_latest_fault_summary_valid;
    if (valid) {
        *out_summary = s_latest_fault_summary;
    }
    portEXIT_CRITICAL(&s_fault_summary_mux);
    return valid;
}

static void post_fault_summary_event(const protocol_fault_summary_payload_t *summary)
{
    cloud_client_event_t event = {
        .id = CLOUD_CLIENT_EVENT_FAULT_SUMMARY,
        .error = ESP_OK,
        .http_status = 200,
    };

    if (summary == NULL) {
        return;
    }
    store_fault_summary(summary);
    ESP_LOGI(TAG,
             "fault summary event push rev=%" PRIu32 " count=%u cloud=%u",
             summary->latest_rev,
             (unsigned int) summary->count,
             (unsigned int) summary->cloud_status);
    event.ref_seq = summary->latest_rev;
    event.frame_id = ((uint32_t) summary->count << 8) | summary->cloud_status;
    copy_fixed_string(event.message, sizeof(event.message), "fault_summary");
    if (s_callback != NULL) {
        s_callback(&event, s_callback_ctx);
    }
}

static void store_time_sync(const protocol_time_sync_payload_t *time_sync)
{
    if (time_sync == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_time_sync_mux);
    s_latest_time_sync = *time_sync;
    s_latest_time_sync_valid = true;
    portEXIT_CRITICAL(&s_time_sync_mux);
}

bool cloud_client_get_time_sync(protocol_time_sync_payload_t *out_time)
{
    bool valid;

    if (out_time == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_time_sync_mux);
    valid = s_latest_time_sync_valid;
    if (valid) {
        *out_time = s_latest_time_sync;
    }
    portEXIT_CRITICAL(&s_time_sync_mux);
    return valid;
}

static void post_time_sync_event(const protocol_time_sync_payload_t *time_sync)
{
    cloud_client_event_t event = {
        .id = CLOUD_CLIENT_EVENT_TIME_SYNC,
        .error = ESP_OK,
        .http_status = 200,
    };

    if (time_sync == NULL || time_sync->valid == 0U) {
        return;
    }
    store_time_sync(time_sync);
    ESP_LOGI(TAG,
             "time sync event push unix=%" PRIu32 " offset=%d local=%s",
             time_sync->unix_utc,
             (int) time_sync->tz_offset_minutes,
             time_sync->iso_local);
    event.ref_seq = time_sync->unix_utc;
    event.frame_id = (uint32_t) ((uint16_t) time_sync->tz_offset_minutes);
    copy_fixed_string(event.message, sizeof(event.message), "time_sync");
    if (s_callback != NULL) {
        s_callback(&event, s_callback_ctx);
    }
}

static bool url_encode_component(const char *src, char *dst, size_t dst_len)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t out = 0U;

    if (src == NULL || dst == NULL || dst_len == 0U) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *) src; *p != '\0'; ++p) {
        unsigned char ch = *p;
        bool safe = ((ch >= 'A' && ch <= 'Z') ||
                     (ch >= 'a' && ch <= 'z') ||
                     (ch >= '0' && ch <= '9') ||
                     ch == '-' || ch == '_' || ch == '.' || ch == '~');
        if (safe) {
            if (out + 1U >= dst_len) {
                return false;
            }
            dst[out++] = (char) ch;
        } else {
            if (out + 3U >= dst_len) {
                return false;
            }
            dst[out++] = '%';
            dst[out++] = hex[(ch >> 4) & 0x0F];
            dst[out++] = hex[ch & 0x0F];
        }
    }
    dst[out] = '\0';
    return true;
}

static const char *json_string_or_empty(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) && item->valuestring != NULL ? item->valuestring : "";
}

static uint32_t json_uint_or_zero(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? (uint32_t) item->valuedouble : 0U;
}

static int json_int_or_default(const cJSON *obj, const char *key, int default_value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? (int) item->valuedouble : default_value;
}

static void maybe_post_time_sync_from_response(const cJSON *root)
{
    protocol_time_sync_payload_t time_sync;
    uint32_t unix_utc;
    int offset_minutes;
    int64_t now_us;

    if (root == NULL) {
        return;
    }

    unix_utc = json_uint_or_zero(root, "server_time_unix");
    if (unix_utc < 946684800U) {
        return;
    }

    now_us = esp_timer_get_time();
    if (s_latest_time_sync_valid &&
        s_last_time_sync_push_us != 0 &&
        (now_us - s_last_time_sync_push_us) < ((int64_t) CLOUD_TIME_SYNC_PUSH_INTERVAL_MS * 1000LL)) {
        return;
    }

    offset_minutes = json_int_or_default(root, "server_tz_offset_minutes", 480);
    if (offset_minutes < -720 || offset_minutes > 840) {
        offset_minutes = 480;
    }

    memset(&time_sync, 0, sizeof(time_sync));
    time_sync.unix_utc = unix_utc;
    time_sync.tz_offset_minutes = (int16_t) offset_minutes;
    time_sync.source = 1U;
    time_sync.valid = 1U;
    copy_fixed_string(time_sync.iso_local, sizeof(time_sync.iso_local), json_string_or_empty(root, "server_time_local"));
    post_time_sync_event(&time_sync);
    s_last_time_sync_push_us = now_us;
}

static uint8_t fault_severity_code(const char *text)
{
    if (text == NULL) {
        return PROTOCOL_FAULT_SEVERITY_UNKNOWN;
    }
    if (strcmp(text, "high") == 0 || strcmp(text, "critical") == 0) {
        return PROTOCOL_FAULT_SEVERITY_HIGH;
    }
    if (strcmp(text, "medium") == 0 || strcmp(text, "warning") == 0) {
        return PROTOCOL_FAULT_SEVERITY_MEDIUM;
    }
    if (strcmp(text, "low") == 0 || strcmp(text, "info") == 0) {
        return PROTOCOL_FAULT_SEVERITY_LOW;
    }
    return PROTOCOL_FAULT_SEVERITY_UNKNOWN;
}

static uint8_t fault_state_code(const char *text)
{
    if (text == NULL) {
        return PROTOCOL_FAULT_STATE_UNKNOWN;
    }
    if (strcmp(text, "active") == 0) {
        return PROTOCOL_FAULT_STATE_ACTIVE;
    }
    if (strcmp(text, "acknowledged") == 0) {
        return PROTOCOL_FAULT_STATE_ACKNOWLEDGED;
    }
    if (strcmp(text, "recovered") == 0) {
        return PROTOCOL_FAULT_STATE_RECOVERED;
    }
    if (strcmp(text, "ignored") == 0) {
        return PROTOCOL_FAULT_STATE_IGNORED;
    }
    return PROTOCOL_FAULT_STATE_UNKNOWN;
}

static uint8_t fault_ai_status_code(const char *text)
{
    if (text == NULL || text[0] == '\0' || strcmp(text, "none") == 0) {
        return PROTOCOL_FAULT_AI_NONE;
    }
    if (strcmp(text, "pending") == 0 || strcmp(text, "queued") == 0 || strcmp(text, "running") == 0) {
        return PROTOCOL_FAULT_AI_PENDING;
    }
    if (strcmp(text, "ready") == 0 || strcmp(text, "succeeded") == 0 || strcmp(text, "success") == 0) {
        return PROTOCOL_FAULT_AI_READY;
    }
    if (strcmp(text, "failed") == 0 || strcmp(text, "error") == 0) {
        return PROTOCOL_FAULT_AI_FAILED;
    }
    if (strcmp(text, "disabled") == 0) {
        return PROTOCOL_FAULT_AI_DISABLED;
    }
    if (strcmp(text, "stale") == 0) {
        return PROTOCOL_FAULT_AI_STALE;
    }
    return PROTOCOL_FAULT_AI_NONE;
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

static bool raw_deadline_expired(int64_t deadline_us)
{
    return deadline_us > 0 && esp_timer_get_time() >= deadline_us;
}

static const char *ascii_strcasestr_local(const char *haystack, const char *needle)
{
    size_t needle_len;

    if (haystack == NULL || needle == NULL) {
        return NULL;
    }
    needle_len = strlen(needle);
    if (needle_len == 0U) {
        return haystack;
    }
    for (const char *p = haystack; *p != '\0'; ++p) {
        size_t i = 0U;
        while (i < needle_len && p[i] != '\0' &&
               (char) tolower((unsigned char) p[i]) == (char) tolower((unsigned char) needle[i])) {
            ++i;
        }
        if (i == needle_len) {
            return p;
        }
    }
    return NULL;
}

static void full_raw_close(const char *reason)
{
    if (s_full_raw_sock >= 0) {
        ESP_LOGW(TAG,
                 "full raw socket close reason=%s host=%s port=%u reuse=%u",
                 reason != NULL ? reason : "unknown",
                 s_full_raw_host,
                 (unsigned int) s_full_raw_port,
                 (unsigned int) s_full_raw_reuse_count);
        close(s_full_raw_sock);
        s_full_raw_sock = -1;
    }
    s_full_raw_reuse_count = 0U;
}

static void full_raw_set_timeouts(int sock, uint32_t timeout_ms)
{
    struct timeval tv = {
        .tv_sec = (long) (timeout_ms / 1000U),
        .tv_usec = (long) ((timeout_ms % 1000U) * 1000U),
    };
    int yes = 1;

    (void) setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void) setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    (void) setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
#ifdef TCP_NODELAY
    (void) setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
#endif
}

static uint32_t full_raw_io_timeout_ms(uint32_t timeout_ms)
{
    if (timeout_ms == 0U || timeout_ms > CLOUD_FULL_RAW_IO_TIMEOUT_MS) {
        return CLOUD_FULL_RAW_IO_TIMEOUT_MS;
    }
    return timeout_ms;
}

static esp_err_t full_raw_connect_socket(const char *host, uint16_t port, uint32_t timeout_ms, int *out_sock)
{
    char port_text[8];
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *it = NULL;
    int ret;
    esp_err_t result = ESP_ERR_HTTP_CONNECT;

    if (host == NULL || out_sock == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_sock = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_text, sizeof(port_text), "%u", (unsigned int) port);

    ret = getaddrinfo(host, port_text, &hints, &res);
    if (ret != 0 || res == NULL) {
        ESP_LOGW(TAG, "full raw getaddrinfo failed host=%s port=%u ret=%d", host, (unsigned int) port, ret);
        return ESP_ERR_HTTP_CONNECT;
    }

    for (it = res; it != NULL; it = it->ai_next) {
        int sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        int flags;
        int so_error = 0;
        socklen_t so_error_len = sizeof(so_error);

        if (sock < 0) {
            continue;
        }
        full_raw_set_timeouts(sock, timeout_ms);

        flags = fcntl(sock, F_GETFL, 0);
        if (flags >= 0) {
            (void) fcntl(sock, F_SETFL, flags | O_NONBLOCK);
        }

        ret = connect(sock, it->ai_addr, it->ai_addrlen);
        if (ret < 0 && errno == EINPROGRESS) {
            fd_set wfds;
            struct timeval tv = {
                .tv_sec = (long) (timeout_ms / 1000U),
                .tv_usec = (long) ((timeout_ms % 1000U) * 1000U),
            };
            FD_ZERO(&wfds);
            FD_SET(sock, &wfds);
            ret = select(sock + 1, NULL, &wfds, NULL, &tv);
            if (ret > 0) {
                ret = getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len);
                if (ret == 0 && so_error == 0) {
                    ret = 0;
                } else {
                    errno = so_error;
                    ret = -1;
                }
            } else {
                errno = ETIMEDOUT;
                ret = -1;
            }
        }

        if (flags >= 0) {
            (void) fcntl(sock, F_SETFL, flags);
        }

        if (ret == 0) {
            full_raw_set_timeouts(sock, full_raw_io_timeout_ms(timeout_ms));
            *out_sock = sock;
            result = ESP_OK;
            break;
        }

        close(sock);
    }

    freeaddrinfo(res);
    return result;
}

static esp_err_t full_raw_send_plain(int sock, const void *data, size_t data_len, int64_t deadline_us)
{
    const uint8_t *bytes = (const uint8_t *) data;
    size_t offset = 0U;
#if CLOUD_FULL_RAW_WRITE_RETRY_MAX > 0
    uint32_t retry_count = 0U;
#endif

    if (sock < 0 || (data == NULL && data_len > 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    while (offset < data_len) {
        ssize_t written;
        if (raw_deadline_expired(deadline_us)) {
            return ESP_ERR_TIMEOUT;
        }
        written = send(sock, bytes + offset, data_len - offset, 0);
        if (written <= 0) {
            int saved_errno = errno;
#if CLOUD_FULL_RAW_WRITE_RETRY_MAX == 0
            ESP_LOGW(TAG,
                     "full raw send failed ret=%d errno=%d offset=%u len=%u",
                     (int) written,
                     saved_errno,
                     (unsigned int) offset,
                     (unsigned int) data_len);
            return ESP_FAIL;
#else
            if (retry_count >= CLOUD_FULL_RAW_WRITE_RETRY_MAX) {
                ESP_LOGW(TAG,
                         "full raw send failed ret=%d errno=%d offset=%u len=%u",
                         (int) written,
                         saved_errno,
                         (unsigned int) offset,
                         (unsigned int) data_len);
                return ESP_FAIL;
            }
            ++retry_count;
            vTaskDelay(pdMS_TO_TICKS(CLOUD_FULL_RAW_WRITE_RETRY_DELAY_MS));
            continue;
#endif
        }
        offset += (size_t) written;
#if CLOUD_FULL_RAW_WRITE_RETRY_MAX > 0
        retry_count = 0U;
#endif
    }
    return ESP_OK;
}

static esp_err_t full_raw_write_all(void *ctx,
                                    const app_config_snapshot_t *config,
                                    const void *data,
                                    size_t data_len,
                                    int64_t deadline_us)
{
    int sock = (int) (intptr_t) ctx;
    const uint8_t *bytes = (const uint8_t *) data;
    size_t offset = 0U;
#if CLOUD_FULL_RAW_WRITE_RETRY_MAX > 0
    uint32_t retry_count = 0U;
#endif
    const size_t write_chunk_limit = report_codec_full_binary_write_chunk_limit(config);
    const uint32_t write_delay_ms = report_codec_full_binary_write_delay_ms(config);

    if (sock < 0 || (data == NULL && data_len > 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    while (offset < data_len) {
        size_t write_len = data_len - offset;
        ssize_t written;
#if CLOUD_FULL_RAW_WRITE_RETRY_MAX > 0
        int64_t write_start_us;
#endif
        if (write_len > write_chunk_limit) {
            write_len = write_chunk_limit;
        }
        if (raw_deadline_expired(deadline_us)) {
            return ESP_ERR_TIMEOUT;
        }
#if CLOUD_FULL_RAW_WRITE_RETRY_MAX > 0
        write_start_us = esp_timer_get_time();
#endif
        written = send(sock, bytes + offset, write_len, 0);
        if (written <= 0) {
            int saved_errno = errno;
#if CLOUD_FULL_RAW_WRITE_RETRY_MAX == 0
            ESP_LOGW(TAG,
                     "full raw body send failed ret=%d errno=%d offset=%u len=%u",
                     (int) written,
                     saved_errno,
                     (unsigned int) offset,
                     (unsigned int) data_len);
            return ESP_FAIL;
#else
            int64_t write_ms = (esp_timer_get_time() - write_start_us) / 1000LL;
            if (write_ms >= (int64_t) CLOUD_FULL_RAW_WRITE_BLOCK_ABORT_MS) {
                ESP_LOGW(TAG,
                         "full raw body send stalled ret=%d errno=%d offset=%u len=%u write_ms=%lld limit_ms=%u",
                         (int) written,
                         saved_errno,
                         (unsigned int) offset,
                         (unsigned int) data_len,
                         (long long) write_ms,
                         (unsigned int) CLOUD_FULL_RAW_WRITE_BLOCK_ABORT_MS);
            }
            if (retry_count >= CLOUD_FULL_RAW_WRITE_RETRY_MAX) {
                ESP_LOGW(TAG,
                         "full raw body send failed ret=%d errno=%d offset=%u len=%u retry=%u/%u",
                         (int) written,
                         saved_errno,
                         (unsigned int) offset,
                         (unsigned int) data_len,
                         (unsigned int) retry_count,
                         (unsigned int) CLOUD_FULL_RAW_WRITE_RETRY_MAX);
                return ESP_FAIL;
            }
            ++retry_count;
            vTaskDelay(pdMS_TO_TICKS(CLOUD_FULL_RAW_WRITE_RETRY_DELAY_MS));
            continue;
#endif
        }
        offset += (size_t) written;
#if CLOUD_FULL_RAW_WRITE_RETRY_MAX > 0
        retry_count = 0U;
#endif
        if (write_delay_ms > 0U && offset < data_len) {
            vTaskDelay(pdMS_TO_TICKS(write_delay_ms));
        }
    }

    return ESP_OK;
}

static const char *find_http_header_end(const char *buffer, size_t len)
{
    if (buffer == NULL || len < 4U) {
        return NULL;
    }
    for (size_t i = 0U; i + 3U < len; ++i) {
        if (buffer[i] == '\r' && buffer[i + 1U] == '\n' && buffer[i + 2U] == '\r' && buffer[i + 3U] == '\n') {
            return buffer + i + 4U;
        }
    }
    return NULL;
}

static int parse_http_content_length(const char *headers)
{
    const char *p = ascii_strcasestr_local(headers, "content-length:");
    if (p == NULL) {
        return -1;
    }
    p += strlen("content-length:");
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    return (int) strtol(p, NULL, 10);
}

static esp_err_t full_raw_read_response(int sock,
                                        char *response,
                                        size_t response_len,
                                        int *out_http_status,
                                        bool *out_keepalive)
{
    char headers[CLOUD_FULL_RAW_RESPONSE_HEADER_MAX_LEN];
    size_t used = 0U;
    const char *body_start = NULL;
    int status = 0;
    int content_len;
    size_t copied = 0U;

    if (sock < 0 || response == NULL || response_len == 0U || out_http_status == NULL || out_keepalive == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    response[0] = '\0';
    *out_http_status = 0;
    *out_keepalive = false;

    while (used + 1U < sizeof(headers)) {
        ssize_t n = recv(sock, headers + used, sizeof(headers) - used - 1U, 0);
        if (n <= 0) {
            return ESP_FAIL;
        }
        used += (size_t) n;
        headers[used] = '\0';
        body_start = find_http_header_end(headers, used);
        if (body_start != NULL) {
            break;
        }
    }
    if (body_start == NULL) {
        return ESP_FAIL;
    }

    if (sscanf(headers, "HTTP/%*s %d", &status) != 1) {
        return ESP_FAIL;
    }
    *out_http_status = status;
    *out_keepalive = (ascii_strcasestr_local(headers, "connection: close") == NULL);
    content_len = parse_http_content_length(headers);

    {
        size_t header_len = (size_t) (body_start - headers);
        size_t already = used > header_len ? used - header_len : 0U;
        size_t take = already;
        if (take >= response_len) {
            take = response_len - 1U;
        }
        if (take > 0U) {
            memcpy(response, body_start, take);
            copied = take;
            response[copied] = '\0';
        }
        if (content_len >= 0) {
            size_t consumed = already;
            char drain[128];
            while (consumed < (size_t) content_len) {
                size_t want = (size_t) content_len - consumed;
                ssize_t n;
                if (copied < response_len - 1U) {
                    size_t room = response_len - 1U - copied;
                    if (want > room) {
                        want = room;
                    }
                    n = recv(sock, response + copied, want, 0);
                    if (n <= 0) {
                        return ESP_FAIL;
                    }
                    copied += (size_t) n;
                    consumed += (size_t) n;
                    response[copied] = '\0';
                } else {
                    if (want > sizeof(drain)) {
                        want = sizeof(drain);
                    }
                    n = recv(sock, drain, want, 0);
                    if (n <= 0) {
                        return ESP_FAIL;
                    }
                    consumed += (size_t) n;
                }
            }
        }
    }

    return ESP_OK;
}

static esp_err_t post_full_binary_raw_keepalive(const app_config_snapshot_t *snapshot,
                                                const report_frame_t *frame,
                                                const report_full_binary_info_t *binary_info,
                                                char *response,
                                                size_t response_len,
                                                int *out_http_status,
                                                int64_t *out_open_ms,
                                                int64_t *out_stream_ms,
                                                int64_t *out_read_ms,
                                                const char **out_stage)
{
    char header[CLOUD_FULL_RAW_HEADER_MAX_LEN];
    int header_len;
    int64_t t_stage_us;
    int64_t deadline_us;
    uint32_t timeout_ms;
    uint32_t raw_io_timeout_ms;
    bool keepalive = false;
    esp_err_t err;

    if (snapshot == NULL || frame == NULL || binary_info == NULL || response == NULL ||
        out_http_status == NULL || out_open_ms == NULL || out_stream_ms == NULL ||
        out_read_ms == NULL || out_stage == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_http_status = 0;
    *out_open_ms = 0;
    *out_stream_ms = 0;
    *out_read_ms = 0;
    *out_stage = "raw_open";
    timeout_ms = report_request_timeout_ms(snapshot, frame);
    raw_io_timeout_ms = full_raw_io_timeout_ms(timeout_ms);
    deadline_us = esp_timer_get_time() + ((int64_t) CLOUD_FULL_HTTP_TOTAL_BUDGET_MS * 1000LL);

    t_stage_us = esp_timer_get_time();
    if (s_full_raw_sock >= 0 && s_full_raw_reuse_count >= CLOUD_FULL_RAW_MAX_REUSE) {
        full_raw_close("max_reuse");
    }
    if (s_full_raw_sock < 0 ||
        s_full_raw_port != snapshot->device.server_port ||
        strncmp(s_full_raw_host, snapshot->device.server_host, sizeof(s_full_raw_host)) != 0) {
        full_raw_close("host_change");
        err = full_raw_connect_socket(snapshot->device.server_host,
                                      snapshot->device.server_port,
                                      raw_io_timeout_ms,
                                      &s_full_raw_sock);
        if (err != ESP_OK) {
            *out_open_ms = (esp_timer_get_time() - t_stage_us) / 1000LL;
            return err;
        }
        snprintf(s_full_raw_host, sizeof(s_full_raw_host), "%s", snapshot->device.server_host);
        s_full_raw_port = snapshot->device.server_port;
        s_full_raw_reuse_count = 0U;
    } else {
        ++s_full_raw_reuse_count;
        full_raw_set_timeouts(s_full_raw_sock, raw_io_timeout_ms);
    }

    header_len = snprintf(header,
                          sizeof(header),
                          "POST /api/node/full_frame_bin HTTP/1.1\r\n"
                          "Host: %s:%u\r\n"
                          "User-Agent: EdgeWind-ESP32\r\n"
                          "Content-Type: application/octet-stream\r\n"
                          "X-EdgeWind-Proto: ewfull/2\r\n"
                          "Content-Length: %u\r\n"
                          "Connection: keep-alive\r\n"
                          "\r\n",
                          snapshot->device.server_host,
                          (unsigned int) snapshot->device.server_port,
                          (unsigned int) binary_info->body_len);
    if (header_len <= 0 || (size_t) header_len >= sizeof(header)) {
        full_raw_close("header_overflow");
        return ESP_ERR_INVALID_SIZE;
    }

    err = full_raw_send_plain(s_full_raw_sock, header, (size_t) header_len, deadline_us);
    *out_open_ms = (esp_timer_get_time() - t_stage_us) / 1000LL;
    if (err != ESP_OK) {
        full_raw_close("header_send_fail");
        return err;
    }

    *out_stage = "stream";
    t_stage_us = esp_timer_get_time();
    err = report_codec_stream_full_binary_with_writer(snapshot,
                                                      frame,
                                                      binary_info->data_crc32,
                                                      full_raw_write_all,
                                                      (void *) (intptr_t) s_full_raw_sock,
                                                      CLOUD_FULL_HTTP_TOTAL_BUDGET_MS);
    *out_stream_ms = (esp_timer_get_time() - t_stage_us) / 1000LL;
    if (err != ESP_OK) {
        full_raw_close("body_send_fail");
        return err;
    }

    *out_stage = "read";
    t_stage_us = esp_timer_get_time();
    err = full_raw_read_response(s_full_raw_sock, response, response_len, out_http_status, &keepalive);
    *out_read_ms = (esp_timer_get_time() - t_stage_us) / 1000LL;
    if (err != ESP_OK) {
        full_raw_close("response_fail");
        return err;
    }
    if (!keepalive) {
        full_raw_close("server_close");
    }
    return ESP_OK;
}

static esp_err_t post_register_request(const app_config_snapshot_t *snapshot)
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
    esp_http_client_set_header(client, "Connection", "close");
    err = esp_http_client_open(client, (int) body_len);
    if (err == ESP_OK) {
        size_t written_total = 0U;
        while (written_total < body_len) {
            int written = esp_http_client_write(client,
                                                body + written_total,
                                                (int) (body_len - written_total));
            if (written <= 0) {
                err = ESP_FAIL;
                break;
            }
            written_total += (size_t) written;
        }
    }
    if (err == ESP_OK) {
        int header_len = esp_http_client_fetch_headers(client);
        if (header_len < 0) {
            err = ESP_FAIL;
        }
    }
    if (err == ESP_OK) {
        err = read_response_body(client, response, sizeof(response), &http_status);
        if (err == ESP_OK) {
            s_registered = (http_status >= 200 && http_status < 300);
            touch_request_timestamp();
            if (report_codec_parse_server_command(response, &server_command)) {
                post_event(CLOUD_CLIENT_EVENT_SERVER_COMMAND,
                           ESP_OK,
                           http_status,
                           0,
                           0,
                           &server_command,
                           "server_command_from_register");
            }
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
        /*
         * Full binary frames normally finish in about 1.0~1.5s on the current
         * cloud path.  Do not let a bad TCP connect/open consume the generic
         * UI/SD HTTP timeout; STM32 needs a quick TX_RESULT to keep the full
         * upload cadence stable.
         */
        if (timeout_ms < CLOUD_FULL_HTTP_TIMEOUT_MIN_MS) {
            timeout_ms = CLOUD_FULL_HTTP_TIMEOUT_MIN_MS;
        }
        if (timeout_ms > CLOUD_FULL_HTTP_TIMEOUT_MAX_MS) {
            timeout_ms = CLOUD_FULL_HTTP_TIMEOUT_MAX_MS;
        }
        return timeout_ms;
    }

    if (timeout_ms == 0U) {
        timeout_ms = CLOUD_FULL_HTTP_TIMEOUT_MIN_MS;
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
    uint32_t fail_threshold = CLOUD_REPORT_WIFI_RECOVER_FAIL_STREAK;

    if (frame != NULL && frame->mode == REPORT_MODE_FULL) {
        /*
         * Full-frame uploads are high-bandwidth best-effort traffic.  A few
         * consecutive full POST failures should not immediately tear down WiFi,
         * otherwise the node keeps oscillating between register and reconnect.
         */
        fail_threshold = CLOUD_REPORT_FULL_WIFI_RECOVER_FAIL_STREAK * 3U;
        if (err == ESP_OK && http_status >= 500) {
            return;
        }
        if ((err == ESP_ERR_HTTP_CONNECT || err == ESP_ERR_TIMEOUT || err == ESP_FAIL) &&
            wifi_manager_is_connected()) {
            if (s_report_transport_fail_streak >= fail_threshold) {
                ESP_LOGW(TAG,
                         "full report transport failures streak=%u err=%s http=%d; keep WiFi up and retry HTTP socket",
                         (unsigned int) s_report_transport_fail_streak,
                         esp_err_to_name(err),
                         http_status);
            }
            return;
        }
    }

    if (s_report_transport_fail_streak < fail_threshold) {
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
                                     bool reporting_enabled,
                                     bool defer_failure_event)
{
    esp_err_t err;
    size_t payload_len = 0;
    report_full_binary_info_t binary_info = { 0 };
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
    const bool use_full_binary = (frame->mode == REPORT_MODE_FULL);
    const size_t full_write_chunk = use_full_binary ?
                                    report_codec_full_binary_write_chunk_limit(snapshot) : 0U;
    const uint32_t full_effective_delay_ms = use_full_binary ?
                                             report_codec_full_binary_write_delay_ms(snapshot) : 0U;
    /*
     * On WAN/full-binary uploads the HTTP client is created and destroyed very
     * frequently.  4KB RX/TX buffers look attractive for throughput, but after
     * some minutes the internal heap becomes fragmented enough that allocating
     * two contiguous 4KB blocks starts failing ("HTTP_CLIENT: Allocation failed").
     * Once that happens, full uploads fall back to long open/connect timeouts and
     * the node appears online but without waveform refresh.  Use compact 1KB
     * buffers instead; the body is streamed chunk-by-chunk anyway, so stability
     * matters more than the marginal buffer-size gain.
     */
    const int http_buffer_size = use_full_binary ? 1024 : 1024;
    const int http_buffer_size_tx = use_full_binary ? 1024 : 1024;

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
        .buffer_size = http_buffer_size,
        .buffer_size_tx = http_buffer_size_tx,
        .keep_alive_enable = false,
    };

    if (!reporting_enabled) {
        if (!defer_failure_event) {
            post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, ESP_ERR_INVALID_STATE, 0, frame->ref_seq, frame->frame_id, NULL, "reporting_disabled");
        }
        return ESP_ERR_INVALID_STATE;
    }
    if (!wifi_manager_is_connected()) {
        if (!defer_failure_event) {
            post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, ESP_ERR_INVALID_STATE, 0, frame->ref_seq, frame->frame_id, NULL, "wifi_not_connected");
        }
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_registered) {
        err = post_register_request(snapshot);
        if (err != ESP_OK || !s_registered) {
            esp_err_t report_err = (err != ESP_OK) ? err : ESP_FAIL;
            if (!defer_failure_event) {
                post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT,
                           report_err,
                           0,
                           frame->ref_seq,
                           frame->frame_id,
                           NULL,
                           (err != ESP_OK) ? "register_before_report_failed" : "register_before_report_not_ok");
            }
            return report_err;
        }
    }
    apply_request_interval(snapshot);

    if (use_full_binary) {
        snprintf(url, sizeof(url), "http://%s:%u/api/node/full_frame_bin", snapshot->device.server_host, snapshot->device.server_port);
        err = report_codec_measure_full_binary(snapshot, frame, &binary_info);
        if (err != ESP_OK) {
            if (!defer_failure_event) {
                post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, err, 0, frame->ref_seq, frame->frame_id, NULL, "measure_full_binary_failed");
            }
            return err;
        }
        payload_len = binary_info.body_len;
        uint32_t free_heap = (uint32_t) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        uint32_t largest_heap = (uint32_t) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (free_heap < CLOUD_FULL_LOW_HEAP_FREE_BYTES || largest_heap < CLOUD_FULL_LOW_HEAP_LARGEST_BYTES) {
            ESP_LOGW(TAG,
                     "skip full POST and force cloud recovery: low heap free=%u largest=%u frame=%" PRIu32 " ref=%" PRIu32,
                     (unsigned int) free_heap,
                     (unsigned int) largest_heap,
                     frame->frame_id,
                     frame->ref_seq);
            clear_request_timestamp();
            s_registered = false;
            if (s_report_transport_fail_streak < CLOUD_REPORT_FULL_WIFI_RECOVER_FAIL_STREAK) {
                s_report_transport_fail_streak = CLOUD_REPORT_FULL_WIFI_RECOVER_FAIL_STREAK;
            }
            maybe_force_wifi_recover(frame, ESP_ERR_NO_MEM, 0);
            if (!defer_failure_event) {
                post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT,
                           ESP_ERR_NO_MEM,
                           0,
                           frame->ref_seq,
                           frame->frame_id,
                           NULL,
                           "full_low_heap_reconnect");
            }
            return ESP_ERR_NO_MEM;
        }
    } else {
        snprintf(url, sizeof(url), "http://%s:%u/api/node/heartbeat", snapshot->device.server_host, snapshot->device.server_port);
        err = report_codec_measure_heartbeat_json(snapshot, frame, &payload_len);
        if (err != ESP_OK) {
            if (!defer_failure_event) {
                post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, err, 0, frame->ref_seq, frame->frame_id, NULL, "measure_json_failed");
            }
            return err;
        }
    }

    t0_us = esp_timer_get_time();
    ESP_LOGI(TAG,
             "report start frame=%" PRIu32 " ref=%" PRIu32 " mode=%u transport=%s len=%u timeout=%ums budget=%ums scratch=%u httpbuf=%u chunk_kb=%u chunk_delay=%u effective_delay=%u write_chunk=%u heap=%u largest=%u q=%u",
             frame->frame_id,
             frame->ref_seq,
             (unsigned int) frame->mode,
             use_full_binary ? "bin" : "json",
             (unsigned int) payload_len,
             (unsigned int) timeout_ms,
             (unsigned int) total_budget_ms,
             (unsigned int) CLOUD_JSON_SCRATCH_LEN,
             (unsigned int) http_buffer_size_tx,
             (unsigned int) snapshot->comm.chunk_kb,
             (unsigned int) snapshot->comm.chunk_delay_ms,
             (unsigned int) full_effective_delay_ms,
             (unsigned int) full_write_chunk,
             (unsigned int) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int) (s_queue != NULL ? uxQueueMessagesWaiting(s_queue) : 0U));

#if CLOUD_FULL_RAW_KEEPALIVE_ENABLE
    if (use_full_binary) {
        const char *raw_stage = "raw_open";
        stage = raw_stage;
        err = post_full_binary_raw_keepalive(snapshot,
                                             frame,
                                             &binary_info,
                                             response,
                                             sizeof(response),
                                             &http_status,
                                             &open_ms,
                                             &stream_ms,
                                             &read_ms,
                                             &raw_stage);
        stage = raw_stage;
        if (err == ESP_OK) {
            if (report_codec_parse_server_command(response, &server_command)) {
                post_event(CLOUD_CLIENT_EVENT_SERVER_COMMAND,
                           ESP_OK,
                           http_status,
                           frame->ref_seq,
                           frame->frame_id,
                           &server_command,
                           "server_command");
            }
            post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT,
                       ESP_OK,
                       http_status,
                       frame->ref_seq,
                       frame->frame_id,
                       NULL,
                       (http_status >= 200 && http_status < 300) ? "report_ok" : "report_http_fail");
            if (http_status == 401 || http_status == 404) {
                s_registered = false;
            }
        } else {
            char detail[64];
            format_err_message(detail, sizeof(detail), "report_raw_fail", err);
            if (!defer_failure_event) {
                post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, err, 0, frame->ref_seq, frame->frame_id, NULL, detail);
            }
        }
        goto cleanup;
    }
#endif

    client = esp_http_client_init(&cfg);
    if (client == NULL) {
        if (!defer_failure_event) {
            post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, ESP_ERR_NO_MEM, 0, frame->ref_seq, frame->frame_id, NULL, "http_client_init_failed");
        }
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", use_full_binary ? "application/octet-stream" : "application/json");
    if (use_full_binary) {
        esp_http_client_set_header(client, "X-EdgeWind-Proto", "ewfull/2");
    }
    esp_http_client_set_header(client, "Connection", "close");

    stage = "open";
    t_stage_us = esp_timer_get_time();
    err = esp_http_client_open(client, (int) payload_len);
    open_ms = (esp_timer_get_time() - t_stage_us) / 1000LL;
    if (err != ESP_OK) {
        char detail[64];
        format_err_message(detail, sizeof(detail), "report_open_fail", err);
        if (!defer_failure_event) {
            post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, err, 0, frame->ref_seq, frame->frame_id, NULL, detail);
        }
        goto cleanup;
    }

    stage = "stream";
    t_stage_us = esp_timer_get_time();
    if (use_full_binary) {
        err = report_codec_stream_full_binary(snapshot, frame, binary_info.data_crc32, client, total_budget_ms);
    } else {
        err = report_codec_stream_heartbeat_json(snapshot, frame, scratch, sizeof(scratch), client, total_budget_ms);
    }
    stream_ms = (esp_timer_get_time() - t_stage_us) / 1000LL;
    if (err != ESP_OK) {
        char detail[64];
        format_err_message(detail, sizeof(detail), use_full_binary ? "report_bin_stream_fail" : "report_stream_fail", err);
        if (!defer_failure_event) {
            post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, err, 0, frame->ref_seq, frame->frame_id, NULL, detail);
        }
        goto cleanup;
    }

    stage = "fetch";
    t_stage_us = esp_timer_get_time();
    err = (esp_err_t) esp_http_client_fetch_headers(client);
    fetch_ms = (esp_timer_get_time() - t_stage_us) / 1000LL;
    if (err < 0) {
        char detail[64];
        format_err_message(detail, sizeof(detail), "report_fetch_fail", err);
        if (!defer_failure_event) {
            post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, err, 0, frame->ref_seq, frame->frame_id, NULL, detail);
        }
        goto cleanup;
    }
    err = ESP_OK;

    stage = "read";
    t_stage_us = esp_timer_get_time();
    err = read_response_body(client, response, sizeof(response), &http_status);
    read_ms = (esp_timer_get_time() - t_stage_us) / 1000LL;
    if (err == ESP_OK) {
            if (report_codec_parse_server_command(response, &server_command)) {
                post_event(CLOUD_CLIENT_EVENT_SERVER_COMMAND,
                           ESP_OK,
                           http_status,
                           frame->ref_seq,
                           frame->frame_id,
                           &server_command,
                           "server_command");
            }
            post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT,
                       ESP_OK,
                       http_status,
                       frame->ref_seq,
                       frame->frame_id,
                       NULL,
                       (http_status >= 200 && http_status < 300) ? "report_ok" : "report_http_fail");
            if (http_status == 401 || http_status == 404) {
                s_registered = false;
            }
    } else {
        char detail[64];
        format_err_message(detail, sizeof(detail), "report_read_fail", err);
        if (!defer_failure_event) {
            post_event(CLOUD_CLIENT_EVENT_REPORT_RESULT, err, 0, frame->ref_seq, frame->frame_id, NULL, detail);
        }
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
         * Full-mode failures are usually short raw-socket/connect transients.
         * Treat them as recent traffic so the idle heartbeat path does not
         * immediately open another TCP connection while the next full frame is
         * already queued to retry.
         */
        if (frame->mode == REPORT_MODE_FULL && s_registered) {
            touch_request_timestamp();
        } else {
            clear_request_timestamp();
        }
        if (!defer_failure_event) {
            if (s_report_transport_fail_streak < 1000000U) {
                ++s_report_transport_fail_streak;
            }
            if (s_report_transport_fail_streak >= CLOUD_REPORT_REREGISTER_FAIL_STREAK) {
                ESP_LOGW(TAG,
                         "report failures streak=%u err=%s http=%d, keep registration and rely on heartbeat/register retry",
                         (unsigned int) s_report_transport_fail_streak,
                          esp_err_to_name(err),
                          http_status);
            }
            maybe_force_wifi_recover(frame, err, http_status);
        }
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
                                              const char *fault_code,
                                              report_mode_t report_mode)
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

    err = report_codec_build_empty_heartbeat_json(snapshot, status, fault_code, report_mode, &body, &body_len);
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

static esp_err_t fetch_fault_summary_request(const app_config_snapshot_t *snapshot)
{
    esp_err_t err;
    char url[384];
    char response[CLOUD_FAULT_RESPONSE_MAX_LEN];
    int http_status = 0;
    protocol_fault_summary_payload_t summary;
    esp_http_client_handle_t client = NULL;
    cJSON *root = NULL;
    const cJSON *items = NULL;
    int item_count = 0;
    char encoded_node_id[APP_MAX_NODE_ID_LEN * 3U];

    if (snapshot == NULL || snapshot->device.node_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!wifi_manager_is_connected() || !s_registered) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!url_encode_component(snapshot->device.node_id, encoded_node_id, sizeof(encoded_node_id))) {
        ESP_LOGW(TAG, "fault summary node_id url encode failed len=%u", (unsigned int) strlen(snapshot->device.node_id));
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(url,
             sizeof(url),
             "http://%s:%u/api/node/faults?node_id=%s&since_rev=%" PRIu32 "&limit=%u&compact=1",
             snapshot->device.server_host,
             snapshot->device.server_port,
             encoded_node_id,
             s_fault_since_rev,
             (unsigned int) PROTOCOL_FAULT_SUMMARY_MAX_ITEMS);
    ESP_LOGI(TAG,
             "fault summary GET node=%s since_rev=%" PRIu32 " url_len=%u",
             snapshot->device.node_id,
             s_fault_since_rev,
             (unsigned int) strlen(url));

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 1000,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
        .keep_alive_enable = false,
    };

    client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_http_client_set_header(client, "Connection", "close");
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fault summary GET open failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    err = (esp_err_t) esp_http_client_fetch_headers(client);
    if (err < 0) {
        ESP_LOGW(TAG, "fault summary fetch headers failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    err = ESP_OK;

    err = read_response_body(client, response, sizeof(response), &http_status);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fault summary read failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    if (http_status < 200 || http_status >= 300) {
        ESP_LOGW(TAG, "fault summary HTTP status=%d", http_status);
        err = ESP_FAIL;
        goto cleanup;
    }

    root = cJSON_Parse(response);
    if (root == NULL || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "success"))) {
        ESP_LOGW(TAG, "fault summary JSON parse/contract failed");
        err = ESP_FAIL;
        goto cleanup;
    }

    memset(&summary, 0, sizeof(summary));
    summary.latest_rev = json_uint_or_zero(root, "latest_rev");
    maybe_post_time_sync_from_response(root);

    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "not_modified"))) {
        s_fault_since_rev = summary.latest_rev;
        ESP_LOGI(TAG, "fault summary not modified rev=%" PRIu32, summary.latest_rev);
        if (!s_fault_summary_sent_once) {
            summary.cloud_status = PROTOCOL_FAULT_CLOUD_NOT_MODIFIED;
            post_fault_summary_event(&summary);
            s_fault_summary_sent_once = true;
            s_last_pushed_fault_rev = summary.latest_rev;
        }
        err = ESP_OK;
        goto cleanup;
    }

    items = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (cJSON_IsArray(items)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, items) {
            protocol_fault_summary_item_t *dst;
            const char *title;
            if (item_count >= (int) PROTOCOL_FAULT_SUMMARY_MAX_ITEMS) {
                break;
            }
            if (!cJSON_IsObject(item)) {
                continue;
            }
            dst = &summary.items[item_count];
            dst->fault_id = json_uint_or_zero(item, "fault_id");
            dst->updated_rev = json_uint_or_zero(item, "updated_rev");
            copy_fixed_string(dst->fault_code, sizeof(dst->fault_code), json_string_or_empty(item, "fault_code"));
            dst->severity = fault_severity_code(json_string_or_empty(item, "severity"));
            dst->state = fault_state_code(json_string_or_empty(item, "status"));
            dst->ai_status = fault_ai_status_code(json_string_or_empty(item, "ai_status"));
            copy_fixed_string(dst->timestamp, sizeof(dst->timestamp), json_string_or_empty(item, "timestamp"));
            title = json_string_or_empty(item, "description");
            if (title[0] == '\0') {
                title = json_string_or_empty(item, "root_cause");
            }
            copy_fixed_string(dst->title, sizeof(dst->title), title);
            copy_fixed_string(dst->advice, sizeof(dst->advice), json_string_or_empty(item, "advice_short"));
            ++item_count;
        }
    }

    summary.count = (uint8_t) item_count;
    summary.cloud_status = (item_count > 0) ? PROTOCOL_FAULT_CLOUD_OK : PROTOCOL_FAULT_CLOUD_EMPTY;
    s_fault_since_rev = summary.latest_rev;
    ESP_LOGI(TAG,
             "fault summary parsed rev=%" PRIu32 " count=%u cloud=%u",
             summary.latest_rev,
             (unsigned int) summary.count,
             (unsigned int) summary.cloud_status);
    if (summary.count > 0 || !s_fault_summary_sent_once || summary.latest_rev != s_last_pushed_fault_rev) {
        post_fault_summary_event(&summary);
        s_fault_summary_sent_once = true;
        s_last_pushed_fault_rev = summary.latest_rev;
    }
    err = ESP_OK;

cleanup:
    if (root != NULL) {
        cJSON_Delete(root);
    }
    if (client != NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
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
            int64_t now_us = esp_timer_get_time();
            if (reporting_enabled &&
                wifi_connected &&
                (s_last_fault_sync_us == 0 ||
                 (now_us - s_last_fault_sync_us) >= ((int64_t) CLOUD_FAULT_SYNC_INTERVAL_MS * 1000LL))) {
                s_last_fault_sync_us = now_us;
                (void) fetch_fault_summary_request(&snapshot);
            }
            if (reporting_enabled &&
                wifi_connected &&
                snapshot.comm.heartbeat_ms > 0U &&
                (s_last_request_us == 0 || (esp_timer_get_time() - s_last_request_us) >= ((int64_t) snapshot.comm.heartbeat_ms * 1000LL))) {
                /*
                 * This idle path keeps the node registered when no report frame
                 * has been posted for a heartbeat interval.  Full POST failures
                 * refresh s_last_request_us above, so this path does not race the
                 * next full-frame reconnect attempt.
                 */
                if (!s_registered) {
                    (void) post_register_request(&snapshot);
                } else {
                    (void) post_empty_heartbeat_request(&snapshot, last_status, last_fault_code, report_mode);
                }
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
                esp_err_t submit_err;
                last_status = msg.data.frame->status;
                strncpy(last_fault_code, msg.data.frame->fault_code, sizeof(last_fault_code) - 1U);
                last_fault_code[sizeof(last_fault_code) - 1U] = '\0';
                submit_err = post_report_request(&snapshot,
                                                 msg.data.frame,
                                                 reporting_enabled,
                                                 false);
                (void) submit_err;
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
