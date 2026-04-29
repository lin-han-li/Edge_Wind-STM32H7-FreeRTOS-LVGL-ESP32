#include "board_support.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"

void board_support_init(void)
{
    gpio_reset_pin(BOARD_LED_GPIO);
    gpio_set_direction(BOARD_LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BOARD_LED_GPIO, 0);
}

void board_support_set_status_led(bool on)
{
    gpio_set_level(BOARD_LED_GPIO, on ? 1 : 0);
}

void board_support_pulse_status_led(int count, int delay_ms)
{
    for (int i = 0; i < count; ++i) {
        board_support_set_status_led(false);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        board_support_set_status_led(true);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

