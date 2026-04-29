#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "app_types.h"

#define PROTOCOL_MAGIC 0x43505243UL
#define PROTOCOL_VERSION 0x01U
#define PROTOCOL_MAX_PAYLOAD 1536U

typedef enum {
    PROTOCOL_FLAG_NONE = 0,
    PROTOCOL_FLAG_ACK_REQUEST = 1U << 0,
} protocol_flags_t;

typedef enum {
    PROTOCOL_MSG_NOOP = 0x00,
    PROTOCOL_MSG_HELLO_REQ = 0x01,
    PROTOCOL_MSG_HELLO_RESP = 0x02,
    PROTOCOL_MSG_SET_DEVICE_CONFIG_REQ = 0x10,
    PROTOCOL_MSG_SET_DEVICE_CONFIG_RESP = 0x11,
    PROTOCOL_MSG_SET_COMM_PARAMS_REQ = 0x12,
    PROTOCOL_MSG_SET_COMM_PARAMS_RESP = 0x13,
    PROTOCOL_MSG_CONNECT_REQ = 0x14,
    PROTOCOL_MSG_CONNECT_RESP = 0x15,
    PROTOCOL_MSG_DISCONNECT_REQ = 0x16,
    PROTOCOL_MSG_DISCONNECT_RESP = 0x17,
    PROTOCOL_MSG_START_REPORT_REQ = 0x18,
    PROTOCOL_MSG_START_REPORT_RESP = 0x19,
    PROTOCOL_MSG_STOP_REPORT_REQ = 0x1A,
    PROTOCOL_MSG_STOP_REPORT_RESP = 0x1B,
    PROTOCOL_MSG_QUERY_STATUS_REQ = 0x1C,
    PROTOCOL_MSG_STATUS_RESP = 0x1D,
    PROTOCOL_MSG_PING_REQ = 0x1E,
    PROTOCOL_MSG_PING_RESP = 0x1F,
    PROTOCOL_MSG_CLOUD_CONNECT_REQ = 0x20,
    PROTOCOL_MSG_CLOUD_CONNECT_RESP = 0x21,
    PROTOCOL_MSG_REGISTER_REQ = 0x22,
    PROTOCOL_MSG_REGISTER_RESP = 0x23,
    PROTOCOL_MSG_REPORT_SUMMARY = 0x30,
    PROTOCOL_MSG_REPORT_FULL_BEGIN = 0x31,
    PROTOCOL_MSG_REPORT_FULL_WAVE_CHUNK = 0x32,
    PROTOCOL_MSG_REPORT_FULL_FFT_CHUNK = 0x33,
    PROTOCOL_MSG_REPORT_FULL_END = 0x34,
    PROTOCOL_MSG_EVENT = 0x40,
    PROTOCOL_MSG_TX_ACCEPTED = 0x41,
    PROTOCOL_MSG_TX_RESULT = 0x42,
    PROTOCOL_MSG_NACK = 0x43,
} protocol_msg_type_t;

typedef enum {
    PROTOCOL_RESULT_OK = 0,
    PROTOCOL_RESULT_INVALID_ARG = 1,
    PROTOCOL_RESULT_BUSY = 2,
    PROTOCOL_RESULT_QUEUE_FULL = 3,
    PROTOCOL_RESULT_NOT_READY = 4,
    PROTOCOL_RESULT_IO_ERROR = 5,
    PROTOCOL_RESULT_INTERNAL = 6,
} protocol_result_code_t;

typedef enum {
    PROTOCOL_NACK_NONE = 0,
    PROTOCOL_NACK_CRC_FAIL = 1,
    PROTOCOL_NACK_BAD_LENGTH = 2,
    PROTOCOL_NACK_UNSUPPORTED_VERSION = 3,
    PROTOCOL_NACK_QUEUE_FULL = 4,
    PROTOCOL_NACK_BUSY = 5,
    PROTOCOL_NACK_SESSION_MISMATCH = 6,
    PROTOCOL_NACK_INVALID_STATE = 7,
    PROTOCOL_NACK_INVALID_PAYLOAD = 8,
} protocol_nack_reason_t;

typedef enum {
    PROTOCOL_EVENT_READY = 1,
    PROTOCOL_EVENT_CONFIG_APPLIED = 2,
    PROTOCOL_EVENT_WIFI_STATE = 3,
    PROTOCOL_EVENT_CLOUD_STATE = 4,
    PROTOCOL_EVENT_REGISTER_RESULT = 5,
    PROTOCOL_EVENT_REPORT_RESULT = 6,
    PROTOCOL_EVENT_SERVER_COMMAND = 7,
    PROTOCOL_EVENT_ERROR = 8,
    PROTOCOL_EVENT_STATUS = 9,
} protocol_event_type_t;

typedef enum {
    PROTOCOL_WIFI_IDLE = 0,
    PROTOCOL_WIFI_CONNECTING = 1,
    PROTOCOL_WIFI_CONNECTED = 2,
    PROTOCOL_WIFI_FAILED = 3,
} protocol_wifi_state_t;

typedef enum {
    PROTOCOL_CLOUD_IDLE = 0,
    PROTOCOL_CLOUD_CONNECTING = 1,
    PROTOCOL_CLOUD_REGISTERED = 2,
    PROTOCOL_CLOUD_FAILED = 3,
} protocol_cloud_state_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t msg_type;
    uint16_t flags;
    uint32_t session_epoch;
    uint32_t seq;
    uint32_t ack_seq;
    uint16_t payload_len;
    uint16_t header_crc16;
    uint32_t payload_crc32;
} protocol_header_t;

typedef struct {
    protocol_header_t header;
    uint8_t payload[PROTOCOL_MAX_PAYLOAD];
} protocol_packet_t;

typedef struct __attribute__((packed)) {
    uint32_t boot_epoch;
    uint32_t capability_flags;
    uint16_t max_payload;
    uint16_t reserved;
} protocol_hello_payload_t;

typedef struct __attribute__((packed)) {
    char wifi_ssid[APP_MAX_SSID_LEN];
    char wifi_password[APP_MAX_PASSWORD_LEN];
    char server_host[APP_MAX_HOST_LEN];
    uint16_t server_port;
    char node_id[APP_MAX_NODE_ID_LEN];
    char node_location[APP_MAX_NODE_LOCATION_LEN];
    char hw_version[APP_MAX_HW_VERSION_LEN];
} protocol_device_config_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t heartbeat_ms;
    uint32_t min_interval_ms;
    uint32_t http_timeout_ms;
    uint32_t reconnect_backoff_ms;
    uint32_t downsample_step;
    uint32_t upload_points;
    uint32_t hardreset_sec;
    uint32_t chunk_kb;
    uint32_t chunk_delay_ms;
} protocol_comm_params_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t report_mode;
    uint8_t reserved0;
    uint16_t reserved1;
} protocol_start_report_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t channel_id;
    uint8_t reserved0;
    uint16_t waveform_count;
    uint16_t fft_count;
    uint16_t reserved1;
    int32_t value_scaled;
    int32_t current_value_scaled;
} protocol_channel_summary_t;

typedef struct __attribute__((packed)) {
    uint32_t frame_id;
    uint64_t timestamp_ms;
    uint32_t downsample_step;
    uint32_t upload_points;
    char fault_code[8];
    uint8_t report_mode;
    uint8_t status_code;
    uint8_t channel_count;
    uint8_t reserved0;
    protocol_channel_summary_t channels[REPORT_MAX_CHANNELS];
} protocol_report_summary_payload_t;

typedef protocol_report_summary_payload_t protocol_report_full_begin_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t frame_id;
    uint8_t channel_id;
    uint8_t reserved0;
    uint16_t element_offset;
    uint16_t element_count;
    uint16_t reserved1;
} protocol_report_chunk_prefix_t;

typedef struct __attribute__((packed)) {
    uint32_t frame_id;
} protocol_report_end_payload_t;

typedef struct __attribute__((packed)) {
    uint16_t event_type;
    uint16_t result_code;
    uint32_t value0;
    uint32_t value1;
    int32_t value2;
    char text[64];
} protocol_event_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t ref_seq;
    uint32_t ref_frame_id;
    uint16_t queue_depth;
    uint16_t reserved;
} protocol_tx_accepted_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t ref_seq;
    uint32_t ref_frame_id;
    int32_t http_status;
    int32_t result_code;
} protocol_tx_result_payload_t;

typedef struct __attribute__((packed)) {
    uint32_t ref_seq;
    uint16_t reason;
    uint16_t reserved;
} protocol_nack_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t ready;
    uint8_t wifi_connected;
    uint8_t cloud_connected;
    uint8_t registered_with_cloud;
    uint8_t reporting_enabled;
    uint8_t report_mode;
    int8_t rssi_dbm;
    uint8_t reserved0;
    uint32_t session_epoch;
    uint32_t last_frame_id;
    uint32_t downsample_step;
    uint32_t upload_points;
    int32_t last_http_status;
    uint32_t config_version;
    uint32_t last_command_id;
    int32_t last_error_code;
    char ip_address[APP_MAX_IP_STR_LEN];
    char node_id[APP_MAX_NODE_ID_LEN];
    char last_error[APP_MAX_ERROR_TEXT_LEN];
} protocol_status_payload_t;

size_t protocol_wire_size(void);
void protocol_packet_prepare(protocol_packet_t *packet,
                             protocol_msg_type_t msg_type,
                             uint32_t session_epoch,
                             uint32_t seq,
                             uint32_t ack_seq,
                             uint16_t flags);
esp_err_t protocol_packet_set_payload(protocol_packet_t *packet, const void *payload, size_t payload_len);
void protocol_packet_finalize(protocol_packet_t *packet);
esp_err_t protocol_packet_validate(const protocol_packet_t *packet,
                                   size_t rx_bytes,
                                   protocol_nack_reason_t *out_reason);
size_t protocol_packet_total_size(const protocol_packet_t *packet);
const char *protocol_msg_type_name(protocol_msg_type_t msg_type);
void protocol_fill_status_payload(protocol_status_payload_t *payload, const app_status_snapshot_t *snapshot);
