#ifndef __ESP32_SPI_DEBUG_H
#define __ESP32_SPI_DEBUG_H

#include <stdbool.h>
#include <stdint.h>

#ifndef ESP32_SPI_ENABLE_FULL_UPLOAD
#define ESP32_SPI_ENABLE_FULL_UPLOAD 1
#endif
#ifndef ESP32_SPI_RESULT_PENDING
#define ESP32_SPI_RESULT_PENDING 0xFFFFU
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t ready;
    uint8_t wifi_connected;
    uint8_t cloud_connected;
    uint8_t registered_with_cloud;
    uint8_t reporting_enabled;
    uint8_t report_mode;
    int8_t rssi_dbm;
    uint32_t session_epoch;
    uint32_t last_frame_id;
    uint32_t downsample_step;
    uint32_t upload_points;
    int32_t last_http_status;
    char ip_address[16];
    char node_id[64];
    char last_error[64];
} esp32_spi_status_t;

typedef struct {
    uint8_t channel_id;
    uint16_t waveform_count;
    uint16_t fft_count;
    int32_t value_scaled;
    int32_t current_value_scaled;
} esp32_spi_report_channel_t;

void ESP32_SPI_DebugRunPing(void);

bool ESP32_SPI_EnsureReady(uint32_t timeout_ms);
bool ESP32_SPI_ApplyDeviceConfig(const char *ssid,
                                 const char *password,
                                 const char *server_host,
                                 uint16_t server_port,
                                 const char *node_id,
                                 const char *node_location,
                                 const char *hw_version);
bool ESP32_SPI_ApplyCommParams(uint32_t heartbeat_ms,
                               uint32_t min_interval_ms,
                               uint32_t http_timeout_ms,
                               uint32_t reconnect_backoff_ms,
                               uint32_t downsample_step,
                               uint32_t upload_points,
                               uint32_t hardreset_sec,
                               uint32_t chunk_kb,
                               uint32_t chunk_delay_ms);
bool ESP32_SPI_ConnectWifi(uint32_t timeout_ms);
bool ESP32_SPI_CloudConnect(uint32_t timeout_ms);
bool ESP32_SPI_RegisterNode(uint32_t timeout_ms);
bool ESP32_SPI_StartReport(uint8_t report_mode, uint32_t timeout_ms);
bool ESP32_SPI_StopReport(uint32_t timeout_ms);
bool ESP32_SPI_QueryStatus(esp32_spi_status_t *out_status, uint32_t timeout_ms);
const esp32_spi_status_t *ESP32_SPI_GetStatus(void);
bool ESP32_SPI_PollEvents(uint32_t timeout_ms);
uint32_t ESP32_SPI_GetLastTxSeq(void);
uint32_t ESP32_SPI_GetLastFullEndRefSeq(void);
uint32_t ESP32_SPI_GetLastNackRefSeq(void);
uint16_t ESP32_SPI_GetLastNackReason(void);
bool ESP32_SPI_GetTxResult(uint32_t ref_seq,
                           int32_t *out_http_status,
                           int32_t *out_result_code,
                           uint32_t *out_frame_id);
bool ESP32_SPI_ReportSummary(uint32_t frame_id,
                             uint64_t timestamp_ms,
                             uint32_t downsample_step,
                             uint32_t upload_points,
                             const char *fault_code,
                             uint8_t report_mode,
                             uint8_t status_code,
                             const esp32_spi_report_channel_t *channels,
                             uint8_t channel_count,
                             uint32_t timeout_ms);
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
uint16_t ESP32_SPI_FullWaveChunkMaxElements(void);
uint16_t ESP32_SPI_FullFftChunkMaxElements(void);
bool ESP32_SPI_ReportFullBegin(uint32_t frame_id,
                               uint64_t timestamp_ms,
                               uint32_t downsample_step,
                               uint32_t upload_points,
                               const char *fault_code,
                               uint8_t status_code,
                               const esp32_spi_report_channel_t *channels,
                               uint8_t channel_count,
                               uint32_t timeout_ms);
bool ESP32_SPI_ReportFullWaveChunk(uint32_t frame_id,
                                   uint8_t channel_id,
                                   const float *waveform,
                                   uint16_t element_offset,
                                   uint16_t element_count,
                                   uint32_t timeout_ms);
bool ESP32_SPI_ReportFullFftChunk(uint32_t frame_id,
                                  uint8_t channel_id,
                                  const float *fft,
                                  uint16_t element_offset,
                                  uint16_t element_count,
                                  uint32_t timeout_ms);
bool ESP32_SPI_ReportFullEnd(uint32_t frame_id,
                             uint32_t timeout_ms);
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
                          uint32_t timeout_ms);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ESP32_SPI_DEBUG_H */
