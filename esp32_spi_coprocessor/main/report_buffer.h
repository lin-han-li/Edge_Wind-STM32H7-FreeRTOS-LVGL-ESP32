#pragma once

#include "esp_err.h"

#include "app_types.h"
#include "comm_protocol.h"

void report_buffer_init(void);
void report_buffer_reset(void);
esp_err_t report_buffer_ingest_summary(const protocol_report_summary_payload_t *payload,
                                       size_t payload_len,
                                       report_frame_t **out_frame);
esp_err_t report_buffer_begin_full(const protocol_report_full_begin_payload_t *payload, size_t payload_len);
esp_err_t report_buffer_ingest_chunk(protocol_msg_type_t msg_type,
                                     const protocol_report_chunk_prefix_t *prefix,
                                     size_t prefix_len,
                                     const uint8_t *data,
                                     size_t data_len);
esp_err_t report_buffer_finalize_full(const protocol_report_end_payload_t *payload,
                                      size_t payload_len,
                                      report_frame_t **out_frame);
void report_frame_free(report_frame_t *frame);

