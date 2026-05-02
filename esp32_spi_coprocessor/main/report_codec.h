#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_client.h"

#include "app_types.h"

typedef struct {
    size_t body_len;
    uint32_t data_crc32;
} report_full_binary_info_t;

esp_err_t report_codec_build_register_json(const app_config_snapshot_t *config, char **out_json, size_t *out_len);
esp_err_t report_codec_build_empty_heartbeat_json(const app_config_snapshot_t *config,
                                                  device_status_t status,
                                                  const char *fault_code,
                                                  report_mode_t report_mode,
                                                  char **out_json,
                                                  size_t *out_len);
esp_err_t report_codec_measure_heartbeat_json(const app_config_snapshot_t *config,
                                              const report_frame_t *frame,
                                              size_t *out_len);
esp_err_t report_codec_stream_heartbeat_json(const app_config_snapshot_t *config,
                                             const report_frame_t *frame,
                                             char *scratch,
                                             size_t scratch_len,
                                             esp_http_client_handle_t client,
                                             uint32_t total_budget_ms);
esp_err_t report_codec_measure_full_binary(const app_config_snapshot_t *config,
                                           const report_frame_t *frame,
                                           report_full_binary_info_t *out_info);
esp_err_t report_codec_stream_full_binary(const app_config_snapshot_t *config,
                                          const report_frame_t *frame,
                                          uint32_t data_crc32,
                                          esp_http_client_handle_t client,
                                          uint32_t total_budget_ms);
bool report_codec_parse_server_command(const char *body, server_command_event_t *out_event);
