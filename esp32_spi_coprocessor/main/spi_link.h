#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "comm_protocol.h"

typedef void (*spi_link_rx_callback_t)(const protocol_packet_t *packet, size_t rx_bytes, void *ctx);

esp_err_t spi_link_init(spi_link_rx_callback_t callback, void *ctx);
esp_err_t spi_link_enqueue_tx(const protocol_packet_t *packet, TickType_t timeout);
void spi_link_flush_tx_queue(void);
