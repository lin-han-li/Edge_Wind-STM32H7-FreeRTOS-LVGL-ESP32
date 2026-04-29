#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "app_types.h"

void app_config_init_defaults(void);
esp_err_t app_config_load_from_nvs(void);
void app_config_get_snapshot(app_config_snapshot_t *out_snapshot);
void app_config_get_runtime_state(app_runtime_state_t *out_state);
esp_err_t app_config_update_device(const app_device_config_t *device, bool persist);
esp_err_t app_config_update_comm(const app_comm_params_t *comm, bool persist);
esp_err_t app_config_update_runtime_state(const app_runtime_state_t *state, bool persist);
void app_config_sanitize_comm(app_comm_params_t *comm);
