#include "esp32_spi_debug.h"
#include "edgewind_units.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "spi.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

extern void ESP_UI_Internal_OnLog(const char *line);

#define ESP32_SPI_MAGIC 0x43505243UL
#define ESP32_SPI_VERSION 0x01U
#define ESP32_SPI_MAX_PAYLOAD 1536U
#define ESP32_SPI_FRAME_SIZE 1564U
#define ESP32_SPI_READY_TIMEOUT_MS 3000U
#define ESP32_SPI_READY_RELEASE_TIMEOUT_MS 10U
#define ESP32_SPI_INTER_TRANSACTION_GUARD_MS 1U
#define ESP32_SPI_XFER_TIMEOUT_MS 300U
#define ESP32_SPI_REPORT_RETRY_ATTEMPTS 5U
#define ESP32_SPI_REPORT_RETRY_BACKOFF_MS 1U
#define ESP32_SPI_DEFAULT_TIMEOUT_MS 3000U
#define ESP32_SPI_WIFI_POLL_INTERVAL_MS 1000U
#define ESP32_SPI_RESULT_PENDING 0xFFFFU
#define ESP32_SPI_LOCK_TIMEOUT_MS 5000U

#ifndef ESP32_SPI_ENABLE_FULL_UPLOAD
#define ESP32_SPI_ENABLE_FULL_UPLOAD 1
#endif

#ifndef ESP32_SPI_LOG_FULL_CHUNKS
#define ESP32_SPI_LOG_FULL_CHUNKS 0
#endif

#ifndef ESP32_SPI_LOG_TX_ACCEPTED
#define ESP32_SPI_LOG_TX_ACCEPTED 0
#endif

#ifndef ESP32_SPI_LOG_RETRYABLE_NACKS
#define ESP32_SPI_LOG_RETRYABLE_NACKS 0
#endif

typedef enum {
    ESP32_MSG_NOOP = 0x00,
    ESP32_MSG_HELLO_REQ = 0x01,
    ESP32_MSG_HELLO_RESP = 0x02,
    ESP32_MSG_SET_DEVICE_CONFIG_REQ = 0x10,
    ESP32_MSG_SET_DEVICE_CONFIG_RESP = 0x11,
    ESP32_MSG_SET_COMM_PARAMS_REQ = 0x12,
    ESP32_MSG_SET_COMM_PARAMS_RESP = 0x13,
    ESP32_MSG_CONNECT_REQ = 0x14,
    ESP32_MSG_CONNECT_RESP = 0x15,
    ESP32_MSG_DISCONNECT_REQ = 0x16,
    ESP32_MSG_DISCONNECT_RESP = 0x17,
    ESP32_MSG_START_REPORT_REQ = 0x18,
    ESP32_MSG_START_REPORT_RESP = 0x19,
    ESP32_MSG_STOP_REPORT_REQ = 0x1A,
    ESP32_MSG_STOP_REPORT_RESP = 0x1B,
    ESP32_MSG_QUERY_STATUS_REQ = 0x1C,
    ESP32_MSG_STATUS_RESP = 0x1D,
    ESP32_MSG_PING_REQ = 0x1E,
    ESP32_MSG_PING_RESP = 0x1F,
    ESP32_MSG_CLOUD_CONNECT_REQ = 0x20,
    ESP32_MSG_CLOUD_CONNECT_RESP = 0x21,
    ESP32_MSG_REGISTER_REQ = 0x22,
    ESP32_MSG_REGISTER_RESP = 0x23,
    ESP32_MSG_REPORT_SUMMARY = 0x30,
    ESP32_MSG_REPORT_FULL_BEGIN = 0x31,
    ESP32_MSG_REPORT_FULL_WAVE_CHUNK = 0x32,
    ESP32_MSG_REPORT_FULL_FFT_CHUNK = 0x33,
    ESP32_MSG_REPORT_FULL_END = 0x34,
    ESP32_MSG_TX_ACCEPTED = 0x41,
    ESP32_MSG_TX_RESULT = 0x42,
    ESP32_MSG_EVENT = 0x40,
    ESP32_MSG_NACK = 0x43,
} esp32_msg_type_t;

typedef enum {
    ESP32_RESULT_OK = 0,
    ESP32_RESULT_INVALID_ARG = 1,
    ESP32_RESULT_BUSY = 2,
    ESP32_RESULT_QUEUE_FULL = 3,
    ESP32_RESULT_NOT_READY = 4,
    ESP32_RESULT_IO_ERROR = 5,
    ESP32_RESULT_INTERNAL = 6,
} esp32_result_code_t;

typedef enum {
    ESP32_NACK_CRC_FAIL = 1,
    ESP32_NACK_BAD_LENGTH = 2,
    ESP32_NACK_SESSION_MISMATCH = 6,
} esp32_nack_reason_t;

typedef enum {
    ESP32_EVENT_READY = 1,
    ESP32_EVENT_CONFIG_APPLIED = 2,
    ESP32_EVENT_WIFI_STATE = 3,
    ESP32_EVENT_CLOUD_STATE = 4,
    ESP32_EVENT_REGISTER_RESULT = 5,
    ESP32_EVENT_REPORT_RESULT = 6,
    ESP32_EVENT_SERVER_COMMAND = 7,
    ESP32_EVENT_ERROR = 8,
    ESP32_EVENT_STATUS = 9,
} esp32_event_type_t;

typedef enum {
    ESP32_WIFI_IDLE = 0,
    ESP32_WIFI_CONNECTING = 1,
    ESP32_WIFI_CONNECTED = 2,
    ESP32_WIFI_FAILED = 3,
} esp32_wifi_state_t;

typedef enum {
    ESP32_CLOUD_IDLE = 0,
    ESP32_CLOUD_CONNECTING = 1,
    ESP32_CLOUD_REGISTERED = 2,
    ESP32_CLOUD_FAILED = 3,
} esp32_cloud_state_t;

#pragma pack(push, 1)
typedef struct {
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
} esp32_spi_header_t;

typedef struct {
    uint32_t boot_epoch;
    uint32_t capability_flags;
    uint16_t max_payload;
    uint16_t reserved;
} esp32_hello_payload_t;

typedef struct {
    char wifi_ssid[64];
    char wifi_password[64];
    char server_host[64];
    uint16_t server_port;
    char node_id[64];
    char node_location[64];
    char hw_version[32];
} esp32_device_config_payload_t;

typedef struct {
    uint32_t heartbeat_ms;
    uint32_t min_interval_ms;
    uint32_t http_timeout_ms;
    uint32_t reconnect_backoff_ms;
    uint32_t downsample_step;
    uint32_t upload_points;
    uint32_t hardreset_sec;
    uint32_t chunk_kb;
    uint32_t chunk_delay_ms;
} esp32_comm_params_payload_t;

typedef struct {
    uint8_t report_mode;
    uint8_t reserved0;
    uint16_t reserved1;
} esp32_start_report_payload_t;

typedef struct {
    uint8_t channel_id;
    uint8_t reserved0;
    uint16_t waveform_count;
    uint16_t fft_count;
    uint16_t reserved1;
    int32_t value_scaled;
    int32_t current_value_scaled;
} esp32_report_channel_summary_payload_t;

typedef struct {
    uint32_t frame_id;
    uint64_t timestamp_ms;
    uint32_t downsample_step;
    uint32_t upload_points;
    char fault_code[8];
    uint8_t report_mode;
    uint8_t status_code;
    uint8_t channel_count;
    uint8_t reserved0;
    esp32_report_channel_summary_payload_t channels[4];
} esp32_report_summary_payload_t;

typedef esp32_report_summary_payload_t esp32_report_full_begin_payload_t;

typedef struct {
    uint32_t frame_id;
    uint8_t channel_id;
    uint8_t reserved0;
    uint16_t element_offset;
    uint16_t element_count;
    uint16_t reserved1;
} esp32_report_chunk_prefix_t;

typedef struct {
    uint32_t frame_id;
} esp32_report_end_payload_t;

typedef struct {
    uint16_t event_type;
    uint16_t result_code;
    uint32_t value0;
    uint32_t value1;
    int32_t value2;
    char text[64];
} esp32_event_payload_t;

typedef struct {
    uint32_t ref_seq;
    uint32_t ref_frame_id;
    uint16_t queue_depth;
    uint16_t reserved;
} esp32_tx_accepted_payload_t;

typedef struct {
    uint32_t ref_seq;
    uint32_t ref_frame_id;
    int32_t http_status;
    int32_t result_code;
} esp32_tx_result_payload_t;

typedef struct {
    uint32_t ref_seq;
    uint16_t reason;
    uint16_t reserved;
} esp32_nack_payload_t;

typedef struct {
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
    char ip_address[16];
    char node_id[64];
    char last_error[64];
} esp32_status_payload_t;
#pragma pack(pop)

typedef struct {
    esp32_spi_header_t header;
    uint8_t payload[ESP32_SPI_MAX_PAYLOAD];
} esp32_spi_packet_t;

typedef bool (*esp32_spi_payload_builder_t)(uint8_t *payload, uint16_t payload_len, void *ctx);

typedef char esp32_spi_header_size_check[(sizeof(esp32_spi_header_t) == 28U) ? 1 : -1];
typedef char esp32_spi_packet_size_check[(sizeof(esp32_spi_packet_t) == ESP32_SPI_FRAME_SIZE) ? 1 : -1];
typedef char esp32_spi_status_size_check[(sizeof(esp32_status_payload_t) == 184U) ? 1 : -1];

static uint8_t s_tx_buf[ESP32_SPI_FRAME_SIZE] __attribute__((aligned(32)));
static uint8_t s_rx_buf[ESP32_SPI_FRAME_SIZE] __attribute__((aligned(32)));
static uint32_t s_tx_seq = 1U;
static uint32_t s_last_rx_seq = 0U;
static uint32_t s_session_epoch = 0U;
static uint32_t s_last_tx_seq = 0U;
static esp32_spi_status_t s_status;
static uint8_t s_last_response_type = ESP32_MSG_NOOP;
static uint16_t s_last_result_code = ESP32_SPI_RESULT_PENDING;
static char s_last_response_text[65];
static uint8_t s_last_event_type = 0U;
static uint16_t s_last_event_result = ESP32_SPI_RESULT_PENDING;
static uint8_t s_wifi_failed_seen = 0U;
static uint8_t s_register_event_seen = 0U;
static uint16_t s_register_result = ESP32_SPI_RESULT_PENDING;
static uint32_t s_last_tx_accepted_ref_seq = 0U;
static uint32_t s_last_tx_result_ref_seq = 0U;
static int32_t s_last_tx_result_code = ESP32_SPI_RESULT_PENDING;
static int32_t s_last_tx_result_http_status = 0;
static uint32_t s_last_tx_result_frame_id = 0U;
static uint32_t s_last_report_full_end_ref_seq = 0U;
static SemaphoreHandle_t s_spi_mutex = NULL;
static SemaphoreHandle_t s_spi_op_mutex = NULL;
static uint32_t s_last_nack_ref_seq = 0U;
static uint16_t s_last_nack_reason = 0U;
static uint8_t s_session_mismatch_seen = 0U;
static uint8_t s_pending_server_reset = 0U;
static uint8_t s_pending_server_report_mode_valid = 0U;
static uint8_t s_pending_server_report_full = 0U;
static uint8_t s_pending_server_downsample_valid = 0U;
static uint32_t s_pending_server_downsample_step = 0U;
static uint8_t s_pending_server_upload_valid = 0U;
static uint32_t s_pending_server_upload_points = 0U;
static uint8_t s_pending_server_heartbeat_valid = 0U;
static uint32_t s_pending_server_heartbeat_ms = 0U;
static uint8_t s_pending_server_min_interval_valid = 0U;
static uint32_t s_pending_server_min_interval_ms = 0U;
static uint8_t s_pending_server_http_timeout_valid = 0U;
static uint32_t s_pending_server_http_timeout_ms = 0U;
static uint8_t s_pending_server_chunk_kb_valid = 0U;
static uint32_t s_pending_server_chunk_kb = 0U;
static uint8_t s_pending_server_chunk_delay_valid = 0U;
static uint32_t s_pending_server_chunk_delay_ms = 0U;
static char s_last_server_cmd_key[65];
static uint32_t s_last_server_cmd_value = 0U;
static uint32_t s_last_server_cmd_tick = 0U;

static bool nack_reason_is_retryable(uint16_t reason);

static uint16_t crc16_le_update(uint16_t crc, const uint8_t *data, uint32_t len)
{
    while (len-- > 0U) {
        crc ^= *data++;
        for (uint32_t i = 0; i < 8U; i++) {
            if ((crc & 1U) != 0U) {
                crc = (uint16_t)((crc >> 1U) ^ 0x8408U);
            } else {
                crc = (uint16_t)(crc >> 1U);
            }
        }
    }
    return crc;
}

static uint32_t crc32_le_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    while (len-- > 0U) {
        crc ^= *data++;
        for (uint32_t i = 0; i < 8U; i++) {
            if ((crc & 1UL) != 0UL) {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            } else {
                crc >>= 1U;
            }
        }
    }
    return crc;
}

static uint16_t packet_header_crc(const esp32_spi_header_t *header)
{
    esp32_spi_header_t tmp = *header;
    tmp.header_crc16 = 0U;
    return crc16_le_update(0xFFFFU, (const uint8_t *)&tmp, (uint32_t)sizeof(tmp));
}

static uint32_t packet_payload_crc(const uint8_t *payload, uint16_t len)
{
    if (len == 0U) {
        return 0U;
    }
    return crc32_le_update(0U, payload, len);
}

static const char *msg_name(uint8_t msg_type)
{
    switch ((esp32_msg_type_t)msg_type) {
    case ESP32_MSG_NOOP: return "NOOP";
    case ESP32_MSG_HELLO_REQ: return "HELLO_REQ";
    case ESP32_MSG_HELLO_RESP: return "HELLO_RESP";
    case ESP32_MSG_SET_DEVICE_CONFIG_REQ: return "SET_DEVICE_CONFIG_REQ";
    case ESP32_MSG_SET_DEVICE_CONFIG_RESP: return "SET_DEVICE_CONFIG_RESP";
    case ESP32_MSG_SET_COMM_PARAMS_REQ: return "SET_COMM_PARAMS_REQ";
    case ESP32_MSG_SET_COMM_PARAMS_RESP: return "SET_COMM_PARAMS_RESP";
    case ESP32_MSG_CONNECT_REQ: return "CONNECT_REQ";
    case ESP32_MSG_CONNECT_RESP: return "CONNECT_RESP";
    case ESP32_MSG_DISCONNECT_REQ: return "DISCONNECT_REQ";
    case ESP32_MSG_DISCONNECT_RESP: return "DISCONNECT_RESP";
    case ESP32_MSG_START_REPORT_REQ: return "START_REPORT_REQ";
    case ESP32_MSG_START_REPORT_RESP: return "START_REPORT_RESP";
    case ESP32_MSG_STOP_REPORT_REQ: return "STOP_REPORT_REQ";
    case ESP32_MSG_STOP_REPORT_RESP: return "STOP_REPORT_RESP";
    case ESP32_MSG_QUERY_STATUS_REQ: return "QUERY_STATUS_REQ";
    case ESP32_MSG_STATUS_RESP: return "STATUS_RESP";
    case ESP32_MSG_PING_REQ: return "PING_REQ";
    case ESP32_MSG_PING_RESP: return "PING_RESP";
    case ESP32_MSG_CLOUD_CONNECT_REQ: return "CLOUD_CONNECT_REQ";
    case ESP32_MSG_CLOUD_CONNECT_RESP: return "CLOUD_CONNECT_RESP";
    case ESP32_MSG_REGISTER_REQ: return "REGISTER_REQ";
    case ESP32_MSG_REGISTER_RESP: return "REGISTER_RESP";
    case ESP32_MSG_REPORT_SUMMARY: return "REPORT_SUMMARY";
    case ESP32_MSG_REPORT_FULL_BEGIN: return "REPORT_FULL_BEGIN";
    case ESP32_MSG_REPORT_FULL_WAVE_CHUNK: return "REPORT_FULL_WAVE_CHUNK";
    case ESP32_MSG_REPORT_FULL_FFT_CHUNK: return "REPORT_FULL_FFT_CHUNK";
    case ESP32_MSG_REPORT_FULL_END: return "REPORT_FULL_END";
    case ESP32_MSG_TX_ACCEPTED: return "TX_ACCEPTED";
    case ESP32_MSG_TX_RESULT: return "TX_RESULT";
    case ESP32_MSG_EVENT: return "EVENT";
    case ESP32_MSG_NACK: return "NACK";
    default: return "UNKNOWN";
    }
}

static bool is_full_chunk_msg(uint8_t msg_type)
{
    return msg_type == ESP32_MSG_REPORT_FULL_WAVE_CHUNK ||
           msg_type == ESP32_MSG_REPORT_FULL_FFT_CHUNK;
}

static bool should_log_event_payload(const esp32_event_payload_t *event)
{
    if (event == NULL) {
        return false;
    }

    if (event->event_type == ESP32_EVENT_ERROR) {
        return true;
    }
    if (event->event_type == ESP32_EVENT_WIFI_STATE &&
        event->result_code == ESP32_WIFI_FAILED) {
        return true;
    }
    if (event->event_type == ESP32_EVENT_CLOUD_STATE &&
        event->result_code == ESP32_CLOUD_FAILED) {
        return true;
    }
    return false;
}

static bool should_log_packet(uint8_t msg_type)
{
    if (msg_type == ESP32_MSG_NOOP) {
        return false;
    }
    if (msg_type == ESP32_MSG_EVENT) {
        return false;
    }
#if (!ESP32_SPI_LOG_FULL_CHUNKS)
    if (is_full_chunk_msg(msg_type)) {
        return false;
    }
#endif
#if (!ESP32_SPI_LOG_TX_ACCEPTED)
    if (msg_type == ESP32_MSG_TX_ACCEPTED) {
        return false;
    }
#endif
    return true;
}

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0U) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return;
    }
    strncpy(dst, src, dst_size - 1U);
    dst[dst_size - 1U] = '\0';
}

static void copy_fixed_text(char *dst, size_t dst_size, const char *src, size_t src_size)
{
    size_t n = 0U;

    if (dst_size == 0U) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL || src_size == 0U) {
        return;
    }
    while (n < src_size && n < (dst_size - 1U) && src[n] != '\0') {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
}

static bool accept_server_command_event(const char *key, uint32_t value)
{
    uint32_t now = HAL_GetTick();

    if (key == NULL) {
        key = "";
    }

    if (strncmp(s_last_server_cmd_key, key, sizeof(s_last_server_cmd_key)) == 0 &&
        s_last_server_cmd_value == value &&
        (now - s_last_server_cmd_tick) < 10000U) {
        return false;
    }
    copy_text(s_last_server_cmd_key, sizeof(s_last_server_cmd_key), key);
    s_last_server_cmd_value = value;
    s_last_server_cmd_tick = now;
    return true;
}

static void packet_prepare(esp32_spi_packet_t *packet, uint8_t msg_type)
{
    memset(packet, 0, sizeof(*packet));
    packet->header.magic = ESP32_SPI_MAGIC;
    packet->header.version = ESP32_SPI_VERSION;
    packet->header.msg_type = msg_type;
    packet->header.session_epoch = (msg_type == ESP32_MSG_HELLO_REQ) ? 0U : s_session_epoch;
    packet->header.ack_seq = s_last_rx_seq;
    if (msg_type != ESP32_MSG_NOOP) {
        packet->header.seq = s_tx_seq++;
    }
}

static void packet_finalize(esp32_spi_packet_t *packet)
{
    packet->header.payload_crc32 = packet_payload_crc(packet->payload, packet->header.payload_len);
    packet->header.header_crc16 = packet_header_crc(&packet->header);
}

static bool packet_validate(const esp32_spi_packet_t *packet, const char **reason)
{
    uint16_t header_crc;
    uint32_t payload_crc;

    if (packet->header.magic != ESP32_SPI_MAGIC) {
        *reason = "bad_magic";
        return false;
    }
    if (packet->header.version != ESP32_SPI_VERSION) {
        *reason = "bad_version";
        return false;
    }
    if (packet->header.payload_len > ESP32_SPI_MAX_PAYLOAD) {
        *reason = "bad_len";
        return false;
    }
    header_crc = packet_header_crc(&packet->header);
    if (header_crc != packet->header.header_crc16) {
        *reason = "bad_header_crc";
        return false;
    }
    payload_crc = packet_payload_crc(packet->payload, packet->header.payload_len);
    if (payload_crc != packet->header.payload_crc32) {
        *reason = "bad_payload_crc";
        return false;
    }

    *reason = "ok";
    return true;
}

static bool wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms) {
        if (HAL_GPIO_ReadPin(ESP32_READY_GPIO_Port, ESP32_READY_Pin) == GPIO_PIN_SET) {
            return true;
        }
        HAL_Delay(1);
    }
    return false;
}

static void wait_ready_release(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while (HAL_GPIO_ReadPin(ESP32_READY_GPIO_Port, ESP32_READY_Pin) == GPIO_PIN_SET &&
           (HAL_GetTick() - start) < timeout_ms) {
        for (volatile uint32_t i = 0; i < 200U; i++) {
            __NOP();
        }
    }
}

static bool spi_bus_lock(void)
{
    SemaphoreHandle_t mutex;

    if (__get_IPSR() != 0U || xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return true;
    }

    mutex = s_spi_mutex;
    if (mutex == NULL) {
        mutex = xSemaphoreCreateMutex();
        if (mutex == NULL) {
            return true;
        }
        s_spi_mutex = mutex;
    }

    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(ESP32_SPI_LOCK_TIMEOUT_MS)) == pdTRUE) {
        return true;
    }
    printf("[ESP32SPI] SPI bus lock timeout\r\n");
    return false;
}

static void spi_bus_unlock(void)
{
    if (__get_IPSR() != 0U || xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return;
    }
    if (s_spi_mutex != NULL) {
        (void)xSemaphoreGive(s_spi_mutex);
    }
}

static bool spi_op_lock(void)
{
    SemaphoreHandle_t mutex;

    if (__get_IPSR() != 0U || xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return true;
    }

    mutex = s_spi_op_mutex;
    if (mutex == NULL) {
        mutex = xSemaphoreCreateRecursiveMutex();
        if (mutex == NULL) {
            return true;
        }
        s_spi_op_mutex = mutex;
    }

    if (xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(ESP32_SPI_LOCK_TIMEOUT_MS)) == pdTRUE) {
        return true;
    }
    printf("[ESP32SPI] SPI op lock timeout\r\n");
    return false;
}

static void spi_op_unlock(void)
{
    if (__get_IPSR() != 0U || xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return;
    }
    if (s_spi_op_mutex != NULL) {
        (void)xSemaphoreGiveRecursive(s_spi_op_mutex);
    }
}

static void cs_set(GPIO_PinState state)
{
    HAL_GPIO_WritePin(ESP32_CS_GPIO_Port, ESP32_CS_Pin, state);
    for (volatile uint32_t i = 0; i < 400U; i++) {
        __NOP();
    }
}

typedef struct {
    const void *payload;
} esp32_spi_copy_payload_ctx_t;

static bool copy_payload_builder(uint8_t *payload, uint16_t payload_len, void *ctx)
{
    const esp32_spi_copy_payload_ctx_t *copy = (const esp32_spi_copy_payload_ctx_t *)ctx;

    if (payload_len == 0U) {
        return true;
    }
    if (payload == NULL || copy == NULL || copy->payload == NULL) {
        return false;
    }
    memcpy(payload, copy->payload, payload_len);
    return true;
}

static bool spi_transaction_built_payload(uint8_t tx_type,
                                          uint16_t payload_len,
                                          esp32_spi_payload_builder_t builder,
                                          void *builder_ctx,
                                          esp32_spi_packet_t *rx_packet)
{
    esp32_spi_packet_t *tx_packet = (esp32_spi_packet_t *)s_tx_buf;
    HAL_StatusTypeDef st;

    if (payload_len > ESP32_SPI_MAX_PAYLOAD) {
        printf("[ESP32SPI] payload too large: type=%s len=%u\r\n",
               msg_name(tx_type), (unsigned int)payload_len);
        return false;
    }
    if (payload_len > 0U && builder == NULL) {
        return false;
    }

    memset(s_rx_buf, 0, sizeof(s_rx_buf));
    packet_prepare(tx_packet, tx_type);
    if (payload_len > 0U && !builder(tx_packet->payload, payload_len, builder_ctx)) {
        return false;
    }
    tx_packet->header.payload_len = payload_len;
    packet_finalize(tx_packet);
    s_last_tx_seq = tx_packet->header.seq;

    if (should_log_packet(tx_type)) {
        printf("[ESP32SPI] TX %-22s seq=%lu ack=%lu session=%lu len=%u ready=%u\r\n",
               msg_name(tx_type),
               (unsigned long)tx_packet->header.seq,
               (unsigned long)tx_packet->header.ack_seq,
               (unsigned long)tx_packet->header.session_epoch,
               (unsigned int)tx_packet->header.payload_len,
               (unsigned int)HAL_GPIO_ReadPin(ESP32_READY_GPIO_Port, ESP32_READY_Pin));
    }

    if (!wait_ready(ESP32_SPI_READY_TIMEOUT_MS)) {
        printf("[ESP32SPI] READY timeout, level=%u\r\n",
               (unsigned int)HAL_GPIO_ReadPin(ESP32_READY_GPIO_Port, ESP32_READY_Pin));
        return false;
    }

    cs_set(GPIO_PIN_RESET);
    st = HAL_SPI_TransmitReceive(&hspi2, s_tx_buf, s_rx_buf, ESP32_SPI_FRAME_SIZE, ESP32_SPI_XFER_TIMEOUT_MS);
    cs_set(GPIO_PIN_SET);
    wait_ready_release(ESP32_SPI_READY_RELEASE_TIMEOUT_MS);
    HAL_Delay(ESP32_SPI_INTER_TRANSACTION_GUARD_MS);

    if (st != HAL_OK) {
        printf("[ESP32SPI] HAL_SPI_TransmitReceive failed, st=%d err=0x%08lX state=%d\r\n",
               (int)st,
               (unsigned long)HAL_SPI_GetError(&hspi2),
               (int)HAL_SPI_GetState(&hspi2));
        (void)HAL_SPI_Abort(&hspi2);
        return false;
    }

    memcpy(rx_packet, s_rx_buf, sizeof(*rx_packet));
    return true;
}

static bool spi_transaction_payload(uint8_t tx_type,
                                    const void *payload,
                                    uint16_t payload_len,
                                    esp32_spi_packet_t *rx_packet)
{
    esp32_spi_copy_payload_ctx_t ctx;

    if (payload_len == 0U) {
        return spi_transaction_built_payload(tx_type, 0U, NULL, NULL, rx_packet);
    }

    ctx.payload = payload;
    return spi_transaction_built_payload(tx_type, payload_len, copy_payload_builder, &ctx, rx_packet);
}

static void print_text64(const char *text)
{
    char tmp[65];
    memcpy(tmp, text, 64U);
    tmp[64] = '\0';
    printf("%s", tmp);
}

static bool is_simple_response(uint8_t msg_type)
{
    switch ((esp32_msg_type_t)msg_type) {
    case ESP32_MSG_SET_DEVICE_CONFIG_RESP:
    case ESP32_MSG_SET_COMM_PARAMS_RESP:
    case ESP32_MSG_CONNECT_RESP:
    case ESP32_MSG_DISCONNECT_RESP:
    case ESP32_MSG_START_REPORT_RESP:
    case ESP32_MSG_STOP_REPORT_RESP:
    case ESP32_MSG_PING_RESP:
    case ESP32_MSG_CLOUD_CONNECT_RESP:
    case ESP32_MSG_REGISTER_RESP:
        return true;
    default:
        return false;
    }
}

static void update_status_from_payload(const esp32_status_payload_t *payload)
{
    s_status.ready = payload->ready;
    s_status.wifi_connected = payload->wifi_connected;
    s_status.cloud_connected = payload->cloud_connected;
    s_status.registered_with_cloud = payload->registered_with_cloud;
    s_status.reporting_enabled = payload->reporting_enabled;
    s_status.report_mode = payload->report_mode;
    s_status.rssi_dbm = payload->rssi_dbm;
    s_status.session_epoch = payload->session_epoch;
    s_status.last_frame_id = payload->last_frame_id;
    s_status.downsample_step = payload->downsample_step;
    s_status.upload_points = payload->upload_points;
    s_status.last_http_status = payload->last_http_status;
    s_status.config_version = payload->config_version;
    s_status.last_command_id = payload->last_command_id;
    s_status.last_error_code = payload->last_error_code;
    copy_fixed_text(s_status.ip_address, sizeof(s_status.ip_address), payload->ip_address, sizeof(payload->ip_address));
    copy_fixed_text(s_status.node_id, sizeof(s_status.node_id), payload->node_id, sizeof(payload->node_id));
    copy_fixed_text(s_status.last_error, sizeof(s_status.last_error), payload->last_error, sizeof(payload->last_error));
    if (payload->session_epoch != 0U) {
        s_session_epoch = payload->session_epoch;
    }
}

static void update_status_from_event(const esp32_event_payload_t *event)
{
    s_last_event_type = (uint8_t)event->event_type;
    s_last_event_result = event->result_code;

    switch ((esp32_event_type_t)event->event_type) {
    case ESP32_EVENT_READY:
        s_status.ready = 1U;
        if (s_session_epoch == 0U && event->value0 != 0U) {
            s_session_epoch = event->value0;
            s_status.session_epoch = event->value0;
        }
        break;
    case ESP32_EVENT_WIFI_STATE:
        if (event->result_code == ESP32_WIFI_CONNECTED) {
            s_status.wifi_connected = 1U;
            s_wifi_failed_seen = 0U;
            copy_fixed_text(s_status.ip_address, sizeof(s_status.ip_address), event->text, sizeof(event->text));
            s_status.rssi_dbm = (int8_t)event->value2;
        } else if (event->result_code == ESP32_WIFI_FAILED) {
            s_status.wifi_connected = 0U;
            s_status.cloud_connected = 0U;
            s_status.registered_with_cloud = 0U;
            s_wifi_failed_seen = 1U;
            copy_text(s_status.last_error, sizeof(s_status.last_error), "wifi_failed");
        } else if (event->result_code == ESP32_WIFI_IDLE) {
            s_status.wifi_connected = 0U;
            s_status.cloud_connected = 0U;
            s_status.registered_with_cloud = 0U;
        }
        break;
    case ESP32_EVENT_CLOUD_STATE:
        if (event->result_code == ESP32_CLOUD_REGISTERED) {
            s_status.cloud_connected = 1U;
            s_status.registered_with_cloud = 1U;
        } else if (event->result_code == ESP32_CLOUD_CONNECTING) {
            s_status.cloud_connected = 1U;
        } else if (event->result_code == ESP32_CLOUD_FAILED) {
            s_status.cloud_connected = 0U;
            s_status.registered_with_cloud = 0U;
        }
        copy_fixed_text(s_status.last_error, sizeof(s_status.last_error), event->text, sizeof(event->text));
        break;
    case ESP32_EVENT_REGISTER_RESULT:
        s_register_event_seen = 1U;
        s_register_result = event->result_code;
        s_status.last_http_status = (int32_t)event->value0;
        if (event->result_code == ESP32_RESULT_OK) {
            s_status.cloud_connected = 1U;
            s_status.registered_with_cloud = 1U;
        } else {
            s_status.cloud_connected = 0U;
            s_status.registered_with_cloud = 0U;
        }
        copy_fixed_text(s_status.last_error, sizeof(s_status.last_error), event->text, sizeof(event->text));
        break;
    case ESP32_EVENT_REPORT_RESULT:
        s_status.last_http_status = (int32_t)event->value0;
        s_status.last_frame_id = event->value1;
        copy_fixed_text(s_status.last_error, sizeof(s_status.last_error), event->text, sizeof(event->text));
        break;
    case ESP32_EVENT_SERVER_COMMAND:
    {
        char text[65];
        copy_fixed_text(text, sizeof(text), event->text, sizeof(event->text));
        copy_text(s_status.last_error, sizeof(s_status.last_error), text);
        if (event->result_code != ESP32_RESULT_OK) {
            break;
        }
        if (!accept_server_command_event(text, event->value1)) {
            break;
        }
        if (strcmp(text, "reset") == 0) {
            s_pending_server_reset = 1U;
        } else if (strcmp(text, "report_mode") == 0) {
            s_pending_server_report_mode_valid = 1U;
            s_pending_server_report_full = (event->value1 != 0U) ? 1U : 0U;
            s_status.report_mode = s_pending_server_report_full;
        } else if (strcmp(text, "downsample_step") == 0) {
            s_pending_server_downsample_valid = 1U;
            s_pending_server_downsample_step = event->value1;
            s_status.downsample_step = event->value1;
        } else if (strcmp(text, "upload_points") == 0) {
            s_pending_server_upload_valid = 1U;
            s_pending_server_upload_points = event->value1;
            s_status.upload_points = event->value1;
        } else if (strcmp(text, "heartbeat_ms") == 0) {
            s_pending_server_heartbeat_valid = 1U;
            s_pending_server_heartbeat_ms = event->value1;
        } else if (strcmp(text, "min_interval_ms") == 0) {
            s_pending_server_min_interval_valid = 1U;
            s_pending_server_min_interval_ms = event->value1;
        } else if (strcmp(text, "http_timeout_ms") == 0) {
            s_pending_server_http_timeout_valid = 1U;
            s_pending_server_http_timeout_ms = event->value1;
        } else if (strcmp(text, "chunk_kb") == 0) {
            s_pending_server_chunk_kb_valid = 1U;
            s_pending_server_chunk_kb = event->value1;
        } else if (strcmp(text, "chunk_delay_ms") == 0) {
            s_pending_server_chunk_delay_valid = 1U;
            s_pending_server_chunk_delay_ms = event->value1;
        }
        break;
    }
    case ESP32_EVENT_ERROR:
        copy_fixed_text(s_status.last_error, sizeof(s_status.last_error), event->text, sizeof(event->text));
        break;
    default:
        break;
    }
}

static bool handle_rx_packet(const esp32_spi_packet_t *packet)
{
    const char *reason = NULL;

    if (!packet_validate(packet, &reason)) {
        printf("[ESP32SPI] RX invalid: %s magic=0x%08lX type=0x%02X len=%u\r\n",
               reason,
               (unsigned long)packet->header.magic,
               (unsigned int)packet->header.msg_type,
               (unsigned int)packet->header.payload_len);
        return false;
    }

    if (should_log_packet(packet->header.msg_type)) {
        printf("[ESP32SPI] RX %-22s seq=%lu ack=%lu session=%lu len=%u\r\n",
               msg_name(packet->header.msg_type),
               (unsigned long)packet->header.seq,
               (unsigned long)packet->header.ack_seq,
               (unsigned long)packet->header.session_epoch,
               (unsigned int)packet->header.payload_len);
    }

    if (packet->header.session_epoch != 0U && packet->header.session_epoch != s_session_epoch) {
        if (s_session_epoch != 0U) {
            printf("[ESP32SPI] session changed: old=%lu new=%lu\r\n",
                   (unsigned long)s_session_epoch,
                   (unsigned long)packet->header.session_epoch);
            s_session_mismatch_seen = 1U;
        }
        s_session_epoch = packet->header.session_epoch;
        s_status.session_epoch = packet->header.session_epoch;
        s_last_rx_seq = 0U;
        s_tx_seq = 1U;
    }
    if (packet->header.seq != 0U && packet->header.seq > s_last_rx_seq) {
        s_last_rx_seq = packet->header.seq;
    }

    if (is_simple_response(packet->header.msg_type) &&
        packet->header.payload_len >= sizeof(esp32_event_payload_t)) {
        const esp32_event_payload_t *resp = (const esp32_event_payload_t *)packet->payload;
        s_last_response_type = packet->header.msg_type;
        s_last_result_code = resp->result_code;
        copy_fixed_text(s_last_response_text, sizeof(s_last_response_text), resp->text, sizeof(resp->text));
        printf("[ESP32SPI] RESP type=%s result=%u text=%s\r\n",
               msg_name(packet->header.msg_type),
               (unsigned int)resp->result_code,
               s_last_response_text);
    }

    switch ((esp32_msg_type_t)packet->header.msg_type) {
    case ESP32_MSG_HELLO_RESP:
        if (packet->header.payload_len >= sizeof(esp32_hello_payload_t)) {
            const esp32_hello_payload_t *hello = (const esp32_hello_payload_t *)packet->payload;
            s_session_epoch = hello->boot_epoch;
            s_status.ready = 1U;
            s_status.session_epoch = hello->boot_epoch;
            s_last_response_type = ESP32_MSG_HELLO_RESP;
            s_last_result_code = ESP32_RESULT_OK;
            printf("[ESP32SPI] HELLO boot=%lu caps=0x%08lX max_payload=%u\r\n",
                   (unsigned long)hello->boot_epoch,
                   (unsigned long)hello->capability_flags,
                   (unsigned int)hello->max_payload);
        }
        break;
    case ESP32_MSG_STATUS_RESP:
        if (packet->header.payload_len >= sizeof(esp32_status_payload_t)) {
            const esp32_status_payload_t *status = (const esp32_status_payload_t *)packet->payload;
            update_status_from_payload(status);
            s_last_response_type = ESP32_MSG_STATUS_RESP;
            s_last_result_code = ESP32_RESULT_OK;
            printf("[ESP32SPI] STATUS ready=%u wifi=%u cloud=%u reg=%u report=%u ip=%s node=%s err=%s\r\n",
                   (unsigned int)s_status.ready,
                   (unsigned int)s_status.wifi_connected,
                   (unsigned int)s_status.cloud_connected,
                   (unsigned int)s_status.registered_with_cloud,
                   (unsigned int)s_status.reporting_enabled,
                   s_status.ip_address,
                   s_status.node_id,
                   s_status.last_error);
        }
        break;
    case ESP32_MSG_EVENT:
    case ESP32_MSG_PING_RESP:
        if (packet->header.payload_len >= sizeof(esp32_event_payload_t)) {
            const esp32_event_payload_t *event = (const esp32_event_payload_t *)packet->payload;
            update_status_from_event(event);
            if (should_log_event_payload(event)) {
                printf("[ESP32SPI] EVENT type=%u result=%u v0=%lu v1=%lu text=",
                       (unsigned int)event->event_type,
                       (unsigned int)event->result_code,
                       (unsigned long)event->value0,
                       (unsigned long)event->value1);
                print_text64(event->text);
                printf("\r\n");
            }
        }
        break;
    case ESP32_MSG_TX_ACCEPTED:
        if (packet->header.payload_len >= sizeof(esp32_tx_accepted_payload_t)) {
            const esp32_tx_accepted_payload_t *accepted = (const esp32_tx_accepted_payload_t *)packet->payload;
            s_last_tx_accepted_ref_seq = accepted->ref_seq;
#if (ESP32_SPI_LOG_TX_ACCEPTED)
            printf("[ESP32SPI] TX_ACCEPTED ref_seq=%lu frame=%lu q=%u\r\n",
                   (unsigned long)accepted->ref_seq,
                   (unsigned long)accepted->ref_frame_id,
                   (unsigned int)accepted->queue_depth);
#endif
        }
        break;
    case ESP32_MSG_TX_RESULT:
        if (packet->header.payload_len >= sizeof(esp32_tx_result_payload_t)) {
            const esp32_tx_result_payload_t *result = (const esp32_tx_result_payload_t *)packet->payload;
            s_last_tx_result_ref_seq = result->ref_seq;
            s_last_tx_result_code = result->result_code;
            s_last_tx_result_http_status = result->http_status;
            s_last_tx_result_frame_id = result->ref_frame_id;
            s_status.last_http_status = result->http_status;
            s_status.last_frame_id = result->ref_frame_id;
            printf("[ESP32SPI] TX_RESULT ref_seq=%lu frame=%lu http=%ld result=%ld\r\n",
                   (unsigned long)result->ref_seq,
                   (unsigned long)result->ref_frame_id,
                   (long)result->http_status,
                   (long)result->result_code);
        }
        break;
    case ESP32_MSG_NACK:
        if (packet->header.payload_len >= sizeof(esp32_nack_payload_t)) {
            const esp32_nack_payload_t *nack = (const esp32_nack_payload_t *)packet->payload;
            s_last_nack_ref_seq = nack->ref_seq;
            s_last_nack_reason = nack->reason;
            if (nack->reason == ESP32_NACK_SESSION_MISMATCH) {
                s_session_epoch = 0U;
                s_status.session_epoch = 0U;
                s_session_mismatch_seen = 1U;
            }
            if (!nack_reason_is_retryable(nack->reason) || ESP32_SPI_LOG_RETRYABLE_NACKS) {
                printf("[ESP32SPI] NACK ref_seq=%lu reason=%u\r\n",
                       (unsigned long)nack->ref_seq,
                       (unsigned int)nack->reason);
            }
        }
        break;
    default:
        break;
    }
    return true;
}

static bool transact_and_handle_payload(uint8_t tx_type,
                                        const void *payload,
                                        uint16_t payload_len,
                                        uint8_t *rx_type)
{
    esp32_spi_packet_t rx_packet;
    bool ok;

    if (!spi_bus_lock()) {
        return false;
    }
    if (!spi_transaction_payload(tx_type, payload, payload_len, &rx_packet)) {
        spi_bus_unlock();
        return false;
    }
    ok = handle_rx_packet(&rx_packet);
    if (rx_type != NULL) {
        *rx_type = rx_packet.header.msg_type;
    }
    spi_bus_unlock();
    return ok;
}

static bool transact_and_handle_built_payload(uint8_t tx_type,
                                              uint16_t payload_len,
                                              esp32_spi_payload_builder_t builder,
                                              void *builder_ctx,
                                              uint8_t *rx_type)
{
    esp32_spi_packet_t rx_packet;
    bool ok;

    if (!spi_bus_lock()) {
        return false;
    }
    if (!spi_transaction_built_payload(tx_type, payload_len, builder, builder_ctx, &rx_packet)) {
        spi_bus_unlock();
        return false;
    }
    ok = handle_rx_packet(&rx_packet);
    if (rx_type != NULL) {
        *rx_type = rx_packet.header.msg_type;
    }
    spi_bus_unlock();
    return ok;
}

static bool poll_noop(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t rx_type;

    do {
        if (!transact_and_handle_payload(ESP32_MSG_NOOP, NULL, 0U, &rx_type)) {
            return false;
        }
        if (rx_type != ESP32_MSG_NOOP) {
            return true;
        }
        HAL_Delay(10);
    } while ((HAL_GetTick() - start) < timeout_ms);

    return true;
}

static void drain_pending_packets(uint32_t max_ms, uint8_t quiet_noop_count)
{
    uint32_t start = HAL_GetTick();
    uint8_t quiet = 0U;
    uint8_t rx_type = ESP32_MSG_NOOP;

    while ((HAL_GetTick() - start) < max_ms && quiet < quiet_noop_count) {
        if (!transact_and_handle_payload(ESP32_MSG_NOOP, NULL, 0U, &rx_type)) {
            return;
        }
        if (rx_type == ESP32_MSG_NOOP) {
            quiet++;
        } else {
            quiet = 0U;
        }
        HAL_Delay(5);
    }
    printf("[ESP32SPI] drain done, quiet=%u/%u last_rx_seq=%lu\r\n",
           (unsigned int)quiet,
           (unsigned int)quiet_noop_count,
           (unsigned long)s_last_rx_seq);
}

static bool wait_for_response(uint8_t response_type, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    if (s_last_response_type == response_type) {
        return s_last_result_code == ESP32_RESULT_OK;
    }

    while ((HAL_GetTick() - start) < timeout_ms) {
        if (!poll_noop(40U)) {
            return false;
        }
        if (s_last_response_type == response_type) {
            return s_last_result_code == ESP32_RESULT_OK;
        }
        if (s_session_mismatch_seen) {
            return false;
        }
    }

    printf("[ESP32SPI] response timeout: expect=%s last=%s result=%u text=%s\r\n",
           msg_name(response_type),
           msg_name(s_last_response_type),
           (unsigned int)s_last_result_code,
           s_last_response_text);
    return false;
}

static bool request_wait_response(uint8_t request_type,
                                  const void *payload,
                                  uint16_t payload_len,
                                  uint8_t response_type,
                                  uint32_t timeout_ms)
{
    uint8_t rx_type;

    s_last_response_type = ESP32_MSG_NOOP;
    s_last_result_code = ESP32_SPI_RESULT_PENDING;
    s_last_response_text[0] = '\0';
    s_last_nack_ref_seq = 0U;
    s_last_nack_reason = 0U;
    s_session_mismatch_seen = 0U;

    if (!transact_and_handle_payload(request_type, payload, payload_len, &rx_type)) {
        return false;
    }
    if (s_session_mismatch_seen) {
        return false;
    }
    if (s_last_response_type == response_type) {
        return s_last_result_code == ESP32_RESULT_OK;
    }
    return wait_for_response(response_type, timeout_ms);
}

static bool query_status_internal(uint32_t timeout_ms)
{
    return request_wait_response(ESP32_MSG_QUERY_STATUS_REQ, NULL, 0U, ESP32_MSG_STATUS_RESP, timeout_ms);
}

static void reset_link_state(void)
{
    s_tx_seq = 1U;
    s_last_rx_seq = 0U;
    s_session_epoch = 0U;
    s_last_tx_seq = 0U;
    s_last_response_type = ESP32_MSG_NOOP;
    s_last_result_code = ESP32_SPI_RESULT_PENDING;
    s_last_response_text[0] = '\0';
    s_last_event_type = 0U;
    s_last_event_result = ESP32_SPI_RESULT_PENDING;
    s_wifi_failed_seen = 0U;
    s_register_event_seen = 0U;
    s_register_result = ESP32_SPI_RESULT_PENDING;
    s_session_mismatch_seen = 0U;
    memset(&s_status, 0, sizeof(s_status));
}

static void esp32_hard_reset(void)
{
    printf("[ESP32SPI] hardware reset ESP32 via EN\r\n");
    HAL_GPIO_WritePin(ESP32_CS_GPIO_Port, ESP32_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ESP32_EN_GPIO_Port, ESP32_EN_Pin, GPIO_PIN_RESET);
    HAL_Delay(150U);
    HAL_GPIO_WritePin(ESP32_EN_GPIO_Port, ESP32_EN_Pin, GPIO_PIN_SET);
    HAL_Delay(2500U);
}

bool ESP32_SPI_EnsureReady(uint32_t timeout_ms)
{
    uint32_t start;
    uint8_t rx_type = ESP32_MSG_NOOP;

    if (!spi_op_lock()) {
        return false;
    }

    if (timeout_ms == 0U) {
        timeout_ms = ESP32_SPI_DEFAULT_TIMEOUT_MS;
    }

    HAL_GPIO_WritePin(ESP32_EN_GPIO_Port, ESP32_EN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ESP32_CS_GPIO_Port, ESP32_CS_Pin, GPIO_PIN_SET);

    if (s_session_epoch != 0U && s_status.ready != 0U) {
        spi_op_unlock();
        return true;
    }

    reset_link_state();
    esp32_hard_reset();
    start = HAL_GetTick();

    printf("\r\n[ESP32SPI] handshake start, wire_size=%u\r\n", (unsigned int)ESP32_SPI_FRAME_SIZE);
    printf("[ESP32SPI] pins: CS=%u READY=%u EN=%u\r\n",
           (unsigned int)HAL_GPIO_ReadPin(ESP32_CS_GPIO_Port, ESP32_CS_Pin),
           (unsigned int)HAL_GPIO_ReadPin(ESP32_READY_GPIO_Port, ESP32_READY_Pin),
           (unsigned int)HAL_GPIO_ReadPin(ESP32_EN_GPIO_Port, ESP32_EN_Pin));

    if (!transact_and_handle_payload(ESP32_MSG_HELLO_REQ, NULL, 0U, &rx_type)) {
        printf("[ESP32SPI] HELLO transaction failed\r\n");
        spi_op_unlock();
        return false;
    }

    while (s_session_epoch == 0U && (HAL_GetTick() - start) < timeout_ms) {
        HAL_Delay(20);
        if (!transact_and_handle_payload(ESP32_MSG_NOOP, NULL, 0U, &rx_type)) {
            spi_op_unlock();
            return false;
        }
    }

    if (s_session_epoch == 0U) {
        printf("[ESP32SPI] handshake timeout\r\n");
        spi_op_unlock();
        return false;
    }

    drain_pending_packets(2000U, 3U);
    (void)query_status_internal(1000U);
    printf("[ESP32SPI] ready, session=%lu\r\n", (unsigned long)s_session_epoch);
    spi_op_unlock();
    return true;
}

static bool last_failure_was_session_mismatch(void)
{
    return (s_session_mismatch_seen != 0U) ||
           (s_last_nack_reason == ESP32_NACK_SESSION_MISMATCH);
}

static bool recover_session_mismatch(const char *op_name)
{
    printf("[ESP32SPI] recover session mismatch before retry op=%s\r\n",
           op_name ? op_name : "unknown");
    reset_link_state();
    return ESP32_SPI_EnsureReady(ESP32_SPI_DEFAULT_TIMEOUT_MS);
}

static bool request_wait_response_session_retry(uint8_t request_type,
                                                const void *payload,
                                                uint16_t payload_len,
                                                uint8_t response_type,
                                                uint32_t timeout_ms)
{
    if (request_wait_response(request_type, payload, payload_len, response_type, timeout_ms)) {
        return true;
    }

    if (!last_failure_was_session_mismatch()) {
        return false;
    }

    if (!recover_session_mismatch(msg_name(request_type))) {
        return false;
    }

    return request_wait_response(request_type, payload, payload_len, response_type, timeout_ms);
}

bool ESP32_SPI_ApplyDeviceConfig(const char *ssid,
                                 const char *password,
                                 const char *server_host,
                                 uint16_t server_port,
                                 const char *node_id,
                                 const char *node_location,
                                 const char *hw_version)
{
    esp32_device_config_payload_t payload;
    bool ok;

    if (!ESP32_SPI_EnsureReady(ESP32_SPI_DEFAULT_TIMEOUT_MS)) {
        return false;
    }

    memset(&payload, 0, sizeof(payload));
    copy_text(payload.wifi_ssid, sizeof(payload.wifi_ssid), ssid);
    copy_text(payload.wifi_password, sizeof(payload.wifi_password), password);
    copy_text(payload.server_host, sizeof(payload.server_host), server_host);
    payload.server_port = server_port;
    copy_text(payload.node_id, sizeof(payload.node_id), node_id);
    copy_text(payload.node_location, sizeof(payload.node_location), node_location);
    copy_text(payload.hw_version, sizeof(payload.hw_version), hw_version);

    printf("[ESP32SPI] apply device cfg ssid=%s host=%s:%u node=%s\r\n",
           payload.wifi_ssid,
           payload.server_host,
           (unsigned int)payload.server_port,
           payload.node_id);

    if (!spi_op_lock()) {
        return false;
    }
    ok = request_wait_response_session_retry(ESP32_MSG_SET_DEVICE_CONFIG_REQ,
                                             &payload,
                                             (uint16_t)sizeof(payload),
                                             ESP32_MSG_SET_DEVICE_CONFIG_RESP,
                                             8000U);
    spi_op_unlock();
    return ok;
}

bool ESP32_SPI_ApplyCommParams(uint32_t heartbeat_ms,
                               uint32_t min_interval_ms,
                               uint32_t http_timeout_ms,
                               uint32_t reconnect_backoff_ms,
                               uint32_t downsample_step,
                               uint32_t upload_points,
                               uint32_t hardreset_sec,
                               uint32_t chunk_kb,
                               uint32_t chunk_delay_ms)
{
    esp32_comm_params_payload_t payload;
    bool ok;

    if (!ESP32_SPI_EnsureReady(ESP32_SPI_DEFAULT_TIMEOUT_MS)) {
        return false;
    }

    memset(&payload, 0, sizeof(payload));
    payload.heartbeat_ms = heartbeat_ms;
    payload.min_interval_ms = min_interval_ms;
    payload.http_timeout_ms = http_timeout_ms;
    payload.reconnect_backoff_ms = reconnect_backoff_ms;
    payload.downsample_step = downsample_step;
    payload.upload_points = upload_points;
    payload.hardreset_sec = hardreset_sec;
    payload.chunk_kb = chunk_kb;
    payload.chunk_delay_ms = chunk_delay_ms;

    printf("[ESP32SPI] apply comm hb=%lums min=%lums http=%lums step=%lu points=%lu\r\n",
           (unsigned long)heartbeat_ms,
           (unsigned long)min_interval_ms,
           (unsigned long)http_timeout_ms,
           (unsigned long)downsample_step,
           (unsigned long)upload_points);

    if (!spi_op_lock()) {
        return false;
    }
    ok = request_wait_response_session_retry(ESP32_MSG_SET_COMM_PARAMS_REQ,
                                             &payload,
                                             (uint16_t)sizeof(payload),
                                             ESP32_MSG_SET_COMM_PARAMS_RESP,
                                             5000U);
    spi_op_unlock();
    return ok;
}

bool ESP32_SPI_QueryStatus(esp32_spi_status_t *out_status, uint32_t timeout_ms)
{
    bool ok;

    if (!ESP32_SPI_EnsureReady(ESP32_SPI_DEFAULT_TIMEOUT_MS)) {
        return false;
    }
    if (!spi_op_lock()) {
        return false;
    }
    ok = query_status_internal(timeout_ms == 0U ? 1000U : timeout_ms);
    if (!ok) {
        if (!last_failure_was_session_mismatch() ||
            !recover_session_mismatch("QUERY_STATUS_REQ") ||
            !query_status_internal(timeout_ms == 0U ? 1000U : timeout_ms)) {
            spi_op_unlock();
            return false;
        }
    }
    if (out_status != NULL) {
        *out_status = s_status;
    }
    spi_op_unlock();
    return true;
}

const esp32_spi_status_t *ESP32_SPI_GetStatus(void)
{
    return &s_status;
}

bool ESP32_SPI_PollEvents(uint32_t timeout_ms)
{
    bool ok;

    if (!spi_op_lock()) {
        return false;
    }
    ok = poll_noop(timeout_ms);
    spi_op_unlock();
    return ok;
}

uint32_t ESP32_SPI_GetLastTxSeq(void)
{
    return s_last_tx_seq;
}

uint32_t ESP32_SPI_GetLastFullEndRefSeq(void)
{
    return s_last_report_full_end_ref_seq;
}

uint32_t ESP32_SPI_GetLastNackRefSeq(void)
{
    return s_last_nack_ref_seq;
}

uint16_t ESP32_SPI_GetLastNackReason(void)
{
    return s_last_nack_reason;
}

bool ESP32_SPI_ConsumeServerCommand(uint8_t *out_reset,
                                    uint8_t *out_has_report_mode,
                                    uint8_t *out_report_full,
                                    uint8_t *out_has_downsample_step,
                                    uint32_t *out_downsample_step,
                                    uint8_t *out_has_upload_points,
                                    uint32_t *out_upload_points,
                                    uint8_t *out_has_heartbeat_ms,
                                    uint32_t *out_heartbeat_ms,
                                    uint8_t *out_has_min_interval_ms,
                                    uint32_t *out_min_interval_ms,
                                    uint8_t *out_has_http_timeout_ms,
                                    uint32_t *out_http_timeout_ms,
                                    uint8_t *out_has_chunk_kb,
                                    uint32_t *out_chunk_kb,
                                    uint8_t *out_has_chunk_delay_ms,
                                    uint32_t *out_chunk_delay_ms)
{
    uint8_t has_any = (s_pending_server_reset ||
                       s_pending_server_report_mode_valid ||
                       s_pending_server_downsample_valid ||
                       s_pending_server_upload_valid ||
                       s_pending_server_heartbeat_valid ||
                       s_pending_server_min_interval_valid ||
                       s_pending_server_http_timeout_valid ||
                       s_pending_server_chunk_kb_valid ||
                       s_pending_server_chunk_delay_valid) ? 1U : 0U;

    if (out_reset != NULL) {
        *out_reset = s_pending_server_reset;
    }
    if (out_has_report_mode != NULL) {
        *out_has_report_mode = s_pending_server_report_mode_valid;
    }
    if (out_report_full != NULL) {
        *out_report_full = s_pending_server_report_full;
    }
    if (out_has_downsample_step != NULL) {
        *out_has_downsample_step = s_pending_server_downsample_valid;
    }
    if (out_downsample_step != NULL) {
        *out_downsample_step = s_pending_server_downsample_step;
    }
    if (out_has_upload_points != NULL) {
        *out_has_upload_points = s_pending_server_upload_valid;
    }
    if (out_upload_points != NULL) {
        *out_upload_points = s_pending_server_upload_points;
    }
    if (out_has_heartbeat_ms != NULL) {
        *out_has_heartbeat_ms = s_pending_server_heartbeat_valid;
    }
    if (out_heartbeat_ms != NULL) {
        *out_heartbeat_ms = s_pending_server_heartbeat_ms;
    }
    if (out_has_min_interval_ms != NULL) {
        *out_has_min_interval_ms = s_pending_server_min_interval_valid;
    }
    if (out_min_interval_ms != NULL) {
        *out_min_interval_ms = s_pending_server_min_interval_ms;
    }
    if (out_has_http_timeout_ms != NULL) {
        *out_has_http_timeout_ms = s_pending_server_http_timeout_valid;
    }
    if (out_http_timeout_ms != NULL) {
        *out_http_timeout_ms = s_pending_server_http_timeout_ms;
    }
    if (out_has_chunk_kb != NULL) {
        *out_has_chunk_kb = s_pending_server_chunk_kb_valid;
    }
    if (out_chunk_kb != NULL) {
        *out_chunk_kb = s_pending_server_chunk_kb;
    }
    if (out_has_chunk_delay_ms != NULL) {
        *out_has_chunk_delay_ms = s_pending_server_chunk_delay_valid;
    }
    if (out_chunk_delay_ms != NULL) {
        *out_chunk_delay_ms = s_pending_server_chunk_delay_ms;
    }

    s_pending_server_reset = 0U;
    s_pending_server_report_mode_valid = 0U;
    s_pending_server_downsample_valid = 0U;
    s_pending_server_upload_valid = 0U;
    s_pending_server_heartbeat_valid = 0U;
    s_pending_server_min_interval_valid = 0U;
    s_pending_server_http_timeout_valid = 0U;
    s_pending_server_chunk_kb_valid = 0U;
    s_pending_server_chunk_delay_valid = 0U;
    return has_any != 0U;
}

bool ESP32_SPI_GetTxResult(uint32_t ref_seq,
                           int32_t *out_http_status,
                           int32_t *out_result_code,
                           uint32_t *out_frame_id)
{
    if (ref_seq == 0U || s_last_tx_result_ref_seq != ref_seq) {
        return false;
    }
    if (out_http_status != NULL) {
        *out_http_status = s_last_tx_result_http_status;
    }
    if (out_result_code != NULL) {
        *out_result_code = s_last_tx_result_code;
    }
    if (out_frame_id != NULL) {
        *out_frame_id = s_last_tx_result_frame_id;
    }
    return true;
}

bool ESP32_SPI_ConnectWifi(uint32_t timeout_ms)
{
    uint32_t start;

    if (!ESP32_SPI_EnsureReady(ESP32_SPI_DEFAULT_TIMEOUT_MS)) {
        return false;
    }
    if (!spi_op_lock()) {
        return false;
    }

    s_wifi_failed_seen = 0U;
    if (!request_wait_response_session_retry(ESP32_MSG_CONNECT_REQ, NULL, 0U, ESP32_MSG_CONNECT_RESP, 3000U)) {
        spi_op_unlock();
        return false;
    }

    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (s_status.wifi_connected) {
            (void)query_status_internal(500U);
            spi_op_unlock();
            return true;
        }
        if (s_wifi_failed_seen) {
            (void)query_status_internal(500U);
            spi_op_unlock();
            return false;
        }
        HAL_Delay(ESP32_SPI_WIFI_POLL_INTERVAL_MS);
        (void)query_status_internal(800U);
        if (s_status.wifi_connected) {
            spi_op_unlock();
            return true;
        }
        if (s_wifi_failed_seen) {
            spi_op_unlock();
            return false;
        }
    }

    printf("[ESP32SPI] WiFi connect timeout, last_error=%s\r\n", s_status.last_error);
    spi_op_unlock();
    return false;
}

bool ESP32_SPI_CloudConnect(uint32_t timeout_ms)
{
    bool ok;

    (void)timeout_ms;
    if (!ESP32_SPI_EnsureReady(ESP32_SPI_DEFAULT_TIMEOUT_MS)) {
        return false;
    }
    if (!spi_op_lock()) {
        return false;
    }
    if (!request_wait_response_session_retry(ESP32_MSG_CLOUD_CONNECT_REQ, NULL, 0U, ESP32_MSG_CLOUD_CONNECT_RESP, 3000U)) {
        (void)query_status_internal(500U);
        spi_op_unlock();
        return false;
    }
    (void)query_status_internal(500U);
    ok = s_status.cloud_connected || s_status.wifi_connected;
    spi_op_unlock();
    return ok;
}

bool ESP32_SPI_RegisterNode(uint32_t timeout_ms)
{
    uint32_t start;

    if (!ESP32_SPI_EnsureReady(ESP32_SPI_DEFAULT_TIMEOUT_MS)) {
        return false;
    }
    if (!spi_op_lock()) {
        return false;
    }

    s_register_event_seen = 0U;
    s_register_result = ESP32_SPI_RESULT_PENDING;
    if (!request_wait_response_session_retry(ESP32_MSG_REGISTER_REQ, NULL, 0U, ESP32_MSG_REGISTER_RESP, 3000U)) {
        (void)query_status_internal(500U);
        spi_op_unlock();
        return false;
    }

    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (s_status.registered_with_cloud) {
            spi_op_unlock();
            return true;
        }
        if (s_register_event_seen) {
            bool ok = (s_register_result == ESP32_RESULT_OK);
            spi_op_unlock();
            return ok;
        }
        (void)query_status_internal(600U);
        if (s_status.registered_with_cloud) {
            spi_op_unlock();
            return true;
        }
        if (s_register_event_seen) {
            bool ok = (s_register_result == ESP32_RESULT_OK);
            spi_op_unlock();
            return ok;
        }
        HAL_Delay(100);
    }

    printf("[ESP32SPI] register timeout, http=%ld err=%s\r\n",
           (long)s_status.last_http_status,
           s_status.last_error);
    spi_op_unlock();
    return false;
}

bool ESP32_SPI_StartReport(uint8_t report_mode, uint32_t timeout_ms)
{
    esp32_start_report_payload_t payload;
    bool ok;

    (void)timeout_ms;
    if (!ESP32_SPI_EnsureReady(ESP32_SPI_DEFAULT_TIMEOUT_MS)) {
        return false;
    }
    if (!spi_op_lock()) {
        return false;
    }

    memset(&payload, 0, sizeof(payload));
    payload.report_mode = report_mode ? 1U : 0U;

    if (!request_wait_response_session_retry(ESP32_MSG_START_REPORT_REQ,
                                             &payload,
                                             (uint16_t)sizeof(payload),
                                             ESP32_MSG_START_REPORT_RESP,
                                             3000U)) {
        (void)query_status_internal(500U);
        spi_op_unlock();
        return false;
    }
    (void)query_status_internal(500U);
    ok = (s_status.reporting_enabled != 0U);
    spi_op_unlock();
    return ok;
}

bool ESP32_SPI_StopReport(uint32_t timeout_ms)
{
    bool ok;

    (void)timeout_ms;
    if (!ESP32_SPI_EnsureReady(ESP32_SPI_DEFAULT_TIMEOUT_MS)) {
        return false;
    }
    if (!spi_op_lock()) {
        return false;
    }
    if (!request_wait_response_session_retry(ESP32_MSG_STOP_REPORT_REQ, NULL, 0U, ESP32_MSG_STOP_REPORT_RESP, 3000U)) {
        (void)query_status_internal(500U);
        spi_op_unlock();
        return false;
    }
    (void)query_status_internal(500U);
    ok = true;
    spi_op_unlock();
    return ok;
}

bool ESP32_SPI_ReportSummary(uint32_t frame_id,
                             uint64_t timestamp_ms,
                             uint32_t downsample_step,
                             uint32_t upload_points,
                             const char *fault_code,
                             uint8_t report_mode,
                             uint8_t status_code,
                             const esp32_spi_report_channel_t *channels,
                             uint8_t channel_count,
                             uint32_t timeout_ms)
{
    esp32_report_summary_payload_t payload;
    uint32_t ref_seq;
    uint32_t start;

    if (channels == NULL || channel_count == 0U || channel_count > 4U) {
        return false;
    }
    if (!ESP32_SPI_EnsureReady(ESP32_SPI_DEFAULT_TIMEOUT_MS)) {
        return false;
    }
    if (!spi_op_lock()) {
        return false;
    }

    memset(&payload, 0, sizeof(payload));
    payload.frame_id = frame_id;
    payload.timestamp_ms = timestamp_ms;
    payload.downsample_step = downsample_step;
    payload.upload_points = upload_points;
    copy_text(payload.fault_code, sizeof(payload.fault_code), fault_code ? fault_code : "E00");
    payload.report_mode = report_mode ? 1U : 0U;
    payload.status_code = status_code;
    payload.channel_count = channel_count;
    for (uint8_t i = 0; i < channel_count; i++) {
        payload.channels[i].channel_id = channels[i].channel_id;
        payload.channels[i].waveform_count = channels[i].waveform_count;
        payload.channels[i].fft_count = channels[i].fft_count;
        payload.channels[i].value_scaled = channels[i].value_scaled;
        payload.channels[i].current_value_scaled = channels[i].current_value_scaled;
    }

    s_last_tx_accepted_ref_seq = 0U;
    s_last_tx_result_ref_seq = 0U;
    s_last_tx_result_code = ESP32_SPI_RESULT_PENDING;
    s_last_tx_result_http_status = 0;
    s_last_tx_result_frame_id = 0U;
    s_last_nack_ref_seq = 0U;
    s_last_nack_reason = 0U;

    if (!transact_and_handle_payload(ESP32_MSG_REPORT_SUMMARY,
                                     &payload,
                                     (uint16_t)sizeof(payload),
                                     NULL)) {
        spi_op_unlock();
        return false;
    }

    ref_seq = s_last_tx_seq;
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (s_last_tx_accepted_ref_seq == ref_seq) {
            spi_op_unlock();
            return true;
        }
        if (s_last_tx_result_ref_seq == ref_seq) {
            bool ok = (s_last_tx_result_code == ESP32_RESULT_OK);
            spi_op_unlock();
            return ok;
        }
        if (s_last_nack_ref_seq == ref_seq) {
            if (!nack_reason_is_retryable(s_last_nack_reason) || ESP32_SPI_LOG_RETRYABLE_NACKS) {
                printf("[ESP32SPI] summary NACK reason=%u\r\n", (unsigned int)s_last_nack_reason);
            }
            spi_op_unlock();
            return false;
        }
        if (!poll_noop(40U)) {
            spi_op_unlock();
            return false;
        }
    }

    printf("[ESP32SPI] summary accepted timeout frame=%lu ref=%lu\r\n",
           (unsigned long)frame_id,
           (unsigned long)ref_seq);
    spi_op_unlock();
    return false;
}

#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
static int32_t scale_float_to_i32(float v, float scale)
{
    double x;

    if (!(v == v) || v > 1.0e20f || v < -1.0e20f) {
        return 0;
    }
    x = (double)v * (double)scale;
    if (x > 2147483647.0) {
        return 2147483647;
    }
    if (x < -2147483648.0) {
        return (int32_t)0x80000000UL;
    }
    return (int32_t)((x >= 0.0) ? (x + 0.5) : (x - 0.5));
}

static int16_t scale_float_to_i16(float v, float scale)
{
    double x;

    if (!(v == v) || v > 1.0e20f || v < -1.0e20f) {
        return 0;
    }
    x = (double)v * (double)scale;
    if (x > 32767.0) {
        return 32767;
    }
    if (x < -32768.0) {
        return (int16_t)-32768;
    }
    return (int16_t)((x >= 0.0) ? (x + 0.5) : (x - 0.5));
}

static bool wait_report_packet_accepted(uint32_t frame_id, uint32_t ref_seq, uint32_t timeout_ms, const char *label)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms) {
        if (s_last_tx_accepted_ref_seq == ref_seq) {
            return true;
        }
        if (s_last_tx_result_ref_seq == ref_seq) {
            return s_last_tx_result_code == ESP32_RESULT_OK;
        }
        if (s_last_nack_ref_seq == ref_seq) {
            if (!nack_reason_is_retryable(s_last_nack_reason) || ESP32_SPI_LOG_RETRYABLE_NACKS) {
                printf("[ESP32SPI] full %s NACK frame=%lu ref=%lu reason=%u\r\n",
                       label ? label : "packet",
                       (unsigned long)frame_id,
                       (unsigned long)ref_seq,
                       (unsigned int)s_last_nack_reason);
            }
            return false;
        }
        if (!poll_noop(40U)) {
            return false;
        }
    }

    printf("[ESP32SPI] full %s accepted timeout frame=%lu ref=%lu\r\n",
           label ? label : "packet",
           (unsigned long)frame_id,
           (unsigned long)ref_seq);
    return false;
}

static bool report_nack_is_retryable(uint32_t ref_seq)
{
    if (s_last_nack_ref_seq != ref_seq) {
        return true;
    }
    return nack_reason_is_retryable(s_last_nack_reason);
}

static bool nack_reason_is_retryable(uint16_t reason)
{
    return (reason == ESP32_NACK_CRC_FAIL ||
            reason == ESP32_NACK_BAD_LENGTH);
}

static void log_report_accept_retry(const char *label,
                                    uint32_t frame_id,
                                    uint32_t ref_seq,
                                    uint8_t attempt)
{
#if ESP32_SPI_LOG_RETRYABLE_NACKS
    printf("[ESP32SPI] full %s accepted retry frame=%lu ref=%lu reason=%u attempt=%u\r\n",
           label ? label : "packet",
           (unsigned long)frame_id,
           (unsigned long)ref_seq,
           (unsigned int)((s_last_nack_ref_seq == ref_seq) ? s_last_nack_reason : 0U),
           (unsigned int)(attempt + 1U));
#else
    (void)label;
    (void)frame_id;
    (void)ref_seq;
    (void)attempt;
#endif
}

static void report_retry_backoff(uint32_t ref_seq)
{
    if (s_last_nack_ref_seq != ref_seq) {
        return;
    }
    if (s_last_nack_reason == ESP32_NACK_CRC_FAIL ||
        s_last_nack_reason == ESP32_NACK_BAD_LENGTH) {
        wait_ready_release(ESP32_SPI_READY_RELEASE_TIMEOUT_MS);
        HAL_Delay(ESP32_SPI_REPORT_RETRY_BACKOFF_MS);
    }
}

static bool send_report_packet_wait(uint8_t msg_type,
                                    const void *payload,
                                    uint16_t payload_len,
                                    uint32_t frame_id,
                                    uint32_t timeout_ms,
                                    const char *label)
{
    uint32_t ref_seq;
    uint32_t recover_timeout = (timeout_ms < 150U) ? timeout_ms : 150U;

    if (!spi_op_lock()) {
        return false;
    }

    for (uint8_t attempt = 0U; attempt < ESP32_SPI_REPORT_RETRY_ATTEMPTS; attempt++) {
        s_last_tx_accepted_ref_seq = 0U;
        s_last_tx_result_ref_seq = 0U;
        s_last_tx_result_code = ESP32_SPI_RESULT_PENDING;
        s_last_tx_result_http_status = 0;
        s_last_tx_result_frame_id = 0U;
        s_last_nack_ref_seq = 0U;
        s_last_nack_reason = 0U;

        if (transact_and_handle_payload(msg_type, payload, payload_len, NULL)) {
            ref_seq = s_last_tx_seq;
            if (wait_report_packet_accepted(frame_id, ref_seq, timeout_ms, label)) {
                if (msg_type == ESP32_MSG_REPORT_FULL_END) {
                    s_last_report_full_end_ref_seq = ref_seq;
                }
                spi_op_unlock();
                return true;
            }
            if (!report_nack_is_retryable(ref_seq)) {
                spi_op_unlock();
                return false;
            }
            report_retry_backoff(ref_seq);
            if (attempt + 1U < ESP32_SPI_REPORT_RETRY_ATTEMPTS) {
                log_report_accept_retry(label, frame_id, ref_seq, attempt);
                continue;
            }
            spi_op_unlock();
            return false;
        }

        ref_seq = s_last_tx_seq;
        if (ref_seq != 0U && recover_timeout != 0U &&
            wait_report_packet_accepted(frame_id, ref_seq, recover_timeout, label)) {
            if (msg_type == ESP32_MSG_REPORT_FULL_END) {
                s_last_report_full_end_ref_seq = ref_seq;
            }
            spi_op_unlock();
            return true;
        }
        printf("[ESP32SPI] full %s transaction retry frame=%lu attempt=%u\r\n",
               label ? label : "packet",
               (unsigned long)frame_id,
               (unsigned int)(attempt + 1U));
    }
    spi_op_unlock();
    return false;
}

static bool send_report_built_packet_wait(uint8_t msg_type,
                                          uint16_t payload_len,
                                          esp32_spi_payload_builder_t builder,
                                          void *builder_ctx,
                                          uint32_t frame_id,
                                          uint32_t timeout_ms,
                                          const char *label)
{
    uint32_t ref_seq;
    uint32_t recover_timeout = (timeout_ms < 150U) ? timeout_ms : 150U;

    if (!spi_op_lock()) {
        return false;
    }

    for (uint8_t attempt = 0U; attempt < ESP32_SPI_REPORT_RETRY_ATTEMPTS; attempt++) {
        s_last_tx_accepted_ref_seq = 0U;
        s_last_tx_result_ref_seq = 0U;
        s_last_tx_result_code = ESP32_SPI_RESULT_PENDING;
        s_last_tx_result_http_status = 0;
        s_last_tx_result_frame_id = 0U;
        s_last_nack_ref_seq = 0U;
        s_last_nack_reason = 0U;

        if (transact_and_handle_built_payload(msg_type, payload_len, builder, builder_ctx, NULL)) {
            ref_seq = s_last_tx_seq;
            if (wait_report_packet_accepted(frame_id, ref_seq, timeout_ms, label)) {
                spi_op_unlock();
                return true;
            }
            if (!report_nack_is_retryable(ref_seq)) {
                spi_op_unlock();
                return false;
            }
            report_retry_backoff(ref_seq);
            if (attempt + 1U < ESP32_SPI_REPORT_RETRY_ATTEMPTS) {
                log_report_accept_retry(label, frame_id, ref_seq, attempt);
                continue;
            }
            spi_op_unlock();
            return false;
        }

        ref_seq = s_last_tx_seq;
        if (ref_seq != 0U && recover_timeout != 0U &&
            wait_report_packet_accepted(frame_id, ref_seq, recover_timeout, label)) {
            spi_op_unlock();
            return true;
        }
        printf("[ESP32SPI] full %s transaction retry frame=%lu attempt=%u\r\n",
               label ? label : "packet",
               (unsigned long)frame_id,
               (unsigned int)(attempt + 1U));
    }
    spi_op_unlock();
    return false;
}

typedef struct {
    uint32_t frame_id;
    uint8_t channel_id;
    uint16_t element_offset;
    uint16_t element_count;
    uint32_t source_step;
    uint16_t source_count;
    const float *values;
} esp32_full_chunk_builder_ctx_t;

static bool build_wave_chunk_payload(uint8_t *payload, uint16_t payload_len, void *ctx)
{
    const esp32_full_chunk_builder_ctx_t *chunk = (const esp32_full_chunk_builder_ctx_t *)ctx;
    esp32_report_chunk_prefix_t prefix;
    uint32_t expected_len;

    if (payload == NULL || chunk == NULL || chunk->values == NULL) {
        return false;
    }
    expected_len = (uint32_t)sizeof(prefix) + ((uint32_t)chunk->element_count * sizeof(int16_t));
    if (payload_len != (uint16_t)expected_len) {
        return false;
    }

    memset(&prefix, 0, sizeof(prefix));
    prefix.frame_id = chunk->frame_id;
    prefix.channel_id = chunk->channel_id;
    prefix.element_offset = chunk->element_offset;
    prefix.element_count = chunk->element_count;
    memcpy(payload, &prefix, sizeof(prefix));

    for (uint16_t i = 0U; i < chunk->element_count; i++) {
        uint32_t step = (chunk->source_step == 0U) ? 1U : chunk->source_step;
        uint32_t src_index = (((uint32_t)chunk->element_offset + (uint32_t)i) * step);
        if (chunk->source_count == 0U || src_index >= (uint32_t)chunk->source_count) {
            return false;
        }
        int16_t scaled = scale_float_to_i16(chunk->values[src_index], EW_UPLOAD_WAVEFORM_SCALE);
        memcpy(payload + sizeof(prefix) + ((uint32_t)i * sizeof(scaled)), &scaled, sizeof(scaled));
    }
    return true;
}

static bool build_fft_chunk_payload(uint8_t *payload, uint16_t payload_len, void *ctx)
{
    const esp32_full_chunk_builder_ctx_t *chunk = (const esp32_full_chunk_builder_ctx_t *)ctx;
    esp32_report_chunk_prefix_t prefix;
    uint32_t expected_len;

    if (payload == NULL || chunk == NULL || chunk->values == NULL) {
        return false;
    }
    expected_len = (uint32_t)sizeof(prefix) + ((uint32_t)chunk->element_count * sizeof(int16_t));
    if (payload_len != (uint16_t)expected_len) {
        return false;
    }

    memset(&prefix, 0, sizeof(prefix));
    prefix.frame_id = chunk->frame_id;
    prefix.channel_id = chunk->channel_id;
    prefix.element_offset = chunk->element_offset;
    prefix.element_count = chunk->element_count;
    memcpy(payload, &prefix, sizeof(prefix));

    for (uint16_t i = 0U; i < chunk->element_count; i++) {
        uint32_t src_index = (uint32_t)chunk->element_offset + (uint32_t)i;
        if (chunk->source_count == 0U || src_index >= (uint32_t)chunk->source_count) {
            return false;
        }
        int16_t scaled = scale_float_to_i16(chunk->values[src_index], EW_UPLOAD_FFT_SCALE);
        memcpy(payload + sizeof(prefix) + ((uint32_t)i * sizeof(scaled)), &scaled, sizeof(scaled));
    }
    return true;
}

static bool send_wave_chunks(uint32_t frame_id,
                             uint8_t channel_id,
                             const float *waveform,
                             uint16_t waveform_count,
                             uint32_t timeout_ms)
{
    const uint16_t max_elements =
        (uint16_t)((ESP32_SPI_MAX_PAYLOAD - sizeof(esp32_report_chunk_prefix_t)) / sizeof(int16_t));
    uint16_t offset = 0U;

    while (offset < waveform_count) {
        uint16_t count = (uint16_t)(waveform_count - offset);
        esp32_full_chunk_builder_ctx_t chunk;
        uint16_t payload_len;

        if (count > max_elements) {
            count = max_elements;
        }
        chunk.frame_id = frame_id;
        chunk.channel_id = channel_id;
        chunk.element_offset = offset;
        chunk.element_count = count;
        chunk.source_step = 1U;
        chunk.source_count = waveform_count;
        chunk.values = waveform;

        payload_len = (uint16_t)(sizeof(esp32_report_chunk_prefix_t) + ((uint32_t)count * sizeof(int16_t)));
        if (!send_report_built_packet_wait(ESP32_MSG_REPORT_FULL_WAVE_CHUNK,
                                           payload_len,
                                           build_wave_chunk_payload,
                                           &chunk,
                                           frame_id,
                                           timeout_ms,
                                           "wave")) {
            return false;
        }
        offset = (uint16_t)(offset + count);
    }
    return true;
}

static bool send_fft_chunks(uint32_t frame_id,
                            uint8_t channel_id,
                            const float *fft,
                            uint16_t fft_count,
                            uint32_t timeout_ms)
{
    const uint16_t max_elements =
        (uint16_t)((ESP32_SPI_MAX_PAYLOAD - sizeof(esp32_report_chunk_prefix_t)) / sizeof(int16_t));
    uint16_t offset = 0U;

    while (offset < fft_count) {
        uint16_t count = (uint16_t)(fft_count - offset);
        esp32_full_chunk_builder_ctx_t chunk;
        uint16_t payload_len;

        if (count > max_elements) {
            count = max_elements;
        }
        chunk.frame_id = frame_id;
        chunk.channel_id = channel_id;
        chunk.element_offset = offset;
        chunk.element_count = count;
        chunk.source_step = 1U;
        chunk.source_count = fft_count;
        chunk.values = fft;

        payload_len = (uint16_t)(sizeof(esp32_report_chunk_prefix_t) + ((uint32_t)count * sizeof(int16_t)));
        if (!send_report_built_packet_wait(ESP32_MSG_REPORT_FULL_FFT_CHUNK,
                                           payload_len,
                                           build_fft_chunk_payload,
                                           &chunk,
                                           frame_id,
                                           timeout_ms,
                                           "fft")) {
            return false;
        }
        offset = (uint16_t)(offset + count);
    }
    return true;
}

uint16_t ESP32_SPI_FullWaveChunkMaxElements(void)
{
    return (uint16_t)((ESP32_SPI_MAX_PAYLOAD - sizeof(esp32_report_chunk_prefix_t)) / sizeof(int16_t));
}

uint16_t ESP32_SPI_FullFftChunkMaxElements(void)
{
    return (uint16_t)((ESP32_SPI_MAX_PAYLOAD - sizeof(esp32_report_chunk_prefix_t)) / sizeof(int16_t));
}

bool ESP32_SPI_ReportFullBegin(uint32_t frame_id,
                               uint64_t timestamp_ms,
                               uint32_t downsample_step,
                               uint32_t upload_points,
                               const char *fault_code,
                               uint8_t status_code,
                               const esp32_spi_report_channel_t *channels,
                               uint8_t channel_count,
                               uint32_t timeout_ms)
{
    esp32_report_full_begin_payload_t begin;
    uint32_t per_packet_timeout = (timeout_ms == 0U) ? 1500U : timeout_ms;

    if (channels == NULL || channel_count == 0U || channel_count > 4U) {
        return false;
    }

    s_last_report_full_end_ref_seq = 0U;
    if (!ESP32_SPI_EnsureReady(ESP32_SPI_DEFAULT_TIMEOUT_MS)) {
        return false;
    }

    memset(&begin, 0, sizeof(begin));
    begin.frame_id = frame_id;
    begin.timestamp_ms = timestamp_ms;
    begin.downsample_step = downsample_step;
    begin.upload_points = upload_points;
    copy_text(begin.fault_code, sizeof(begin.fault_code), fault_code ? fault_code : "E00");
    begin.report_mode = 1U;
    begin.status_code = status_code;
    begin.channel_count = channel_count;

    for (uint8_t i = 0U; i < channel_count; i++) {
        begin.channels[i].channel_id = channels[i].channel_id;
        begin.channels[i].waveform_count = channels[i].waveform_count;
        begin.channels[i].fft_count = channels[i].fft_count;
        begin.channels[i].value_scaled = channels[i].value_scaled;
        begin.channels[i].current_value_scaled = channels[i].current_value_scaled;
    }

    return send_report_packet_wait(ESP32_MSG_REPORT_FULL_BEGIN,
                                   &begin,
                                   (uint16_t)sizeof(begin),
                                   frame_id,
                                   per_packet_timeout,
                                   "begin");
}

bool ESP32_SPI_ReportFullWaveChunk(uint32_t frame_id,
                                   uint8_t channel_id,
                                   const float *waveform,
                                   uint16_t element_offset,
                                   uint16_t element_count,
                                   uint32_t source_step,
                                   uint16_t source_count,
                                   uint32_t timeout_ms)
{
    esp32_full_chunk_builder_ctx_t chunk;
    uint16_t payload_len;
    uint32_t per_packet_timeout = (timeout_ms == 0U) ? 1500U : timeout_ms;
    uint32_t step = (source_step == 0U) ? 1U : source_step;
    uint32_t last_source_index;

    if (waveform == NULL || element_count == 0U ||
        element_count > ESP32_SPI_FullWaveChunkMaxElements()) {
        return false;
    }
    if (source_count == 0U) {
        return false;
    }
    last_source_index = (((uint32_t)element_offset + (uint32_t)element_count - 1U) * step);
    if (last_source_index >= (uint32_t)source_count) {
        return false;
    }

    chunk.frame_id = frame_id;
    chunk.channel_id = channel_id;
    chunk.element_offset = element_offset;
    chunk.element_count = element_count;
    chunk.source_step = step;
    chunk.source_count = source_count;
    chunk.values = waveform;
    payload_len = (uint16_t)(sizeof(esp32_report_chunk_prefix_t) +
                             ((uint32_t)element_count * sizeof(int16_t)));

    return send_report_built_packet_wait(ESP32_MSG_REPORT_FULL_WAVE_CHUNK,
                                         payload_len,
                                         build_wave_chunk_payload,
                                         &chunk,
                                         frame_id,
                                         per_packet_timeout,
                                         "wave");
}

bool ESP32_SPI_ReportFullFftChunk(uint32_t frame_id,
                                  uint8_t channel_id,
                                  const float *fft,
                                  uint16_t element_offset,
                                  uint16_t element_count,
                                  uint32_t timeout_ms)
{
    esp32_full_chunk_builder_ctx_t chunk;
    uint16_t payload_len;
    uint32_t per_packet_timeout = (timeout_ms == 0U) ? 1500U : timeout_ms;

    if (fft == NULL || element_count == 0U ||
        element_count > ESP32_SPI_FullFftChunkMaxElements()) {
        return false;
    }

    chunk.frame_id = frame_id;
    chunk.channel_id = channel_id;
    chunk.element_offset = element_offset;
    chunk.element_count = element_count;
    chunk.source_step = 1U;
    chunk.source_count = element_offset + element_count;
    chunk.values = fft;
    payload_len = (uint16_t)(sizeof(esp32_report_chunk_prefix_t) +
                             ((uint32_t)element_count * sizeof(int16_t)));

    return send_report_built_packet_wait(ESP32_MSG_REPORT_FULL_FFT_CHUNK,
                                         payload_len,
                                         build_fft_chunk_payload,
                                         &chunk,
                                         frame_id,
                                         per_packet_timeout,
                                         "fft");
}

bool ESP32_SPI_ReportFullEnd(uint32_t frame_id,
                             uint32_t timeout_ms)
{
    esp32_report_end_payload_t end_payload;
    uint32_t per_packet_timeout = (timeout_ms == 0U) ? 1500U : timeout_ms;

    memset(&end_payload, 0, sizeof(end_payload));
    end_payload.frame_id = frame_id;
    return send_report_packet_wait(ESP32_MSG_REPORT_FULL_END,
                                   &end_payload,
                                   (uint16_t)sizeof(end_payload),
                                   frame_id,
                                   per_packet_timeout,
                                   "end");
}

bool ESP32_SPI_ReportFull(uint32_t frame_id,
                          uint64_t timestamp_ms,
                          uint32_t downsample_step,
                          uint32_t upload_points,
                          const char *fault_code,
                          uint8_t status_code,
                          const esp32_spi_report_channel_t *channels,
                          uint8_t channel_count,
                          const float * const waveforms[],
                          const float * const ffts[],
                          uint16_t waveform_count,
                          uint16_t fft_count,
                          uint32_t timeout_ms)
{
    esp32_report_full_begin_payload_t begin;
    esp32_report_end_payload_t end_payload;
    uint32_t per_packet_timeout = (timeout_ms == 0U) ? 1500U : timeout_ms;

    if (channels == NULL || waveforms == NULL || ffts == NULL ||
        channel_count == 0U || channel_count > 4U) {
        return false;
    }
    s_last_report_full_end_ref_seq = 0U;
    if (!ESP32_SPI_EnsureReady(ESP32_SPI_DEFAULT_TIMEOUT_MS)) {
        return false;
    }

    memset(&begin, 0, sizeof(begin));
    begin.frame_id = frame_id;
    begin.timestamp_ms = timestamp_ms;
    begin.downsample_step = downsample_step;
    begin.upload_points = upload_points;
    copy_text(begin.fault_code, sizeof(begin.fault_code), fault_code ? fault_code : "E00");
    begin.report_mode = 1U;
    begin.status_code = status_code;
    begin.channel_count = channel_count;

    for (uint8_t i = 0U; i < channel_count; i++) {
        if (waveforms[i] == NULL || ffts[i] == NULL ||
            channels[i].waveform_count > waveform_count ||
            channels[i].fft_count > fft_count) {
            return false;
        }
        begin.channels[i].channel_id = channels[i].channel_id;
        begin.channels[i].waveform_count = channels[i].waveform_count;
        begin.channels[i].fft_count = channels[i].fft_count;
        begin.channels[i].value_scaled = channels[i].value_scaled;
        begin.channels[i].current_value_scaled = channels[i].current_value_scaled;
    }

    if (!send_report_packet_wait(ESP32_MSG_REPORT_FULL_BEGIN,
                                 &begin,
                                 (uint16_t)sizeof(begin),
                                 frame_id,
                                 per_packet_timeout,
                                 "begin")) {
        return false;
    }

    for (uint8_t i = 0U; i < channel_count; i++) {
        if (!send_wave_chunks(frame_id,
                              channels[i].channel_id,
                              waveforms[i],
                              channels[i].waveform_count,
                              per_packet_timeout)) {
            return false;
        }
        if (!send_fft_chunks(frame_id,
                             channels[i].channel_id,
                             ffts[i],
                             channels[i].fft_count,
                             per_packet_timeout)) {
            return false;
        }
    }

    memset(&end_payload, 0, sizeof(end_payload));
    end_payload.frame_id = frame_id;
    return send_report_packet_wait(ESP32_MSG_REPORT_FULL_END,
                                   &end_payload,
                                   (uint16_t)sizeof(end_payload),
                                   frame_id,
                                   per_packet_timeout,
                                   "end");
}
#endif

void ESP32_SPI_DebugRunPing(void)
{
    bool got_ping = false;
    uint8_t rx_type = ESP32_MSG_NOOP;

    if (!ESP32_SPI_EnsureReady(3000U)) {
        printf("[ESP32SPI] debug ping FAIL: handshake\r\n");
        return;
    }

    if (!transact_and_handle_payload(ESP32_MSG_PING_REQ, NULL, 0U, &rx_type)) {
        printf("[ESP32SPI] PING transaction failed\r\n");
        return;
    }
    got_ping = (rx_type == ESP32_MSG_PING_RESP) ||
               (s_last_response_type == ESP32_MSG_PING_RESP && s_last_result_code == ESP32_RESULT_OK);

    for (uint32_t i = 0; i < 80U && !got_ping; i++) {
        HAL_Delay(10);
        if (!transact_and_handle_payload(ESP32_MSG_NOOP, NULL, 0U, &rx_type)) {
            printf("[ESP32SPI] PING poll failed\r\n");
            return;
        }
        got_ping = (rx_type == ESP32_MSG_PING_RESP) ||
                   (s_last_response_type == ESP32_MSG_PING_RESP && s_last_result_code == ESP32_RESULT_OK);
    }

    printf("[ESP32SPI] debug ping %s, session=%lu last_rx_seq=%lu\r\n",
           got_ping ? "PASS" : "FAIL",
           (unsigned long)s_session_epoch,
           (unsigned long)s_last_rx_seq);
}
