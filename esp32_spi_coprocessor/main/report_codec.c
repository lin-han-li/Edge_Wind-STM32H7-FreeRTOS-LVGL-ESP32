#include "report_codec.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

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
                                esp_http_client_handle_t client)
{
    memset(builder, 0, sizeof(*builder));
    builder->mode = BUILDER_MODE_STREAM;
    builder->buf = buffer;
    builder->cap = capacity;
    builder->client = client;
    if (builder->cap > 0U) {
        builder->buf[0] = '\0';
    }
}

static bool builder_flush(json_builder_t *builder)
{
    size_t offset = 0;

    if (builder->mode != BUILDER_MODE_STREAM || builder->len == 0U) {
        return true;
    }

    while (offset < builder->len) {
        int written = esp_http_client_write(builder->client, builder->buf + offset, (int) (builder->len - offset));
        if (written <= 0) {
            builder->failed = true;
            return false;
        }
        offset += (size_t) written;
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

    if (builder->failed) {
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

static void builder_append_i32_array(json_builder_t *builder, const int32_t *values, size_t count)
{
    builder_append(builder, "[");
    for (size_t i = 0; i < count; ++i) {
        builder_append(builder, (i == 0U) ? "%" PRId32 : ",%" PRId32, values[i]);
    }
    builder_append(builder, "]");
}

static void builder_append_i16_tenths_array(json_builder_t *builder, const int16_t *values, size_t count)
{
    builder_append(builder, "[");
    for (size_t i = 0; i < count; ++i) {
        int32_t raw = values[i];
        int32_t abs_raw = raw < 0 ? -raw : raw;
        builder_append(builder,
                       (i == 0U) ? "%s%" PRId32 ".%" PRId32 : ",%s%" PRId32 ".%" PRId32,
                       raw < 0 ? "-" : "",
                       abs_raw / 10,
                       abs_raw % 10);
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
        builder_append(builder, ",\"value\":%" PRId32 ",\"current_value\":%" PRId32 ",", channel->value_scaled, channel->current_value_scaled);
        builder_append(builder, "\"unit\":");
        builder_append_json_string(builder, meta->unit);

        if (frame->mode == REPORT_MODE_FULL) {
            builder_append(builder, ",\"waveform\":");
            builder_append_i32_array(builder,
                                     channel->waveform_scaled,
                                     channel->waveform_scaled != NULL ? channel->waveform_count : 0U);
            builder_append(builder, ",\"fft_spectrum\":");
            builder_append_i16_tenths_array(builder,
                                            channel->fft_tenths,
                                            channel->fft_tenths != NULL ? channel->fft_count : 0U);
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
        !cJSON_AddStringToObject(root, "fault_code", fault)) {
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
                                             esp_http_client_handle_t client)
{
    json_builder_t builder;

    if (config == NULL || frame == NULL || scratch == NULL || scratch_len == 0U || client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    builder_init_stream(&builder, scratch, scratch_len, client);
    return emit_heartbeat_json(&builder, config, frame) ? ESP_OK : ESP_FAIL;
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
        if (cJSON_IsNumber(item)) {
            out_event->has_downsample_step = true;
            out_event->downsample_step = (uint32_t) item->valuedouble;
        }

        item = find_key_recursive(root, "upload_points");
        if (cJSON_IsNumber(item)) {
            out_event->has_upload_points = true;
            out_event->upload_points = (uint32_t) item->valuedouble;
        }
        cJSON_Delete(root);
    } else {
        if (strstr(body, "\"command\":\"reset\"") != NULL || strstr(body, "command=reset") != NULL) {
            out_event->has_reset = true;
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
    }

    return out_event->has_reset ||
           out_event->has_report_mode ||
           out_event->has_downsample_step ||
           out_event->has_upload_points;
}
