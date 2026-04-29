#include "report_buffer.h"

#include <stdlib.h>
#include <string.h>

static report_frame_t *s_inflight;

#ifndef REPORT_FULL_MAX_WAVEFORM_COUNT
#define REPORT_FULL_MAX_WAVEFORM_COUNT 4096U
#endif
#ifndef REPORT_FULL_MAX_FFT_COUNT
#define REPORT_FULL_MAX_FFT_COUNT 2048U
#endif
#ifndef REPORT_FULL_MAX_ALLOC_BYTES
#define REPORT_FULL_MAX_ALLOC_BYTES (128U * 1024U)
#endif

typedef struct {
    uint8_t *waveform_received;
    uint8_t *fft_received;
} report_channel_tracking_t;

static report_channel_tracking_t s_tracking[REPORT_MAX_CHANNELS];

/*
 * Full frames are large (~85 KB for 4ch x 4096 waveform + 2048 FFT).
 * Repeated malloc/free every few seconds eventually fragments ESP32 heap and
 * was observed to make full uploads fail after long runs.  Keep one reusable
 * full-frame assembly buffer in static RAM.  STM32 sends the next full frame
 * only after the previous HTTP result, so one in-flight full buffer is enough
 * for the normal producer/consumer flow.
 */
static report_frame_t s_full_frame_storage;
static bool s_full_frame_in_use;
static int32_t s_full_waveform_storage[REPORT_MAX_CHANNELS][REPORT_FULL_MAX_WAVEFORM_COUNT];
static int16_t s_full_fft_storage[REPORT_MAX_CHANNELS][REPORT_FULL_MAX_FFT_COUNT];
static uint8_t s_full_waveform_received_storage[REPORT_MAX_CHANNELS][(REPORT_FULL_MAX_WAVEFORM_COUNT + 7U) / 8U];
static uint8_t s_full_fft_received_storage[REPORT_MAX_CHANNELS][(REPORT_FULL_MAX_FFT_COUNT + 7U) / 8U];

static void copy_fixed_string(char *dst, size_t dst_size, const char *src, size_t src_size)
{
    size_t copy_len;

    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL || src_size == 0U) {
        dst[0] = '\0';
        return;
    }

    copy_len = strnlen(src, src_size);
    if (copy_len >= dst_size) {
        copy_len = dst_size - 1U;
    }
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

static size_t bitset_size_for_count(size_t count)
{
    return (count + 7U) / 8U;
}

static void bitset_mark_range(uint8_t *bitset, size_t offset, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        size_t idx = offset + i;
        bitset[idx / 8U] |= (uint8_t) (1U << (idx % 8U));
    }
}

static bool bitset_all_marked(const uint8_t *bitset, size_t count)
{
    size_t full_bytes = count / 8U;
    size_t tail_bits = count % 8U;

    for (size_t i = 0; i < full_bytes; ++i) {
        if (bitset[i] != 0xFFU) {
            return false;
        }
    }
    if (tail_bits > 0U) {
        uint8_t expected = (uint8_t) ((1U << tail_bits) - 1U);
        if (bitset[full_bytes] != expected) {
            return false;
        }
    }
    return true;
}

static void free_tracking(report_channel_tracking_t *tracking)
{
    if (tracking == NULL) {
        return;
    }
    for (size_t i = 0; i < REPORT_MAX_CHANNELS; ++i) {
        if (tracking[i].waveform_received != NULL &&
            tracking[i].waveform_received != s_full_waveform_received_storage[i]) {
            free(tracking[i].waveform_received);
        }
        if (tracking[i].fft_received != NULL &&
            tracking[i].fft_received != s_full_fft_received_storage[i]) {
            free(tracking[i].fft_received);
        }
        tracking[i].waveform_received = NULL;
        tracking[i].fft_received = NULL;
    }
}

static void report_frame_fill_from_summary(report_frame_t *frame,
                                           const protocol_report_summary_payload_t *payload)
{
    if (frame == NULL || payload == NULL) {
        return;
    }

    frame->frame_id = payload->frame_id;
    frame->timestamp_ms = payload->timestamp_ms;
    frame->mode = (payload->report_mode == REPORT_MODE_FULL) ? REPORT_MODE_FULL : REPORT_MODE_SUMMARY;
    frame->status = (payload->status_code == DEVICE_STATUS_ONLINE) ? DEVICE_STATUS_ONLINE : DEVICE_STATUS_OFFLINE;
    frame->downsample_step = payload->downsample_step;
    frame->upload_points = payload->upload_points;
    frame->channel_count = payload->channel_count > REPORT_MAX_CHANNELS ? REPORT_MAX_CHANNELS : payload->channel_count;
    copy_fixed_string(frame->fault_code, sizeof(frame->fault_code), payload->fault_code, sizeof(payload->fault_code));

    for (size_t i = 0; i < frame->channel_count; ++i) {
        const protocol_channel_summary_t *src = &payload->channels[i];
        report_channel_data_t *dst = &frame->channels[i];
        dst->channel_id = src->channel_id;
        dst->value_scaled = src->value_scaled;
        dst->current_value_scaled = src->current_value_scaled;
        dst->waveform_count = src->waveform_count;
        dst->fft_count = src->fft_count;
    }
}

static report_frame_t *report_frame_alloc_from_summary(const protocol_report_summary_payload_t *payload)
{
    report_frame_t *frame = (report_frame_t *) calloc(1, sizeof(report_frame_t));
    if (frame == NULL) {
        return NULL;
    }

    report_frame_fill_from_summary(frame, payload);
    return frame;
}

void report_frame_free(report_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }
    if (frame == &s_full_frame_storage) {
        s_full_frame_in_use = false;
        if (s_inflight == frame) {
            s_inflight = NULL;
        }
        return;
    }
    for (size_t i = 0; i < frame->channel_count; ++i) {
        free(frame->channels[i].waveform_scaled);
        free(frame->channels[i].fft_tenths);
    }
    free(frame);
}

void report_buffer_init(void)
{
    s_inflight = NULL;
    s_full_frame_in_use = false;
    memset(&s_full_frame_storage, 0, sizeof(s_full_frame_storage));
    memset(s_tracking, 0, sizeof(s_tracking));
}

void report_buffer_reset(void)
{
    report_frame_free(s_inflight);
    s_inflight = NULL;
    free_tracking(s_tracking);
}

esp_err_t report_buffer_ingest_summary(const protocol_report_summary_payload_t *payload,
                                       size_t payload_len,
                                       report_frame_t **out_frame)
{
    report_frame_t *frame;

    if (payload == NULL || out_frame == NULL || payload_len < sizeof(*payload)) {
        return ESP_ERR_INVALID_ARG;
    }

    frame = report_frame_alloc_from_summary(payload);
    if (frame == NULL) {
        return ESP_ERR_NO_MEM;
    }

    *out_frame = frame;
    return ESP_OK;
}

esp_err_t report_buffer_begin_full(const protocol_report_full_begin_payload_t *payload, size_t payload_len)
{
    report_frame_t *frame;
    report_channel_tracking_t tracking[REPORT_MAX_CHANNELS] = { 0 };

    if (payload == NULL || payload_len < sizeof(*payload)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_inflight != NULL && payload->frame_id == s_inflight->frame_id) {
        return ESP_OK;
    }
    if (s_full_frame_in_use && s_inflight == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    report_buffer_reset();
    frame = &s_full_frame_storage;
    memset(frame, 0, sizeof(*frame));
    report_frame_fill_from_summary(frame, payload);

    {
        size_t alloc_bytes = 0U;
        if (frame->channel_count == 0U || frame->channel_count > REPORT_MAX_CHANNELS) {
            return ESP_ERR_INVALID_SIZE;
        }
        for (size_t i = 0; i < frame->channel_count; ++i) {
            const report_channel_data_t *ch = &frame->channels[i];
            if (ch->waveform_count > REPORT_FULL_MAX_WAVEFORM_COUNT ||
                ch->fft_count > REPORT_FULL_MAX_FFT_COUNT) {
                return ESP_ERR_INVALID_SIZE;
            }
            alloc_bytes += ((size_t) ch->waveform_count * sizeof(int32_t)) +
                           ((size_t) ch->fft_count * sizeof(int16_t)) +
                           bitset_size_for_count(ch->waveform_count) +
                           bitset_size_for_count(ch->fft_count);
        }
        if (alloc_bytes > REPORT_FULL_MAX_ALLOC_BYTES) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    for (size_t i = 0; i < frame->channel_count; ++i) {
        report_channel_data_t *ch = &frame->channels[i];
        if (ch->waveform_count > 0U) {
            ch->waveform_scaled = s_full_waveform_storage[i];
            memset(ch->waveform_scaled, 0, ch->waveform_count * sizeof(int32_t));
            tracking[i].waveform_received = s_full_waveform_received_storage[i];
            memset(tracking[i].waveform_received, 0, bitset_size_for_count(ch->waveform_count));
        }
        if (ch->fft_count > 0U) {
            ch->fft_tenths = s_full_fft_storage[i];
            memset(ch->fft_tenths, 0, ch->fft_count * sizeof(int16_t));
            tracking[i].fft_received = s_full_fft_received_storage[i];
            memset(tracking[i].fft_received, 0, bitset_size_for_count(ch->fft_count));
        }
    }

    s_inflight = frame;
    s_full_frame_in_use = true;
    memcpy(s_tracking, tracking, sizeof(s_tracking));
    return ESP_OK;
}

static report_channel_data_t *find_channel(report_frame_t *frame, uint8_t channel_id)
{
    for (size_t i = 0; i < frame->channel_count; ++i) {
        if (frame->channels[i].channel_id == channel_id) {
            return &frame->channels[i];
        }
    }
    return NULL;
}

esp_err_t report_buffer_ingest_chunk(protocol_msg_type_t msg_type,
                                     const protocol_report_chunk_prefix_t *prefix,
                                     size_t prefix_len,
                                     const uint8_t *data,
                                     size_t data_len)
{
    report_channel_data_t *ch;
    size_t channel_index;
    size_t bytes_needed;

    if (s_inflight == NULL || prefix == NULL || data == NULL || prefix_len < sizeof(*prefix)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (prefix->frame_id != s_inflight->frame_id) {
        return ESP_ERR_INVALID_STATE;
    }

    ch = find_channel(s_inflight, prefix->channel_id);
    if (ch == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    channel_index = (size_t) (ch - s_inflight->channels);

    if (msg_type == PROTOCOL_MSG_REPORT_FULL_WAVE_CHUNK) {
        if (prefix->element_offset + prefix->element_count > ch->waveform_count) {
            return ESP_ERR_INVALID_SIZE;
        }
        bytes_needed = (size_t) prefix->element_count * sizeof(int32_t);
        if (data_len != bytes_needed) {
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(&ch->waveform_scaled[prefix->element_offset], data, bytes_needed);
        bitset_mark_range(s_tracking[channel_index].waveform_received, prefix->element_offset, prefix->element_count);
        return ESP_OK;
    }

    if (msg_type == PROTOCOL_MSG_REPORT_FULL_FFT_CHUNK) {
        if (prefix->element_offset + prefix->element_count > ch->fft_count) {
            return ESP_ERR_INVALID_SIZE;
        }
        bytes_needed = (size_t) prefix->element_count * sizeof(int16_t);
        if (data_len != bytes_needed) {
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(&ch->fft_tenths[prefix->element_offset], data, bytes_needed);
        bitset_mark_range(s_tracking[channel_index].fft_received, prefix->element_offset, prefix->element_count);
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}

esp_err_t report_buffer_finalize_full(const protocol_report_end_payload_t *payload,
                                      size_t payload_len,
                                      report_frame_t **out_frame)
{
    if (out_frame == NULL || payload == NULL || payload_len < sizeof(*payload)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_inflight == NULL || payload->frame_id != s_inflight->frame_id) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < s_inflight->channel_count; ++i) {
        report_channel_data_t *ch = &s_inflight->channels[i];
        if (ch->waveform_count > 0U &&
            !bitset_all_marked(s_tracking[i].waveform_received, ch->waveform_count)) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (ch->fft_count > 0U &&
            !bitset_all_marked(s_tracking[i].fft_received, ch->fft_count)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    *out_frame = s_inflight;
    s_inflight = NULL;
    free_tracking(s_tracking);
    return ESP_OK;
}
