#pragma once

#include <stdbool.h>

void board_support_init(void);
void board_support_set_status_led(bool on);
void board_support_pulse_status_led(int count, int delay_ms);

