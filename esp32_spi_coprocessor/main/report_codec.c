#include "report_codec.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cJSON.h"

static const char *TAG = "report_codec";

#define REPORT_HTTP_WRITE_RETRY_MAX 32U
#define REPORT_HTTP_WRITE_RETRY_DELAY_MS 5U
#define REPORT_HTTP_WRITE_CHUNK_AUTO 512U
#define REPORT_HTTP_WRITE_CHUNK_MIN 512U
#define REPORT_HTTP_WRITE_CHUNK_MAX 1024U
#define REPORT_HTTP_WRITE_BLOCK_ABORT_MS 1000U
#define EW_FULL_V1_MAGIC UINT32_C(0x31465745)
#define EW_FULL_V2_VERSION 2U
#define EW_REPORT_ENGINEERING_VALUE_SCALE 10U
#define EW_REPORT_WAVEFORM_SCALE 10U
#define EW_REPORT_FFT_SCALE 10U

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_len;
    uint32_t frame_id;
    uint64_t timestamp_ms;
    uint32_t flags;
    char node_id[APP_MAX_NODE_ID_LEN];
    char fault_code[8];
    uint8_t report_mode;
    uint8_t status_code;
    uint8_t channel_count;
    uint8_t reserved0;
    uint32_t downsample_step;
    uint32_t upload_points;
    uint32_t heartbeat_ms;
    uint32_t min_interval_ms;
    uint32_t http_timeout_ms;
    uint32_t chunk_kb;
    uint32_t chunk_delay_ms;
    uint32_t data_crc32;
} ew_full_v1_header_t;

typedef struct __attribute__((packed)) {
    uint8_t channel_id;
    uint8_t reserved0;
    uint16_t waveform_count;
    uint16_t fft_count;
    uint16_t reserved1;
    int32_t value_scaled;
    int32_t current_value_scaled;
} ew_full_v1_channel_meta_t;

_Static_assert(sizeof(ew_full_v1_header_t) == 132U, "unexpected ew_full_v1_header_t size");
_Static_assert(sizeof(ew_full_v1_channel_meta_t) == 16U, "unexpected ew_full_v1_channel_meta_t size");

typedef enum {
    BUILDER_MODE_COUNT = 0,
    BUILDER_MODE_STREAM = 1,
} builder_mode_t;

typedef struct {
    builder_mode_t mode;
    char *buf;
    size_t cap;
    size_t len;
    size_t total_len;
    bool failed;
    esp_http_client_handle_t client;
    int64_t deadline_us;
} json_builder_t;

typedef struct {
    uint8_t channel_id;
    const char *label;
    const char *unit;
} channel_meta_t;

static const channel_meta_t s_channel_meta[REPORT_MAX_CHANNELS] = {
    {0, "\xE7\x9B\xB4\xE6\xB5\x81\xE6\xAF\x8D\xE7\xBA\xBF(+)", "V"},
    {1, "\xE7\x9B\xB4\xE6\xB5\x81\xE6\xAF\x8D\xE7\xBA\xBF(-)", "V"},
    {2, "\xE8\xB4\x9F\xE8\xBD\xBD\xE7\x94\xB5\xE6\xB5\x81", "A"},
    {3, "\xE6\xBC\x8F\xE7\x94\xB5\xE6\xB5\x81", "mA"},
};

static const channel_meta_t *find_channel_meta(uint8_t channel_id)
{
    for (size_t i = 0; i < REPORT_MAX_CHANNELS; ++i) {
        if (s_channel_meta[i].channel_id == channel_id) {
            return &s_channel_meta[i];
        }
    }
    return &s_channel_meta[0];
}

static const char *status_to_text(device_status_t status)
{
    return (status == DEVICE_STATUS_ONLINE) ? "online" : "offline";
}

static void builder_init_count(json_builder_t *builder)
{
    memset(builder, 0, sizeof(*builder));
    builder->mode = BUILDER_MODE_COUNT;
}

static void builder_init_stream(json_builder_t *builder,
                                char *buffer,
                                size_t capacity,
                                esp_http_client_handle_t client,
                                uint32_t total_budget_ms)
{
    memset(builder, 0, sizeof(*builder));
    builder->mode = BUILDER_MODE_STREAM;
    builder->buf = buffer;
    builder->cap = capacity;
    builder->client = client;
    if (total_budget_ms > 0U) {
        builder->deadline_us = esp_timer_get_time() + ((int64_t) total_budget_ms * 1000LL);
    }
    if (builder->cap > 0U) {
        builder->buf[0] = '\0';
    }
}

static bool builder_deadline_expired(json_builder_t *builder)
{
    if (builder == NULL || builder->deadline_us <= 0) {
        return false;
    }
    if (esp_timer_get_time() < builder->deadline_us) {
        return false;
    }
    builder->failed = true;
    ESP_LOGW(TAG,
             "stream heartbeat json budget expired total=%u pending=%u heap=%u min=%u largest=%u",
             (unsigned int) builder->total_len,
             (unsigned int) builder->len,
             (unsigned int) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return true;
}

static bool stream_deadline_expired(int64_t deadline_us, size_t pending_len)
{
    if (deadline_us <= 0) {
        return false;
    }
    if (esp_timer_get_time() < deadline_us) {
        return false;
    }
    ESP_LOGW(TAG,
             "stream binary budget expired pending=%u heap=%u min=%u largest=%u",
             (unsigned int) pending_len,
             (unsigned int) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned int) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return true;
}

static size_t http_write_chunk_limit(const app_config_snapshot_t *config)
{
    uint32_t chunk_kb = 0U;
    size_t chunk_bytes;

    if (config != NULL) {
        chunk_kb = config->comm.chunk_kb;
    }
    if (chunk_kb == 0U) {
        return REPORT_HTTP_WRITE_CHUNK_AUTO;
    }

    chunk_bytes = (size_t) chunk_kb * 1024U;
    if (chunk_bytes < REPORT_HTTP_WRITE_CHUNK_MIN) {
        chunk_bytes = REPORT_HTTP_WRITE_CHUNK_MIN;
    }
    if (chunk_bytes > REPORT_HTTP_WRITE_CHUNK_MAX) {
        chunk_bytes = REPORT_HTTP_WRITE_CHUNK_MAX;
    }
    return chunk_bytes;
}

static uint32_t http_write_chunk_delay_ms(const app_config_snapshot_t *config)
{
    if (config == NULL) {
        return 0U;
    }
    return config->comm.chunk_delay_ms;
}

static bool http_write_should_abort_socket(int saved_errno, int64_t write_ms)
{
    if (write_ms >= (int64_t) REPORT_HTTP_WRITE_BLOCK_ABORT_MS) {
        return true;
    }
#ifdef EINPROGRESS
    if (saved_errno == EINPROGRESS) {
        return true;
    }
#endif
#ifdef ETIMEDOUT
    if (saved_errno == ETIMEDOUT) {
        return true;
    }
#endif
#ifdef ECONNRESET
    if (saved_errno == ECONNRESET) {
        return true;
    }
#endif
#ifdef ENOTCONN
    if (saved_errno == ENOTCONN) {
        return true;
    }
#endif
    return false;
}

static esp_err_t http_write_all(esp_http_client_handle_t client,
                                const app_config_snapshot_t *config,
                                const void *data,
                                size_t data_len,
                                int64_t deadline_us)
{
    const uint8_t *bytes = (const uint8_t *) data;
    size_t offset = 0U;
    const size_t write_chunk_limit = http_write_chunk_limit(config);
    const uint32_t write_delay_ms = http_write_chunk_delay_ms(config);
    uint32_t retry_count = 0U;

    if (client == NULL || (data == NULL && data_len > 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    while (offset < data_len) {
        size_t write_len = data_len - offset;
        if (write_len > write_chunk_limit) {
            write_len = write_chunk_limit;
        }
        if (stream_deadline_expired(deadline_us, data_len - offset)) {
            return ESP_ERR_TIMEOUT;
        }

        int64_t write_start_us = esp_timer_get_time();
        int written = esp_http_client_write(client, (const char *) bytes + offset, (int) write_len);
        int64_t write_ms = (esp_timer_get_time() - write_start_us) / 1000LL;
        if (written <= 0) {
            const int saved_errno = errno;
            bool retry_allowed = retry_count < REPORT_HTTP_WRITE_RETRY_MAX;

            ESP_LOGW(TAG,
                     "http_write(binary) backpressure ret=%d errno=%d offset=%u len=%u write_ms=%lld retry=%u/%u heap=%u min=%u largest=%u",
                     written,
                     saved_errno,
                     (unsigned int) offset,
                     (unsigned int) data_len,
                     (long long) write_ms,
                     (unsigned int) retry_count,
                     (unsigned int) REPORT_HTTP_WRITE_RETRY_MAX,
                     (unsigned int) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                     (unsigned int) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                     (unsigned int) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            if (http_write_should_abort_socket(saved_errno, write_ms)) {
                ESP_LOGW(TAG,
                         "http_write(binary) abort stalled socket errno=%d offset=%u len=%u write_ms=%lld",
                         saved_errno,
                         (unsigned int) offset,
                         (unsigned int) data_len,
                         (long long) write_ms);
                return ESP_ERR_TIMEOUT;
            }
            if (!retry_allowed) {
                return ESP_FAIL;
            }
            ++retry_count;
            vTaskDelay(pdMS_TO_TICKS(REPORT_HTTP_WRITE_RETRY_DELAY_MS));
            continue;
        }
        offset += (size_t) written;
        retry_count = 0U;
        if (write_delay_ms > 0U && offset < data_len) {
            vTaskDelay(pdMS_TO_TICKS(write_delay_ms));
        }
    }

    return ESP_OK;
}

static bool builder_flush(json_builder_t *builder)
{
    size_t offset = 0;
    uint32_t retry_count = 0;

    if (builder->mode != BUILDER_MODE_STREAM || builder->len == 0U) {
        return true;
    }

    while (offset < builder->len) {
        size_t write_len = builder->len - offset;

        if (write_len > REPORT_HTTP_WRITE_CHUNK_AUTO) {
            write_len = REPORT_HTTP_WRITE_CHUNK_AUTO;
        }
        if (builder_deadline_expired(builder)) {
            return false;
        }
        int64_t write_start_us = esp_timer_get_time();
        int written = esp_http_client_write(builder->client, builder->buf + offset, (int) write_len);
        int64_t write_ms = (esp_timer_get_time() - write_start_us) / 1000LL;
        if (written <= 0) {
            const int saved_errno = errno;
            bool retry_allowed = retry_count < REPORT_HTTP_WRITE_RETRY_MAX;

            ESP_LOGW(TAG,
                     "http_write backpressure ret=%d errno=%d offset=%u len=%u write_ms=%lld retry=%u/%u heap=%u min=%u largest=%u",
                     written,
                     saved_errno,
                     (unsigned int) offset,
                     (unsigned int) builder->len,
                     (long long) write_ms,
                     (unsigned int) retry_count,
                     (unsigned int) REPORT_HTTP_WRITE_RETRY_MAX,
                     (unsigned int) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                     (unsigned int) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                     (unsigned int) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

            if (!retry_allowed) {
                ESP_LOGE(TAG,
                         "http_write failed ret=%d errno=%d offset=%u len=%u write_ms=%lld heap=%u min=%u largest=%u",
                         written,
                         saved_errno,
                         (unsigned int) offset,
                         (unsigned int) builder->len,
                         (long long) write_ms,
                         (unsigned int) heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                         (unsigned int) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                         (unsigned int) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                builder->failed = true;
                return false;
            }

            ++retry_count;
            vTaskDelay(pdMS_TO_TICKS(REPORT_HTTP_WRITE_RETRY_DELAY_MS));
            continue;
        }
        offset += (size_t) written;
        retry_count = 0;
        if (builder_deadline_expired(builder)) {
            return false;
        }
    }

    builder->len = 0U;
    if (builder->cap > 0U) {
        builder->buf[0] = '\0';
    }
    return true;
}

static void builder_append(json_builder_t *builder, const char *fmt, ...)
{
    va_list args;
    va_list copy;
    int required;

    if (builder->failed || builder_deadline_expired(builder)) {
        return;
    }

    va_start(args, fmt);
    va_copy(copy, args);
    required = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);

    if (required < 0) {
        builder->failed = true;
        va_end(args);
        return;
    }

    builder->total_len += (size_t) required;

    if (builder->mode == BUILDER_MODE_COUNT) {
        va_end(args);
        return;
    }

    if ((size_t) required >= builder->cap) {
        builder->failed = true;
        va_end(args);
        return;
    }

    if (builder->len + (size_t) required >= builder->cap) {
        if (!builder_flush(builder)) {
            va_end(args);
            return;
        }
    }

    if (vsnprintf(builder->buf + builder->len, builder->cap - builder->len, fmt, args) != required) {
        builder->failed = true;
        va_end(args);
        return;
    }
    va_end(args);
    builder->len += (size_t) required;
}

static void builder_append_json_string(json_builder_t *builder, const char *text)
{
    const char *ptr = text != NULL ? text : "";

    builder_append(builder, "\"");
    while (*ptr != '\0') {
        switch (*ptr) {
        case '\\':
            builder_append(builder, "\\\\");
            break;
        case '"':
            builder_append(builder, "\\\"");
            break;
        case '\n':
            builder_append(builder, "\\n");
            break;
        case '\r':
            builder_append(builder, "\\r");
            break;
        case '\t':
            builder_append(builder, "\\t");
            break;
        default:
            builder_append(builder, "%c", *ptr);
            break;
        }
        ++ptr;
    }
    builder_append(builder, "\"");
}

static void builder_append_scaled_i32(json_builder_t *builder, int32_t value, uint32_t scale)
{
    int64_t raw = (int64_t) value;
    uint64_t abs_raw = (raw < 0) ? (uint64_t) (-raw) : (uint64_t) raw;
    uint64_t denom = (scale == 0U) ? 1U : (uint64_t) scale;

    builder_append(builder,
                   "%s%" PRIu64 ".%" PRIu64,
                   raw < 0 ? "-" : "",
                   abs_raw / denom,
                   abs_raw % denom);
}

static void builder_append_i16_scaled_array(json_builder_t *builder,
                                            const int16_t *values,
                                            size_t count,
                                            uint32_t scale)
{
    builder_append(builder, "[");
    for (size_t i = 0; i < count; ++i) {
        if (i > 0U) {
            builder_append(builder, ",");
        }
        builder_append_scaled_i32(builder, (int32_t) values[i], scale);
    }
    builder_append(builder, "]");
}

static bool builder_finish(json_builder_t *builder)
{
    if (builder->failed) {
        return false;
    }
    if (builder->mode == BUILDER_MODE_STREAM) {
        return builder_flush(builder);
    }
    return true;
}

static bool emit_heartbeat_json(json_builder_t *builder,
                                const app_config_snapshot_t *config,
                                const report_frame_t *frame)
{
    builder_append(builder, "{");
    builder_append(builder, "\"node_id\":");
    builder_append_json_string(builder, config->device.node_id);
    builder_append(builder, ",\"status\":");
    builder_append_json_string(builder, status_to_text(frame->status));
    builder_append(builder, ",\"fault_code\":");
    builder_append_json_string(builder, frame->fault_code[0] != '\0' ? frame->fault_code : "E00");
    builder_append(builder, ",\"seq\":%" PRIu32, frame->frame_id);
    builder_append(builder, ",\"downsample_step\":%" PRIu32, frame->downsample_step);
    builder_append(builder, ",\"upload_points\":%" PRIu32, frame->upload_points);
    builder_append(builder, ",\"report_mode\":");
    builder_append_json_string(builder, (frame->mode == REPORT_MODE_FULL) ? "full" : "summary");
    builder_append(builder, ",\"heartbeat_ms\":%" PRIu32, config->comm.heartbeat_ms);
    builder_append(builder, ",\"min_interval_ms\":%" PRIu32, config->comm.min_interval_ms);
    builder_append(builder, ",\"http_timeout_ms\":%" PRIu32, config->comm.http_timeout_ms);
    builder_append(builder, ",\"chunk_kb\":%" PRIu32, config->comm.chunk_kb);
    builder_append(builder, ",\"chunk_delay_ms\":%" PRIu32, config->comm.chunk_delay_ms);
    builder_append(builder, ",\"channels\":[");

    for (size_t i = 0; i < frame->channel_count; ++i) {
        const report_channel_data_t *channel = &frame->channels[i];
        const channel_meta_t *meta = find_channel_meta(channel->channel_id);

        if (i > 0U) {
            builder_append(builder, ",");
        }

        builder_append(builder, "{");
        builder_append(builder, "\"id\":%u,\"channel_id\":%u,", channel->channel_id, channel->channel_id);
        builder_append(builder, "\"label\":");
        builder_append_json_string(builder, meta->label);
        builder_append(builder, ",\"name\":");
        builder_append_json_string(builder, meta->label);
        builder_append(builder, ",\"value\":");
        builder_append_scaled_i32(builder, channel->value_scaled, EW_REPORT_ENGINEERING_VALUE_SCALE);
        builder_append(builder, ",\"current_value\":");
        builder_append_scaled_i32(builder, channel->current_value_scaled, EW_REPORT_ENGINEERING_VALUE_SCALE);
        builder_append(builder, ",");
        builder_append(builder, "\"unit\":");
        builder_append_json_string(builder, meta->unit);

        if (frame->mode == REPORT_MODE_FULL) {
            builder_append(builder, ",\"waveform\":");
            builder_append_i16_scaled_array(builder,
                                            channel->waveform_scaled,
                                            channel->waveform_scaled != NULL ? channel->waveform_count : 0U,
                                            EW_REPORT_WAVEFORM_SCALE);
            builder_append(builder, ",\"fft_spectrum\":");
            builder_append_i16_scaled_array(builder,
                                            channel->fft_tenths,
                                            channel->fft_tenths != NULL ? channel->fft_count : 0U,
                                            EW_REPORT_FFT_SCALE);
        }

        builder_append(builder, "}");
    }

    builder_append(builder, "]}");
    return builder_finish(builder);
}

esp_err_t report_codec_build_register_json(const app_config_snapshot_t *config, char **out_json, size_t *out_len)
{
    cJSON *root = NULL;
    char *json = NULL;

    if (config == NULL || out_json == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (!cJSON_AddStringToObject(root, "device_id", config->device.node_id) ||
        !cJSON_AddStringToObject(root, "location", config->device.node_location) ||
        !cJSON_AddStringToObject(root, "hw_version", config->device.hw_version)) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    *out_json = json;
    *out_len = strlen(json);
    return ESP_OK;
}

esp_err_t report_codec_build_empty_heartbeat_json(const app_config_snapshot_t *config,
                                                  device_status_t status,
                                                  const char *fault_code,
                                                  report_mode_t report_mode,
                                                  char **out_json,
                                                  size_t *out_len)
{
    cJSON *root = NULL;
    cJSON *channels = NULL;
    char *json = NULL;
    const char *fault = (fault_code != NULL && fault_code[0] != '\0') ? fault_code : "E00";

    if (config == NULL || out_json == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }
    channels = cJSON_AddArrayToObject(root, "channels");
    if (channels == NULL ||
        !cJSON_AddStringToObject(root, "node_id", config->device.node_id) ||
        !cJSON_AddStringToObject(root, "status", status_to_text(status)) ||
        !cJSON_AddStringToObject(root, "fault_code", fault) ||
        !cJSON_AddStringToObject(root, "report_mode", (report_mode == REPORT_MODE_FULL) ? "full" : "summary") ||
        !cJSON_AddNumberToObject(root, "downsample_step", config->comm.downsample_step) ||
        !cJSON_AddNumberToObject(root, "upload_points", config->comm.upload_points) ||
        !cJSON_AddNumberToObject(root, "heartbeat_ms", config->comm.heartbeat_ms) ||
        !cJSON_AddNumberToObject(root, "min_interval_ms", config->comm.min_interval_ms) ||
        !cJSON_AddNumberToObject(root, "http_timeout_ms", config->comm.http_timeout_ms) ||
        !cJSON_AddNumberToObject(root, "chunk_kb", config->comm.chunk_kb) ||
        !cJSON_AddNumberToObject(root, "chunk_delay_ms", config->comm.chunk_delay_ms)) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    *out_json = json;
    *out_len = strlen(json);
    return ESP_OK;
}

esp_err_t report_codec_measure_heartbeat_json(const app_config_snapshot_t *config,
                                              const report_frame_t *frame,
                                              size_t *out_len)
{
    json_builder_t builder;

    if (config == NULL || frame == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    builder_init_count(&builder);
    if (!emit_heartbeat_json(&builder, config, frame)) {
        return ESP_FAIL;
    }

    *out_len = builder.total_len;
    return ESP_OK;
}

esp_err_t report_codec_stream_heartbeat_json(const app_config_snapshot_t *config,
                                             const report_frame_t *frame,
                                             char *scratch,
                                             size_t scratch_len,
                                             esp_http_client_handle_t client,
                                             uint32_t total_budget_ms)
{
    json_builder_t builder;

    if (config == NULL || frame == NULL || scratch == NULL || scratch_len == 0U || client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    builder_init_stream(&builder, scratch, scratch_len, client, total_budget_ms);
    return emit_heartbeat_json(&builder, config, frame) ? ESP_OK : ESP_FAIL;
}

static bool report_frame_validate_full(const report_frame_t *frame)
{
    if (frame == NULL || frame->channel_count == 0U || frame->channel_count > REPORT_MAX_CHANNELS) {
        return false;
    }

    for (size_t i = 0; i < frame->channel_count; ++i) {
        const report_channel_data_t *channel = &frame->channels[i];
        if (channel->waveform_count > UINT16_MAX || channel->fft_count > UINT16_MAX) {
            return false;
        }
        if (channel->waveform_count > 0U && channel->waveform_scaled == NULL) {
            return false;
        }
        if (channel->fft_count > 0U && channel->fft_tenths == NULL) {
            return false;
        }
    }
    return true;
}

static void fill_full_channel_meta(ew_full_v1_channel_meta_t *meta,
                                   const report_channel_data_t *channel)
{
    memset(meta, 0, sizeof(*meta));
    meta->channel_id = channel->channel_id;
    meta->waveform_count = (uint16_t) channel->waveform_count;
    meta->fft_count = (uint16_t) channel->fft_count;
    meta->value_scaled = channel->value_scaled;
    meta->current_value_scaled = channel->current_value_scaled;
}

esp_err_t report_codec_measure_full_binary(const app_config_snapshot_t *config,
                                           const report_frame_t *frame,
                                           report_full_binary_info_t *out_info)
{
    size_t total_len;
    uint32_t crc;

    (void) config;

    if (frame == NULL || out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!report_frame_validate_full(frame)) {
        return ESP_ERR_INVALID_ARG;
    }

    total_len = sizeof(ew_full_v1_header_t) + (frame->channel_count * sizeof(ew_full_v1_channel_meta_t));
    crc = 0U;
    for (size_t i = 0; i < frame->channel_count; ++i) {
        const report_channel_data_t *channel = &frame->channels[i];
        ew_full_v1_channel_meta_t meta;
        fill_full_channel_meta(&meta, channel);
        crc = esp_crc32_le(crc, (const uint8_t *) &meta, sizeof(meta));
        total_len += channel->waveform_count * sizeof(int16_t);
        total_len += channel->fft_count * sizeof(int16_t);
    }
    for (size_t i = 0; i < frame->channel_count; ++i) {
        const report_channel_data_t *channel = &frame->channels[i];
        if (channel->waveform_count > 0U) {
            crc = esp_crc32_le(crc,
                               (const uint8_t *) channel->waveform_scaled,
                               channel->waveform_count * sizeof(int16_t));
        }
        if (channel->fft_count > 0U) {
            crc = esp_crc32_le(crc,
                               (const uint8_t *) channel->fft_tenths,
                               channel->fft_count * sizeof(int16_t));
        }
    }

    out_info->body_len = total_len;
    out_info->data_crc32 = crc;
    return ESP_OK;
}

esp_err_t report_codec_stream_full_binary(const app_config_snapshot_t *config,
                                          const report_frame_t *frame,
                                          uint32_t data_crc32,
                                          esp_http_client_handle_t client,
                                          uint32_t total_budget_ms)
{
    ew_full_v1_header_t header;
    int64_t deadline_us = 0;
    esp_err_t err;

    if (config == NULL || frame == NULL || client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!report_frame_validate_full(frame)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&header, 0, sizeof(header));
    header.magic = EW_FULL_V1_MAGIC;
    header.version = EW_FULL_V2_VERSION;
    header.header_len = (uint16_t) sizeof(header);
    header.frame_id = frame->frame_id;
    header.timestamp_ms = frame->timestamp_ms;
    header.flags = 0U;
    snprintf(header.node_id, sizeof(header.node_id), "%s", config->device.node_id);
    snprintf(header.fault_code, sizeof(header.fault_code), "%s", frame->fault_code[0] != '\0' ? frame->fault_code : "E00");
    header.report_mode = (uint8_t) frame->mode;
    header.status_code = (uint8_t) frame->status;
    header.channel_count = (uint8_t) frame->channel_count;
    header.downsample_step = frame->downsample_step;
    header.upload_points = frame->upload_points;
    header.heartbeat_ms = config->comm.heartbeat_ms;
    header.min_interval_ms = config->comm.min_interval_ms;
    header.http_timeout_ms = config->comm.http_timeout_ms;
    header.chunk_kb = config->comm.chunk_kb;
    header.chunk_delay_ms = config->comm.chunk_delay_ms;
    header.data_crc32 = data_crc32;

    if (total_budget_ms > 0U) {
        deadline_us = esp_timer_get_time() + ((int64_t) total_budget_ms * 1000LL);
    }

    err = http_write_all(client, config, &header, sizeof(header), deadline_us);
    if (err != ESP_OK) {
        return err;
    }

    for (size_t i = 0; i < frame->channel_count; ++i) {
        ew_full_v1_channel_meta_t meta;
        fill_full_channel_meta(&meta, &frame->channels[i]);
        err = http_write_all(client, config, &meta, sizeof(meta), deadline_us);
        if (err != ESP_OK) {
            return err;
        }
    }

    for (size_t i = 0; i < frame->channel_count; ++i) {
        const report_channel_data_t *channel = &frame->channels[i];
        if (channel->waveform_count > 0U) {
            err = http_write_all(client,
                                 config,
                                 channel->waveform_scaled,
                                 channel->waveform_count * sizeof(int16_t),
                                 deadline_us);
            if (err != ESP_OK) {
                return err;
            }
        }
        if (channel->fft_count > 0U) {
            err = http_write_all(client,
                                 config,
                                 channel->fft_tenths,
                                 channel->fft_count * sizeof(int16_t),
                                 deadline_us);
            if (err != ESP_OK) {
                return err;
            }
        }
    }

    return ESP_OK;
}

static cJSON *find_key_recursive(cJSON *node, const char *key)
{
    cJSON *child;
    cJSON *found;

    if (node == NULL || key == NULL) {
        return NULL;
    }

    if (cJSON_IsObject(node)) {
        found = cJSON_GetObjectItemCaseSensitive(node, key);
        if (found != NULL) {
            return found;
        }
    }

    child = node->child;
    while (child != NULL) {
        found = find_key_recursive(child, key);
        if (found != NULL) {
            return found;
        }
        child = child->next;
    }
    return NULL;
}

static bool fallback_parse_u32(const char *body, const char *key, uint32_t *out_value)
{
    const char *pos;

    if (body == NULL || key == NULL || out_value == NULL) {
        return false;
    }

    pos = strstr(body, key);
    if (pos == NULL) {
        return false;
    }

    pos += strlen(key);
    while (*pos == ' ' || *pos == '"' || *pos == ':' || *pos == '=') {
        ++pos;
    }

    *out_value = (uint32_t) strtoul(pos, NULL, 10);
    return true;
}

static bool json_item_to_u32(const cJSON *item, uint32_t *out_value)
{
    if (item == NULL || out_value == NULL) {
        return false;
    }
    if (cJSON_IsNumber(item)) {
        double value = item->valuedouble;
        if (value < 0.0 || value > 4294967295.0) {
            return false;
        }
        *out_value = (uint32_t) value;
        return true;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        char *endptr = NULL;
        unsigned long parsed = strtoul(item->valuestring, &endptr, 10);
        if (endptr != item->valuestring) {
            *out_value = (uint32_t) parsed;
            return true;
        }
    }
    return false;
}

bool report_codec_parse_server_command(const char *body, server_command_event_t *out_event)
{
    cJSON *root = NULL;
    cJSON *item = NULL;

    if (body == NULL || out_event == NULL) {
        return false;
    }

    memset(out_event, 0, sizeof(*out_event));
    root = cJSON_Parse(body);
    if (root != NULL) {
        item = find_key_recursive(root, "command");
        if (cJSON_IsString(item) && item->valuestring != NULL && strcmp(item->valuestring, "reset") == 0) {
            out_event->has_reset = true;
        }

        item = find_key_recursive(root, "command_id");
        if (json_item_to_u32(item, &out_event->command_id)) {
            out_event->has_command_id = true;
        }

        /*
         * The cloud response also carries normal node state fields such as
         * report_mode/upload_points for UI synchronization.  They are not
         * server commands unless the response includes a persisted command_id
         * (or the legacy reset command string).  Without this guard ESP32
         * re-emits every heartbeat response as a SERVER_COMMAND event, causing
         * STM32 to repeatedly re-apply report mode while full upload is active.
         */
        if (!out_event->has_command_id && !out_event->has_reset) {
            cJSON_Delete(root);
            return false;
        }

        item = find_key_recursive(root, "report_mode");
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            if (strcmp(item->valuestring, "full") == 0) {
                out_event->has_report_mode = true;
                out_event->report_mode = REPORT_MODE_FULL;
            } else if (strcmp(item->valuestring, "summary") == 0) {
                out_event->has_report_mode = true;
                out_event->report_mode = REPORT_MODE_SUMMARY;
            }
        }

        item = find_key_recursive(root, "downsample_step");
        if (json_item_to_u32(item, &out_event->downsample_step)) {
            out_event->has_downsample_step = true;
        }

        item = find_key_recursive(root, "upload_points");
        if (json_item_to_u32(item, &out_event->upload_points)) {
            out_event->has_upload_points = true;
        }

        item = find_key_recursive(root, "heartbeat_ms");
        if (json_item_to_u32(item, &out_event->heartbeat_ms)) {
            out_event->has_heartbeat_ms = true;
        }

        item = find_key_recursive(root, "min_interval_ms");
        if (json_item_to_u32(item, &out_event->min_interval_ms)) {
            out_event->has_min_interval_ms = true;
        }

        item = find_key_recursive(root, "http_timeout_ms");
        if (json_item_to_u32(item, &out_event->http_timeout_ms)) {
            out_event->has_http_timeout_ms = true;
        }

        item = find_key_recursive(root, "chunk_kb");
        if (json_item_to_u32(item, &out_event->chunk_kb)) {
            out_event->has_chunk_kb = true;
        }

        item = find_key_recursive(root, "chunk_delay_ms");
        if (json_item_to_u32(item, &out_event->chunk_delay_ms)) {
            out_event->has_chunk_delay_ms = true;
        }
        cJSON_Delete(root);
    } else {
        if (strstr(body, "\"command\":\"reset\"") != NULL || strstr(body, "command=reset") != NULL) {
            out_event->has_reset = true;
        }
        out_event->has_command_id = fallback_parse_u32(body, "command_id", &out_event->command_id);
        if (!out_event->has_command_id && !out_event->has_reset) {
            return false;
        }
        if (strstr(body, "\"report_mode\":\"full\"") != NULL || strstr(body, "report_mode=full") != NULL) {
            out_event->has_report_mode = true;
            out_event->report_mode = REPORT_MODE_FULL;
        } else if (strstr(body, "\"report_mode\":\"summary\"") != NULL || strstr(body, "report_mode=summary") != NULL) {
            out_event->has_report_mode = true;
            out_event->report_mode = REPORT_MODE_SUMMARY;
        }
        out_event->has_downsample_step = fallback_parse_u32(body, "downsample_step", &out_event->downsample_step);
        out_event->has_upload_points = fallback_parse_u32(body, "upload_points", &out_event->upload_points);
        out_event->has_heartbeat_ms = fallback_parse_u32(body, "heartbeat_ms", &out_event->heartbeat_ms);
        out_event->has_min_interval_ms = fallback_parse_u32(body, "min_interval_ms", &out_event->min_interval_ms);
        out_event->has_http_timeout_ms = fallback_parse_u32(body, "http_timeout_ms", &out_event->http_timeout_ms);
        out_event->has_chunk_kb = fallback_parse_u32(body, "chunk_kb", &out_event->chunk_kb);
        out_event->has_chunk_delay_ms = fallback_parse_u32(body, "chunk_delay_ms", &out_event->chunk_delay_ms);
    }

    return out_event->has_reset ||
           out_event->has_report_mode ||
           out_event->has_downsample_step ||
           out_event->has_upload_points ||
           out_event->has_heartbeat_ms ||
           out_event->has_min_interval_ms ||
           out_event->has_http_timeout_ms ||
           out_event->has_chunk_kb ||
           out_event->has_chunk_delay_ms;
}
