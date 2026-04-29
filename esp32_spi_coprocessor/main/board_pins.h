#pragma once

#include "driver/gpio.h"
#include "driver/spi_common.h"

#define BOARD_LED_GPIO GPIO_NUM_2

#define SPI_LINK_HOST SPI3_HOST
#define SPI_LINK_SCLK_GPIO GPIO_NUM_18
#define SPI_LINK_MOSI_GPIO GPIO_NUM_23
#define SPI_LINK_MISO_GPIO GPIO_NUM_19
#define SPI_LINK_CS_GPIO GPIO_NUM_5
#define SPI_LINK_READY_GPIO GPIO_NUM_27
