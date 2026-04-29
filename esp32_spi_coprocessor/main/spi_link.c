#include "spi_link.h"

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "board_pins.h"

static const char *TAG = "spi_link";

#define SPI_LINK_QUEUE_LENGTH 32
#define SPI_LINK_TASK_STACK 8192
#define SPI_LINK_TASK_PRIORITY 9

static QueueHandle_t s_tx_queue;
static spi_link_rx_callback_t s_rx_callback;
static void *s_rx_ctx;
static uint8_t *s_tx_dma;
static uint8_t *s_rx_dma;
static bool s_has_pending;
static protocol_packet_t s_pending_tx;
static uint32_t s_next_tx_seq = 1;
static uint32_t s_last_rx_seq = 0;

static void spi_link_cleanup_partial_init(void)
{
    if (s_tx_queue != NULL) {
        vQueueDelete(s_tx_queue);
        s_tx_queue = NULL;
    }
    heap_caps_free(s_tx_dma);
    heap_caps_free(s_rx_dma);
    s_tx_dma = NULL;
    s_rx_dma = NULL;
}

static void IRAM_ATTR post_setup_cb(spi_slave_transaction_t *trans)
{
    gpio_set_level(SPI_LINK_READY_GPIO, 1);
}

static void IRAM_ATTR post_trans_cb(spi_slave_transaction_t *trans)
{
    gpio_set_level(SPI_LINK_READY_GPIO, 0);
}

static void build_idle_packet(protocol_packet_t *packet)
{
    protocol_packet_prepare(packet, PROTOCOL_MSG_NOOP, 0, 0, s_last_rx_seq, 0);
    protocol_packet_finalize(packet);
}

static void queue_link_nack(protocol_nack_reason_t reason, uint32_t ref_seq)
{
    protocol_packet_t packet;
    protocol_nack_payload_t payload = {
        .ref_seq = ref_seq,
        .reason = (uint16_t) reason,
    };

    protocol_packet_prepare(&packet, PROTOCOL_MSG_NACK, 0, 0, s_last_rx_seq, 0);
    if (protocol_packet_set_payload(&packet, &payload, sizeof(payload)) != ESP_OK) {
        return;
    }
    protocol_packet_finalize(&packet);
    if (!s_has_pending) {
        s_pending_tx = packet;
        s_has_pending = true;
        return;
    }
    (void) xQueueSendToFront(s_tx_queue, &packet, 0);
}

static void assign_tx_sequence(protocol_packet_t *packet)
{
    if (packet->header.seq == 0U && packet->header.msg_type != PROTOCOL_MSG_NOOP) {
        packet->header.seq = s_next_tx_seq++;
    }
    packet->header.ack_seq = s_last_rx_seq;
    protocol_packet_finalize(packet);
}

static void process_ack_from_master(const protocol_packet_t *rx_packet)
{
    if (s_has_pending && rx_packet->header.ack_seq == s_pending_tx.header.seq) {
        s_has_pending = false;
        memset(&s_pending_tx, 0, sizeof(s_pending_tx));
    }
}

static void load_next_packet(protocol_packet_t *out_packet)
{
    if (!s_has_pending) {
        if (xQueueReceive(s_tx_queue, &s_pending_tx, 0) == pdTRUE) {
            assign_tx_sequence(&s_pending_tx);
            s_has_pending = true;
        }
    }

    if (s_has_pending) {
        *out_packet = s_pending_tx;
    } else {
        build_idle_packet(out_packet);
    }
}

static void spi_link_task(void *arg)
{
    spi_slave_transaction_t trans = { 0 };
    protocol_packet_t tx_packet;
    protocol_packet_t *rx_packet = (protocol_packet_t *) s_rx_dma;
    protocol_nack_reason_t nack_reason = PROTOCOL_NACK_NONE;
    size_t rx_bytes;

    trans.length = (uint32_t) protocol_wire_size() * 8U;

    for (;;) {
        memset(s_rx_dma, 0, protocol_wire_size());
        memset(s_tx_dma, 0, protocol_wire_size());
        load_next_packet(&tx_packet);
        memcpy(s_tx_dma, &tx_packet, sizeof(tx_packet));

        trans.tx_buffer = s_tx_dma;
        trans.rx_buffer = s_rx_dma;
        trans.trans_len = 0;
        ESP_ERROR_CHECK(spi_slave_transmit(SPI_LINK_HOST, &trans, portMAX_DELAY));

        rx_bytes = trans.trans_len / 8U;
        if (rx_bytes == 0U) {
            continue;
        }
        if (protocol_packet_validate(rx_packet, rx_bytes, &nack_reason) == ESP_OK) {
            s_last_rx_seq = rx_packet->header.seq;
            process_ack_from_master(rx_packet);
            if (s_rx_callback != NULL && rx_packet->header.msg_type != PROTOCOL_MSG_NOOP) {
                s_rx_callback(rx_packet, rx_bytes, s_rx_ctx);
            }
        } else {
            bool malformed = (rx_bytes < sizeof(protocol_header_t)) ||
                             (rx_packet->header.magic != PROTOCOL_MAGIC);
            ESP_LOGW(TAG,
                     "Dropped invalid RX packet, reason=%u, rx_bytes=%u, magic=0x%08" PRIx32 ", type=0x%02x, len=%u",
                     (unsigned) nack_reason,
                     (unsigned) rx_bytes,
                     (rx_bytes >= sizeof(uint32_t)) ? rx_packet->header.magic : 0U,
                     (rx_bytes >= offsetof(protocol_header_t, msg_type) + sizeof(rx_packet->header.msg_type)) ?
                         (unsigned) rx_packet->header.msg_type : 0U,
                     (rx_bytes >= offsetof(protocol_header_t, payload_len) + sizeof(rx_packet->header.payload_len)) ?
                         (unsigned) rx_packet->header.payload_len : 0U);
            if (!malformed) {
                queue_link_nack(nack_reason, rx_packet->header.seq);
            }
        }
    }
}

esp_err_t spi_link_init(spi_link_rx_callback_t callback, void *ctx)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SPI_LINK_MOSI_GPIO,
        .miso_io_num = SPI_LINK_MISO_GPIO,
        .sclk_io_num = SPI_LINK_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int) protocol_wire_size(),
    };
    spi_slave_interface_config_t slave_cfg = {
        .mode = 0,
        .spics_io_num = SPI_LINK_CS_GPIO,
        .queue_size = 1,
        .flags = 0,
        .post_setup_cb = post_setup_cb,
        .post_trans_cb = post_trans_cb,
    };
    gpio_config_t ready_gpio_cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << SPI_LINK_READY_GPIO,
        .pull_down_en = 0,
        .pull_up_en = 0,
    };

    s_rx_callback = callback;
    s_rx_ctx = ctx;
    s_tx_queue = xQueueCreate(SPI_LINK_QUEUE_LENGTH, sizeof(protocol_packet_t));
    if (s_tx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_tx_dma = heap_caps_calloc(1, protocol_wire_size(), MALLOC_CAP_DMA);
    s_rx_dma = heap_caps_calloc(1, protocol_wire_size(), MALLOC_CAP_DMA);
    if (s_tx_dma == NULL || s_rx_dma == NULL) {
        spi_link_cleanup_partial_init();
        return ESP_ERR_NO_MEM;
    }

    if (gpio_config(&ready_gpio_cfg) != ESP_OK) {
        spi_link_cleanup_partial_init();
        return ESP_FAIL;
    }
    gpio_set_level(SPI_LINK_READY_GPIO, 0);
    gpio_set_pull_mode(SPI_LINK_MOSI_GPIO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SPI_LINK_SCLK_GPIO, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(SPI_LINK_CS_GPIO, GPIO_PULLUP_ONLY);

    if (spi_slave_initialize(SPI_LINK_HOST, &bus_cfg, &slave_cfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        spi_link_cleanup_partial_init();
        return ESP_FAIL;
    }
    if (xTaskCreate(spi_link_task, "spi_link_task", SPI_LINK_TASK_STACK, NULL, SPI_LINK_TASK_PRIORITY, NULL) != pdPASS) {
        spi_slave_free(SPI_LINK_HOST);
        spi_link_cleanup_partial_init();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t spi_link_enqueue_tx(const protocol_packet_t *packet, TickType_t timeout)
{
    if (packet == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return (xQueueSend(s_tx_queue, packet, timeout) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

void spi_link_flush_tx_queue(void)
{
    if (s_tx_queue == NULL) {
        return;
    }
    (void) xQueueReset(s_tx_queue);
    s_has_pending = false;
    memset(&s_pending_tx, 0, sizeof(s_pending_tx));
}
