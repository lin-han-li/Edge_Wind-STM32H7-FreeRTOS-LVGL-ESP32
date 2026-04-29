#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_client.h"

#include "app_types.h"

esp_err_t report_codec_build_register_json(const app_config_snapshot_t *config, char **out_json, size_t *out_len);
esp_err_t report_codec_build_empty_heartbeat_json(const app_config_snapshot_t *config,
                                                  device_status_t status,
                                                  const char *fault_code,
                                                  char **out_json,
                                                  size_t *out_len);
esp_err_t report_codec_measure_heartbeat_json(const app_config_snapshot_t *config,
                                              const report_frame_t *frame,
                                              size_t *out_len);
esp_err_t report_codec_stream_heartbeat_json(const app_config_snapshot_t *config,
                                             const report_frame_t *frame,
                                             char *scratch,
                                             size_t scratch_len,
                                             esp_http_client_handle_t client);
bool report_codec_parse_server_command(const char *body, server_command_event_t *out_event);
