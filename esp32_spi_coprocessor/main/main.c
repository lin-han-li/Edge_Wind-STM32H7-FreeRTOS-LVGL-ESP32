#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "board_support.h"
#include "cloud_client.h"
#include "comm_protocol.h"
#include "report_buffer.h"
#include "spi_link.h"
#include "wifi_manager.h"

static const char *TAG = "spi_coproc";

#define APP_EVENT_QUEUE_LEN 32
#define APP_EVENT_QUEUE_TIMEOUT_MS 100
#define SPI_PACKET_QUEUE_TIMEOUT_MS 500
#define CAPABILITY_SPI_SLAVE (1U << 0)
#define CAPABILITY_HTTP_CLIENT (1U << 1)
#define CAPABILITY_REGISTER (1U << 2)
#define CAPABILITY_HEARTBEAT (1U << 3)
#define CAPABILITY_SERVER_COMMAND (1U << 4)

#ifndef APP_BOOT_AUTO_RECONNECT
#define APP_BOOT_AUTO_RECONNECT 0
#endif

typedef enum {
    APP_EVENT_WIFI = 1,
    APP_EVENT_CLOUD = 2,
} app_event_kind_t;

typedef struct {
    app_event_kind_t kind;
    union {
        wifi_manager_event_t wifi;
        cloud_client_event_t cloud;
    } data;
} app_event_t;

static QueueHandle_t s_app_event_queue;
static SemaphoreHandle_t s_status_mutex;
static app_status_snapshot_t s_status;
static uint32_t s_session_epoch;
static uint32_t s_last_processed_seq;
static uint32_t s_last_processed_session;

static void lock_status(void)
{
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
}

static void unlock_status(void)
{
    xSemaphoreGive(s_status_mutex);
}

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

static void copy_fixed_string(char *dst, size_t dst_size, const char *src, size_t src_len)
{
    size_t copy_len;

    if (dst == NULL || dst_size == 0U) {
        return;
    }
    if (src == NULL || src_len == 0U) {
        dst[0] = '\0';
        return;
    }

    copy_len = strnlen(src, src_len);
    if (copy_len >= dst_size) {
        copy_len = dst_size - 1U;
    }
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

static bool enqueue_app_event(const app_event_t *app_event)
{
    if (s_app_event_queue == NULL || app_event == NULL) {
        return false;
    }
    return xQueueSend(s_app_event_queue, app_event, pdMS_TO_TICKS(APP_EVENT_QUEUE_TIMEOUT_MS)) == pdTRUE;
}

static void refresh_status_from_config(void)
{
    app_config_snapshot_t snapshot;
    app_config_get_snapshot(&snapshot);

    lock_status();
    copy_string(s_status.node_id, sizeof(s_status.node_id), snapshot.device.node_id);
    s_status.downsample_step = snapshot.comm.downsample_step;
    s_status.upload_points = snapshot.comm.upload_points;
    unlock_status();
}

static void bump_config_version_locked(void)
{
    s_status.config_version++;
    if (s_status.config_version == 0U) {
        s_status.config_version = 1U;
    }
}

static bool device_config_valid(const app_device_config_t *device)
{
    return device != NULL &&
           device->wifi_ssid[0] != '\0' &&
           device->server_host[0] != '\0' &&
           device->server_port != 0U &&
           device->node_id[0] != '\0';
}

static bool comm_params_valid(const app_comm_params_t *comm)
{
    return comm != NULL &&
           comm->heartbeat_ms >= 200U &&
           comm->heartbeat_ms < 55000U &&
           comm->http_timeout_ms >= 1000U &&
           comm->downsample_step >= 1U &&
           comm->downsample_step <= 64U &&
           comm->upload_points >= 256U &&
           comm->upload_points <= 4096U &&
           (comm->upload_points % 256U) == 0U &&
           comm->chunk_kb <= 256U &&
           comm->chunk_delay_ms <= 5000U;
}

static void get_status_snapshot(app_status_snapshot_t *out_status)
{
    lock_status();
    *out_status = s_status;
    unlock_status();
}

static esp_err_t persist_runtime_auto_reconnect(bool enabled)
{
    app_runtime_state_t state;

    app_config_get_runtime_state(&state);
    state.auto_reconnect_enabled = enabled;
    if (!enabled) {
        state.last_reporting = false;
    }
    return app_config_update_runtime_state(&state, true);
}

static esp_err_t persist_runtime_reporting(bool enabled, report_mode_t mode)
{
    app_runtime_state_t state;

    app_config_get_runtime_state(&state);
    state.auto_reconnect_enabled = enabled ? true : state.auto_reconnect_enabled;
    state.last_reporting = enabled;
    state.last_report_mode = (mode == REPORT_MODE_FULL) ? REPORT_MODE_FULL : REPORT_MODE_SUMMARY;
    return app_config_update_runtime_state(&state, true);
}

static protocol_result_code_t map_err_to_result(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return PROTOCOL_RESULT_OK;
    case ESP_FAIL:
        return PROTOCOL_RESULT_IO_ERROR;
    case ESP_ERR_INVALID_ARG:
        return PROTOCOL_RESULT_INVALID_ARG;
    case ESP_ERR_TIMEOUT:
        return PROTOCOL_RESULT_BUSY;
    case ESP_ERR_INVALID_STATE:
        return PROTOCOL_RESULT_NOT_READY;
    case ESP_ERR_NO_MEM:
        return PROTOCOL_RESULT_QUEUE_FULL;
    case ESP_ERR_HTTP_CONNECT:
    case ESP_ERR_HTTP_WRITE_DATA:
    case ESP_ERR_HTTP_FETCH_HEADER:
    case ESP_ERR_HTTP_INVALID_TRANSPORT:
    case ESP_ERR_HTTP_CONNECTING:
    case ESP_ERR_HTTP_EAGAIN:
    case ESP_ERR_HTTP_CONNECTION_CLOSED:
    case ESP_ERR_HTTP_READ_TIMEOUT:
    case ESP_ERR_HTTP_INCOMPLETE_DATA:
        return PROTOCOL_RESULT_IO_ERROR;
    default:
        return PROTOCOL_RESULT_INTERNAL;
    }
}

static protocol_result_code_t map_transport_and_http_to_result(esp_err_t err, int http_status)
{
    if (err != ESP_OK) {
        return map_err_to_result(err);
    }
    return (http_status >= 200 && http_status < 300) ? PROTOCOL_RESULT_OK : PROTOCOL_RESULT_IO_ERROR;
}

static bool protocol_requires_current_session(protocol_msg_type_t msg_type)
{
    switch (msg_type) {
    case PROTOCOL_MSG_HELLO_REQ:
    case PROTOCOL_MSG_QUERY_STATUS_REQ:
    case PROTOCOL_MSG_PING_REQ:
        return false;
    default:
        return true;
    }
}

static bool protocol_has_side_effect(protocol_msg_type_t msg_type)
{
    switch (msg_type) {
    case PROTOCOL_MSG_SET_DEVICE_CONFIG_REQ:
    case PROTOCOL_MSG_SET_COMM_PARAMS_REQ:
    case PROTOCOL_MSG_CONNECT_REQ:
    case PROTOCOL_MSG_CLOUD_CONNECT_REQ:
    case PROTOCOL_MSG_REGISTER_REQ:
    case PROTOCOL_MSG_DISCONNECT_REQ:
    case PROTOCOL_MSG_START_REPORT_REQ:
    case PROTOCOL_MSG_STOP_REPORT_REQ:
    case PROTOCOL_MSG_REPORT_SUMMARY:
    case PROTOCOL_MSG_REPORT_FULL_BEGIN:
    case PROTOCOL_MSG_REPORT_FULL_WAVE_CHUNK:
    case PROTOCOL_MSG_REPORT_FULL_FFT_CHUNK:
    case PROTOCOL_MSG_REPORT_FULL_END:
        return true;
    default:
        return false;
    }
}

static bool protocol_is_duplicate_side_effect(const protocol_packet_t *packet, protocol_msg_type_t msg_type)
{
    if (!protocol_has_side_effect(msg_type) || packet->header.seq == 0U) {
        return false;
    }
    return packet->header.session_epoch == s_last_processed_session &&
           packet->header.seq <= s_last_processed_seq;
}

static void protocol_mark_processed(const protocol_packet_t *packet)
{
    if (packet->header.seq == 0U) {
        return;
    }
    s_last_processed_session = packet->header.session_epoch;
    s_last_processed_seq = packet->header.seq;
}

static void queue_packet(protocol_packet_t *packet)
{
    packet->header.session_epoch = s_session_epoch;
    protocol_packet_finalize(packet);
    if (spi_link_enqueue_tx(packet, pdMS_TO_TICKS(SPI_PACKET_QUEUE_TIMEOUT_MS)) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to queue TX packet %s", protocol_msg_type_name((protocol_msg_type_t) packet->header.msg_type));
    }
}

static void send_protocol_event(uint16_t event_type,
                                uint16_t result_code,
                                uint32_t value0,
                                uint32_t value1,
                                int32_t value2,
                                const char *text)
{
    protocol_packet_t packet;
    protocol_event_payload_t payload = {
        .event_type = event_type,
        .result_code = result_code,
        .value0 = value0,
        .value1 = value1,
        .value2 = value2,
    };

    if (text != NULL) {
        copy_string(payload.text, sizeof(payload.text), text);
    }

    protocol_packet_prepare(&packet, PROTOCOL_MSG_EVENT, s_session_epoch, 0, 0, 0);
    if (protocol_packet_set_payload(&packet, &payload, sizeof(payload)) == ESP_OK) {
        queue_packet(&packet);
    }
}

static void send_simple_response(protocol_msg_type_t response_type,
                                 protocol_result_code_t result,
                                 const char *text)
{
    protocol_packet_t packet;
    protocol_event_payload_t payload = {
        .event_type = 0,
        .result_code = result,
    };

    if (text != NULL) {
        copy_string(payload.text, sizeof(payload.text), text);
    }

    protocol_packet_prepare(&packet, response_type, s_session_epoch, 0, 0, 0);
    if (protocol_packet_set_payload(&packet, &payload, sizeof(payload)) == ESP_OK) {
        queue_packet(&packet);
    }
}

static void send_nack(uint32_t ref_seq, protocol_nack_reason_t reason)
{
    protocol_packet_t packet;
    protocol_nack_payload_t payload = {
        .ref_seq = ref_seq,
        .reason = (uint16_t) reason,
    };

    protocol_packet_prepare(&packet, PROTOCOL_MSG_NACK, s_session_epoch, 0, 0, 0);
    if (protocol_packet_set_payload(&packet, &payload, sizeof(payload)) == ESP_OK) {
        queue_packet(&packet);
    }
}

static void send_tx_accepted(uint32_t ref_seq, uint32_t frame_id)
{
    protocol_packet_t packet;
    protocol_tx_accepted_payload_t payload = {
        .ref_seq = ref_seq,
        .ref_frame_id = frame_id,
        .queue_depth = 0,
    };

    protocol_packet_prepare(&packet, PROTOCOL_MSG_TX_ACCEPTED, s_session_epoch, 0, 0, 0);
    if (protocol_packet_set_payload(&packet, &payload, sizeof(payload)) == ESP_OK) {
        queue_packet(&packet);
    }
}

static void send_tx_result(uint32_t ref_seq, uint32_t frame_id, int http_status, int result_code)
{
    protocol_packet_t packet;
    protocol_tx_result_payload_t payload = {
        .ref_seq = ref_seq,
        .ref_frame_id = frame_id,
        .http_status = http_status,
        .result_code = result_code,
    };

    protocol_packet_prepare(&packet, PROTOCOL_MSG_TX_RESULT, s_session_epoch, 0, 0, 0);
    if (protocol_packet_set_payload(&packet, &payload, sizeof(payload)) == ESP_OK) {
        queue_packet(&packet);
    }
}

static void send_status_snapshot(void)
{
    protocol_packet_t packet;
    protocol_status_payload_t payload;
    app_status_snapshot_t snapshot;

    get_status_snapshot(&snapshot);
    protocol_fill_status_payload(&payload, &snapshot);

    protocol_packet_prepare(&packet, PROTOCOL_MSG_STATUS_RESP, s_session_epoch, 0, 0, 0);
    if (protocol_packet_set_payload(&packet, &payload, sizeof(payload)) == ESP_OK) {
        queue_packet(&packet);
    }
}

static void send_hello_response(void)
{
    protocol_packet_t packet;
    protocol_hello_payload_t payload = {
        .boot_epoch = s_session_epoch,
        .capability_flags = CAPABILITY_SPI_SLAVE |
                            CAPABILITY_HTTP_CLIENT |
                            CAPABILITY_REGISTER |
                            CAPABILITY_HEARTBEAT |
                            CAPABILITY_SERVER_COMMAND,
        .max_payload = PROTOCOL_MAX_PAYLOAD,
        .reserved = 0,
    };

    protocol_packet_prepare(&packet, PROTOCOL_MSG_HELLO_RESP, s_session_epoch, 0, 0, 0);
    if (protocol_packet_set_payload(&packet, &payload, sizeof(payload)) == ESP_OK) {
        queue_packet(&packet);
    }
}

static esp_err_t submit_frame(report_frame_t *frame, uint32_t ref_seq)
{
    esp_err_t err;
    report_mode_t current_mode;

    lock_status();
    if (!s_status.reporting_enabled) {
        s_status.last_error_code = (int32_t) ESP_ERR_INVALID_STATE;
        unlock_status();
        report_frame_free(frame);
        send_nack(ref_seq, PROTOCOL_NACK_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    current_mode = s_status.report_mode;
    if (frame == NULL || frame->mode != current_mode) {
        s_status.last_error_code = (int32_t) ESP_ERR_INVALID_STATE;
        unlock_status();
        report_frame_free(frame);
        send_nack(ref_seq, PROTOCOL_NACK_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    unlock_status();

    err = cloud_client_submit_frame(frame);
    if (err != ESP_OK) {
        lock_status();
        s_status.last_error_code = (int32_t) err;
        unlock_status();
        report_frame_free(frame);
        send_nack(ref_seq, (err == ESP_ERR_TIMEOUT) ? PROTOCOL_NACK_QUEUE_FULL : PROTOCOL_NACK_BUSY);
        return err;
    }

    lock_status();
    s_status.last_frame_id = frame->frame_id;
    s_status.last_error_code = 0;
    unlock_status();
    send_tx_accepted(ref_seq, frame->frame_id);
    return ESP_OK;
}

static void apply_server_command_event(const server_command_event_t *command)
{
    esp_err_t err = ESP_OK;
    uint32_t command_id = 0U;

    if (command == NULL) {
        return;
    }

    if (command->has_command_id) {
        command_id = command->command_id;
    }

    ESP_LOGI(TAG,
             "server command id=%" PRIu32 " reset=%d mode=%d downsample=%d/%" PRIu32 " upload=%d/%" PRIu32
             " hb=%d/%" PRIu32 " min=%d/%" PRIu32 " http=%d/%" PRIu32 " chunk=%d/%" PRIu32 " delay=%d/%" PRIu32,
             command_id,
             command->has_reset ? 1 : 0,
             command->has_report_mode ? (int) command->report_mode : -1,
             command->has_downsample_step ? 1 : 0,
             command->downsample_step,
             command->has_upload_points ? 1 : 0,
             command->upload_points,
             command->has_heartbeat_ms ? 1 : 0,
             command->heartbeat_ms,
             command->has_min_interval_ms ? 1 : 0,
             command->min_interval_ms,
             command->has_http_timeout_ms ? 1 : 0,
             command->http_timeout_ms,
             command->has_chunk_kb ? 1 : 0,
             command->chunk_kb,
             command->has_chunk_delay_ms ? 1 : 0,
             command->chunk_delay_ms);

    if ((command->has_downsample_step && (command->downsample_step < 1U || command->downsample_step > 64U)) ||
        (command->has_upload_points && (command->upload_points < 256U || command->upload_points > 4096U || (command->upload_points % 256U) != 0U)) ||
        (command->has_heartbeat_ms && (command->heartbeat_ms < 200U || command->heartbeat_ms >= 55000U)) ||
        (command->has_min_interval_ms && command->min_interval_ms > 600000U) ||
        (command->has_http_timeout_ms && (command->http_timeout_ms < 1000U || command->http_timeout_ms > 600000U)) ||
        (command->has_chunk_kb && command->chunk_kb > 16U) ||
        (command->has_chunk_delay_ms && command->chunk_delay_ms > 200U)) {
        err = ESP_ERR_INVALID_ARG;
    }

    /* STM32 SD is the authority.  ESP32 only validates and delivers cloud
     * commands as SPI events; STM32 writes SD and then re-applies runtime
     * SET_* / START_REPORT commands back to ESP32.
     *
     * Also mirror valid target values into STATUS as a diagnostic/fallback
     * cache.  This does not make ESP32 the authority, but it lets STM32/UI see
     * the last cloud command even if the async event has not been consumed yet.
     */
    lock_status();
    s_status.last_command_id = command_id;
    s_status.last_error_code = (err == ESP_OK) ? 0 : (int32_t) err;
    if (err == ESP_OK) {
        if (command->has_report_mode) {
            s_status.report_mode = command->report_mode;
        }
        if (command->has_downsample_step) {
            s_status.downsample_step = command->downsample_step;
        }
        if (command->has_upload_points) {
            s_status.upload_points = command->upload_points;
        }
    }
    unlock_status();

    if (command->has_reset) {
        send_protocol_event(PROTOCOL_EVENT_SERVER_COMMAND, PROTOCOL_RESULT_OK, 1, 0, 0, "reset");
    }
    if (command->has_report_mode) {
        send_protocol_event(PROTOCOL_EVENT_SERVER_COMMAND,
                            PROTOCOL_RESULT_OK,
                            2,
                            (uint32_t) command->report_mode,
                            0,
                            "report_mode");
    }
    if (command->has_downsample_step) {
        send_protocol_event(PROTOCOL_EVENT_SERVER_COMMAND,
                            map_err_to_result(err),
                            3,
                            command->downsample_step,
                            0,
                            "downsample_step");
    }
    if (command->has_upload_points) {
        send_protocol_event(PROTOCOL_EVENT_SERVER_COMMAND,
                            map_err_to_result(err),
                            4,
                            command->upload_points,
                            0,
                            "upload_points");
    }
    if (command->has_heartbeat_ms) {
        send_protocol_event(PROTOCOL_EVENT_SERVER_COMMAND,
                            map_err_to_result(err),
                            5,
                            command->heartbeat_ms,
                            0,
                            "heartbeat_ms");
    }
    if (command->has_min_interval_ms) {
        send_protocol_event(PROTOCOL_EVENT_SERVER_COMMAND,
                            map_err_to_result(err),
                            6,
                            command->min_interval_ms,
                            0,
                            "min_interval_ms");
    }
    if (command->has_http_timeout_ms) {
        send_protocol_event(PROTOCOL_EVENT_SERVER_COMMAND,
                            map_err_to_result(err),
                            7,
                            command->http_timeout_ms,
                            0,
                            "http_timeout_ms");
    }
    if (command->has_chunk_kb) {
        send_protocol_event(PROTOCOL_EVENT_SERVER_COMMAND,
                            map_err_to_result(err),
                            8,
                            command->chunk_kb,
                            0,
                            "chunk_kb");
    }
    if (command->has_chunk_delay_ms) {
        send_protocol_event(PROTOCOL_EVENT_SERVER_COMMAND,
                            map_err_to_result(err),
                            9,
                            command->chunk_delay_ms,
                            0,
                            "chunk_delay_ms");
    }
}

static void handle_set_device_config(const protocol_packet_t *packet)
{
    protocol_device_config_payload_t payload;
    app_device_config_t device;
    app_config_snapshot_t old_snapshot;
    app_config_snapshot_t snapshot;
    esp_err_t err;
    esp_err_t apply_err = ESP_OK;

    if (packet->header.payload_len < sizeof(payload)) {
        send_nack(packet->header.seq, PROTOCOL_NACK_INVALID_PAYLOAD);
        return;
    }

    memcpy(&payload, packet->payload, sizeof(payload));
    memset(&device, 0, sizeof(device));
    copy_fixed_string(device.wifi_ssid, sizeof(device.wifi_ssid), payload.wifi_ssid, sizeof(payload.wifi_ssid));
    copy_fixed_string(device.wifi_password, sizeof(device.wifi_password), payload.wifi_password, sizeof(payload.wifi_password));
    copy_fixed_string(device.server_host, sizeof(device.server_host), payload.server_host, sizeof(payload.server_host));
    device.server_port = payload.server_port;
    copy_fixed_string(device.node_id, sizeof(device.node_id), payload.node_id, sizeof(payload.node_id));
    copy_fixed_string(device.node_location, sizeof(device.node_location), payload.node_location, sizeof(payload.node_location));
    copy_fixed_string(device.hw_version, sizeof(device.hw_version), payload.hw_version, sizeof(payload.hw_version));

    if (!device_config_valid(&device)) {
        lock_status();
        s_status.last_error_code = (int32_t) ESP_ERR_INVALID_ARG;
        unlock_status();
        send_simple_response(PROTOCOL_MSG_SET_DEVICE_CONFIG_RESP, PROTOCOL_RESULT_INVALID_ARG, "config_invalid");
        return;
    }

    app_config_get_snapshot(&old_snapshot);
    err = app_config_update_device(&device, false);
    if (err == ESP_OK) {
        app_config_get_snapshot(&snapshot);
        apply_err = wifi_manager_apply_config(&snapshot.device);
        if (apply_err == ESP_OK) {
            apply_err = cloud_client_apply_snapshot(&snapshot);
        }
        refresh_status_from_config();
        if (apply_err == ESP_OK) {
            err = app_config_update_device(&device, true);
            if (err == ESP_OK) {
                lock_status();
                s_status.last_error_code = 0;
                bump_config_version_locked();
                unlock_status();
                send_simple_response(PROTOCOL_MSG_SET_DEVICE_CONFIG_RESP, PROTOCOL_RESULT_OK, "config_applied");
                send_protocol_event(PROTOCOL_EVENT_CONFIG_APPLIED, PROTOCOL_RESULT_OK, 0, 0, 0, "device_config");
                protocol_mark_processed(packet);
            } else {
                (void) app_config_update_device(&old_snapshot.device, false);
                (void) wifi_manager_apply_config(&old_snapshot.device);
                (void) cloud_client_apply_snapshot(&old_snapshot);
                refresh_status_from_config();
                lock_status();
                s_status.last_error_code = (int32_t) err;
                unlock_status();
                send_simple_response(PROTOCOL_MSG_SET_DEVICE_CONFIG_RESP, map_err_to_result(err), "config_persist_failed");
            }
        } else {
            (void) app_config_update_device(&old_snapshot.device, false);
            (void) wifi_manager_apply_config(&old_snapshot.device);
            (void) cloud_client_apply_snapshot(&old_snapshot);
            refresh_status_from_config();
            lock_status();
            s_status.last_error_code = (int32_t) apply_err;
            unlock_status();
            send_simple_response(PROTOCOL_MSG_SET_DEVICE_CONFIG_RESP, map_err_to_result(apply_err), "config_runtime_failed");
        }
    } else {
        lock_status();
        s_status.last_error_code = (int32_t) err;
        unlock_status();
        send_simple_response(PROTOCOL_MSG_SET_DEVICE_CONFIG_RESP, map_err_to_result(err), "config_failed");
    }
}

static void handle_set_comm_params(const protocol_packet_t *packet)
{
    protocol_comm_params_payload_t payload;
    app_comm_params_t comm;
    app_config_snapshot_t old_snapshot;
    app_config_snapshot_t snapshot;
    esp_err_t err;
    esp_err_t apply_err = ESP_OK;

    if (packet->header.payload_len < sizeof(payload)) {
        send_nack(packet->header.seq, PROTOCOL_NACK_INVALID_PAYLOAD);
        return;
    }

    memcpy(&payload, packet->payload, sizeof(payload));
    memset(&comm, 0, sizeof(comm));
    comm.heartbeat_ms = payload.heartbeat_ms;
    comm.min_interval_ms = payload.min_interval_ms;
    comm.http_timeout_ms = payload.http_timeout_ms;
    comm.reconnect_backoff_ms = payload.reconnect_backoff_ms;
    comm.downsample_step = payload.downsample_step;
    comm.upload_points = payload.upload_points;
    comm.hardreset_sec = payload.hardreset_sec;
    comm.chunk_kb = payload.chunk_kb;
    comm.chunk_delay_ms = payload.chunk_delay_ms;

    if (!comm_params_valid(&comm)) {
        lock_status();
        s_status.last_error_code = (int32_t) ESP_ERR_INVALID_ARG;
        unlock_status();
        send_simple_response(PROTOCOL_MSG_SET_COMM_PARAMS_RESP, PROTOCOL_RESULT_INVALID_ARG, "comm_params_invalid");
        return;
    }

    app_config_get_snapshot(&old_snapshot);
    err = app_config_update_comm(&comm, false);
    if (err == ESP_OK) {
        app_config_get_snapshot(&snapshot);
        apply_err = wifi_manager_set_reconnect_backoff_ms(snapshot.comm.reconnect_backoff_ms);
        if (apply_err == ESP_OK) {
            apply_err = cloud_client_apply_snapshot(&snapshot);
        }
        refresh_status_from_config();
        if (apply_err == ESP_OK) {
            err = app_config_update_comm(&snapshot.comm, true);
            if (err == ESP_OK) {
                lock_status();
                s_status.last_error_code = 0;
                bump_config_version_locked();
                unlock_status();
                send_simple_response(PROTOCOL_MSG_SET_COMM_PARAMS_RESP, PROTOCOL_RESULT_OK, "comm_params_applied");
                send_protocol_event(PROTOCOL_EVENT_CONFIG_APPLIED, PROTOCOL_RESULT_OK, 1, 0, 0, "comm_params");
                protocol_mark_processed(packet);
            } else {
                (void) app_config_update_comm(&old_snapshot.comm, false);
                (void) wifi_manager_set_reconnect_backoff_ms(old_snapshot.comm.reconnect_backoff_ms);
                (void) cloud_client_apply_snapshot(&old_snapshot);
                refresh_status_from_config();
                lock_status();
                s_status.last_error_code = (int32_t) err;
                unlock_status();
                send_simple_response(PROTOCOL_MSG_SET_COMM_PARAMS_RESP, map_err_to_result(err), "comm_params_persist_failed");
            }
        } else {
            (void) app_config_update_comm(&old_snapshot.comm, false);
            (void) wifi_manager_set_reconnect_backoff_ms(old_snapshot.comm.reconnect_backoff_ms);
            (void) cloud_client_apply_snapshot(&old_snapshot);
            refresh_status_from_config();
            lock_status();
            s_status.last_error_code = (int32_t) apply_err;
            unlock_status();
            send_simple_response(PROTOCOL_MSG_SET_COMM_PARAMS_RESP, map_err_to_result(apply_err), "comm_params_runtime_failed");
        }
    } else {
        lock_status();
        s_status.last_error_code = (int32_t) err;
        unlock_status();
        send_simple_response(PROTOCOL_MSG_SET_COMM_PARAMS_RESP, map_err_to_result(err), "comm_params_failed");
    }
}

static void handle_start_report(const protocol_packet_t *packet)
{
    protocol_start_report_payload_t payload = { 0 };
    esp_err_t err;
    report_mode_t requested_mode;

    if (packet->header.payload_len >= sizeof(payload)) {
        memcpy(&payload, packet->payload, sizeof(payload));
    }
    requested_mode = (payload.report_mode == REPORT_MODE_FULL) ? REPORT_MODE_FULL : REPORT_MODE_SUMMARY;
    err = cloud_client_set_reporting(true, requested_mode);
    if (err == ESP_OK) {
        lock_status();
        s_status.reporting_enabled = true;
        s_status.report_mode = requested_mode;
        s_status.last_error_code = 0;
        unlock_status();
        send_simple_response(PROTOCOL_MSG_START_REPORT_RESP, PROTOCOL_RESULT_OK, "report_started");
        (void) persist_runtime_reporting(true, requested_mode);
        protocol_mark_processed(packet);
    } else {
        lock_status();
        s_status.last_error_code = (int32_t) err;
        unlock_status();
        send_simple_response(PROTOCOL_MSG_START_REPORT_RESP, map_err_to_result(err), "report_start_failed");
    }
}

static void handle_stop_report(const protocol_packet_t *packet)
{
    esp_err_t err;
    report_mode_t current_mode;

    err = cloud_client_set_reporting(false, REPORT_MODE_SUMMARY);
    if (err == ESP_OK) {
        lock_status();
        current_mode = s_status.report_mode;
        s_status.reporting_enabled = false;
        s_status.last_error_code = 0;
        unlock_status();
        send_simple_response(PROTOCOL_MSG_STOP_REPORT_RESP, PROTOCOL_RESULT_OK, "report_stopped");
        (void) persist_runtime_reporting(false, current_mode);
        protocol_mark_processed(packet);
    } else {
        lock_status();
        s_status.last_error_code = (int32_t) err;
        unlock_status();
        send_simple_response(PROTOCOL_MSG_STOP_REPORT_RESP, map_err_to_result(err), "report_stop_failed");
    }
}

static void handle_protocol_packet(const protocol_packet_t *packet, size_t rx_bytes, void *ctx)
{
    report_frame_t *frame = NULL;
    protocol_report_chunk_prefix_t prefix;
    protocol_report_end_payload_t end_payload;
    esp_err_t err = ESP_OK;
    protocol_msg_type_t msg_type = (protocol_msg_type_t) packet->header.msg_type;

    (void) rx_bytes;
    (void) ctx;

    if (protocol_requires_current_session(msg_type) && packet->header.session_epoch != s_session_epoch) {
        send_nack(packet->header.seq, PROTOCOL_NACK_SESSION_MISMATCH);
        return;
    }
    if (protocol_is_duplicate_side_effect(packet, msg_type)) {
        ESP_LOGW(TAG, "Ignoring duplicate side-effect packet type=%s seq=%" PRIu32,
                 protocol_msg_type_name(msg_type),
                 packet->header.seq);
        return;
    }

    switch (msg_type) {
    case PROTOCOL_MSG_HELLO_REQ:
        send_hello_response();
        send_status_snapshot();
        break;

    case PROTOCOL_MSG_SET_DEVICE_CONFIG_REQ:
        handle_set_device_config(packet);
        break;

    case PROTOCOL_MSG_SET_COMM_PARAMS_REQ:
        handle_set_comm_params(packet);
        break;

    case PROTOCOL_MSG_CONNECT_REQ:
        err = wifi_manager_connect();
        send_simple_response(PROTOCOL_MSG_CONNECT_RESP, map_err_to_result(err), (err == ESP_OK) ? "wifi_connecting" : "wifi_connect_failed");
        if (err == ESP_OK) {
            (void) persist_runtime_auto_reconnect(true);
            protocol_mark_processed(packet);
        }
        break;

    case PROTOCOL_MSG_CLOUD_CONNECT_REQ:
        if (wifi_manager_is_connected()) {
            lock_status();
            s_status.cloud_connected = true;
            copy_string(s_status.last_error, sizeof(s_status.last_error), "cloud_ready");
            unlock_status();
            send_simple_response(PROTOCOL_MSG_CLOUD_CONNECT_RESP, PROTOCOL_RESULT_OK, "cloud_ready");
            send_protocol_event(PROTOCOL_EVENT_CLOUD_STATE, PROTOCOL_CLOUD_CONNECTING, 0, 0, 0, "cloud_ready");
            protocol_mark_processed(packet);
        } else {
            send_simple_response(PROTOCOL_MSG_CLOUD_CONNECT_RESP, PROTOCOL_RESULT_NOT_READY, "wifi_not_connected");
        }
        break;

    case PROTOCOL_MSG_REGISTER_REQ:
        if (!wifi_manager_is_connected()) {
            send_simple_response(PROTOCOL_MSG_REGISTER_RESP, PROTOCOL_RESULT_NOT_READY, "wifi_not_connected");
            break;
        }
        err = cloud_client_request_register();
        send_simple_response(PROTOCOL_MSG_REGISTER_RESP,
                             map_err_to_result(err),
                             (err == ESP_OK) ? "registering" : "register_queue_failed");
        if (err == ESP_OK) {
            protocol_mark_processed(packet);
        }
        break;

    case PROTOCOL_MSG_DISCONNECT_REQ:
        (void) cloud_client_set_reporting(false, REPORT_MODE_SUMMARY);
        (void) wifi_manager_disconnect();
        (void) cloud_client_notify_wifi_state(false);
        lock_status();
        s_status.reporting_enabled = false;
        s_status.wifi_connected = false;
        s_status.cloud_connected = false;
        s_status.registered_with_cloud = false;
        s_status.ip_address[0] = '\0';
        unlock_status();
        board_support_set_status_led(false);
        send_simple_response(PROTOCOL_MSG_DISCONNECT_RESP, PROTOCOL_RESULT_OK, "wifi_disconnected");
        (void) persist_runtime_auto_reconnect(false);
        protocol_mark_processed(packet);
        break;

    case PROTOCOL_MSG_START_REPORT_REQ:
        handle_start_report(packet);
        break;

    case PROTOCOL_MSG_STOP_REPORT_REQ:
        handle_stop_report(packet);
        break;

    case PROTOCOL_MSG_QUERY_STATUS_REQ:
        send_status_snapshot();
        break;

    case PROTOCOL_MSG_PING_REQ:
        send_simple_response(PROTOCOL_MSG_PING_RESP, PROTOCOL_RESULT_OK, "pong");
        break;

    case PROTOCOL_MSG_REPORT_SUMMARY:
        err = report_buffer_ingest_summary((const protocol_report_summary_payload_t *) packet->payload,
                                           packet->header.payload_len,
                                           &frame);
        if (err == ESP_OK && frame != NULL) {
            frame->ref_seq = packet->header.seq;
            if (submit_frame(frame, packet->header.seq) == ESP_OK) {
                protocol_mark_processed(packet);
            }
        } else {
            send_nack(packet->header.seq, PROTOCOL_NACK_INVALID_PAYLOAD);
        }
        break;

    case PROTOCOL_MSG_REPORT_FULL_BEGIN:
        (void) cloud_client_drop_pending_summary_frames();
        err = report_buffer_begin_full((const protocol_report_full_begin_payload_t *) packet->payload, packet->header.payload_len);
        if (err == ESP_OK) {
            const protocol_report_full_begin_payload_t *begin =
                (const protocol_report_full_begin_payload_t *) packet->payload;
            /*
             * Do NOT flush the ESP32->STM32 TX queue here.
             *
             * Continuous full upload starts a new frame immediately after the
             * previous HTTP response is parsed.  That response is exactly where
             * cloud-side configuration commands (upload_points/downsample/etc.)
             * arrive.  The old unconditional flush on every FULL_BEGIN could
             * erase a queued SERVER_COMMAND event before STM32 had a chance to
             * clock it out, so the web UI showed "delivered" while STM32 never
             * applied the command.
             */
            send_tx_accepted(packet->header.seq, begin->frame_id);
            protocol_mark_processed(packet);
        } else {
            /* A previous full frame may still be owned by the HTTP/cloud task.
             * Tell STM32 this is temporary back-pressure instead of a bad frame. */
            send_nack(packet->header.seq,
                      (err == ESP_ERR_INVALID_STATE) ? PROTOCOL_NACK_BUSY : PROTOCOL_NACK_INVALID_PAYLOAD);
        }
        break;

    case PROTOCOL_MSG_REPORT_FULL_WAVE_CHUNK:
    case PROTOCOL_MSG_REPORT_FULL_FFT_CHUNK:
        if (packet->header.payload_len < sizeof(prefix)) {
            send_nack(packet->header.seq, PROTOCOL_NACK_INVALID_PAYLOAD);
            break;
        }
        memcpy(&prefix, packet->payload, sizeof(prefix));
        err = report_buffer_ingest_chunk((protocol_msg_type_t) packet->header.msg_type,
                                         &prefix,
                                         sizeof(prefix),
                                         packet->payload + sizeof(prefix),
                                         packet->header.payload_len - sizeof(prefix));
        if (err == ESP_OK) {
            /*
             * Keep the per-chunk TX_ACCEPTED: the STM32 full-upload state
             * machine paces every waveform/FFT chunk by waiting for this ACK.
             *
             * The actual server-command loss was the FULL_BEGIN queue flush
             * above, not the ACK itself. Removing this ACK stalls full upload
             * after registration, so only the destructive flush is disabled.
             */
            send_tx_accepted(packet->header.seq, prefix.frame_id);
            protocol_mark_processed(packet);
        } else {
            send_nack(packet->header.seq, PROTOCOL_NACK_INVALID_PAYLOAD);
        }
        break;

    case PROTOCOL_MSG_REPORT_FULL_END:
        if (packet->header.payload_len < sizeof(end_payload)) {
            send_nack(packet->header.seq, PROTOCOL_NACK_INVALID_PAYLOAD);
            break;
        }
        memcpy(&end_payload, packet->payload, sizeof(end_payload));
        err = report_buffer_finalize_full(&end_payload, packet->header.payload_len, &frame);
        if (err == ESP_OK && frame != NULL) {
            frame->ref_seq = packet->header.seq;
            if (submit_frame(frame, packet->header.seq) == ESP_OK) {
                protocol_mark_processed(packet);
            }
        } else {
            send_nack(packet->header.seq, (err == ESP_ERR_INVALID_SIZE) ? PROTOCOL_NACK_INVALID_PAYLOAD : PROTOCOL_NACK_INVALID_STATE);
        }
        break;

    default:
        send_nack(packet->header.seq, PROTOCOL_NACK_INVALID_PAYLOAD);
        break;
    }
}

static void wifi_event_callback(const wifi_manager_event_t *event, void *ctx)
{
    app_event_t app_event = {
        .kind = APP_EVENT_WIFI,
    };

    (void) ctx;
    app_event.data.wifi = *event;
    if (!enqueue_app_event(&app_event)) {
        ESP_LOGW(TAG, "Dropped Wi-Fi app event %d", event->id);
    }
}

static void cloud_event_callback(const cloud_client_event_t *event, void *ctx)
{
    app_event_t app_event = {
        .kind = APP_EVENT_CLOUD,
    };

    (void) ctx;
    app_event.data.cloud = *event;
    if (!enqueue_app_event(&app_event)) {
        ESP_LOGW(TAG, "Dropped cloud app event %d", event->id);
    }
}

static void process_wifi_event(const wifi_manager_event_t *event)
{
    lock_status();
    s_status.rssi_dbm = event->rssi_dbm;
    copy_string(s_status.ip_address, sizeof(s_status.ip_address), event->ip_address);
    switch (event->id) {
    case WIFI_MANAGER_EVENT_CONNECTING:
        copy_string(s_status.last_error, sizeof(s_status.last_error), "wifi_connecting");
        break;
    case WIFI_MANAGER_EVENT_CONNECTED:
        s_status.wifi_connected = true;
        s_status.last_error[0] = '\0';
        break;
    case WIFI_MANAGER_EVENT_DISCONNECTED:
        s_status.wifi_connected = false;
        s_status.cloud_connected = false;
        s_status.registered_with_cloud = false;
        copy_string(s_status.last_error, sizeof(s_status.last_error), "wifi_disconnected");
        break;
    case WIFI_MANAGER_EVENT_FAILED:
        s_status.wifi_connected = false;
        s_status.cloud_connected = false;
        s_status.registered_with_cloud = false;
        copy_string(s_status.last_error, sizeof(s_status.last_error), "wifi_failed");
        break;
    default:
        break;
    }
    unlock_status();

    if (event->id == WIFI_MANAGER_EVENT_CONNECTED) {
        esp_err_t notify_err;

        board_support_set_status_led(true);
        notify_err = cloud_client_notify_wifi_state(true);
        if (notify_err != ESP_OK) {
            send_protocol_event(PROTOCOL_EVENT_ERROR, map_err_to_result(notify_err), 0, 0, 0, "cloud_queue_busy");
        }
        send_protocol_event(PROTOCOL_EVENT_WIFI_STATE, PROTOCOL_WIFI_CONNECTED, 0, 0, event->rssi_dbm, event->ip_address);
        send_protocol_event(PROTOCOL_EVENT_CLOUD_STATE, PROTOCOL_CLOUD_IDLE, 0, 0, 0, "cloud_wait_register");
    } else if (event->id == WIFI_MANAGER_EVENT_CONNECTING) {
        board_support_set_status_led(false);
        send_protocol_event(PROTOCOL_EVENT_WIFI_STATE, PROTOCOL_WIFI_CONNECTING, (uint32_t) event->reason_code, 0, 0, "connecting");
    } else if (event->id == WIFI_MANAGER_EVENT_DISCONNECTED) {
        board_support_set_status_led(false);
        (void) cloud_client_notify_wifi_state(false);
        send_protocol_event(PROTOCOL_EVENT_WIFI_STATE, PROTOCOL_WIFI_IDLE, (uint32_t) event->reason_code, 0, 0, "disconnected");
        send_protocol_event(PROTOCOL_EVENT_CLOUD_STATE, PROTOCOL_CLOUD_IDLE, 0, 0, 0, "cloud_idle");
    } else if (event->id == WIFI_MANAGER_EVENT_FAILED) {
        board_support_set_status_led(false);
        (void) cloud_client_notify_wifi_state(false);
        send_protocol_event(PROTOCOL_EVENT_WIFI_STATE, PROTOCOL_WIFI_FAILED, (uint32_t) event->reason_code, 0, 0, "failed");
        send_protocol_event(PROTOCOL_EVENT_CLOUD_STATE, PROTOCOL_CLOUD_FAILED, 0, 0, 0, "cloud_failed");
    }
}

static void process_cloud_event(const cloud_client_event_t *event)
{
    switch (event->id) {
    case CLOUD_CLIENT_EVENT_REGISTER_RESULT:
        lock_status();
        s_status.last_http_status = event->http_status;
        s_status.registered_with_cloud = (event->error == ESP_OK && event->http_status >= 200 && event->http_status < 300);
        s_status.cloud_connected = s_status.registered_with_cloud;
        s_status.last_error_code = s_status.registered_with_cloud ? 0 : (int32_t) (event->error != ESP_OK ? event->error : event->http_status);
        copy_string(s_status.last_error, sizeof(s_status.last_error), event->message);
        unlock_status();
        send_protocol_event(PROTOCOL_EVENT_REGISTER_RESULT,
                            (uint16_t) map_transport_and_http_to_result(event->error, event->http_status),
                            (uint32_t) event->http_status,
                            0,
                            0,
                            event->message);
        send_protocol_event(PROTOCOL_EVENT_CLOUD_STATE,
                            s_status.registered_with_cloud ? PROTOCOL_CLOUD_REGISTERED : PROTOCOL_CLOUD_FAILED,
                            (uint32_t) event->http_status,
                            0,
                            0,
                            event->message);
        break;

    case CLOUD_CLIENT_EVENT_REPORT_RESULT:
        lock_status();
        s_status.last_http_status = event->http_status;
        s_status.last_frame_id = event->frame_id;
        if (event->error != ESP_OK || event->http_status < 200 || event->http_status >= 300) {
            bool auth_lost = (event->http_status == 401 || event->http_status == 404);
            bool wifi_alive = wifi_manager_is_connected();
            /*
             * A single full-frame POST can fail because one TCP connect/write
             * attempt hit its short timeout.  Treat that as a frame TX failure,
             * not as global cloud logout.  Clearing cloud_connected here makes
             * STM32 run the slower auto-recovery/status loop before the next
             * full frame, which turns one 3s transient into a visible 8~10s gap.
             * Only mark cloud offline for authentication loss or real WiFi loss;
             * heartbeat/error events still handle sustained cloud outages.
             */
            if (auth_lost || !wifi_alive) {
                s_status.cloud_connected = false;
            } else if (s_status.registered_with_cloud) {
                s_status.cloud_connected = true;
            }
            s_status.last_error_code = (int32_t) (event->error != ESP_OK ? event->error : event->http_status);
            if (auth_lost || !wifi_alive) {
                s_status.registered_with_cloud = false;
            }
        } else {
            s_status.cloud_connected = true;
            s_status.registered_with_cloud = true;
            s_status.last_error_code = 0;
        }
        copy_string(s_status.last_error, sizeof(s_status.last_error), event->message);
        unlock_status();
        send_tx_result(event->ref_seq,
                       event->frame_id,
                       event->http_status,
                       map_transport_and_http_to_result(event->error, event->http_status));
        if ((event->error != ESP_OK || event->http_status < 200 || event->http_status >= 300) &&
            (event->http_status == 401 || event->http_status == 404 || !wifi_manager_is_connected())) {
            send_protocol_event(PROTOCOL_EVENT_CLOUD_STATE,
                                PROTOCOL_CLOUD_FAILED,
                                (uint32_t) event->http_status,
                                event->frame_id,
                                0,
                                event->message);
        }
        break;

    case CLOUD_CLIENT_EVENT_HEARTBEAT_RESULT:
        lock_status();
        s_status.last_http_status = event->http_status;
        s_status.cloud_connected = true;
        s_status.registered_with_cloud = true;
        s_status.last_error_code = 0;
        copy_string(s_status.last_error, sizeof(s_status.last_error), event->message);
        unlock_status();
        send_protocol_event(PROTOCOL_EVENT_CLOUD_STATE,
                            PROTOCOL_CLOUD_REGISTERED,
                            (uint32_t) event->http_status,
                            0,
                            0,
                            event->message);
        break;

    case CLOUD_CLIENT_EVENT_SERVER_COMMAND:
        apply_server_command_event(&event->server_command);
        break;

    case CLOUD_CLIENT_EVENT_ERROR:
        lock_status();
        s_status.cloud_connected = false;
        s_status.last_error_code = (int32_t) (event->error != ESP_OK ? event->error : event->http_status);
        copy_string(s_status.last_error, sizeof(s_status.last_error), event->message);
        unlock_status();
        send_protocol_event(PROTOCOL_EVENT_ERROR,
                            (uint16_t) map_transport_and_http_to_result(event->error, event->http_status),
                            (uint32_t) event->http_status,
                            event->frame_id,
                            0,
                            event->message);
        break;

    default:
        break;
    }
}

void app_main(void)
{
    esp_err_t err;
    app_event_t app_event;
    app_config_snapshot_t snapshot;
    app_runtime_state_t runtime_state;

    board_support_init();
    board_support_pulse_status_led(2, 80);

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    app_config_init_defaults();
    ESP_ERROR_CHECK(app_config_load_from_nvs());
    app_config_get_snapshot(&snapshot);
    app_config_get_runtime_state(&runtime_state);

    s_status_mutex = xSemaphoreCreateMutex();
    s_app_event_queue = xQueueCreate(APP_EVENT_QUEUE_LEN, sizeof(app_event_t));
    if (s_status_mutex == NULL || s_app_event_queue == NULL) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    s_session_epoch = esp_random();
    if (s_session_epoch == 0U) {
        s_session_epoch = 1U;
    }

    memset(&s_status, 0, sizeof(s_status));
    s_status.ready = true;
    s_status.report_mode = (runtime_state.auto_reconnect_enabled && runtime_state.last_reporting)
                               ? runtime_state.last_report_mode
                               : REPORT_MODE_SUMMARY;
    s_status.session_epoch = s_session_epoch;
    copy_string(s_status.node_id, sizeof(s_status.node_id), snapshot.device.node_id);
    s_status.downsample_step = snapshot.comm.downsample_step;
    s_status.upload_points = snapshot.comm.upload_points;
    s_status.config_version = 1U;
    s_status.last_command_id = 0U;
    s_status.last_error_code = 0;
    s_status.reporting_enabled = APP_BOOT_AUTO_RECONNECT &&
                                  runtime_state.auto_reconnect_enabled &&
                                  runtime_state.last_reporting;

    report_buffer_init();

    ESP_ERROR_CHECK(wifi_manager_init(wifi_event_callback, NULL));
    ESP_ERROR_CHECK(wifi_manager_apply_config(&snapshot.device));
    ESP_ERROR_CHECK(wifi_manager_set_reconnect_backoff_ms(snapshot.comm.reconnect_backoff_ms));
    ESP_ERROR_CHECK(cloud_client_init(cloud_event_callback, NULL));
    ESP_ERROR_CHECK(cloud_client_apply_snapshot(&snapshot));
    ESP_ERROR_CHECK(spi_link_init(handle_protocol_packet, NULL));

    if (APP_BOOT_AUTO_RECONNECT &&
        runtime_state.auto_reconnect_enabled &&
        runtime_state.last_reporting) {
        err = cloud_client_set_reporting(true, runtime_state.last_report_mode);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to restore reporting state: %s", esp_err_to_name(err));
            s_status.reporting_enabled = false;
        }
    }
    if (APP_BOOT_AUTO_RECONNECT &&
        runtime_state.auto_reconnect_enabled &&
        snapshot.device.wifi_ssid[0] != '\0') {
        err = wifi_manager_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to restore Wi-Fi connection: %s", esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "ESP32 SPI coprocessor started.");
    ESP_LOGI(TAG, "Session epoch=%" PRIu32 ", node_id=%s, server=%s:%u",
             s_session_epoch,
             snapshot.device.node_id,
             snapshot.device.server_host,
             snapshot.device.server_port);

    send_protocol_event(PROTOCOL_EVENT_READY, PROTOCOL_RESULT_OK, s_session_epoch, PROTOCOL_MAX_PAYLOAD, 0, "ready");

    for (;;) {
        if (xQueueReceive(s_app_event_queue, &app_event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (app_event.kind) {
        case APP_EVENT_WIFI:
            process_wifi_event(&app_event.data.wifi);
            break;
        case APP_EVENT_CLOUD:
            process_cloud_event(&app_event.data.cloud);
            break;
        default:
            break;
        }
    }
}
