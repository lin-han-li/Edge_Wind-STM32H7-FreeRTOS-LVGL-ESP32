#include "comm_protocol.h"

#include <string.h>

#include "esp_crc.h"

static uint16_t compute_header_crc(protocol_header_t header)
{
    header.header_crc16 = 0;
    return (uint16_t) ~esp_crc16_le((uint16_t) ~0xFFFFU, (const uint8_t *) &header, sizeof(header));
}

size_t protocol_wire_size(void)
{
    return sizeof(protocol_packet_t);
}

void protocol_packet_prepare(protocol_packet_t *packet,
                             protocol_msg_type_t msg_type,
                             uint32_t session_epoch,
                             uint32_t seq,
                             uint32_t ack_seq,
                             uint16_t flags)
{
    memset(packet, 0, sizeof(*packet));
    packet->header.magic = PROTOCOL_MAGIC;
    packet->header.version = PROTOCOL_VERSION;
    packet->header.msg_type = (uint8_t) msg_type;
    packet->header.flags = flags;
    packet->header.session_epoch = session_epoch;
    packet->header.seq = seq;
    packet->header.ack_seq = ack_seq;
}

esp_err_t protocol_packet_set_payload(protocol_packet_t *packet, const void *payload, size_t payload_len)
{
    if (packet == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload_len > PROTOCOL_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (payload_len > 0U && payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (payload_len > 0U) {
        memcpy(packet->payload, payload, payload_len);
    }
    packet->header.payload_len = (uint16_t) payload_len;
    return ESP_OK;
}

void protocol_packet_finalize(protocol_packet_t *packet)
{
    packet->header.payload_crc32 = 0;
    if (packet->header.payload_len > 0U) {
        packet->header.payload_crc32 = ~esp_crc32_le(~0U, packet->payload, packet->header.payload_len);
    }
    packet->header.header_crc16 = compute_header_crc(packet->header);
}

esp_err_t protocol_packet_validate(const protocol_packet_t *packet,
                                   size_t rx_bytes,
                                   protocol_nack_reason_t *out_reason)
{
    protocol_nack_reason_t reason = PROTOCOL_NACK_NONE;
    uint16_t expected_header_crc;
    uint32_t expected_payload_crc = 0;

    if (packet == NULL || rx_bytes < sizeof(protocol_header_t)) {
        reason = PROTOCOL_NACK_BAD_LENGTH;
        goto fail;
    }
    if (packet->header.magic != PROTOCOL_MAGIC) {
        reason = PROTOCOL_NACK_BAD_LENGTH;
        goto fail;
    }
    if (packet->header.version != PROTOCOL_VERSION) {
        reason = PROTOCOL_NACK_UNSUPPORTED_VERSION;
        goto fail;
    }
    if (packet->header.payload_len > PROTOCOL_MAX_PAYLOAD) {
        reason = PROTOCOL_NACK_BAD_LENGTH;
        goto fail;
    }
    if (rx_bytes < (sizeof(protocol_header_t) + packet->header.payload_len)) {
        reason = PROTOCOL_NACK_BAD_LENGTH;
        goto fail;
    }

    expected_header_crc = compute_header_crc(packet->header);
    if (expected_header_crc != packet->header.header_crc16) {
        reason = PROTOCOL_NACK_CRC_FAIL;
        goto fail;
    }

    if (packet->header.payload_len > 0U) {
        expected_payload_crc = ~esp_crc32_le(~0U, packet->payload, packet->header.payload_len);
    }
    if (expected_payload_crc != packet->header.payload_crc32) {
        reason = PROTOCOL_NACK_CRC_FAIL;
        goto fail;
    }

    if (out_reason != NULL) {
        *out_reason = PROTOCOL_NACK_NONE;
    }
    return ESP_OK;

fail:
    if (out_reason != NULL) {
        *out_reason = reason;
    }
    return ESP_FAIL;
}

size_t protocol_packet_total_size(const protocol_packet_t *packet)
{
    return sizeof(protocol_header_t) + packet->header.payload_len;
}

const char *protocol_msg_type_name(protocol_msg_type_t msg_type)
{
    switch (msg_type) {
    case PROTOCOL_MSG_NOOP:
        return "NOOP";
    case PROTOCOL_MSG_HELLO_REQ:
        return "HELLO_REQ";
    case PROTOCOL_MSG_HELLO_RESP:
        return "HELLO_RESP";
    case PROTOCOL_MSG_SET_DEVICE_CONFIG_REQ:
        return "SET_DEVICE_CONFIG_REQ";
    case PROTOCOL_MSG_SET_DEVICE_CONFIG_RESP:
        return "SET_DEVICE_CONFIG_RESP";
    case PROTOCOL_MSG_SET_COMM_PARAMS_REQ:
        return "SET_COMM_PARAMS_REQ";
    case PROTOCOL_MSG_SET_COMM_PARAMS_RESP:
        return "SET_COMM_PARAMS_RESP";
    case PROTOCOL_MSG_CONNECT_REQ:
        return "CONNECT_REQ";
    case PROTOCOL_MSG_CONNECT_RESP:
        return "CONNECT_RESP";
    case PROTOCOL_MSG_DISCONNECT_REQ:
        return "DISCONNECT_REQ";
    case PROTOCOL_MSG_DISCONNECT_RESP:
        return "DISCONNECT_RESP";
    case PROTOCOL_MSG_START_REPORT_REQ:
        return "START_REPORT_REQ";
    case PROTOCOL_MSG_START_REPORT_RESP:
        return "START_REPORT_RESP";
    case PROTOCOL_MSG_STOP_REPORT_REQ:
        return "STOP_REPORT_REQ";
    case PROTOCOL_MSG_STOP_REPORT_RESP:
        return "STOP_REPORT_RESP";
    case PROTOCOL_MSG_QUERY_STATUS_REQ:
        return "QUERY_STATUS_REQ";
    case PROTOCOL_MSG_STATUS_RESP:
        return "STATUS_RESP";
    case PROTOCOL_MSG_PING_REQ:
        return "PING_REQ";
    case PROTOCOL_MSG_PING_RESP:
        return "PING_RESP";
    case PROTOCOL_MSG_CLOUD_CONNECT_REQ:
        return "CLOUD_CONNECT_REQ";
    case PROTOCOL_MSG_CLOUD_CONNECT_RESP:
        return "CLOUD_CONNECT_RESP";
    case PROTOCOL_MSG_REGISTER_REQ:
        return "REGISTER_REQ";
    case PROTOCOL_MSG_REGISTER_RESP:
        return "REGISTER_RESP";
    case PROTOCOL_MSG_REPORT_SUMMARY:
        return "REPORT_SUMMARY";
    case PROTOCOL_MSG_REPORT_FULL_BEGIN:
        return "REPORT_FULL_BEGIN";
    case PROTOCOL_MSG_REPORT_FULL_WAVE_CHUNK:
        return "REPORT_FULL_WAVE_CHUNK";
    case PROTOCOL_MSG_REPORT_FULL_FFT_CHUNK:
        return "REPORT_FULL_FFT_CHUNK";
    case PROTOCOL_MSG_REPORT_FULL_END:
        return "REPORT_FULL_END";
    case PROTOCOL_MSG_EVENT:
        return "EVENT";
    case PROTOCOL_MSG_TX_ACCEPTED:
        return "TX_ACCEPTED";
    case PROTOCOL_MSG_TX_RESULT:
        return "TX_RESULT";
    case PROTOCOL_MSG_NACK:
        return "NACK";
    default:
        return "UNKNOWN";
    }
}

void protocol_fill_status_payload(protocol_status_payload_t *payload, const app_status_snapshot_t *snapshot)
{
    memset(payload, 0, sizeof(*payload));
    payload->ready = snapshot->ready ? 1U : 0U;
    payload->wifi_connected = snapshot->wifi_connected ? 1U : 0U;
    payload->cloud_connected = snapshot->cloud_connected ? 1U : 0U;
    payload->registered_with_cloud = snapshot->registered_with_cloud ? 1U : 0U;
    payload->reporting_enabled = snapshot->reporting_enabled ? 1U : 0U;
    payload->report_mode = (uint8_t) snapshot->report_mode;
    payload->rssi_dbm = (int8_t) snapshot->rssi_dbm;
    payload->session_epoch = snapshot->session_epoch;
    payload->last_frame_id = snapshot->last_frame_id;
    payload->downsample_step = snapshot->downsample_step;
    payload->upload_points = snapshot->upload_points;
    payload->last_http_status = snapshot->last_http_status;
    payload->config_version = snapshot->config_version;
    payload->last_command_id = snapshot->last_command_id;
    payload->last_error_code = snapshot->last_error_code;
    strncpy(payload->ip_address, snapshot->ip_address, sizeof(payload->ip_address) - 1U);
    strncpy(payload->node_id, snapshot->node_id, sizeof(payload->node_id) - 1U);
    strncpy(payload->last_error, snapshot->last_error, sizeof(payload->last_error) - 1U);
}
