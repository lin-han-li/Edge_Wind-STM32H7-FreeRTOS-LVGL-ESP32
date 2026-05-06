/**
 ******************************************************************************
 * @file    edge_comm.c
 * @author  STM32H7 Optimization Expert
 * @brief   EdgeComm WiFi 模组驱动程序 (STM32H7 专用优化版)
 * @note    核心特性：
 * 1. AXI SRAM + D-Cache 一致性维护 (DMA 必备)
 * 2. 2Mbps 高波特率下的软件容错机制 (抗 ORE/FE 错误)
 * 3. 智能流式接收解析 (滑动窗口)
 * 4. 自动故障恢复与重连逻辑
 ******************************************************************************
 */

#include "edge_comm.h"
#include "SPI_AD7606.h"
#include "ad_acq_buffers.h"
#include "ESP32SPI/esp32_spi_debug.h"
#include "usart.h"
#include "arm_math.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "fatfs.h"
#include "diskio.h"
#include "bsp_driver_sd.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#ifndef EW_USE_ESP32_SPI_UI
#define EW_USE_ESP32_SPI_UI 1
#endif

#ifndef ESP32_SPI_STRESS_TEST
#define ESP32_SPI_STRESS_TEST 0
#endif

#ifndef ESP32_SPI_STRESS_FULL_UPLOAD
#define ESP32_SPI_STRESS_FULL_UPLOAD 0
#endif

#ifndef ESP32_SPI_STRESS_SENDLIMIT_MS
#define ESP32_SPI_STRESS_SENDLIMIT_MS 500U
#endif
#ifndef ESP32_SPI_FULL_IDLE_HEARTBEAT_MS
#define ESP32_SPI_FULL_IDLE_HEARTBEAT_MS 5000U
#endif

#ifndef ESP32_SPI_STRESS_DATA_UPDATE_MS
#define ESP32_SPI_STRESS_DATA_UPDATE_MS 1000U
#endif

#ifndef ESP32_SPI_STRESS_SPI_LABEL
#define ESP32_SPI_STRESS_SPI_LABEL "5MHz"
#endif

#ifndef ESP32_SPI_RESULT_OK
#define ESP32_SPI_RESULT_OK 0
#endif
#ifndef ESP32_SPI_NACK_BUSY
#define ESP32_SPI_NACK_BUSY 5U
#endif
#ifndef ESP32_SPI_NACK_UNSUPPORTED_VERSION
#define ESP32_SPI_NACK_UNSUPPORTED_VERSION 3U
#endif
#ifndef ESP32_SPI_NACK_INVALID_STATE
#define ESP32_SPI_NACK_INVALID_STATE 7U
#endif
#ifndef ESP32_SPI_NACK_INVALID_PAYLOAD
#define ESP32_SPI_NACK_INVALID_PAYLOAD 8U
#endif

#ifndef ESP32_SPI_INVALID_STATE_AUTOSTOP_COUNT
#define ESP32_SPI_INVALID_STATE_AUTOSTOP_COUNT 3U
#endif

#ifndef ESP32_SPI_FULL_CONTINUOUS_DEFAULT
#define ESP32_SPI_FULL_CONTINUOUS_DEFAULT 0
#endif

#ifndef ESP32_SPI_FULL_NOT_ARMED_LOG_MS
#define ESP32_SPI_FULL_NOT_ARMED_LOG_MS 5000U
#endif
#ifndef ESP32_SPI_FULL_RESULT_TIMEOUT_MS
#define ESP32_SPI_FULL_RESULT_TIMEOUT_MS 8000U
#endif
#ifndef ESP32_SPI_FULL_WAIT_LOG_MS
#define ESP32_SPI_FULL_WAIT_LOG_MS 2000U
#endif
#ifndef ESP32_SPI_FULL_RESULT_POLL_MS
#define ESP32_SPI_FULL_RESULT_POLL_MS 20U
#endif
#ifndef ESP32_SPI_FULL_TIMEOUT_HOLDOFF_MS
#define ESP32_SPI_FULL_TIMEOUT_HOLDOFF_MS 1000U
#endif
#ifndef ESP32_SPI_FULL_BUSY_HOLDOFF_MS
#define ESP32_SPI_FULL_BUSY_HOLDOFF_MS 3000U
#endif
#ifndef ESP32_SPI_FULL_MAX_HOLDOFF_MS
#define ESP32_SPI_FULL_MAX_HOLDOFF_MS 5000U
#endif
#ifndef ESP_ALGO_MIN_INTERVAL_MS
#define ESP_ALGO_MIN_INTERVAL_MS 0U
#endif
#ifndef ESP32_SPI_FULL_PACKETS_PER_POLL
#define ESP32_SPI_FULL_PACKETS_PER_POLL 8U
#endif
#ifndef ESP32_SPI_FULL_YIELD_EVERY
#define ESP32_SPI_FULL_YIELD_EVERY 2U
#endif
#ifndef ESP32_SPI_FULL_PACKET_ACCEPT_TIMEOUT_MS
#define ESP32_SPI_FULL_PACKET_ACCEPT_TIMEOUT_MS 500U
#endif
#ifndef ESP_SERVER_CMD_DEDUPE_WINDOW_MS
#define ESP_SERVER_CMD_DEDUPE_WINDOW_MS 120000U
#endif
#ifndef ESP32_SPI_AUTO_RECOVER_POLL_MS
#define ESP32_SPI_AUTO_RECOVER_POLL_MS 1500U
#endif
#ifndef ESP32_SPI_AUTO_RECOVER_RETRY_MS
#define ESP32_SPI_AUTO_RECOVER_RETRY_MS 3000U
#endif
#ifndef ESP32_SPI_AUTO_RECOVER_FAIL_BACKOFF_MS
#define ESP32_SPI_AUTO_RECOVER_FAIL_BACKOFF_MS 5000U
#endif
#ifndef ESP_UPLOAD_SNAPSHOT_SLOT_COUNT
#define ESP_UPLOAD_SNAPSHOT_SLOT_COUNT 2U
#endif

/* 引用 usart.c 中定义的句柄 */
#define ESP_PRINT_WAVEFORM_POINTS 0
#define ADS_BADFRAME_MONITOR 1
#ifndef ADS_BADFRAME_MONITOR
#define ADS_BADFRAME_MONITOR 0
#endif
/* =================================================================================
 * 浮点数输出安全：
 * - C 库 printf/sprintf 对 NaN/Inf 往往会输出 "nan"/"inf"（小写），这会导致 Python json.loads 直接解析失败，
 *   进而出现“服务器端全 0 / 节点超时注销 / 过几秒又恢复”等现象。
 * - 所以：所有写入 JSON 的 float 必须先做有限值钳位。
 * ================================================================================= */
static inline float ESP_SafeFloat(float v)
{
    /* NaN: v!=v；Inf: 与阈值比较会成立 */
    if (!(v == v))
        return 0.0f;
    if (v > 1.0e20f || v < -1.0e20f)
        return 0.0f;
    return v;
}

/* ========= 上报数值格式（无浮点） =========
 * 用户要求：采集数据 *200 后按整数上传，JSON 里不再出现浮点数。
 * 说明：内部仍用 float 参与 FFT/计算，仅在“序列化到 JSON”阶段做定点缩放。 */
#ifndef ESP_UPLOAD_SCALE
#define ESP_UPLOAD_SCALE 200
#endif

/* 串口打印开关（默认关闭，避免影响 UI/上报性能）
 * 需求：打印“瞬时值(每点)”：printf("%f,%f,%f,%f", ch1,ch2,ch3,ch4)
 * 注意：若 STEP=1 且采样点很多，会显著占用 CPU/串口带宽，调试用即可。 */
#ifndef ESP_PRINT_WAVEFORM_POINTS
#define ESP_PRINT_WAVEFORM_POINTS 0
#endif

/* 打印步进：1=每点都打印；2=每 2 点打印一次... */
#ifndef ESP_PRINT_POINT_STEP
#define ESP_PRINT_POINT_STEP 1
#endif

static inline int32_t ESP_FloatToI32Scaled(float v)
{
    float x = ESP_SafeFloat(v) * (float)ESP_UPLOAD_SCALE;
    /* 四舍五入到整数 */
    return (int32_t)((x >= 0.0f) ? (x + 0.5f) : (x - 0.5f));
}

/* 安全追加格式化字符串到缓冲区（防止 http_packet_buf 越界导致“偶发坏帧/服务器解析失败/节点重连”） */
static inline int ESP_Appendf(char **pp, const char *end, const char *fmt, ...)
{
    if (!pp || !*pp || !end || *pp >= end)
        return 0;
    va_list ap;
    va_start(ap, fmt);
    int rem = (int)(end - *pp);
    int n = vsnprintf(*pp, (size_t)rem, fmt, ap);
    va_end(ap);
    if (n < 0)
        return 0;
    if (n >= rem)
    {
        /* 截断：把指针推进到末尾，返回失败 */
        *pp = (char *)end;
        return 0;
    }
    *pp += n;
    return 1;
}

/* Debug console uses USART1 only; ESP32 transport is SPI. */
static void ESP_Log(const char *format, ...);
static UART_HandleTypeDef *ESP_GetLogUart(void);
static void ESP_SetFaultCode(const char *code);
static void ESP_Console_HandleLine(char *line);
static void StrTrimInPlace(char *s);
void ESP_UI_Internal_OnLog(const char *line);
static void ESP_SetServerReportMode(uint8_t full);
static void ESP_SPI_ResetLocalReportState(const char *reason);
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
static void ESP_SPI_FullClearWaitState(void);
static void ESP_SPI_FullResetUploadRuntimeState(void);
static void ESP_SPI_FullEnterHoldoff(const char *reason, uint32_t holdoff_ms);
static bool ESP_SPI_FullHoldoffActive(uint32_t now_tick);
#endif
static void ESP_SetServerDownsampleStep(uint32_t step);
static bool ESP_TryParseDownsampleStep(const char *s, uint32_t *out_step);
static void ESP_SetServerUploadPoints(uint32_t points);
static bool ESP_TryParseUploadPoints(const char *s, uint32_t *out_points);
static bool ESP_CommParams_SaveToSD(void);
static void ESP_SPI_ApplyCommParamsToCoprocessor(const ESP_CommParams_t *p);
static bool ESP_UploadMode_LoadFromSD(void);
static bool ESP_UploadMode_SaveToSD(uint8_t full);
static void ESP_SPI_FullArmFrames(uint16_t frames);
static void ESP_SPI_FullSetContinuous(uint8_t enable);
static void ESP_SPI_FullPrintStatus(void);
static void ESP_UI_SPI_LogStatus(const char *prefix);
static bool ESP_UI_DoApplyConfig(void);
static bool ESP_UI_DoWiFi(void);
static bool ESP_UI_DoTCP(void);
static bool ESP_UI_DoRegister(void);
static bool ESP_UI_EnsureReportStarted(uint8_t mode, const char *reason_tag);
static void ESP_UI_ScheduleAutoRecover(const char *reason, uint8_t want_report);
static void ESP_UI_PollAutoRecover(void);
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
static uint8_t ESP_SPI_FullControlBusy(void);
static void ESP_SPI_QueueCommParamsSync(const ESP_CommParams_t *p);
static void ESP_SPI_QueueReportModeSync(uint8_t full);
static void ESP_SPI_ServiceDeferredSync(void);
#endif


// 当前上报的故障码（默认正常 E00），可通过串口控制台动态修改
static char g_fault_code[4] = "E00";

#ifndef AXI_SRAM_SECTION
#define AXI_SRAM_SECTION __attribute__((section(".axi_sram")))
#endif

Channel_Data_t node_channels[4] AXI_SRAM_SECTION;
volatile uint8_t g_esp_ready = 0U;
static volatile uint8_t g_report_enabled = 0U;
static volatile uint8_t g_ui_wifi_ok = 0U;
static volatile uint8_t g_ui_tcp_ok = 0U;
static volatile uint8_t g_ui_reg_ok = 0U;
static volatile uint8_t g_ui_auto_recover_pending = 0U;
static volatile uint8_t g_ui_auto_recover_want_report = 0U;
static uint32_t g_ui_auto_recover_next_poll_tick = 0U;
static uint32_t g_ui_auto_recover_next_attempt_tick = 0U;
static uint32_t g_ui_last_session_epoch = 0U;
static uint8_t g_ui_last_session_valid = 0U;

static SystemConfig_t g_sys_cfg;
static uint8_t g_sys_cfg_loaded = 0U;

static arm_rfft_fast_instance_f32 S;
static uint8_t fft_initialized = 0U;
static uint8_t channels_metadata_initialized = 0U;
static float32_t fft_input_buf[WAVEFORM_POINTS] AXI_SRAM_SECTION;
static float32_t fft_output_buf[WAVEFORM_POINTS] AXI_SRAM_SECTION;
static float32_t fft_mag_buf[WAVEFORM_POINTS] AXI_SRAM_SECTION;

static inline void ESP_RtosYield(void)
{
    if ((osKernelGetState() == osKernelRunning) && (__get_IPSR() == 0U))
    {
        osDelay(1);
    }
}

static void ESP_LoadConfig(void)
{
    if (g_sys_cfg_loaded)
    {
        return;
    }

    SD_Config_SetDefaults(&g_sys_cfg);
    g_sys_cfg_loaded = 1U;
}

const SystemConfig_t *ESP_Config_Get(void)
{
    if (!g_sys_cfg_loaded)
    {
        ESP_LoadConfig();
    }
    return &g_sys_cfg;
}

void ESP_Config_Apply(const SystemConfig_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }
    g_sys_cfg = *cfg;
    g_sys_cfg_loaded = 1U;
}

static void ESP_Init_Channels_And_DSP(void)
{
    if (!fft_initialized)
    {
        arm_rfft_fast_init_f32(&S, WAVEFORM_POINTS);
        fft_initialized = 1U;
    }

    if (channels_metadata_initialized)
    {
        return;
    }

    memset(node_channels, 0, sizeof(node_channels));
    node_channels[0].id = 0;
    strncpy(node_channels[0].label, "直流母线(+)", sizeof(node_channels[0].label) - 1U);
    strncpy(node_channels[0].unit, "V", sizeof(node_channels[0].unit) - 1U);

    node_channels[1].id = 1;
    strncpy(node_channels[1].label, "直流母线(-)", sizeof(node_channels[1].label) - 1U);
    strncpy(node_channels[1].unit, "V", sizeof(node_channels[1].unit) - 1U);

    node_channels[2].id = 2;
    strncpy(node_channels[2].label, "负载电流", sizeof(node_channels[2].label) - 1U);
    strncpy(node_channels[2].unit, "A", sizeof(node_channels[2].unit) - 1U);

    node_channels[3].id = 3;
    strncpy(node_channels[3].label, "漏电流", sizeof(node_channels[3].label) - 1U);
    strncpy(node_channels[3].unit, "mA", sizeof(node_channels[3].unit) - 1U);

    channels_metadata_initialized = 1U;
}

#if (ESP_UPLOAD_SNAPSHOT_SLOT_COUNT < 2)
#error "ESP_UPLOAD_SNAPSHOT_SLOT_COUNT must be at least 2"
#endif

#define ESP_UPLOAD_SNAPSHOT_MAGIC      0x45575550UL /* "EWUP" */
#define ESP_UPLOAD_SNAPSHOT_SDRAM_ADDR ((uintptr_t)0xC0680000UL)

typedef struct
{
    uint32_t magic;
    uint32_t seq;
    uint32_t tick_ms;
    uint8_t ready_buffer;
    uint8_t channel_count;
    uint8_t report_mode;
    uint8_t status_code;
    char fault_code[8];
    esp32_spi_report_channel_t channels[4];
    float current_value[4];
    float waveform[4][WAVEFORM_POINTS];
    float fft_data[4][FFT_POINTS];
} ESP_UploadSnapshot_t;

static ESP_UploadSnapshot_t * const g_upload_snapshots =
    (ESP_UploadSnapshot_t *)ESP_UPLOAD_SNAPSHOT_SDRAM_ADDR;
static volatile int8_t g_upload_snapshot_latest_idx = -1;
static volatile int8_t g_upload_snapshot_inflight_idx = -1;
static volatile int8_t g_upload_snapshot_writing_idx = -1;
static volatile uint8_t g_upload_snapshot_next_idx = 0U;
static volatile uint32_t g_upload_snapshot_seq = 0U;
static volatile uint32_t g_upload_snapshot_publish_count = 0U;
static volatile uint32_t g_upload_snapshot_drop_count = 0U;

static int8_t ESP_UploadSnapshot_BeginWrite(void)
{
    int8_t idx;

    taskENTER_CRITICAL();
    idx = (int8_t)(g_upload_snapshot_next_idx % ESP_UPLOAD_SNAPSHOT_SLOT_COUNT);
    if (idx == g_upload_snapshot_inflight_idx)
    {
        idx = (int8_t)((idx + 1) % ESP_UPLOAD_SNAPSHOT_SLOT_COUNT);
    }
    if (idx == g_upload_snapshot_inflight_idx)
    {
        taskEXIT_CRITICAL();
        return -1;
    }

    if (g_upload_snapshot_latest_idx >= 0)
    {
        g_upload_snapshot_drop_count++;
        g_upload_snapshot_latest_idx = -1;
    }
    g_upload_snapshot_writing_idx = idx;
    taskEXIT_CRITICAL();
    return idx;
}

static void ESP_UploadSnapshot_FinishWrite(int8_t idx, ESP_UploadSnapshot_t *slot)
{
    if (idx < 0 || slot == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();
    slot->seq = ++g_upload_snapshot_seq;
    slot->magic = ESP_UPLOAD_SNAPSHOT_MAGIC;
    g_upload_snapshot_latest_idx = idx;
    g_upload_snapshot_next_idx = (uint8_t)((idx + 1) % ESP_UPLOAD_SNAPSHOT_SLOT_COUNT);
    g_upload_snapshot_writing_idx = -1;
    g_upload_snapshot_publish_count++;
    taskEXIT_CRITICAL();
}

static void ESP_UploadSnapshot_PublishFromNodeChannels(uint8_t ready_buffer)
{
    int8_t idx = ESP_UploadSnapshot_BeginWrite();
    ESP_UploadSnapshot_t *slot;

    if (idx < 0)
    {
        g_upload_snapshot_drop_count++;
        return;
    }

    slot = &g_upload_snapshots[(uint8_t)idx];
    memset(slot, 0, sizeof(*slot));
    slot->tick_ms = HAL_GetTick();
    slot->ready_buffer = ready_buffer;
    slot->channel_count = 4U;
    slot->report_mode = 1U;
    slot->status_code = 1U;
    strncpy(slot->fault_code, g_fault_code, sizeof(slot->fault_code) - 1U);
    slot->fault_code[sizeof(slot->fault_code) - 1U] = '\0';

    for (uint8_t ch = 0U; ch < 4U; ch++)
    {
        int32_t cv_i = ESP_FloatToI32Scaled(node_channels[ch].current_value);
        slot->channels[ch].channel_id = node_channels[ch].id;
        slot->channels[ch].waveform_count = (uint16_t)WAVEFORM_POINTS;
        slot->channels[ch].fft_count = (uint16_t)FFT_POINTS;
        slot->channels[ch].value_scaled = cv_i;
        slot->channels[ch].current_value_scaled = cv_i;
        slot->current_value[ch] = node_channels[ch].current_value;
        memcpy(slot->waveform[ch], node_channels[ch].waveform, sizeof(slot->waveform[ch]));
        memcpy(slot->fft_data[ch], node_channels[ch].fft_data, sizeof(slot->fft_data[ch]));
        ESP_RtosYield();
    }

    ESP_UploadSnapshot_FinishWrite(idx, slot);
}

static const ESP_UploadSnapshot_t *ESP_UploadSnapshot_TryAcquireLatest(uint32_t *out_seq)
{
    int8_t idx;
    uint32_t seq;

    taskENTER_CRITICAL();
    idx = g_upload_snapshot_latest_idx;
    if (idx < 0 || g_upload_snapshot_inflight_idx >= 0 || idx == g_upload_snapshot_writing_idx)
    {
        taskEXIT_CRITICAL();
        return NULL;
    }
    g_upload_snapshot_latest_idx = -1;
    g_upload_snapshot_inflight_idx = idx;
    seq = g_upload_snapshots[(uint8_t)idx].seq;
    taskEXIT_CRITICAL();

    if (g_upload_snapshots[(uint8_t)idx].magic != ESP_UPLOAD_SNAPSHOT_MAGIC)
    {
        taskENTER_CRITICAL();
        if (g_upload_snapshot_inflight_idx == idx)
        {
            g_upload_snapshot_inflight_idx = -1;
        }
        taskEXIT_CRITICAL();
        return NULL;
    }

    if (out_seq != NULL)
    {
        *out_seq = seq;
    }
    return &g_upload_snapshots[(uint8_t)idx];
}

static void ESP_UploadSnapshot_Release(uint32_t seq)
{
    int8_t idx;

    taskENTER_CRITICAL();
    idx = g_upload_snapshot_inflight_idx;
    if (idx >= 0 && g_upload_snapshots[(uint8_t)idx].seq == seq)
    {
        g_upload_snapshot_inflight_idx = -1;
    }
    taskEXIT_CRITICAL();
}

#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
typedef enum
{
    ESP_FULL_TX_IDLE = 0,
    ESP_FULL_TX_BEGIN,
    ESP_FULL_TX_WAVE,
    ESP_FULL_TX_FFT,
    ESP_FULL_TX_END,
} ESP_FullTxPhase_t;

typedef struct
{
    uint8_t active;
    uint8_t one_shot;
    uint8_t channel_count;
    ESP_FullTxPhase_t phase;
    uint32_t frame_id;
    uint32_t snapshot_seq;
    uint32_t start_tick;
    uint32_t packet_count;
    uint32_t wave_step;
    uint32_t upload_points;
    const ESP_UploadSnapshot_t *snapshot;
    esp32_spi_report_channel_t channels[4];
    const float *waves[4];
    const float *ffts[4];
    uint16_t wave_source_counts[4];
    uint8_t channel_index;
    uint16_t element_offset;
} ESP_FullTxState_t;

static ESP_FullTxState_t g_full_tx_sm;

static void ESP_FullTx_ClearAndRelease(void)
{
    if (g_full_tx_sm.snapshot_seq != 0U)
    {
        ESP_UploadSnapshot_Release(g_full_tx_sm.snapshot_seq);
    }
    memset(&g_full_tx_sm, 0, sizeof(g_full_tx_sm));
}
#endif

// ---------- 串口控制台（调试串口 RX 中断） ----------
static uint8_t g_console_rx_byte = 0;
static volatile uint8_t g_console_line_ready = 0;
static char g_console_line[32];
static volatile uint8_t g_console_line_len = 0;

// ---------- 服务器下发命令解析 ----------
static volatile uint8_t g_server_reset_pending = 0;
// 服务器请求的上报模式：0=summary, 1=full
static volatile uint8_t g_server_report_full = (ESP32_SPI_STRESS_FULL_UPLOAD != 0) ? 1U : 0U;
static volatile uint8_t g_server_report_full_dirty = 0;
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
static volatile uint16_t g_spi_full_manual_frames = 0;
static volatile uint8_t g_spi_full_continuous = (ESP32_SPI_FULL_CONTINUOUS_DEFAULT != 0) ? 1U : 0U;
static volatile uint8_t g_spi_full_waiting_result = 0;
static uint32_t g_spi_full_result_ref_seq = 0U;
static uint32_t g_spi_full_result_frame_id = 0U;
static uint32_t g_spi_full_result_start_tick = 0U;
static uint32_t g_spi_full_result_session_epoch = 0U;
static uint32_t g_spi_full_result_last_log_tick = 0U;
static uint32_t g_spi_full_result_last_poll_tick = 0U;
static uint32_t g_spi_full_holdoff_until_tick = 0U;
static uint32_t g_spi_full_timeout_count = 0U;
static uint32_t g_spi_full_busy_nack_count = 0U;
static uint32_t g_spi_full_http_fail_count = 0U;
#endif
// 服务器请求的降采样步进：1..64
static volatile uint32_t g_server_downsample_step = (uint32_t)WAVEFORM_SEND_STEP;
static volatile uint8_t g_server_downsample_dirty = 0;
// 服务器请求的上传点数（降采样后）：256..4096，256步进
static volatile uint32_t g_server_upload_points = (uint32_t)WAVEFORM_POINTS;
static volatile uint8_t g_server_upload_points_dirty = 0;
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
static volatile uint8_t g_spi_comm_params_sync_pending = 0U;
static volatile uint8_t g_spi_report_mode_sync_pending = 0U;
static volatile uint8_t g_spi_report_mode_sync_target = 0U;
static ESP_CommParams_t g_spi_pending_comm_params;
#endif
// 当检测到链路异常关键字（CLOSED/ERROR）时置 1，主循环触发软重连



/* 滑动窗口：用于在流式数据中查找关键字。
 * 解决 DMA 分包导致关键字（如 "HTTP/1.1"）被切断的问题。 */

/* 调试与统计变量 */

/* 链路健康监测 */

/* HTTP 发送流控门控
 * 作用：发送 HTTP 请求后置为 1，收到回复或超时后置为 0。
 * 防止请求发送过快淹没服务器，导致 TCP 拥塞或解析错误。 */

/* 分段发送状态（仅用于 ESP_Post_Data 的大包上报） */

/* 非分段 HTTP 发送链状态：header DMA -> body DMA */

/* ================= 通讯参数（运行时缓存） =================
 * 由 SD 文件 0:/config/ui_param.cfg 加载；若未加载则使用宏默认值。
 */
static volatile uint32_t g_comm_heartbeat_ms    = (uint32_t)ESP_HEARTBEAT_INTERVAL_MS;
static volatile uint32_t g_comm_min_interval_ms = (uint32_t)ESP_MIN_SEND_INTERVAL_MS;
static volatile uint32_t g_comm_http_timeout_ms = (uint32_t)ESP_HTTP_TIMEOUT_MS_DEFAULT;
static volatile uint32_t g_comm_hardreset_sec   = (uint32_t)ESP_NO_SERVER_RESPONSE_RECOVER_SEC;
static volatile uint32_t g_comm_wave_step       = (uint32_t)WAVEFORM_SEND_STEP;
static volatile uint32_t g_comm_upload_points  = (uint32_t)WAVEFORM_POINTS;
static volatile uint32_t g_comm_chunk_kb        = (uint32_t)ESP_CHUNK_KB_DEFAULT;
static volatile uint32_t g_comm_chunk_delay_ms  = (uint32_t)ESP_CHUNK_DELAY_MS_DEFAULT;




/* ================= ESP 通讯参数 API（运行时可配置） ================= */
uint32_t ESP_CommParams_HeartbeatMs(void)    { return (uint32_t)g_comm_heartbeat_ms; }
uint32_t ESP_CommParams_MinIntervalMs(void) { return (uint32_t)g_comm_min_interval_ms; }
uint32_t ESP_CommParams_HttpTimeoutMs(void) { return (uint32_t)g_comm_http_timeout_ms; }
uint32_t ESP_CommParams_HardResetSec(void)  { return (uint32_t)g_comm_hardreset_sec; }
uint32_t ESP_CommParams_WaveStep(void)      { return (uint32_t)g_comm_wave_step; }
uint32_t ESP_CommParams_UploadPoints(void)  { return (uint32_t)g_comm_upload_points; }
uint32_t ESP_CommParams_ChunkKb(void)       { return (uint32_t)g_comm_chunk_kb; }
uint32_t ESP_CommParams_ChunkDelayMs(void)  { return (uint32_t)g_comm_chunk_delay_ms; }

void ESP_CommParams_Get(ESP_CommParams_t *out)
{
    if (!out) return;
    out->heartbeat_ms    = (uint32_t)g_comm_heartbeat_ms;
    out->min_interval_ms = (uint32_t)g_comm_min_interval_ms;
    out->http_timeout_ms = (uint32_t)g_comm_http_timeout_ms;
    out->hardreset_sec   = (uint32_t)g_comm_hardreset_sec;
    out->wave_step       = (uint32_t)g_comm_wave_step;
    out->upload_points   = (uint32_t)g_comm_upload_points;
    out->chunk_kb        = (uint32_t)g_comm_chunk_kb;
    out->chunk_delay_ms  = (uint32_t)g_comm_chunk_delay_ms;
}

static bool ESP_CommParams_Equals(const ESP_CommParams_t *a, const ESP_CommParams_t *b)
{
    if (!a || !b) return false;
    return (a->heartbeat_ms == b->heartbeat_ms &&
            a->min_interval_ms == b->min_interval_ms &&
            a->http_timeout_ms == b->http_timeout_ms &&
            a->hardreset_sec == b->hardreset_sec &&
            a->wave_step == b->wave_step &&
            a->upload_points == b->upload_points &&
            a->chunk_kb == b->chunk_kb &&
            a->chunk_delay_ms == b->chunk_delay_ms);
}

static void ESP_ServerCmdAppendText(char *buf, size_t buf_size, const char *text)
{
    if (!buf || buf_size == 0U || !text || text[0] == '\0') {
        return;
    }
    size_t used = strlen(buf);
    if (used >= (buf_size - 1U)) {
        return;
    }
    if (used > 0U) {
        (void)snprintf(buf + used, buf_size - used, ", ");
        used = strlen(buf);
        if (used >= (buf_size - 1U)) {
            return;
        }
    }
    (void)snprintf(buf + used, buf_size - used, "%s", text);
}

static void ESP_ServerCmdAppendU32(char *buf, size_t buf_size, const char *key, uint32_t value)
{
    char item[40];
    (void)snprintf(item, sizeof(item), "%s=%lu", key, (unsigned long)value);
    ESP_ServerCmdAppendText(buf, buf_size, item);
}

static void ESP_BuildServerCommandSummary(char *buf,
                                          size_t buf_size,
                                          uint8_t reset,
                                          uint8_t has_mode,
                                          uint8_t full,
                                          uint8_t has_downsample,
                                          uint32_t downsample,
                                          uint8_t has_upload,
                                          uint32_t upload_points,
                                          uint8_t has_hb,
                                          uint32_t hb_ms,
                                          uint8_t has_min,
                                          uint32_t min_ms,
                                          uint8_t has_http,
                                          uint32_t http_ms,
                                          uint8_t has_chunk,
                                          uint32_t chunk_kb,
                                          uint8_t has_chunk_delay,
                                          uint32_t chunk_delay_ms)
{
    if (!buf || buf_size == 0U) {
        return;
    }
    buf[0] = '\0';
    if (reset) {
        ESP_ServerCmdAppendText(buf, buf_size, "reset=1");
    }
    if (has_mode) {
        ESP_ServerCmdAppendText(buf, buf_size, full ? "report_mode=full" : "report_mode=summary");
    }
    if (has_downsample) {
        ESP_ServerCmdAppendU32(buf, buf_size, "downsample_step", downsample);
    }
    if (has_upload) {
        ESP_ServerCmdAppendU32(buf, buf_size, "upload_points", upload_points);
    }
    if (has_hb) {
        ESP_ServerCmdAppendU32(buf, buf_size, "heartbeat_ms", hb_ms);
    }
    if (has_min) {
        ESP_ServerCmdAppendU32(buf, buf_size, "min_interval_ms", min_ms);
    }
    if (has_http) {
        ESP_ServerCmdAppendU32(buf, buf_size, "http_timeout_ms", http_ms);
    }
    if (has_chunk) {
        ESP_ServerCmdAppendU32(buf, buf_size, "chunk_kb", chunk_kb);
    }
    if (has_chunk_delay) {
        ESP_ServerCmdAppendU32(buf, buf_size, "chunk_delay_ms", chunk_delay_ms);
    }
    if (buf[0] == '\0') {
        ESP_ServerCmdAppendText(buf, buf_size, "command");
    }
}

static char g_server_cmd_last_applied_summary[192];
static uint32_t g_server_cmd_last_applied_tick = 0U;
static char g_server_cmd_pending_summary[192];
static uint8_t g_server_cmd_pending_active = 0U;
static uint8_t g_server_cmd_pending_saved = 0U;
static uint8_t g_server_cmd_pending_save_failed = 0U;
static uint8_t g_server_cmd_pending_applied = 0U;

static bool ESP_ServerCommandRecentlyApplied(const char *summary, uint32_t now_tick)
{
    if (!summary || summary[0] == '\0' || g_server_cmd_last_applied_summary[0] == '\0') {
        return false;
    }
    if (strncmp(summary, g_server_cmd_last_applied_summary, sizeof(g_server_cmd_last_applied_summary)) != 0) {
        return false;
    }
    return ((uint32_t)(now_tick - g_server_cmd_last_applied_tick) < (uint32_t)ESP_SERVER_CMD_DEDUPE_WINDOW_MS);
}

static void ESP_ServerCommandRememberApplied(const char *summary, uint32_t now_tick)
{
    if (!summary || summary[0] == '\0') {
        return;
    }
    strncpy(g_server_cmd_last_applied_summary, summary, sizeof(g_server_cmd_last_applied_summary) - 1U);
    g_server_cmd_last_applied_summary[sizeof(g_server_cmd_last_applied_summary) - 1U] = '\0';
    g_server_cmd_last_applied_tick = now_tick;
}

static void ESP_ServerCommandLogClear(void)
{
    g_server_cmd_pending_summary[0] = '\0';
    g_server_cmd_pending_active = 0U;
    g_server_cmd_pending_saved = 0U;
    g_server_cmd_pending_save_failed = 0U;
    g_server_cmd_pending_applied = 0U;
}

static void ESP_ServerCommandLogStore(const char *summary,
                                      uint8_t seen,
                                      uint8_t saved,
                                      uint8_t save_failed,
                                      uint8_t applied)
{
    if (seen == 0U || summary == NULL || summary[0] == '\0') {
        return;
    }
    strncpy(g_server_cmd_pending_summary, summary, sizeof(g_server_cmd_pending_summary) - 1U);
    g_server_cmd_pending_summary[sizeof(g_server_cmd_pending_summary) - 1U] = '\0';
    g_server_cmd_pending_active = 1U;
    if (saved != 0U) {
        g_server_cmd_pending_saved = 1U;
    }
    if (save_failed != 0U) {
        g_server_cmd_pending_save_failed = 1U;
    }
    if (applied != 0U) {
        g_server_cmd_pending_applied = 1U;
    }
}

static void ESP_ServerCommandLogRestore(char *summary,
                                        size_t summary_size,
                                        uint8_t *seen,
                                        uint8_t *saved,
                                        uint8_t *save_failed,
                                        uint8_t *applied)
{
    if (g_server_cmd_pending_active == 0U || summary == NULL || summary_size == 0U) {
        return;
    }
    strncpy(summary, g_server_cmd_pending_summary, summary_size - 1U);
    summary[summary_size - 1U] = '\0';
    if (seen != NULL) {
        *seen = 1U;
    }
    if (saved != NULL && g_server_cmd_pending_saved != 0U) {
        *saved = 1U;
    }
    if (save_failed != NULL && g_server_cmd_pending_save_failed != 0U) {
        *save_failed = 1U;
    }
    if (applied != NULL && g_server_cmd_pending_applied != 0U) {
        *applied = 1U;
    }
}

static uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void ESP_CommParams_Apply(const ESP_CommParams_t *p)
{
    if (!p) return;
    /* 约束：避免极端值导致系统抖动或“永不发送” */
    uint32_t hb    = clamp_u32(p->heartbeat_ms,    200u, 54999u);
    uint32_t minit = clamp_u32(p->min_interval_ms, 0u,   600000u);
    uint32_t http  = clamp_u32(p->http_timeout_ms, 1000u, 600000u);
    uint32_t hrs   = clamp_u32(p->hardreset_sec,   5u,   3600u);
    uint32_t step  = clamp_u32(p->wave_step,       1u,   64u);
    uint32_t upmax = (uint32_t)WAVEFORM_POINTS;
    uint32_t up    = (uint32_t)p->upload_points;
    if (upmax == 0u) {
        up = 0u;
    } else if (upmax < 256u) {
        up = upmax;
    } else {
        if (up < 256u) up = 256u;
        if (up > upmax) up = upmax;
        /* 容错：非 256 步进时就近取整到 256 倍数 */
        up = ((up + 128u) / 256u) * 256u;
        if (up < 256u) up = 256u;
        if (up > upmax) up = upmax;
    }
    uint32_t ckb   = p->chunk_kb;
    if (ckb > 16u) ckb = 16u; /* 允许 0 表示“关闭分段” */
    uint32_t cdly  = clamp_u32(p->chunk_delay_ms,  0u,   (uint32_t)ESP_CHUNK_DELAY_MS_MAX);

    g_comm_heartbeat_ms    = hb;
    g_comm_min_interval_ms = minit;
    g_comm_http_timeout_ms = http;
    g_comm_hardreset_sec   = hrs;
    g_comm_wave_step       = step;
    g_comm_upload_points  = up;
    g_comm_chunk_kb        = ckb;
    g_comm_chunk_delay_ms  = cdly;

#if (ESP_DEBUG)
    ESP_Log("[PARAM] apply hb=%lums min=%lums http=%lums hrs=%lus step=%lu up=%lu chunk=%luKB delay=%lums\r\n",
            (unsigned long)hb, (unsigned long)minit, (unsigned long)http, (unsigned long)hrs,
            (unsigned long)step, (unsigned long)up, (unsigned long)ckb, (unsigned long)cdly);
#endif
}

static void cfg_rstrip(char *s)
{
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[n - 1] = '\0';
        n--;
    }
}

static bool cfg_parse_u32_relaxed(const char *s, uint32_t *out)
{
    if (!out) return false;
    if (!s) return false;
    /* 容错：允许前面多余的 '=' 或空白（例如 "==60" / "=60"） */
    while (*s == '=' || *s == ' ' || *s == '\t')
        s++;
    if (!*s) return false;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s) return false;
    *out = (uint32_t)v;
    return true;
}

/* 上电/进入界面时加载一次：仅影响通讯参数缓存，不影响 WiFi/Server/SystemConfig_t */
bool ESP_CommParams_LoadFromSD(void)
{
    /* 避免与 QSPI/SD 同步竞争（该标志在 GUI_Assets_SyncFromSD 期间置位） */
    extern volatile uint8_t g_qspi_sd_sync_in_progress;
    if (g_qspi_sd_sync_in_progress) {
        return false;
    }

    /* 确保 FatFs 已初始化 */
    if (SDPath[0] == '\0') {
        MX_FATFS_Init();
    }

    /* 初始化磁盘 */
    (void)disk_initialize(0);

    /* 等待卡就绪（短超时） */
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < 200U) {
        if (BSP_SD_GetCardState() == SD_TRANSFER_OK) break;
        osDelay(5);
    }

    if (f_mount(&SDFatFS, (TCHAR const *)SDPath, 1) != FR_OK) {
        return false;
    }

    FIL fil;
    if (f_open(&fil, "0:/config/ui_param.cfg", FA_READ) != FR_OK) {
        return false;
    }

    ESP_CommParams_t p;
    ESP_CommParams_Get(&p); /* 先取当前值作为兜底 */

    char line[160];
    while (f_gets(line, sizeof(line), &fil)) {
        cfg_rstrip(line);
        if (strncmp(line, "HEARTBEAT_MS=", 13) == 0) {
            uint32_t v;
            if (cfg_parse_u32_relaxed(line + 13, &v)) p.heartbeat_ms = v;
        } else if (strncmp(line, "SENDLIMIT_MS=", 13) == 0) {
            uint32_t v;
            if (cfg_parse_u32_relaxed(line + 13, &v)) p.min_interval_ms = v;
        } else if (strncmp(line, "HTTP_TIMEOUT_MS=", 16) == 0) {
            uint32_t v;
            if (cfg_parse_u32_relaxed(line + 16, &v)) p.http_timeout_ms = v;
        } else if (strncmp(line, "HARDRESET_S=", 11) == 0) {
            uint32_t v;
            if (cfg_parse_u32_relaxed(line + 11, &v)) p.hardreset_sec = v;
        } else if (strncmp(line, "DOWNSAMPLE_STEP=", 16) == 0) {
            uint32_t v;
            if (cfg_parse_u32_relaxed(line + 16, &v)) p.wave_step = v;
        } else if (strncmp(line, "UPLOAD_POINTS=", 14) == 0) {
            uint32_t v;
            if (cfg_parse_u32_relaxed(line + 14, &v)) p.upload_points = v;
        } else if (strncmp(line, "CHUNK_KB=", 9) == 0) {
            uint32_t v;
            if (cfg_parse_u32_relaxed(line + 9, &v)) p.chunk_kb = v;
        } else if (strncmp(line, "CHUNK_DELAY_MS=", 15) == 0) {
            uint32_t v;
            if (cfg_parse_u32_relaxed(line + 15, &v)) p.chunk_delay_ms = v;
        }
    }
    (void)f_close(&fil);

    ESP_CommParams_Apply(&p);
    return true;
}

/* ================= 断电重连/上报状态持久化（SD 标志位） ================= */
#define UI_CFG_DIR               "0:/config"
#define UI_WIFI_CFG_FILE         "0:/config/ui_wifi.cfg"
#define UI_SERVER_CFG_FILE       "0:/config/ui_server.cfg"
#define UI_PARAM_CFG_FILE        "0:/config/ui_param.cfg"
#define UI_PARAM_TMP_FILE        "0:/config/.ui_param.cfg.tmp"
#define UI_AUTOREPORT_CFG_FILE   "0:/config/ui_autoreport.cfg"
#define UI_AUTOREPORT_TMP_FILE   "0:/config/.ui_autoreport.cfg.tmp"
#define UI_UPLOAD_CFG_FILE       "0:/config/ui_upload.cfg"
#define UI_UPLOAD_TMP_FILE       "0:/config/.ui_upload.cfg.tmp"

static bool esp_sd_try_mount(uint32_t wait_ms)
{
    /* 避免与 QSPI/SD 同步竞争（该标志在 GUI_Assets_SyncFromSD 期间置位） */
    extern volatile uint8_t g_qspi_sd_sync_in_progress;
    if (g_qspi_sd_sync_in_progress) {
        return false;
    }

    if (SDPath[0] == '\0') {
        MX_FATFS_Init();
    }
    (void)disk_initialize(0);

    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < wait_ms) {
        if (BSP_SD_GetCardState() == SD_TRANSFER_OK) break;
        osDelay(5);
    }

    return (f_mount(&SDFatFS, (TCHAR const *)SDPath, 1) == FR_OK);
}

static FRESULT esp_cfg_ensure_dir(void)
{
    FILINFO fno;
    FRESULT res = f_stat(UI_CFG_DIR, &fno);
    if (res == FR_OK) {
        return FR_OK;
    }
    res = f_mkdir(UI_CFG_DIR);
    if (res == FR_EXIST) {
        return FR_OK;
    }
    return res;
}

static bool ESP_CommParams_SaveToSD(void)
{
    if (!esp_sd_try_mount(200U)) {
        return false;
    }
    if (esp_cfg_ensure_dir() != FR_OK) {
        return false;
    }

    ESP_CommParams_t p;
    ESP_CommParams_Get(&p);

    FIL fil;
    if (f_open(&fil, UI_PARAM_TMP_FILE, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        return false;
    }

    char buf[320];
    int n = snprintf(buf, sizeof(buf),
                     "HEARTBEAT_MS=%lu\n"
                     "SENDLIMIT_MS=%lu\n"
                     "HTTP_TIMEOUT_MS=%lu\n"
                     "HARDRESET_S=%lu\n"
                     "DOWNSAMPLE_STEP=%lu\n"
                     "UPLOAD_POINTS=%lu\n"
                     "CHUNK_KB=%lu\n"
                     "CHUNK_DELAY_MS=%lu\n",
                     (unsigned long)p.heartbeat_ms,
                     (unsigned long)p.min_interval_ms,
                     (unsigned long)p.http_timeout_ms,
                     (unsigned long)p.hardreset_sec,
                     (unsigned long)p.wave_step,
                     (unsigned long)p.upload_points,
                     (unsigned long)p.chunk_kb,
                     (unsigned long)p.chunk_delay_ms);
    if (n <= 0 || n >= (int)sizeof(buf)) {
        (void)f_close(&fil);
        (void)f_unlink(UI_PARAM_TMP_FILE);
        return false;
    }

    UINT bw = 0;
    FRESULT wr = f_write(&fil, buf, (UINT)n, &bw);
    if (wr != FR_OK || bw != (UINT)n) {
        (void)f_close(&fil);
        (void)f_unlink(UI_PARAM_TMP_FILE);
        return false;
    }

    FRESULT sr = f_sync(&fil);
    (void)f_close(&fil);
    if (sr != FR_OK) {
        (void)f_unlink(UI_PARAM_TMP_FILE);
        return false;
    }

    (void)f_unlink(UI_PARAM_CFG_FILE);
    FRESULT r = f_rename(UI_PARAM_TMP_FILE, UI_PARAM_CFG_FILE);
    if (r != FR_OK) {
        (void)f_unlink(UI_PARAM_TMP_FILE);
        return false;
    }
    return true;
}

static bool esp_autoreport_write(bool auto_reconnect_en, bool last_reporting)
{
    if (!esp_sd_try_mount(200U)) {
        return false;
    }
    if (esp_cfg_ensure_dir() != FR_OK) {
        return false;
    }

    FIL fil;
    if (f_open(&fil, UI_AUTOREPORT_TMP_FILE, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        return false;
    }

    char buf[96];
    int n = snprintf(buf, sizeof(buf),
                     "AUTO_RECONNECT=%u\r\n"
                     "LAST_REPORTING=%u\r\n",
                     auto_reconnect_en ? 1u : 0u,
                     last_reporting ? 1u : 0u);
    if (n < 0) {
        (void)f_close(&fil);
        (void)f_unlink(UI_AUTOREPORT_TMP_FILE);
        return false;
    }

    UINT bw = 0;
    (void)f_write(&fil, buf, (UINT)strlen(buf), &bw);
    (void)f_close(&fil);

    /* 原子替换：先删旧文件，再 rename 临时文件 */
    (void)f_unlink(UI_AUTOREPORT_CFG_FILE);
    FRESULT r = f_rename(UI_AUTOREPORT_TMP_FILE, UI_AUTOREPORT_CFG_FILE);
    if (r != FR_OK) {
        (void)f_unlink(UI_AUTOREPORT_TMP_FILE);
        return false;
    }
    return true;
}

bool ESP_AutoReconnect_Read(bool *auto_reconnect_en, bool *last_reporting)
{
    /* 默认值：开启“断电重连”功能，但上次上报默认=否（这样首次不会自动连接） */
    bool en = true;
    bool last = false;

    if (!esp_sd_try_mount(120U)) {
        if (auto_reconnect_en) *auto_reconnect_en = en;
        if (last_reporting) *last_reporting = last;
        return false;
    }

    FIL fil;
    if (f_open(&fil, UI_AUTOREPORT_CFG_FILE, FA_READ) != FR_OK) {
        if (auto_reconnect_en) *auto_reconnect_en = en;
        if (last_reporting) *last_reporting = last;
        return false;
    }

    char line[96];
    while (f_gets(line, sizeof(line), &fil)) {
        cfg_rstrip(line);
        if (strncmp(line, "AUTO_RECONNECT=", 15) == 0) {
            uint32_t v;
            if (cfg_parse_u32_relaxed(line + 15, &v)) en = (v != 0U);
        } else if (strncmp(line, "LAST_REPORTING=", 15) == 0) {
            uint32_t v;
            if (cfg_parse_u32_relaxed(line + 15, &v)) last = (v != 0U);
        }
    }
    (void)f_close(&fil);

    if (auto_reconnect_en) *auto_reconnect_en = en;
    if (last_reporting) *last_reporting = last;
    return true;
}

bool ESP_AutoReconnect_SetEnabled(bool auto_reconnect_en)
{
    bool cur_en = true, cur_last = false;
    (void)ESP_AutoReconnect_Read(&cur_en, &cur_last);
    return esp_autoreport_write(auto_reconnect_en, cur_last);
}

bool ESP_AutoReconnect_SetLastReporting(bool last_reporting)
{
    bool cur_en = true, cur_last = false;
    (void)ESP_AutoReconnect_Read(&cur_en, &cur_last);
    return esp_autoreport_write(cur_en, last_reporting);
}

/* 启动前置：从 UI 保存的 SD 文件读取 WiFi/Server 配置并应用 */

static void ESP_UploadMode_ApplyLoaded(uint8_t full)
{
    full = full ? 1U : 0U;
    g_server_report_full = full;
    g_server_report_full_dirty = 0U;
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    g_spi_full_continuous = full;
    if (!full) {
        g_spi_full_manual_frames = 0U;
        ESP_SPI_FullResetUploadRuntimeState();
    }
#endif
}

bool ESP_CommParams_SaveRuntimeToSD(void)
{
    return ESP_CommParams_SaveToSD();
}

static bool ESP_UploadMode_LoadFromSD(void)
{
    if (!esp_sd_try_mount(120U)) {
        return false;
    }

    FIL fil;
    if (f_open(&fil, UI_UPLOAD_CFG_FILE, FA_READ) != FR_OK) {
        return false;
    }

    uint8_t full = g_server_report_full ? 1U : 0U;
    char line[96];
    while (f_gets(line, sizeof(line), &fil)) {
        cfg_rstrip(line);
        if (strncmp(line, "REPORT_MODE=", 12) == 0) {
            const char *v = line + 12;
            if (strcmp(v, "full") == 0 || strcmp(v, "FULL") == 0 || strcmp(v, "1") == 0) {
                full = 1U;
            } else if (strcmp(v, "summary") == 0 || strcmp(v, "SUMMARY") == 0 || strcmp(v, "0") == 0) {
                full = 0U;
            }
        }
    }
    (void)f_close(&fil);

    ESP_UploadMode_ApplyLoaded(full);
    ESP_Log("[ESP] upload mode loaded from SD: %s\r\n", full ? "full" : "summary");
    return true;
}

static bool ESP_UploadMode_SaveToSD(uint8_t full)
{
    if (!esp_sd_try_mount(200U)) {
        return false;
    }
    if (esp_cfg_ensure_dir() != FR_OK) {
        return false;
    }

    FIL fil;
    if (f_open(&fil, UI_UPLOAD_TMP_FILE, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        return false;
    }

    char buf[64];
    int n = snprintf(buf, sizeof(buf),
                     "REPORT_MODE=%s\r\n",
                     full ? "full" : "summary");
    if (n <= 0 || n >= (int)sizeof(buf)) {
        (void)f_close(&fil);
        (void)f_unlink(UI_UPLOAD_TMP_FILE);
        return false;
    }

    UINT bw = 0;
    FRESULT wr = f_write(&fil, buf, (UINT)n, &bw);
    FRESULT sr = f_sync(&fil);
    (void)f_close(&fil);
    if (wr != FR_OK || sr != FR_OK || bw != (UINT)n) {
        (void)f_unlink(UI_UPLOAD_TMP_FILE);
        return false;
    }

    (void)f_unlink(UI_UPLOAD_CFG_FILE);
    FRESULT rn = f_rename(UI_UPLOAD_TMP_FILE, UI_UPLOAD_CFG_FILE);
    if (rn != FR_OK) {
        (void)f_unlink(UI_UPLOAD_TMP_FILE);
        return false;
    }
    return true;
}

bool ESP_Config_LoadFromSD_UIFiles(void)
{
    if (!esp_sd_try_mount(120U)) {
        return false;
    }

    /* 以当前配置为基底（若未加载则会先装载默认值） */
    SystemConfig_t cfg = *ESP_Config_Get();
    bool wifi_loaded = false;
    bool server_loaded = false;

    /* WiFi */
    {
        FIL fil;
        if (f_open(&fil, UI_WIFI_CFG_FILE, FA_READ) == FR_OK) {
            char line[160];
            while (f_gets(line, sizeof(line), &fil)) {
                cfg_rstrip(line);
                if (strncmp(line, "SSID=", 5) == 0) {
                    strncpy(cfg.wifi_ssid, line + 5, sizeof(cfg.wifi_ssid) - 1);
                    cfg.wifi_ssid[sizeof(cfg.wifi_ssid) - 1] = '\0';
                } else if (strncmp(line, "PWD=", 4) == 0) {
                    strncpy(cfg.wifi_password, line + 4, sizeof(cfg.wifi_password) - 1);
                    cfg.wifi_password[sizeof(cfg.wifi_password) - 1] = '\0';
                }
            }
            (void)f_close(&fil);
            wifi_loaded = true;
        }
    }

    /* Server */
    {
        FIL fil;
        if (f_open(&fil, UI_SERVER_CFG_FILE, FA_READ) == FR_OK) {
            char line[160];
            while (f_gets(line, sizeof(line), &fil)) {
                cfg_rstrip(line);
                if (strncmp(line, "IP=", 3) == 0) {
                    strncpy(cfg.server_ip, line + 3, sizeof(cfg.server_ip) - 1);
                    cfg.server_ip[sizeof(cfg.server_ip) - 1] = '\0';
                } else if (strncmp(line, "PORT=", 5) == 0) {
                    uint32_t v;
                    if (cfg_parse_u32_relaxed(line + 5, &v) && v > 0U && v <= 65535U) {
                        cfg.server_port = (uint16_t)v;
                    }
                } else if (strncmp(line, "ID=", 3) == 0) {
                    strncpy(cfg.node_id, line + 3, sizeof(cfg.node_id) - 1);
                    cfg.node_id[sizeof(cfg.node_id) - 1] = '\0';
                } else if (strncmp(line, "LOC=", 4) == 0) {
                    strncpy(cfg.node_location, line + 4, sizeof(cfg.node_location) - 1);
                    cfg.node_location[sizeof(cfg.node_location) - 1] = '\0';
                }
            }
            (void)f_close(&fil);
            server_loaded = true;
        }
    }

    if (wifi_loaded && server_loaded) {
        ESP_Config_Apply(&cfg);
    } else {
        ESP_Log("[ESP_CFG] SD config incomplete: wifi=%u server=%u\r\n",
                wifi_loaded ? 1U : 0U,
                server_loaded ? 1U : 0U);
    }
    (void)ESP_UploadMode_LoadFromSD();
    return (wifi_loaded && server_loaded);
}

static bool ESP_Config_IsValidForLink(const SystemConfig_t *cfg)
{
    if (cfg == NULL) return false;
    if (cfg->wifi_ssid[0] == '\0') return false;
    if (cfg->server_ip[0] == '\0') return false;
    if (cfg->server_port == 0U) return false;
    if (cfg->node_id[0] == '\0') return false;
    return true;
}

bool ESP_Config_LoadRuntimeFromSD(void)
{
    bool cfg_ok;
    bool comm_ok;
    const SystemConfig_t *cfg;

    ESP_LoadConfig();
    cfg_ok = ESP_Config_LoadFromSD_UIFiles();
    comm_ok = ESP_CommParams_LoadFromSD();
    (void)ESP_UploadMode_LoadFromSD();

    cfg = ESP_Config_Get();
    if (!ESP_Config_IsValidForLink(cfg)) {
        ESP_Log("[ESP_CFG] runtime SD load invalid: cfg_ok=%u ssid=%u host=%u port=%u node=%u\r\n",
                cfg_ok ? 1U : 0U,
                (cfg && cfg->wifi_ssid[0]) ? 1U : 0U,
                (cfg && cfg->server_ip[0]) ? 1U : 0U,
                (cfg && cfg->server_port) ? 1U : 0U,
                (cfg && cfg->node_id[0]) ? 1U : 0U);
        return false;
    }
    if (!cfg_ok) {
        ESP_Log("[ESP_CFG] SD UI config incomplete; using current runtime link config.\r\n");
    }

    if (!comm_ok) {
        ESP_Log("[ESP_CFG] ui_param.cfg missing/unreadable; using runtime/default comm params.\r\n");
    }
    return true;
}

/* Utility functions. */

/* Debug console uses USART1 only; ESP32 transport is SPI. */
static UART_HandleTypeDef *ESP_GetLogUart(void)
{
#if (ESP_LOG_UART_PORT == 1)
    extern UART_HandleTypeDef huart1;
    return &huart1;
#else
    return NULL;
#endif
}

/* ================= 核心代码 ================= */

/**
 * @brief  EdgeComm 初始化主流程
 */
#if 0
/* 通用波形生成与FFT计算函数（已停用：改为 ADS131A04 真实采样） */
static void Process_Channel_Data(int ch_id, float base_dc, float ripple_amp, float noise_level)
{
    static float time_t = 0.0f;
    float noise;
    int i;

    // 模拟瞬时值波动 (低频慢变化)
    node_channels[ch_id].current_value = base_dc + ripple_amp * 0.1f * arm_sin_f32(2.0f * PI * 0.5f * time_t);

    // 1. 生成波形 (50Hz + 150Hz谐波 + 噪声)
    for (i = 0; i < WAVEFORM_POINTS; i++)
    {
        // 基础噪声
        noise = ((float)(rand() % 100) / 100.0f - 0.5f) * noise_level;

        // 漏电流特殊处理：加一些随机尖峰
        if (ch_id == 3 && (rand() % 100) > 98)
        {
            noise += 5.0f; // 突发漏电尖峰
        }

        // 信号组合：直流基准 + 50Hz纹波 + 150Hz谐波 + 噪声
        float phase = (float)ch_id * 0.5f; // 不同通道相位错开

        node_channels[ch_id].waveform[i] = node_channels[ch_id].current_value + ripple_amp * arm_sin_f32(2.0f * PI * 50.0f * ((float)i * 0.0005f) + phase) + (ripple_amp * 0.3f) * arm_sin_f32(2.0f * PI * 150.0f * ((float)i * 0.0005f)) + noise;
    }

    // 2. FFT 计算 (使用 CMSIS-DSP 库)
    arm_rfft_fast_f32(&S, node_channels[ch_id].waveform, fft_output_buf, 0);
    arm_cmplx_mag_f32(fft_output_buf, fft_mag_buf, WAVEFORM_POINTS / 2);

    // 3. 填充 FFT (归一化处理)
    node_channels[ch_id].fft_data[0] = 0; // 去直流分量
    for (i = 1; i < FFT_POINTS; i++)
    {
        // 归一化公式：幅度 = (FFT模值 / (N/2)) * 2
        node_channels[ch_id].fft_data[i] = (fft_mag_buf[i] / (float)(WAVEFORM_POINTS / 2)) * 2.0f;
    }

    // 4. 恢复波形 (因为 arm_rfft_fast_f32 是 In-Place 运算，会破坏原 buffer)
    // 这里为了 JSON 发送原始波形，需要重新生成一遍
    double sum = 0.0;
    for (i = 0; i < WAVEFORM_POINTS; i++)
    {
        float phase = (float)ch_id * 0.5f;
        float y = node_channels[ch_id].current_value +
                  ripple_amp * arm_sin_f32(2.0f * PI * 50.0f * ((float)i * 0.0005f) + phase) +
                  (ripple_amp * 0.3f) * arm_sin_f32(2.0f * PI * 150.0f * ((float)i * 0.0005f));
        node_channels[ch_id].waveform[i] = y;
        sum += (double)y;
    }

    /* 卡片显示值改为“本帧波形平均值（mean）”
     * - 之前 current_value 是模拟瞬时值（带慢变化），不是严格平均值
     * - 这里用 mean 更符合“平均值”语义，并且对噪声/非整周期截断更鲁棒 */
    node_channels[ch_id].current_value = ESP_SafeFloat((float)(sum / (double)WAVEFORM_POINTS));

    if (ch_id == 3)
        time_t += 0.05f; // 时间步进
}
#endif

void ESP_Update_Data_And_FFT(void)
{
    static uint32_t last_calc_tick = 0;
    static uint32_t last_dsp_log_tick = 0;
    static uint8_t last_ready = 0;
    uint32_t min_itv = ESP_ALGO_MIN_INTERVAL_MS;
    uint32_t now = HAL_GetTick();

    ESP_Init_Channels_And_DSP();

    uint8_t ready = 0;
    float (*src)[WAVEFORM_POINTS] = NULL;
    if (ADS131A04_flag == 1 && last_ready != 1)
    {
        ready = 1;
        src = ADSA_B;
    }
    else if (ADS131A04_flag2 == 2 && last_ready != 2)
    {
        ready = 2;
        src = ADSA_B2;
    }
    else
    {
        return;
    }

    if (min_itv > 0u)
    {
        if ((now - last_calc_tick) < min_itv)
        {
            return;
        }
    }
    last_calc_tick = now;

    double sum0 = 0.0, sum1 = 0.0, sum2 = 0.0, sum3 = 0.0;
    for (int i = 0; i < WAVEFORM_POINTS; i++)
    {
        float v0 = src[0][i];
        float v1 = src[1][i];
        float v2 = src[2][i];
        float v3 = src[3][i];
#if (ESP_PRINT_WAVEFORM_POINTS)
        /* 瞬时值(每点)：默认每点都打；可通过 ESP_PRINT_POINT_STEP 降频 */
        if ((ESP_PRINT_POINT_STEP <= 1) || ((i % ESP_PRINT_POINT_STEP) == 0))
        {
            printf("%f,%f,%f,%f\r\n", (double)v0, (double)v1, (double)v2, (double)v3);
        }
#endif
        node_channels[0].waveform[i] = v0;
        node_channels[1].waveform[i] = v1;
        node_channels[2].waveform[i] = v2;
        node_channels[3].waveform[i] = v3;
        sum0 += (double)v0;
        sum1 += (double)v1;
        sum2 += (double)v2;
        sum3 += (double)v3;
        /* 每 1024 点让出 CPU，避免长时间占用导致 UI 卡死（4096 点波形） */
        if ((i + 1) % 1024 == 0)
            ESP_RtosYield();
    }

    node_channels[0].current_value = ESP_SafeFloat((float)(sum0 / (double)WAVEFORM_POINTS));
    node_channels[1].current_value = ESP_SafeFloat((float)(sum1 / (double)WAVEFORM_POINTS));
    node_channels[2].current_value = ESP_SafeFloat((float)(sum2 / (double)WAVEFORM_POINTS));
    node_channels[3].current_value = ESP_SafeFloat((float)(sum3 / (double)WAVEFORM_POINTS));

    for (int ch = 0; ch < 4; ch++)
    {
        memcpy(fft_input_buf, node_channels[ch].waveform, sizeof(fft_input_buf));
        arm_rfft_fast_f32(&S, fft_input_buf, fft_output_buf, 0);
        arm_cmplx_mag_f32(fft_output_buf, fft_mag_buf, WAVEFORM_POINTS / 2);
        node_channels[ch].fft_data[0] = 0;
        for (int i = 1; i < FFT_POINTS; i++)
        {
            node_channels[ch].fft_data[i] = (fft_mag_buf[i] / (float)(WAVEFORM_POINTS / 2)) * 2.0f;
        }
        /* 每通道 FFT 完成后让出 CPU，避免 4 通道连续计算导致 UI 卡死 */
        ESP_RtosYield();
    }

    last_ready = ready;
    ESP_UploadSnapshot_PublishFromNodeChannels(ready);
    if ((HAL_GetTick() - last_dsp_log_tick) >= 2000U)
    {
        last_dsp_log_tick = HAL_GetTick();
        ESP_Log("[DSP] snapshot seq=%lu pub=%lu drop=%lu ready=%u dt=%lums\r\n",
                (unsigned long)g_upload_snapshot_seq,
                (unsigned long)g_upload_snapshot_publish_count,
                (unsigned long)g_upload_snapshot_drop_count,
                (unsigned int)ready,
                (unsigned long)(HAL_GetTick() - now));
    }
}

static void StrTrimInPlace(char *s)
{
    if (!s)
        return;
    // trim left
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    // trim right
    size_t n = strlen(s);
    while (n > 0)
    {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            s[n - 1] = 0;
            n--;
        }
        else
            break;
    }
}

static void ESP_SPI_FullArmFrames(uint16_t frames)
{
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    uint32_t next;

    if (frames == 0U) {
        frames = 1U;
    }
    next = (uint32_t)g_spi_full_manual_frames + (uint32_t)frames;
    if (next > 1000U) {
        next = 1000U;
    }
    g_spi_full_manual_frames = (uint16_t)next;
    ESP_SetServerReportMode(1U);
    ESP_Log("[ESP32SPI] full armed: pending=%u continuous=%u\r\n",
            (unsigned int)g_spi_full_manual_frames,
            (unsigned int)g_spi_full_continuous);
#else
    (void)frames;
    ESP_Log("[ESP32SPI] full upload is not compiled in this build.\r\n");
#endif
}

static void ESP_SPI_FullSetContinuous(uint8_t enable)
{
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    g_spi_full_continuous = enable ? 1U : 0U;
    if (g_spi_full_continuous) {
        ESP_SetServerReportMode(1U);
    } else {
        g_spi_full_manual_frames = 0U;
        ESP_SetServerReportMode(0U);
    }
    ESP_Log("[ESP32SPI] full continuous=%u pending=%u\r\n",
            (unsigned int)g_spi_full_continuous,
            (unsigned int)g_spi_full_manual_frames);
#else
    (void)enable;
    ESP_Log("[ESP32SPI] full upload is not compiled in this build.\r\n");
#endif
}

static void ESP_SPI_FullPrintStatus(void)
{
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    ESP_Log("[ESP32SPI] full status: requested=%u continuous=%u pending=%u reporting=%u ready=%u\r\n",
            (unsigned int)g_server_report_full,
            (unsigned int)g_spi_full_continuous,
            (unsigned int)g_spi_full_manual_frames,
            (unsigned int)ESP_UI_IsReporting(),
            (unsigned int)g_esp_ready);
    if (g_spi_full_waiting_result != 0U) {
        ESP_Log("[ESP32SPI] full waiting: frame=%lu ref=%lu elapsed=%lums\r\n",
                (unsigned long)g_spi_full_result_frame_id,
                (unsigned long)g_spi_full_result_ref_seq,
                (unsigned long)(HAL_GetTick() - g_spi_full_result_start_tick));
    }
#else
    ESP_Log("[ESP32SPI] full upload is not compiled in this build.\r\n");
#endif
}

static void ESP_SetFaultCode(const char *code)
{
    if (!code)
        return;
    char c0 = code[0];
    char c1 = code[1];
    char c2 = code[2];
    if (c0 == 'e')
        c0 = 'E'; // 兼容小写
    if (c0 != 'E')
        return;
    if (c1 < '0' || c1 > '9')
        return;
    if (c2 < '0' || c2 > '9')
        return;
    g_fault_code[0] = 'E';
    g_fault_code[1] = c1;
    g_fault_code[2] = c2;
    g_fault_code[3] = 0;
    ESP_Log("[控制台] 故障码已切换为: %s（下一次上报生效）\r\n", g_fault_code);
}

static void ESP_Console_HandleLine(char *line)
{
    if (!line)
        return;
    StrTrimInPlace(line);
    if (line[0] == 0)
        return;

    if (strcmp(line, "spiping") == 0 || strcmp(line, "SPIPING") == 0)
    {
        ESP32_SPI_DebugRunPing();
        return;
    }

    if (strcmp(line, "wifi") == 0 || strcmp(line, "WIFI") == 0)
    {
        (void)ESP_UI_SendCmd(ESP_UI_CMD_WIFI);
        ESP_Log("[Console] queued WIFI step\r\n");
        return;
    }

    if (strcmp(line, "tcp") == 0 || strcmp(line, "TCP") == 0)
    {
        (void)ESP_UI_SendCmd(ESP_UI_CMD_TCP);
        ESP_Log("[Console] queued TCP/CLOUD step\r\n");
        return;
    }

    if (strcmp(line, "reg") == 0 || strcmp(line, "REG") == 0)
    {
        (void)ESP_UI_SendCmd(ESP_UI_CMD_REG);
        ESP_Log("[Console] queued REG step\r\n");
        return;
    }

    if (strcmp(line, "report") == 0 || strcmp(line, "REPORT") == 0)
    {
        (void)ESP_UI_SendCmd(ESP_UI_CMD_REPORT_TOGGLE);
        ESP_Log("[Console] queued REPORT toggle\r\n");
        return;
    }

    if (strcmp(line, "auto") == 0 || strcmp(line, "AUTO") == 0)
    {
        (void)ESP_UI_SendCmd(ESP_UI_CMD_AUTO_CONNECT);
        ESP_Log("[Console] queued AUTO connect/report sequence\r\n");
        return;
    }

    if (strcmp(line, "full1") == 0 || strcmp(line, "FULL1") == 0)
    {
        ESP_SPI_FullArmFrames(1U);
        return;
    }

    if (strcmp(line, "full10") == 0 || strcmp(line, "FULL10") == 0)
    {
        ESP_SPI_FullArmFrames(10U);
        return;
    }

    if (strcmp(line, "fullon") == 0 || strcmp(line, "FULLON") == 0)
    {
        ESP_SPI_FullSetContinuous(1U);
        return;
    }

    if (strcmp(line, "fulloff") == 0 || strcmp(line, "FULLOFF") == 0)
    {
        ESP_SPI_FullSetContinuous(0U);
        return;
    }

    {
        ESP_SPI_FullPrintStatus();
        return;
    }

    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0)
    {
        ESP_Log("[控制台] 可用命令：\r\n");
        ESP_Log("  - auto/wifi/tcp/reg/report : SPI cloud upload control\r\n");
        ESP_Log("  - spiping/fullstat/full1/full10/fullon/fulloff : SPI diagnostics\r\n");
        ESP_Log("  - E00/E01/E02... ：切换上报故障码\r\n");
        ESP_Log("  - help 或 ?      ：显示帮助\r\n");
        return;
    }

    // 格式: E01
    if ((line[0] == 'E' || line[0] == 'e') && strlen(line) == 3)
    {
        ESP_SetFaultCode(line);
        return;
    }

    // 格式: fault E01
    if (strncmp(line, "fault", 5) == 0)
    {
        char *p = line + 5;
        while (*p == ' ' || *p == '\t')
            p++;
        if (strlen(p) == 3 && (p[0] == 'E' || p[0] == 'e'))
        {
            ESP_SetFaultCode(p);
            return;
        }
    }

    ESP_Log("[控制台] 未识别命令: %s（输入 help 查看帮助）\r\n", line);
}

static void ESP_SetServerReportMode(uint8_t full)
{
    full = (full != 0U) ? 1U : 0U;
    /* Treat every cloud command as an apply request.  After boot the runtime
       mode may come from SD while this command-cache variable is still at its
       compile-time default, so comparing only against g_server_report_full can
       incorrectly drop idempotent-looking commands. */
    g_server_report_full = full;
    g_server_report_full_dirty = 1U;
}

static void ESP_SPI_ResetLocalReportState(const char *reason)
{
    uint8_t want_report = (g_report_enabled != 0U) ? 1U : 0U;
    g_esp_ready = 0U;
    g_ui_reg_ok = 0U;
    g_ui_tcp_ok = 0U;
    g_ui_wifi_ok = 0U;
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    g_spi_full_manual_frames = 0U;
    ESP_SPI_FullResetUploadRuntimeState();
#endif
    if (want_report != 0U) {
        g_ui_auto_recover_want_report = 1U;
        (void)ESP_AutoReconnect_SetLastReporting(true);
        ESP_Log("[ESP32SPI] local report path degraded, keep desired reporting and recover: %s\r\n",
                (reason != NULL) ? reason : "unspecified");
        ESP_UI_ScheduleAutoRecover(reason, 1U);
    } else {
        g_report_enabled = 0U;
        ESP_SetServerReportMode(0U);
        (void)ESP_AutoReconnect_SetLastReporting(false);
        ESP_Log("[ESP32SPI] local report state reset: %s\r\n",
                (reason != NULL) ? reason : "unspecified");
    }
}

#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
static void ESP_SPI_FullClearWaitState(void)
{
    g_spi_full_waiting_result = 0U;
    g_spi_full_result_ref_seq = 0U;
    g_spi_full_result_frame_id = 0U;
    g_spi_full_result_start_tick = 0U;
    g_spi_full_result_session_epoch = 0U;
    g_spi_full_result_last_log_tick = 0U;
    g_spi_full_result_last_poll_tick = 0U;
}

static void ESP_SPI_FullResetUploadRuntimeState(void)
{
    ESP_SPI_FullClearWaitState();
    g_spi_full_holdoff_until_tick = 0U;
    g_spi_full_timeout_count = 0U;
    g_spi_full_busy_nack_count = 0U;
    g_spi_full_http_fail_count = 0U;
    ESP_FullTx_ClearAndRelease();
}

static void ESP_SPI_FullEnterHoldoff(const char *reason, uint32_t holdoff_ms)
{
    uint32_t now = HAL_GetTick();

    if (holdoff_ms == 0U)
    {
        holdoff_ms = ESP32_SPI_FULL_BUSY_HOLDOFF_MS;
    }
    if (holdoff_ms > ESP32_SPI_FULL_MAX_HOLDOFF_MS)
    {
        holdoff_ms = ESP32_SPI_FULL_MAX_HOLDOFF_MS;
    }
    g_spi_full_holdoff_until_tick = now + holdoff_ms;
    ESP_Log("[ESP32SPI] full recovery holdoff=%lums reason=%s timeout=%lu busy=%lu httpfail=%lu\r\n",
            (unsigned long)holdoff_ms,
            (reason != NULL) ? reason : "unknown",
            (unsigned long)g_spi_full_timeout_count,
            (unsigned long)g_spi_full_busy_nack_count,
            (unsigned long)g_spi_full_http_fail_count);
    (void)ESP32_SPI_PollEvents(50U);
}

static bool ESP_SPI_FullHoldoffActive(uint32_t now_tick)
{
    if (g_spi_full_holdoff_until_tick == 0U)
    {
        return false;
    }
    if ((int32_t)(now_tick - g_spi_full_holdoff_until_tick) < 0)
    {
        return true;
    }
    g_spi_full_holdoff_until_tick = 0U;
    ESP_Log("[ESP32SPI] full recovery holdoff ended\r\n");
    return false;
}

static uint8_t ESP_SPI_FullControlBusy(void)
{
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    if (g_spi_full_holdoff_until_tick != 0U) {
        (void)ESP_SPI_FullHoldoffActive(HAL_GetTick());
    }
#endif
    return (g_full_tx_sm.active != 0U ||
            g_spi_full_waiting_result != 0U ||
            g_spi_full_holdoff_until_tick != 0U) ? 1U : 0U;
}

static void ESP_SPI_QueueCommParamsSync(const ESP_CommParams_t *p)
{
    if (p == NULL) {
        return;
    }
    taskENTER_CRITICAL();
    g_spi_pending_comm_params = *p;
    g_spi_comm_params_sync_pending = 1U;
    taskEXIT_CRITICAL();
}

static void ESP_SPI_QueueReportModeSync(uint8_t full)
{
    taskENTER_CRITICAL();
    g_spi_report_mode_sync_target = (full != 0U) ? 1U : 0U;
    g_spi_report_mode_sync_pending = 1U;
    taskEXIT_CRITICAL();
}

static void ESP_SPI_ServiceDeferredSync(void)
{
    uint8_t do_comm_sync = 0U;
    uint8_t do_report_sync = 0U;
    uint8_t report_target = 0U;
    ESP_CommParams_t pending_comm;

    if (!g_esp_ready || ESP_SPI_FullControlBusy()) {
        return;
    }

    taskENTER_CRITICAL();
    if (g_spi_comm_params_sync_pending != 0U) {
        pending_comm = g_spi_pending_comm_params;
        g_spi_comm_params_sync_pending = 0U;
        do_comm_sync = 1U;
    }
    if (g_spi_report_mode_sync_pending != 0U) {
        report_target = g_spi_report_mode_sync_target;
        g_spi_report_mode_sync_pending = 0U;
        do_report_sync = 1U;
    }
    taskEXIT_CRITICAL();

    if (do_comm_sync != 0U) {
        if (!ESP32_SPI_ApplyCommParams(pending_comm.heartbeat_ms,
                                       pending_comm.min_interval_ms,
                                       pending_comm.http_timeout_ms,
                                       3000U,
                                       pending_comm.wave_step,
                                       pending_comm.upload_points,
                                       pending_comm.hardreset_sec,
                                       pending_comm.chunk_kb,
                                       pending_comm.chunk_delay_ms)) {
            ESP_Log("[ESP32SPI] deferred comm params sync failed, retry later\r\n");
            ESP_SPI_QueueCommParamsSync(&pending_comm);
            ESP_SPI_FullEnterHoldoff("deferred comm sync failed",
                                     ESP32_SPI_FULL_BUSY_HOLDOFF_MS);
            return;
        }
    }

    if (do_report_sync != 0U && g_report_enabled != 0U) {
        if (g_spi_full_waiting_result == 0U) {
            ESP_SPI_FullResetUploadRuntimeState();
        }
        if (!ESP32_SPI_StartReport(report_target, 3000U)) {
            ESP_Log("[ESP32SPI] deferred report mode sync failed, retry later\r\n");
            ESP_SPI_QueueReportModeSync(report_target);
            ESP_SPI_FullEnterHoldoff("deferred report sync failed",
                                     ESP32_SPI_FULL_BUSY_HOLDOFF_MS);
            return;
        }
    }
}
#endif

static void ESP_SetServerDownsampleStep(uint32_t step)
{
    if (step < 1U) step = 1U;
    if (step > 64U) step = 64U;
    /* Always apply a cloud command.  The current runtime value is authoritative
       and may differ from this command-cache after SD restore. */
    g_server_downsample_step = step;
    g_server_downsample_dirty = 1U;
}

static bool ESP_TryParseDownsampleStep(const char *s, uint32_t *out_step)
{
    if (!s || !out_step) {
        return false;
    }

    const char *p = strstr(s, "\"downsample_step\"");
    if (!p) {
        return false;
    }
    p += strlen("\"downsample_step\"");
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p < '0' || *p > '9') {
        return false;
    }

    uint32_t step = 0;
    while (*p >= '0' && *p <= '9')
    {
        step = (step * 10U) + (uint32_t)(*p - '0');
        if (step > 9999U) {
            break;
        }
        p++;
    }

    if (step < 1U) step = 1U;
    if (step > 64U) step = 64U;
    *out_step = step;
    return true;
}

static void ESP_SetServerUploadPoints(uint32_t points)
{
    uint32_t maxp = (uint32_t)WAVEFORM_POINTS;
    if (maxp == 0u) {
        points = 0u;
    } else if (maxp < 256u) {
        points = maxp;
    } else {
        if (points < 256u) points = 256u;
        if (points > maxp) points = maxp;
        /* 容错：非 256 步进时就近取整到 256 倍数 */
        points = ((points + 128u) / 256u) * 256u;
        if (points < 256u) points = 256u;
        if (points > maxp) points = maxp;
    }

    /* Always apply a cloud command.  This fixes the 2048 -> 4096 case after
       reboot/SD restore: g_comm_upload_points can be 2048 while this cache is
       still the default 4096, and the old comparison dropped the command. */
    g_server_upload_points = points;
    g_server_upload_points_dirty = 1U;
}

static bool ESP_TryParseUploadPoints(const char *s, uint32_t *out_points)
{
    if (!s || !out_points) {
        return false;
    }

    const char *p = strstr(s, "\"upload_points\"");
    if (!p) {
        return false;
    }
    p += strlen("\"upload_points\"");
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p < '0' || *p > '9') {
        return false;
    }

    uint32_t points = 0;
    while (*p >= '0' && *p <= '9')
    {
        points = (points * 10U) + (uint32_t)(*p - '0');
        if (points > 10000000U) {
            break;
        }
        p++;
    }

    *out_points = points;
    return true;
}

static void ESP_SPI_ApplyCommParamsToCoprocessor(const ESP_CommParams_t *p)
{
    if (!p || !g_esp_ready) {
        return;
    }
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    if (ESP_SPI_FullControlBusy()) {
        ESP_SPI_QueueCommParamsSync(p);
        return;
    }
#endif
    if (!ESP32_SPI_ApplyCommParams(p->heartbeat_ms,
                                   p->min_interval_ms,
                                   p->http_timeout_ms,
                                   3000U,
                                   p->wave_step,
                                   p->upload_points,
                                   p->hardreset_sec,
                                   p->chunk_kb,
                                   p->chunk_delay_ms)) {
        ESP_Log("[ESP32SPI] comm params sync failed\r\n");
    }
}

void ESP_Console_Init(void)
{
#if (ESP_CONSOLE_ENABLE)
    UART_HandleTypeDef *hu = ESP_GetLogUart(); // 默认 USART1
    if (!hu)
        return;

    // 启动 1 字节 RX 中断（比主循环轮询稳定，避免“输入了但没反应”）
    HAL_UART_Receive_IT(hu, &g_console_rx_byte, 1);
    ESP_Log("[控制台] 已启用：输入 E01 回车注入故障；输入 E00 回车清除。\r\n");
#endif
}

/**
 * @brief  控制台轮询任务
 * @note   在主循环中调用，处理用户输入和服务器指令
 */
void ESP_Console_Poll(void)
{
#if (ESP_CONSOLE_ENABLE)
    uint32_t now = HAL_GetTick();
    uint8_t server_cmd_seen = 0U;
    uint8_t server_cmd_saved = 0U;
    uint8_t server_cmd_save_failed = 0U;
    uint8_t server_cmd_applied = 0U;
    static char server_cmd_summary[192];

    server_cmd_summary[0] = '\0';
    ESP_ServerCommandLogRestore(server_cmd_summary,
                                sizeof(server_cmd_summary),
                                &server_cmd_seen,
                                &server_cmd_saved,
                                &server_cmd_save_failed,
                                &server_cmd_applied);
    // 1) 处理“已收到的一整行”控制台指令
    if (g_console_line_ready)
    {
        g_console_line_ready = 0;
        g_console_line[g_console_line_len] = 0;
        ESP_Console_HandleLine(g_console_line);
        g_console_line_len = 0;
    }

    {
        static uint32_t s_last_async_event_poll_tick = 0U;
        uint8_t can_apply_server_cmd = 1U;
        uint8_t reset = 0U, has_mode = 0U, full = 0U;
        uint8_t has_downsample = 0U, has_upload = 0U;
        uint8_t has_hb = 0U, has_min = 0U, has_http = 0U, has_chunk = 0U, has_chunk_delay = 0U;
        uint32_t downsample = 0U, upload_points = 0U;
        uint32_t hb_ms = 0U, min_ms = 0U, http_ms = 0U, chunk_kb = 0U, chunk_delay_ms = 0U;

        /* Async cloud events are delivered only when STM32 clocks the SPI link.
           In summary/idle periods there may be no full-frame result wait loop,
           so server commands can otherwise sit inside the ESP32 event queue for
           minutes.  Poll a lightweight NOOP regularly whenever the link is idle
           enough, so report_mode/upload_points/downsample commands become
           visible quickly in every operating mode. */
        if (g_esp_ready != 0U &&
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
            !ESP_SPI_FullControlBusy() &&
#endif
            ((now - s_last_async_event_poll_tick) >= 100U)) {
            s_last_async_event_poll_tick = now;
            (void)ESP32_SPI_PollEvents(10U);
        }

#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
        can_apply_server_cmd = (ESP_SPI_FullControlBusy() == 0U) ? 1U : 0U;
#endif

        if (server_cmd_seen == 0U &&
            can_apply_server_cmd != 0U &&
            ESP32_SPI_ConsumeServerCommand(&reset,
                                           &has_mode,
                                           &full,
                                           &has_downsample,
                                           &downsample,
                                           &has_upload,
                                           &upload_points,
                                           &has_hb,
                                           &hb_ms,
                                           &has_min,
                                           &min_ms,
                                           &has_http,
                                           &http_ms,
                                            &has_chunk,
                                            &chunk_kb,
                                           &has_chunk_delay,
                                           &chunk_delay_ms)) {
            ESP_BuildServerCommandSummary(server_cmd_summary,
                                          sizeof(server_cmd_summary),
                                          reset,
                                          has_mode,
                                          full,
                                          has_downsample,
                                          downsample,
                                          has_upload,
                                          upload_points,
                                          has_hb,
                                          hb_ms,
                                          has_min,
                                          min_ms,
                                          has_http,
                                          http_ms,
                                          has_chunk,
                                          chunk_kb,
                                          has_chunk_delay,
                                          chunk_delay_ms);
            if (!ESP_ServerCommandRecentlyApplied(server_cmd_summary, now)) {
                server_cmd_seen = 1U;
                ESP_ServerCommandLogStore(server_cmd_summary, server_cmd_seen, 0U, 0U, 0U);
                if (reset) {
                    g_server_reset_pending = 1U;
                }
                if (has_mode) {
                    ESP_SetServerReportMode(full);
                }
                if (has_downsample) {
                    ESP_SetServerDownsampleStep(downsample);
                }
                if (has_upload) {
                    ESP_SetServerUploadPoints(upload_points);
                }
                if (has_hb || has_min || has_http || has_chunk || has_chunk_delay) {
                    ESP_CommParams_t old_p;
                    ESP_CommParams_t p;
                    ESP_CommParams_Get(&old_p);
                    p = old_p;
                    if (has_hb) {
                        p.heartbeat_ms = hb_ms;
                    }
                    if (has_min) {
                        p.min_interval_ms = min_ms;
                    }
                    if (has_http) {
                        p.http_timeout_ms = http_ms;
                    }
                    if (has_chunk) {
                        p.chunk_kb = chunk_kb;
                    }
                    if (has_chunk_delay) {
                        p.chunk_delay_ms = chunk_delay_ms;
                    }
                    bool params_changed = !ESP_CommParams_Equals(&old_p, &p);
                    if (params_changed) {
                        ESP_CommParams_Apply(&p);
                        ESP_CommParams_Get(&p);
                        if (ESP_CommParams_SaveToSD()) {
                            server_cmd_saved = 1U;
                            ESP_SPI_ApplyCommParamsToCoprocessor(&p);
                            server_cmd_applied = 1U;
                        } else {
                            ESP_CommParams_Apply(&old_p);
                            server_cmd_save_failed = 1U;
                        }
                    } else {
                        ESP_SPI_ApplyCommParamsToCoprocessor(&p);
                        server_cmd_applied = 1U;
                    }
                }
            }
        }
    }

#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    if (ESP_SPI_FullControlBusy()) {
        ESP_ServerCommandLogStore(server_cmd_summary,
                                  server_cmd_seen,
                                  server_cmd_saved,
                                  server_cmd_save_failed,
                                  server_cmd_applied);
        return;
    }
#endif

    // 2) 处理“服务器下发 reset”指令
    if (g_server_reset_pending)
    {
        g_server_reset_pending = 0;
        ESP_SetFaultCode("E00");
        server_cmd_applied = 1U;
    }

    // 2.0) Apply server-issued report_mode.
    if (g_server_report_full_dirty)
    {
        uint8_t full = g_server_report_full ? 1U : 0U;
        const char *mode_text = full ? "full" : "summary";
        uint8_t mode_persist_changed = 1U;
        g_server_report_full_dirty = 0;
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
        mode_persist_changed = (g_spi_full_continuous != full) ? 1U : 0U;
#endif
        if (mode_persist_changed != 0U) {
            if (!ESP_UploadMode_SaveToSD(full)) {
                server_cmd_save_failed = 1U;
                if (server_cmd_seen != 0U) {
                    ESP_Log("[SERVER_CMD] save_failed: %s\r\n", server_cmd_summary);
                }
            } else {
                server_cmd_saved = 1U;
            }
        }
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
        g_spi_full_continuous = full;
        if (!full) {
            g_spi_full_manual_frames = 0U;
            if (g_spi_full_waiting_result == 0U && g_full_tx_sm.active == 0U) {
                ESP_SPI_FullResetUploadRuntimeState();
            }
        }
#endif
        /* The final clean status log is emitted below.  Avoid an extra
         * user-visible UI log here.
         */
        if (g_esp_ready && g_report_enabled) {
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
            if (ESP_SPI_FullControlBusy()) {
                ESP_SPI_QueueReportModeSync(full);
            } else {
                if (g_spi_full_waiting_result == 0U) {
                    ESP_SPI_FullResetUploadRuntimeState();
                }
                if (!ESP32_SPI_StartReport(full, 3000U)) {
                    ESP_Log("[ESP32SPI] report mode sync failed: %s\r\n", mode_text);
                }
            }
#else
            if (!ESP32_SPI_StartReport(full, 3000U)) {
                ESP_Log("[ESP32SPI] report mode sync failed: %s\r\n", mode_text);
            }
#endif
        }
        server_cmd_applied = 1U;
    }

    // 2.1) 处理“服务器下发 downsample_step”指令
    if (g_server_downsample_dirty)
    {
        g_server_downsample_dirty = 0;

        ESP_CommParams_t old_p;
        ESP_CommParams_t p;
        ESP_CommParams_Get(&old_p);
        p = old_p;
        uint32_t target = (uint32_t)g_server_downsample_step;
        if (target < 1U) target = 1U;
        if (target > 64U) target = 64U;

        bool params_changed = (p.wave_step != target);
        if (p.wave_step != target) {
            p.wave_step = target;
            ESP_CommParams_Apply(&p);
        }

        if (params_changed) {
            if (ESP_CommParams_SaveToSD()) {
                server_cmd_saved = 1U;
                ESP_SPI_ApplyCommParamsToCoprocessor(&p);
                server_cmd_applied = 1U;
            } else {
                ESP_CommParams_Apply(&old_p);
                server_cmd_save_failed = 1U;
            }
        } else {
            ESP_SPI_ApplyCommParamsToCoprocessor(&p);
            server_cmd_applied = 1U;
        }
    }

    // 2.2) 处理“服务器下发 upload_points”指令
    if (g_server_upload_points_dirty)
    {
        g_server_upload_points_dirty = 0;

        ESP_CommParams_t old_p;
        ESP_CommParams_t p;
        ESP_CommParams_Get(&old_p);
        p = old_p;
        uint32_t target = (uint32_t)g_server_upload_points;

        bool params_changed = (p.upload_points != target);
        if (p.upload_points != target) {
            p.upload_points = target;
            ESP_CommParams_Apply(&p);
        }

        if (params_changed) {
            if (ESP_CommParams_SaveToSD()) {
                server_cmd_saved = 1U;
                ESP_SPI_ApplyCommParamsToCoprocessor(&p);
                server_cmd_applied = 1U;
            } else {
                ESP_CommParams_Apply(&old_p);
                server_cmd_save_failed = 1U;
            }
        } else {
            ESP_SPI_ApplyCommParamsToCoprocessor(&p);
            server_cmd_applied = 1U;
        }
    }

    if (server_cmd_seen != 0U) {
        if (server_cmd_save_failed != 0U || server_cmd_applied != 0U) {
            ESP_Log("[SERVER_CMD] received: %s\r\n", server_cmd_summary);
        }
        if (server_cmd_save_failed != 0U) {
            ESP_Log("[SERVER_CMD] save_failed: %s\r\n", server_cmd_summary);
            ESP_ServerCommandLogClear();
        } else if (server_cmd_saved != 0U) {
            ESP_Log("[SERVER_CMD] saved: SD\r\n");
        }
        if (server_cmd_save_failed == 0U && server_cmd_applied != 0U) {
            ESP_Log("[SERVER_CMD] applied: %s\r\n", server_cmd_summary);
            ESP_ServerCommandRememberApplied(server_cmd_summary, now);
            ESP_ServerCommandLogClear();
        } else if (server_cmd_save_failed == 0U) {
            ESP_ServerCommandLogStore(server_cmd_summary,
                                      server_cmd_seen,
                                      server_cmd_saved,
                                      server_cmd_save_failed,
                                      server_cmd_applied);
        }
    }


#if (ADS_BADFRAME_MONITOR)
    /* 每 1s 输出一次采样统计（在任务上下文输出，避免在TIM2 ISR里printf影响时序） */
    {
        static uint32_t last_print = 0;
        uint32_t now2 = HAL_GetTick();
        if ((now2 - last_print) >= 1000u)
        {
            last_print = now2;
            static uint32_t last_frames = 0, last_miss = 0;
            uint32_t frames = 0, miss = 0;
            AD7606_GetStats(&frames, &miss);
            uint32_t df = frames - last_frames;
            uint32_t dm = miss - last_miss;
            printf("[AD7606] frames=%lu (+%lu/s) miss=%lu (+%lu/s)\r\n",
                   (unsigned long)frames, (unsigned long)df,
                   (unsigned long)miss, (unsigned long)dm);
            last_frames = frames;
            last_miss = miss;
        }
    }
#endif
#endif
}

#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
static uint16_t ESP_FullTx_EffectiveWaveCount(uint32_t wave_step,
                                              uint32_t upload_points,
                                              uint16_t source_count)
{
    uint32_t step = (wave_step == 0U) ? 1U : wave_step;
    uint32_t limit = upload_points;
    uint32_t available;

    if (source_count == 0U)
    {
        return 0U;
    }
    if (limit == 0U || limit > (uint32_t)WAVEFORM_POINTS)
    {
        limit = (uint32_t)WAVEFORM_POINTS;
    }

    /* Number of samples that can be emitted from the full local window after
       applying downsample_step.  upload_points is a limit for the transmitted
       waveform, not a reason to touch the local DSP/algorithm window. */
    available = (((uint32_t)source_count + step - 1U) / step);
    if (limit > available)
    {
        limit = available;
    }
    if (limit > 65535U)
    {
        limit = 65535U;
    }
    return (uint16_t)limit;
}

static bool ESP_FullTx_StartFrame(uint32_t frame_id,
                                  const ESP_UploadSnapshot_t *snapshot,
                                  uint32_t snapshot_seq,
                                  uint8_t one_shot)
{
    uint8_t channel_count;
    uint32_t wave_step;
    uint32_t upload_points;

    if (snapshot == NULL || snapshot->magic != ESP_UPLOAD_SNAPSHOT_MAGIC)
    {
        return false;
    }
    channel_count = snapshot->channel_count;
    if (channel_count == 0U || channel_count > 4U)
    {
        return false;
    }

    wave_step = ESP_CommParams_WaveStep();
    if (wave_step == 0U)
    {
        wave_step = 1U;
    }
    upload_points = ESP_CommParams_UploadPoints();
    if (upload_points == 0U || upload_points > (uint32_t)WAVEFORM_POINTS)
    {
        upload_points = (uint32_t)WAVEFORM_POINTS;
    }

    memset(&g_full_tx_sm, 0, sizeof(g_full_tx_sm));
    g_full_tx_sm.active = 1U;
    g_full_tx_sm.one_shot = one_shot;
    g_full_tx_sm.channel_count = channel_count;
    g_full_tx_sm.phase = ESP_FULL_TX_BEGIN;
    g_full_tx_sm.frame_id = frame_id;
    g_full_tx_sm.snapshot_seq = snapshot_seq;
    g_full_tx_sm.start_tick = HAL_GetTick();
    g_full_tx_sm.snapshot = snapshot;
    g_full_tx_sm.wave_step = wave_step;
    g_full_tx_sm.upload_points = upload_points;
    for (uint8_t i = 0U; i < channel_count; i++)
    {
        uint16_t source_wave_count;
        uint16_t tx_wave_count;

        if (snapshot->channels[i].waveform_count > WAVEFORM_POINTS ||
            snapshot->channels[i].fft_count > FFT_POINTS)
        {
            memset(&g_full_tx_sm, 0, sizeof(g_full_tx_sm));
            return false;
        }
        source_wave_count = snapshot->channels[i].waveform_count;
        tx_wave_count = ESP_FullTx_EffectiveWaveCount(wave_step,
                                                      upload_points,
                                                      source_wave_count);
        if (tx_wave_count == 0U)
        {
            memset(&g_full_tx_sm, 0, sizeof(g_full_tx_sm));
            return false;
        }
        g_full_tx_sm.channels[i] = snapshot->channels[i];
        g_full_tx_sm.channels[i].waveform_count = tx_wave_count;
        g_full_tx_sm.wave_source_counts[i] = source_wave_count;
        g_full_tx_sm.waves[i] = snapshot->waveform[i];
        g_full_tx_sm.ffts[i] = snapshot->fft_data[i];
    }
    return true;
}

static bool ESP_FullTx_StepPacket(uint32_t timeout_ms)
{
    const ESP_UploadSnapshot_t *snapshot = g_full_tx_sm.snapshot;
    uint8_t ch_idx;
    uint16_t total;
    uint16_t max_elements;
    uint16_t count;
    bool ok;

    if (g_full_tx_sm.active == 0U || snapshot == NULL)
    {
        return true;
    }

    switch (g_full_tx_sm.phase)
    {
    case ESP_FULL_TX_BEGIN:
        ok = ESP32_SPI_ReportFullBegin(g_full_tx_sm.frame_id,
                                        (uint64_t)snapshot->tick_ms,
                                        g_full_tx_sm.wave_step,
                                        g_full_tx_sm.upload_points,
                                        snapshot->fault_code,
                                        snapshot->status_code,
                                        g_full_tx_sm.channels,
                                        g_full_tx_sm.channel_count,
                                        timeout_ms);
        if (!ok)
        {
            return false;
        }
        g_full_tx_sm.packet_count++;
        g_full_tx_sm.channel_index = 0U;
        g_full_tx_sm.element_offset = 0U;
        g_full_tx_sm.phase = ESP_FULL_TX_WAVE;
        return true;

    case ESP_FULL_TX_WAVE:
        ch_idx = g_full_tx_sm.channel_index;
        if (ch_idx >= g_full_tx_sm.channel_count)
        {
            g_full_tx_sm.phase = ESP_FULL_TX_END;
            return true;
        }
        total = g_full_tx_sm.channels[ch_idx].waveform_count;
        if (g_full_tx_sm.element_offset >= total)
        {
            g_full_tx_sm.element_offset = 0U;
            g_full_tx_sm.phase = ESP_FULL_TX_FFT;
            return true;
        }
        max_elements = ESP32_SPI_FullWaveChunkMaxElements();
        if (max_elements == 0U)
        {
            return false;
        }
        count = (uint16_t)(total - g_full_tx_sm.element_offset);
        if (count > max_elements)
        {
            count = max_elements;
        }
        ok = ESP32_SPI_ReportFullWaveChunk(g_full_tx_sm.frame_id,
                                           g_full_tx_sm.channels[ch_idx].channel_id,
                                           g_full_tx_sm.waves[ch_idx],
                                           g_full_tx_sm.element_offset,
                                           count,
                                           g_full_tx_sm.wave_step,
                                           g_full_tx_sm.wave_source_counts[ch_idx],
                                           timeout_ms);
        if (!ok)
        {
            return false;
        }
        g_full_tx_sm.packet_count++;
        g_full_tx_sm.element_offset = (uint16_t)(g_full_tx_sm.element_offset + count);
        if (g_full_tx_sm.element_offset >= total)
        {
            g_full_tx_sm.element_offset = 0U;
            g_full_tx_sm.phase = ESP_FULL_TX_FFT;
        }
        return true;

    case ESP_FULL_TX_FFT:
        ch_idx = g_full_tx_sm.channel_index;
        if (ch_idx >= g_full_tx_sm.channel_count)
        {
            g_full_tx_sm.phase = ESP_FULL_TX_END;
            return true;
        }
        total = g_full_tx_sm.channels[ch_idx].fft_count;
        if (g_full_tx_sm.element_offset >= total)
        {
            g_full_tx_sm.channel_index++;
            g_full_tx_sm.element_offset = 0U;
            g_full_tx_sm.phase = (g_full_tx_sm.channel_index >= g_full_tx_sm.channel_count) ?
                                  ESP_FULL_TX_END : ESP_FULL_TX_WAVE;
            return true;
        }
        max_elements = ESP32_SPI_FullFftChunkMaxElements();
        if (max_elements == 0U)
        {
            return false;
        }
        count = (uint16_t)(total - g_full_tx_sm.element_offset);
        if (count > max_elements)
        {
            count = max_elements;
        }
        ok = ESP32_SPI_ReportFullFftChunk(g_full_tx_sm.frame_id,
                                          g_full_tx_sm.channels[ch_idx].channel_id,
                                          g_full_tx_sm.ffts[ch_idx],
                                          g_full_tx_sm.element_offset,
                                          count,
                                          timeout_ms);
        if (!ok)
        {
            return false;
        }
        g_full_tx_sm.packet_count++;
        g_full_tx_sm.element_offset = (uint16_t)(g_full_tx_sm.element_offset + count);
        if (g_full_tx_sm.element_offset >= total)
        {
            g_full_tx_sm.channel_index++;
            g_full_tx_sm.element_offset = 0U;
            g_full_tx_sm.phase = (g_full_tx_sm.channel_index >= g_full_tx_sm.channel_count) ?
                                  ESP_FULL_TX_END : ESP_FULL_TX_WAVE;
        }
        return true;

    case ESP_FULL_TX_END:
        ok = ESP32_SPI_ReportFullEnd(g_full_tx_sm.frame_id, timeout_ms);
        if (!ok)
        {
            return false;
        }
        g_full_tx_sm.packet_count++;
        g_full_tx_sm.active = 0U;
        return true;

    default:
        return false;
    }
}
#endif

/**
 * @brief  数据发送主函数
 * @note   负责打包 JSON，通过 DMA 发送
 */
void ESP_Post_Summary(void)
{
    if (g_esp_ready == 0)
        return;

#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    ESP_SPI_ServiceDeferredSync();
    /* Full-upload stress mode must not be mixed with summary packets.
       This keeps the 10-minute test as continuous full frames only. */
    if (ESP_ServerReportFull() || g_spi_full_continuous) {
        return;
    }
#endif

    {
    static uint32_t last_spi_send_time = 0;
    static uint32_t tx_try = 0, tx_ok = 0, tx_err = 0;
    static uint32_t last_tx_log = 0;
    static uint32_t s_spi_seq = 0;
    static uint8_t invalid_state_nack_count = 0U;
    uint32_t now_tick = HAL_GetTick();
    uint32_t min_itv = ESP_CommParams_MinIntervalMs();

    if (min_itv && (now_tick - last_spi_send_time < min_itv)) {
        return;
    }
    last_spi_send_time = now_tick;

    esp32_spi_report_channel_t ch[4];
    for (uint8_t i = 0; i < 4U; i++) {
        int32_t cv_i = ESP_FloatToI32Scaled(node_channels[i].current_value);
        ch[i].channel_id = node_channels[i].id;
        ch[i].waveform_count = (uint16_t)WAVEFORM_POINTS;
        ch[i].fft_count = (uint16_t)FFT_POINTS;
        ch[i].value_scaled = cv_i;
        ch[i].current_value_scaled = cv_i;
    }

    tx_try++;
    if (ESP32_SPI_ReportSummary(++s_spi_seq,
                                (uint64_t)now_tick,
                                ESP_CommParams_WaveStep(),
                                ESP_CommParams_UploadPoints(),
                                g_fault_code,
                                ESP_ServerReportFull() ? 1U : 0U,
                                1U,
                                ch,
                                4U,
                                1200U)) {
        tx_ok++;
        invalid_state_nack_count = 0U;
    } else {
        tx_err++;
        if (ESP32_SPI_GetLastNackReason() == ESP32_SPI_NACK_INVALID_STATE) {
            if (invalid_state_nack_count < 255U) {
                invalid_state_nack_count++;
            }
            if (invalid_state_nack_count >= ESP32_SPI_INVALID_STATE_AUTOSTOP_COUNT) {
                invalid_state_nack_count = 0U;
                ESP_SPI_ResetLocalReportState("summary NACK invalid_state");
                return;
            }
        } else {
            invalid_state_nack_count = 0U;
        }
    }

    if ((now_tick - last_tx_log) >= 1000U) {
        last_tx_log = now_tick;
        ESP_Log("[ESP32SPI] summary tx try=%lu ok=%lu err=%lu\r\n",
                (unsigned long)tx_try,
                (unsigned long)tx_ok,
                (unsigned long)tx_err);
    }
    return;
    }

}


/**
 * @brief  Data upload entry point.
 * @note   Summary and full-frame uploads are sent through the ESP32 SPI protocol.
 */
void ESP_Post_Data(void)
{
    if (g_esp_ready == 0)
        return;

#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    ESP_SPI_ServiceDeferredSync();
    {
    static uint32_t last_full_send_time = 0;
    static uint32_t full_try = 0, full_ok = 0, full_err = 0;
    static uint32_t last_full_log = 0;
    static uint32_t last_not_armed_log = 0;
    static uint32_t last_no_snapshot_log = 0;
    static uint32_t s_full_frame_id = 0;
    uint32_t now_tick = HAL_GetTick();
    uint32_t min_itv = ESP_CommParams_MinIntervalMs();
    uint8_t packets_sent = 0U;
    bool ok = true;

    if (g_spi_full_waiting_result != 0U) {
        int32_t http_status = 0;
        int32_t result_code = ESP32_SPI_RESULT_PENDING;
        uint32_t result_frame_id = 0U;

        if ((HAL_GetTick() - g_spi_full_result_last_poll_tick) >= ESP32_SPI_FULL_RESULT_POLL_MS) {
            g_spi_full_result_last_poll_tick = HAL_GetTick();
            (void)ESP32_SPI_PollEvents(20U);
        }
        {
            const esp32_spi_status_t *st = ESP32_SPI_GetStatus();
            if (st != NULL &&
                g_spi_full_result_session_epoch != 0U &&
                st->session_epoch != 0U &&
                st->session_epoch != g_spi_full_result_session_epoch) {
                ESP_Log("[ESP32SPI] full wait abort: ESP32 session changed frame=%lu ref=%lu old=%lu new=%lu\r\n",
                        (unsigned long)g_spi_full_result_frame_id,
                        (unsigned long)g_spi_full_result_ref_seq,
                        (unsigned long)g_spi_full_result_session_epoch,
                        (unsigned long)st->session_epoch);
                ESP_SPI_FullClearWaitState();
                if (g_spi_full_timeout_count < 1000000UL) {
                    g_spi_full_timeout_count++;
                }
                ESP_UI_ScheduleAutoRecover("ESP32 session changed during full wait", 1U);
                ESP_SPI_FullEnterHoldoff("full wait session changed",
                                         ESP32_SPI_FULL_TIMEOUT_HOLDOFF_MS);
                return;
            }
        }
        if (ESP32_SPI_GetTxResult(g_spi_full_result_ref_seq,
                                  &http_status,
                                  &result_code,
                                  &result_frame_id)) {
            uint32_t done_elapsed = HAL_GetTick() - g_spi_full_result_start_tick;
            ESP_Log("[ESP32SPI] full http done frame=%lu ref=%lu elapsed=%lums http=%ld result=%ld\r\n",
                    (unsigned long)result_frame_id,
                    (unsigned long)g_spi_full_result_ref_seq,
                    (unsigned long)done_elapsed,
                    (long)http_status,
                    (long)result_code);
            ESP_SPI_FullClearWaitState();
            /* The ESP32 cloud task can queue SERVER_COMMAND immediately after
               REPORT_RESULT for the same HTTP response.  If we stop polling as
               soon as TX_RESULT arrives, the command event can remain stuck in
               the ESP32->STM32 queue until some unrelated status poll happens.
               Drain a few follow-up events right here so cloud commands become
               visible to the main loop in the next iteration instead of minutes
               later. */
            for (uint8_t drain = 0U; drain < 3U; ++drain) {
                if (!ESP32_SPI_PollEvents(20U)) {
                    break;
                }
            }
            if (result_code == ESP32_SPI_RESULT_OK && http_status >= 200 && http_status < 300) {
                g_spi_full_holdoff_until_tick = 0U;
                g_spi_full_timeout_count = 0U;
                g_spi_full_busy_nack_count = 0U;
                g_spi_full_http_fail_count = 0U;
            } else {
                if (g_spi_full_http_fail_count < 1000000UL) {
                    g_spi_full_http_fail_count++;
                }
                ESP_SPI_FullEnterHoldoff("full http result failed",
                                         ESP32_SPI_FULL_TIMEOUT_HOLDOFF_MS * g_spi_full_http_fail_count);
            }
            if (g_spi_full_manual_frames == 0U && g_spi_full_continuous == 0U) {
                ESP_SetServerReportMode(0U);
            }
        } else {
            uint32_t wait_elapsed = HAL_GetTick() - g_spi_full_result_start_tick;
            if (wait_elapsed >= ESP32_SPI_FULL_RESULT_TIMEOUT_MS) {
                ESP_Log("[ESP32SPI] full http timeout frame=%lu ref=%lu elapsed=%lums\r\n",
                        (unsigned long)g_spi_full_result_frame_id,
                        (unsigned long)g_spi_full_result_ref_seq,
                        (unsigned long)wait_elapsed);
                ESP_SPI_FullClearWaitState();
                if (g_spi_full_timeout_count < 1000000UL) {
                    g_spi_full_timeout_count++;
                }
                ESP_SPI_FullEnterHoldoff("full http timeout",
                                         ESP32_SPI_FULL_TIMEOUT_HOLDOFF_MS * g_spi_full_timeout_count);
                if (g_spi_full_manual_frames == 0U && g_spi_full_continuous == 0U) {
                    ESP_SetServerReportMode(0U);
                }
            } else if ((HAL_GetTick() - g_spi_full_result_last_log_tick) >= ESP32_SPI_FULL_WAIT_LOG_MS) {
                g_spi_full_result_last_log_tick = HAL_GetTick();
                ESP_Log("[ESP32SPI] full waiting http frame=%lu ref=%lu elapsed=%lums\r\n",
                        (unsigned long)g_spi_full_result_frame_id,
                        (unsigned long)g_spi_full_result_ref_seq,
                        (unsigned long)wait_elapsed);
            }
        }
        return;
    }

    if (g_full_tx_sm.active == 0U) {
        if (ESP_SPI_FullHoldoffActive(now_tick)) {
            (void)ESP32_SPI_PollEvents(20U);
            return;
        }

        const ESP_UploadSnapshot_t *snapshot;
        uint32_t snapshot_seq = 0U;
        uint8_t one_shot = 0U;

        if (min_itv && (now_tick - last_full_send_time < min_itv)) {
            return;
        }

        if (g_spi_full_continuous == 0U) {
            if (g_spi_full_manual_frames == 0U) {
                if ((now_tick - last_not_armed_log) >= ESP32_SPI_FULL_NOT_ARMED_LOG_MS) {
                    last_not_armed_log = now_tick;
                    ESP_Log("[ESP32SPI] full requested but not armed; type full1/full10/fullon on STM32 console.\r\n");
                }
                return;
            }
            one_shot = 1U;
        }

        snapshot = ESP_UploadSnapshot_TryAcquireLatest(&snapshot_seq);
        if (snapshot == NULL) {
            if ((now_tick - last_no_snapshot_log) >= 2000U) {
                last_no_snapshot_log = now_tick;
                ESP_Log("[ESP32SPI] full waiting DSP snapshot pub=%lu drop=%lu inflight=%d latest=%d\r\n",
                        (unsigned long)g_upload_snapshot_publish_count,
                        (unsigned long)g_upload_snapshot_drop_count,
                        (int)g_upload_snapshot_inflight_idx,
                        (int)g_upload_snapshot_latest_idx);
            }
            return;
        }

        if (!ESP_FullTx_StartFrame(++s_full_frame_id, snapshot, snapshot_seq, one_shot)) {
            ESP_UploadSnapshot_Release(snapshot_seq);
            full_err++;
            ESP_Log("[ESP32SPI] full start failed snapshot=%lu err=%lu\r\n",
                    (unsigned long)snapshot_seq,
                    (unsigned long)full_err);
            return;
        }
        if (one_shot != 0U && g_spi_full_manual_frames > 0U) {
            g_spi_full_manual_frames--;
        }
        full_try++;
        last_full_send_time = now_tick;
    }

    while (g_full_tx_sm.active != 0U && packets_sent < ESP32_SPI_FULL_PACKETS_PER_POLL) {
        ok = ESP_FullTx_StepPacket(ESP32_SPI_FULL_PACKET_ACCEPT_TIMEOUT_MS);
        if (!ok) {
            break;
        }
        packets_sent++;
        if (ESP32_SPI_FULL_YIELD_EVERY > 0U &&
            (packets_sent % ESP32_SPI_FULL_YIELD_EVERY) == 0U) {
            ESP_RtosYield();
        }
    }

    if (!ok) {
        uint32_t failed_frame = g_full_tx_sm.frame_id;
        uint32_t elapsed_ms = HAL_GetTick() - g_full_tx_sm.start_tick;
        uint8_t one_shot_failed = g_full_tx_sm.one_shot;
        uint8_t phase_failed = (uint8_t)g_full_tx_sm.phase;
        uint16_t nack_reason = ESP32_SPI_GetLastNackReason();
        full_err++;
        ESP_Log("[ESP32SPI] full tx fail frame=%lu elapsed=%lums pkt=%lu phase=%u try=%lu ok=%lu err=%lu snapshot=%lu drop=%lu nack=%u\r\n",
                (unsigned long)failed_frame,
                (unsigned long)elapsed_ms,
                (unsigned long)g_full_tx_sm.packet_count,
                (unsigned int)g_full_tx_sm.phase,
                (unsigned long)full_try,
                (unsigned long)full_ok,
                (unsigned long)full_err,
                (unsigned long)g_full_tx_sm.snapshot_seq,
                (unsigned long)g_upload_snapshot_drop_count,
                (unsigned int)nack_reason);
        if (nack_reason == ESP32_SPI_NACK_INVALID_STATE) {
            ESP_SPI_ResetLocalReportState("full NACK invalid_state");
        } else if (nack_reason == ESP32_SPI_NACK_BUSY ||
                   (phase_failed == (uint8_t)ESP_FULL_TX_BEGIN &&
                    nack_reason == ESP32_SPI_NACK_INVALID_PAYLOAD)) {
            if (g_spi_full_busy_nack_count < 1000000UL) {
                g_spi_full_busy_nack_count++;
            }
            ESP_FullTx_ClearAndRelease();
            ESP_SPI_FullEnterHoldoff((nack_reason == ESP32_SPI_NACK_BUSY) ?
                                     "full NACK busy" : "full begin NACK invalid_payload",
                                     ESP32_SPI_FULL_BUSY_HOLDOFF_MS * g_spi_full_busy_nack_count);
        } else {
            ESP_FullTx_ClearAndRelease();
        }
        if (one_shot_failed != 0U && g_spi_full_manual_frames == 0U && g_spi_full_continuous == 0U) {
            ESP_SetServerReportMode(0U);
        }
        return;
    }

    if (g_full_tx_sm.active == 0U) {
        uint32_t done_frame = g_full_tx_sm.frame_id;
        uint32_t done_seq = g_full_tx_sm.snapshot_seq;
        uint32_t done_packets = g_full_tx_sm.packet_count;
        uint32_t done_wave_step = g_full_tx_sm.wave_step;
        uint32_t done_upload_points = g_full_tx_sm.upload_points;
        uint16_t done_wave_points = (g_full_tx_sm.channel_count > 0U) ?
                                    g_full_tx_sm.channels[0].waveform_count : 0U;
        uint32_t elapsed_ms = HAL_GetTick() - g_full_tx_sm.start_tick;
        full_ok++;
        g_spi_full_busy_nack_count = 0U;
        g_spi_full_waiting_result = 1U;
        g_spi_full_result_ref_seq = ESP32_SPI_GetLastFullEndRefSeq();
        g_spi_full_result_frame_id = done_frame;
        g_spi_full_result_start_tick = HAL_GetTick();
        {
            const esp32_spi_status_t *st = ESP32_SPI_GetStatus();
            g_spi_full_result_session_epoch = (st != NULL) ? st->session_epoch : 0U;
        }
        g_spi_full_result_last_log_tick = g_spi_full_result_start_tick;
        g_spi_full_result_last_poll_tick = 0U;
        ESP_UploadSnapshot_Release(done_seq);
        memset(&g_full_tx_sm, 0, sizeof(g_full_tx_sm));

        if ((HAL_GetTick() - last_full_log) >= 1000U) {
            last_full_log = HAL_GetTick();
            ESP_Log("[ESP32SPI] full tx done frame=%lu snap=%lu elapsed=%lums pkt=%lu try=%lu ok=%lu err=%lu step=%lu upload_points=%lu wave=%u fft=%u drop=%lu\r\n",
                    (unsigned long)done_frame,
                    (unsigned long)done_seq,
                    (unsigned long)elapsed_ms,
                    (unsigned long)done_packets,
                    (unsigned long)full_try,
                    (unsigned long)full_ok,
                    (unsigned long)full_err,
                    (unsigned long)done_wave_step,
                    (unsigned long)done_upload_points,
                    (unsigned int)done_wave_points,
                    (unsigned int)FFT_POINTS,
                    (unsigned long)g_upload_snapshot_drop_count);
        }
    } else if ((HAL_GetTick() - last_full_log) >= 1000U) {
        last_full_log = HAL_GetTick();
        ESP_Log("[ESP32SPI] full tx progress frame=%lu snap=%lu phase=%u ch=%u off=%u pkt=%lu drop=%lu\r\n",
                (unsigned long)g_full_tx_sm.frame_id,
                (unsigned long)g_full_tx_sm.snapshot_seq,
                (unsigned int)g_full_tx_sm.phase,
                (unsigned int)g_full_tx_sm.channel_index,
                (unsigned int)g_full_tx_sm.element_offset,
                (unsigned long)g_full_tx_sm.packet_count,
                (unsigned long)g_upload_snapshot_drop_count);
    }
    return;
    }
#else
    static uint32_t last_full_log = 0;
    uint32_t now_full = HAL_GetTick();
    if ((now_full - last_full_log) >= 2000U) {
        last_full_log = now_full;
        ESP_Log("[ESP32SPI] full report over SPI disabled; summary path remains active.\r\n");
    }
    return;
#endif
}


// ---------------- Debug console input callback (USART1 only) ----------------
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
#if (ESP_CONSOLE_ENABLE)
    UART_HandleTypeDef *hu = ESP_GetLogUart();
    if (huart == hu)
    {
        // 若上一条命令还未被主循环处理，为避免覆盖缓冲区，先丢弃后续输入
        if (g_console_line_ready)
        {
            HAL_UART_Receive_IT(hu, &g_console_rx_byte, 1);
            return;
        }

        uint8_t ch = g_console_rx_byte;
        if (ch == '\r' || ch == '\n')
        {
            if (g_console_line_len > 0)
            {
                g_console_line_ready = 1;
            }
        }
        else
        {
            if (g_console_line_len < (sizeof(g_console_line) - 1))
            {
                g_console_line[g_console_line_len++] = (char)ch;
            }
            else
            {
                g_console_line_len = 0; // 溢出清空
            }
        }
        HAL_UART_Receive_IT(hu, &g_console_rx_byte, 1);
        return;
    }
#endif
    (void)huart;
}

static void ESP_Log(const char *format, ...)
{
    char log_buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(log_buf, sizeof(log_buf), format, args);
    va_end(args);
#if (ESP_DEBUG)
    // 直接打到指定调试串口（避免 printf 重定向配置不一致导致“看不到日志”）
    UART_HandleTypeDef *hu = ESP_GetLogUart();
    if (hu)
    {
        HAL_UART_Transmit(hu, (uint8_t *)log_buf, (uint16_t)strlen(log_buf), 100);
    }
    else
    {
        printf("%s", log_buf);
    }
#else
    (void)log_buf;
#endif

    /* 额外：推送给 UI（DeviceConnect 控制台） */
    ESP_UI_Internal_OnLog(log_buf);
}

/* ================= DeviceConnect(UI) 驱动实现 ================= */

typedef struct
{
    esp_ui_log_hook_t log_hook;
    void *log_ctx;
    esp_ui_step_hook_t step_hook;
    void *step_ctx;
} esp_ui_hooks_t;

static esp_ui_hooks_t g_ui_hooks;
static osMessageQueueId_t g_esp_ui_q = NULL;

void ESP_UI_SetHooks(esp_ui_log_hook_t log_hook, void *log_ctx,
                     esp_ui_step_hook_t step_hook, void *step_ctx)
{
    g_ui_hooks.log_hook = log_hook;
    g_ui_hooks.log_ctx = log_ctx;
    g_ui_hooks.step_hook = step_hook;
    g_ui_hooks.step_ctx = step_ctx;
}

/* 供 ESP_Log 调用：避免静态函数直接访问 hook 造成声明顺序问题 */
void ESP_UI_Internal_OnLog(const char *line)
{
    if (g_ui_hooks.log_hook && line)
    {
        g_ui_hooks.log_hook(line, g_ui_hooks.log_ctx);
    }
}

static void esp_ui_step_done(esp_ui_cmd_t step, bool ok)
{
    if (g_ui_hooks.step_hook)
    {
        g_ui_hooks.step_hook(step, ok, g_ui_hooks.step_ctx);
    }
}

void ESP_UI_TaskInit(void)
{
    if (g_esp_ui_q)
        return;
    g_esp_ui_q = osMessageQueueNew(8, sizeof(uint8_t), NULL);

}

bool ESP_UI_SendCmd(esp_ui_cmd_t cmd)
{
    if (!g_esp_ui_q)
        return false;
    uint8_t c = (uint8_t)cmd;
    return (osMessageQueuePut(g_esp_ui_q, &c, 0U, 0U) == osOK);
}

bool ESP_UI_IsReporting(void)
{
    return (g_report_enabled != 0U);
}

bool ESP_ServerReportFull(void)
{
    return (g_server_report_full != 0U);
}

bool ESP_UI_IsWiFiOk(void)
{
    return (g_ui_wifi_ok != 0U);
}

bool ESP_UI_IsTcpOk(void)
{
    return (g_ui_tcp_ok != 0U);
}

bool ESP_UI_IsRegOk(void)
{
    /* 注册 OK：以 g_ui_reg_ok 为准；同时 g_esp_ready 也代表“已注册+就绪可上报” */
    return (g_ui_reg_ok != 0U) || (g_esp_ready != 0U);
}

const char *ESP_UI_NodeId(void)
{
    const SystemConfig_t *cfg = ESP_Config_Get();
    if (cfg && cfg->node_id[0]) {
        return cfg->node_id;
    }
    return "--";
}

void ESP_UI_InvalidateReg(void)
{
    /* 仅清除“就绪可上报”标志，不做任何后台重连/硬复位 */
    g_esp_ready = 0;
    g_ui_reg_ok = 0;
    ESP_Log("[UI] Registration expired, please click REG again.\r\n");
}


static void ESP_UI_SyncLinkFlagsFromStatus(const esp32_spi_status_t *st)
{
    if (st == NULL) {
        return;
    }
    g_ui_wifi_ok = st->wifi_connected ? 1U : 0U;
    g_ui_tcp_ok = (st->cloud_connected || st->registered_with_cloud || st->reporting_enabled) ? 1U : 0U;
    g_ui_reg_ok = (st->registered_with_cloud || st->reporting_enabled) ? 1U : 0U;
    g_esp_ready = (st->registered_with_cloud || st->reporting_enabled) ? 1U : 0U;
}

static void ESP_UI_ScheduleAutoRecover(const char *reason, uint8_t want_report)
{
    uint32_t now = HAL_GetTick();
    uint8_t should_log = (g_ui_auto_recover_pending == 0U);

    g_ui_auto_recover_pending = 1U;
    if (want_report != 0U) {
        g_ui_auto_recover_want_report = 1U;
    }
    if (g_ui_auto_recover_next_attempt_tick == 0U ||
        (int32_t)(g_ui_auto_recover_next_attempt_tick - now) > 1000) {
        g_ui_auto_recover_next_attempt_tick = now + 500U;
    }
    if (should_log) {
        ESP_Log("[ESP32SPI] scheduled auto recovery: %s (want_report=%u)\r\n",
                (reason != NULL) ? reason : "unspecified",
                (unsigned int)((want_report != 0U) ? 1U : g_ui_auto_recover_want_report));
    }
}

static bool ESP_UI_EnsureReportStarted(uint8_t mode, const char *reason_tag)
{
    esp32_spi_status_t st;
    uint8_t target_mode = (mode != 0U) ? 1U : 0U;

    if (!g_esp_ready) {
        ESP_Log("[ESP32SPI] start report denied: link not ready.\r\n");
        return false;
    }

#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    ESP_SPI_FullResetUploadRuntimeState();
#endif

    if (ESP32_SPI_QueryStatus(&st, 500U)) {
        ESP_UI_SyncLinkFlagsFromStatus(&st);
        if (st.reporting_enabled && st.report_mode == target_mode) {
            g_report_enabled = 1U;
            (void)ESP_AutoReconnect_SetLastReporting(true);
            return true;
        }
    }

    if (!ESP32_SPI_StartReport(target_mode, 3000U)) {
        if (ESP32_SPI_QueryStatus(&st, 1000U)) {
            ESP_UI_SyncLinkFlagsFromStatus(&st);
            if (st.reporting_enabled && st.report_mode == target_mode) {
                g_report_enabled = 1U;
                (void)ESP_AutoReconnect_SetLastReporting(true);
                return true;
            }
        }
        ESP_UI_SPI_LogStatus((reason_tag != NULL) ? reason_tag : "start report failed");
        return false;
    }

    g_report_enabled = 1U;
    (void)ESP_AutoReconnect_SetLastReporting(true);
    return true;
}

static void ESP_UI_PollAutoRecover(void)
{
    uint32_t now = HAL_GetTick();
    bool auto_reconnect_en = false;
    bool last_reporting = false;
    uint8_t want_report = 0U;
    esp32_spi_status_t st;
    bool st_ok;
    bool need_recover = false;

    if ((int32_t)(now - g_ui_auto_recover_next_poll_tick) < 0) {
        return;
    }
    g_ui_auto_recover_next_poll_tick = now + ESP32_SPI_AUTO_RECOVER_POLL_MS;

    (void)ESP_AutoReconnect_Read(&auto_reconnect_en, &last_reporting);
    if (g_report_enabled != 0U || g_ui_auto_recover_want_report != 0U ||
        (auto_reconnect_en && last_reporting)) {
        want_report = 1U;
    }

#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    if (ESP_SPI_FullControlBusy()) {
        return;
    }
#endif

    st_ok = ESP32_SPI_QueryStatus(&st, 500U);
    if (st_ok) {
        ESP_UI_SyncLinkFlagsFromStatus(&st);
        if (st.session_epoch != 0U) {
            if (g_ui_last_session_valid != 0U &&
                st.session_epoch != g_ui_last_session_epoch) {
                ESP_Log("[ESP32SPI] detected ESP32 session change old=%lu new=%lu\r\n",
                        (unsigned long)g_ui_last_session_epoch,
                        (unsigned long)st.session_epoch);
                need_recover = true;
            }
            g_ui_last_session_epoch = st.session_epoch;
            g_ui_last_session_valid = 1U;
        }
        if (want_report != 0U) {
            if (!st.wifi_connected ||
                !st.registered_with_cloud ||
                !st.reporting_enabled) {
                need_recover = true;
            }
        }
    } else if (want_report != 0U) {
        need_recover = true;
        g_ui_wifi_ok = 0U;
        g_ui_tcp_ok = 0U;
        g_ui_reg_ok = 0U;
        g_esp_ready = 0U;
    }

    if (need_recover) {
        ESP_UI_ScheduleAutoRecover(st_ok ? "status degraded" : "status query failed",
                                   want_report);
    }

    if (g_ui_auto_recover_pending == 0U) {
        return;
    }
    if ((int32_t)(now - g_ui_auto_recover_next_attempt_tick) < 0) {
        return;
    }

    g_ui_auto_recover_next_attempt_tick = now + ESP32_SPI_AUTO_RECOVER_RETRY_MS;
    want_report = (g_ui_auto_recover_want_report != 0U || g_report_enabled != 0U ||
                   (auto_reconnect_en && last_reporting)) ? 1U : 0U;

    ESP_Log("[ESP32SPI] auto recovery start want_report=%u auto=%u last=%u\r\n",
            (unsigned int)want_report,
            (unsigned int)(auto_reconnect_en ? 1U : 0U),
            (unsigned int)(last_reporting ? 1U : 0U));

    if (!ESP_Config_LoadRuntimeFromSD()) {
        ESP_Log("[ESP32SPI] auto recovery aborted: SD runtime config invalid.\r\n");
        g_ui_auto_recover_next_attempt_tick = now + ESP32_SPI_AUTO_RECOVER_FAIL_BACKOFF_MS;
        return;
    }

    if (!st_ok) {
        memset(&st, 0, sizeof(st));
    }
    if (!st_ok || !st.wifi_connected) {
        if (!ESP_UI_DoWiFi()) {
            ESP_Log("[ESP32SPI] auto recovery failed at WIFI step.\r\n");
            g_ui_auto_recover_next_attempt_tick = now + ESP32_SPI_AUTO_RECOVER_FAIL_BACKOFF_MS;
            return;
        }
        st_ok = ESP32_SPI_QueryStatus(&st, 1000U);
        if (st_ok) {
            ESP_UI_SyncLinkFlagsFromStatus(&st);
        }
    }
    if (!st_ok || (!st.cloud_connected && !st.registered_with_cloud && !st.reporting_enabled)) {
        if (!ESP_UI_DoTCP()) {
            ESP_Log("[ESP32SPI] auto recovery failed at TCP/cloud step.\r\n");
            g_ui_auto_recover_next_attempt_tick = now + ESP32_SPI_AUTO_RECOVER_FAIL_BACKOFF_MS;
            return;
        }
        st_ok = ESP32_SPI_QueryStatus(&st, 1000U);
        if (st_ok) {
            ESP_UI_SyncLinkFlagsFromStatus(&st);
        }
    }
    if (!st_ok || !st.registered_with_cloud) {
        if (!ESP_UI_DoRegister()) {
            ESP_Log("[ESP32SPI] auto recovery failed at REG step.\r\n");
            g_ui_auto_recover_next_attempt_tick = now + ESP32_SPI_AUTO_RECOVER_FAIL_BACKOFF_MS;
            return;
        }
        st_ok = ESP32_SPI_QueryStatus(&st, 1000U);
        if (st_ok) {
            ESP_UI_SyncLinkFlagsFromStatus(&st);
        }
    }
    if (want_report != 0U &&
        (!st_ok || !st.reporting_enabled || st.report_mode != (ESP_ServerReportFull() ? 1U : 0U))) {
        if (!ESP_UI_EnsureReportStarted(ESP_ServerReportFull() ? 1U : 0U,
                                        "auto recovery start report failed")) {
            ESP_Log("[ESP32SPI] auto recovery failed at report start step.\r\n");
            g_ui_auto_recover_next_attempt_tick = now + ESP32_SPI_AUTO_RECOVER_FAIL_BACKOFF_MS;
            return;
        }
        st_ok = ESP32_SPI_QueryStatus(&st, 1000U);
        if (st_ok) {
            ESP_UI_SyncLinkFlagsFromStatus(&st);
        }
    }

    g_ui_auto_recover_pending = 0U;
    g_ui_auto_recover_want_report = 0U;
    if (st_ok && st.session_epoch != 0U) {
        g_ui_last_session_epoch = st.session_epoch;
        g_ui_last_session_valid = 1U;
    }
    ESP_Log("[ESP32SPI] auto recovery complete: wifi=%u cloud=%u reg=%u report=%u mode=%u\r\n",
            st_ok ? (unsigned int)st.wifi_connected : 0U,
            st_ok ? (unsigned int)st.cloud_connected : 0U,
            st_ok ? (unsigned int)st.registered_with_cloud : 0U,
            st_ok ? (unsigned int)st.reporting_enabled : 0U,
            st_ok ? (unsigned int)st.report_mode : 0U);
}

static bool ESP_UI_SPI_LoadAndApplyConfig(void)
{
    const SystemConfig_t *cfg;
    ESP_CommParams_t p;

    if (!ESP_Config_LoadRuntimeFromSD()) {
        ESP_Log("[ESP32SPI] SD runtime config invalid; abort apply.\r\n");
        return false;
    }

    cfg = ESP_Config_Get();
    ESP_CommParams_Get(&p);

#if (ESP32_SPI_STRESS_TEST)
    p.min_interval_ms = ESP32_SPI_STRESS_SENDLIMIT_MS;
#if (ESP32_SPI_STRESS_FULL_UPLOAD)
    /* Keep lightweight idle heartbeats below server NODE_TIMEOUT as a fallback
       if full-frame uploads are backing off or waiting for recovery. */
    p.heartbeat_ms = ESP32_SPI_FULL_IDLE_HEARTBEAT_MS;
    g_server_report_full = 1U;
    g_server_report_full_dirty = 0U;
#endif
    ESP_CommParams_Apply(&p);
#endif

    if (!ESP_Config_IsValidForLink(cfg)) {
        ESP_Log("[ESP32SPI] config invalid: ssid/server/node is empty.\r\n");
        return false;
    }

    ESP_Log("[ESP32SPI] config ssid=%s server=%s:%u node=%s\r\n",
            cfg->wifi_ssid,
            cfg->server_ip,
            (unsigned int)cfg->server_port,
            cfg->node_id);
#if (ESP32_SPI_STRESS_TEST)
    ESP_Log("[ESP32SPI] stress test: spi=%s sendlimit=%lums mode=%s\r\n",
            ESP32_SPI_STRESS_SPI_LABEL,
            (unsigned long)p.min_interval_ms,
            ESP32_SPI_STRESS_FULL_UPLOAD ? "full" : "summary");
#endif

    if (!ESP32_SPI_ApplyDeviceConfig(cfg->wifi_ssid,
                                     cfg->wifi_password,
                                     cfg->server_ip,
                                     cfg->server_port,
                                     cfg->node_id,
                                     cfg->node_location,
                                     "STM32H750_ESP32_SPI")) {
        ESP_Log("[ESP32SPI] apply device config failed.\r\n");
        return false;
    }

    if (!ESP32_SPI_ApplyCommParams(p.heartbeat_ms,
                                   p.min_interval_ms,
                                   p.http_timeout_ms,
                                   3000U,
                                   p.wave_step,
                                   p.upload_points,
                                   p.hardreset_sec,
                                   p.chunk_kb,
                                   p.chunk_delay_ms)) {
        ESP_Log("[ESP32SPI] apply comm params failed.\r\n");
        return false;
    }

    return true;
}

static void ESP_UI_SPI_LogStatus(const char *prefix)
{
    const esp32_spi_status_t *st = ESP32_SPI_GetStatus();
    ESP_Log("[ESP32SPI] %s ready=%u wifi=%u cloud=%u reg=%u report=%u ip=%s err=%s\r\n",
            prefix ? prefix : "status",
            (unsigned int)st->ready,
            (unsigned int)st->wifi_connected,
            (unsigned int)st->cloud_connected,
            (unsigned int)st->registered_with_cloud,
            (unsigned int)st->reporting_enabled,
            st->ip_address,
            st->last_error);
}

static bool ESP_UI_DoApplyConfig(void)
{
    ESP_Log("[UI] Applying saved SD config to ESP32...\r\n");
    if (g_report_enabled) {
        ESP_Log("[UI] Apply config denied: reporting active, stop/reconnect first.\r\n");
        return false;
    }
    if (!ESP32_SPI_EnsureReady(5000U)) {
        ESP_Log("[ESP32SPI] ESP32 not ready for config apply.\r\n");
        return false;
    }
    if (!ESP_UI_SPI_LoadAndApplyConfig()) {
        return false;
    }
    g_esp_ready = 0;
    g_ui_wifi_ok = 0;
    g_ui_tcp_ok = 0;
    g_ui_reg_ok = 0;
    (void)ESP32_SPI_QueryStatus(NULL, 500U);
    ESP_UI_SPI_LogStatus("config applied");
    ESP_Log("[UI] Saved SD config applied. Re-run WiFi/TCP/REG/Report if needed.\r\n");
    return true;
}

static bool ESP_UI_DoWiFi(void)
{
    ESP_Log("[UI] Executing WIFI via ESP32 SPI...\r\n");
    g_esp_ready = 0;
    g_report_enabled = 0;
    g_ui_wifi_ok = 0;
    g_ui_tcp_ok = 0;
    g_ui_reg_ok = 0;
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    ESP_SPI_FullResetUploadRuntimeState();
#endif

    if (!ESP32_SPI_EnsureReady(5000U)) {
        ESP_Log("[ESP32SPI] ESP32 not ready.\r\n");
        return false;
    }
    if (!ESP_UI_SPI_LoadAndApplyConfig()) {
        return false;
    }
    if (!ESP32_SPI_ConnectWifi(30000U)) {
        esp32_spi_status_t st;
        if (!ESP32_SPI_QueryStatus(&st, 1000U) || !st.wifi_connected) {
            ESP_UI_SPI_LogStatus("wifi failed");
            g_ui_wifi_ok = 0;
            return false;
        }
        ESP_Log("[ESP32SPI] wifi connect response missed, but status is connected; continue.\r\n");
    }

    g_ui_wifi_ok = 1;
    ESP_UI_SPI_LogStatus("wifi ok");
    return true;
}

static bool ESP_UI_DoTCP(void)
{
    esp32_spi_status_t st;
    ESP_Log("[UI] Executing TCP via ESP32 SPI...\r\n");

    if (!ESP32_SPI_QueryStatus(&st, 1000U) || !st.wifi_connected) {
        ESP_Log("[ESP32SPI] TCP denied: WiFi not connected.\r\n");
        g_ui_tcp_ok = 0;
        return false;
    }
    if (!ESP32_SPI_CloudConnect(5000U)) {
        if (!ESP32_SPI_QueryStatus(&st, 1000U) ||
            (!st.cloud_connected && !st.registered_with_cloud && !st.reporting_enabled)) {
            ESP_UI_SPI_LogStatus("tcp/cloud failed");
            g_ui_tcp_ok = 0;
            return false;
        }
        ESP_Log("[ESP32SPI] cloud connect response missed, but status is already active; continue.\r\n");
    }
    g_ui_tcp_ok = 1;
    ESP_UI_SPI_LogStatus("tcp/cloud ok");
    return true;
}

static bool ESP_UI_DoRegister(void)
{
    esp32_spi_status_t st;
    ESP_Log("[UI] Executing REG via ESP32 SPI...\r\n");

    if (!ESP32_SPI_QueryStatus(&st, 1000U) || !st.wifi_connected) {
        ESP_Log("[ESP32SPI] REG denied: WiFi not connected.\r\n");
        g_ui_reg_ok = 0;
        return false;
    }
    if (!g_ui_tcp_ok) {
        ESP_Log("[ESP32SPI] REG denied: TCP/cloud step not completed.\r\n");
        g_ui_reg_ok = 0;
        return false;
    }
    if (!ESP32_SPI_RegisterNode(20000U)) {
        ESP_UI_SPI_LogStatus("register failed");
        g_ui_reg_ok = 0;
        return false;
    }

    ESP_Init_Channels_And_DSP();
    g_esp_ready = 1;
    g_ui_reg_ok = 1;
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    ESP_SPI_FullResetUploadRuntimeState();
#endif
    ESP_UI_SPI_LogStatus("register ok");
    {
        esp32_spi_status_t st;
        if (ESP32_SPI_QueryStatus(&st, 500U)) {
            if (st.reporting_enabled) {
                ESP_Log("[ESP32SPI] clearing residual ESP32 reporting state after REG.\r\n");
                (void)ESP32_SPI_StopReport(3000U);
            }
            g_report_enabled = 0U;
        } else {
            g_report_enabled = 0U;
        }
    }
#if (ESP32_SPI_STRESS_FULL_UPLOAD && ESP32_SPI_ENABLE_FULL_UPLOAD)
    ESP_SetServerReportMode(1U);
    g_spi_full_continuous = 1U;
    if (ESP32_SPI_StartReport(1U, 3000U)) {
        g_report_enabled = 1U;
        (void)ESP_AutoReconnect_SetLastReporting(true);
        ESP_Log("[ESP32SPI] forced full continuous report armed after REG.\r\n");
    } else {
        ESP_UI_SPI_LogStatus("force full start report failed");
    }
#endif
    ESP_Log("[ESP32SPI] registration complete, SPI link ready.\r\n");
    return true;
}

static bool ESP_UI_ToggleReport(void)
{
    if (!g_report_enabled)
    {
        uint8_t mode = ESP_ServerReportFull() ? 1U : 0U;
        if (!g_esp_ready)
        {
            ESP_Log("[UI] Report denied: link not ready (need REG first)\r\n");
            return false;
        }
        if (!ESP_UI_EnsureReportStarted(mode, "start report failed")) {
            return false;
        }
        ESP_Log("[UI] Started ESP32 SPI data upload loop.\r\n");
        return true;
    }
    else
    {
        if (!ESP32_SPI_StopReport(3000U)) {
            esp32_spi_status_t st;
            if (!ESP32_SPI_QueryStatus(&st, 1000U) || st.reporting_enabled) {
                ESP_UI_SPI_LogStatus("stop report failed");
                return false;
            }
            ESP_Log("[ESP32SPI] stop response missed, but status is stopped; continue.\r\n");
        }
        g_report_enabled = 0U;
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
        ESP_SPI_FullResetUploadRuntimeState();
#endif
        ESP_Log("[UI] Data upload stopped.\r\n");
        (void)ESP_AutoReconnect_SetLastReporting(false);
        return true;
    }
}

static void ESP_UI_AutoConnect(void)
{
    ESP_Log("[UI] Auto sequence started...\r\n");
    bool ok_wifi = ESP_UI_DoWiFi();
    esp_ui_step_done(ESP_UI_CMD_WIFI, ok_wifi);
    if (!ok_wifi)
        return;

    bool ok_tcp = ESP_UI_DoTCP();
    esp_ui_step_done(ESP_UI_CMD_TCP, ok_tcp);
    if (!ok_tcp)
        return;

    bool ok_reg = ESP_UI_DoRegister();
    esp_ui_step_done(ESP_UI_CMD_REG, ok_reg);
    if (!ok_reg)
        return;

    if (!g_report_enabled)
    {
        if (!ESP_UI_ToggleReport()) {
            esp_ui_step_done(ESP_UI_CMD_REPORT_TOGGLE, false);
            return;
        }
    }
    else if (!ESP_UI_EnsureReportStarted(ESP_ServerReportFull() ? 1U : 0U,
                                         "auto connect start report failed")) {
        esp_ui_step_done(ESP_UI_CMD_REPORT_TOGGLE, false);
        return;
    }
    esp_ui_step_done(ESP_UI_CMD_REPORT_TOGGLE, true);
}

void ESP_UI_TaskPoll(void)
{
    if (!g_esp_ui_q)
        return;

    uint8_t c = 0xFF;
    /* 保持 FIFO：不要丢弃前置 WiFi/TCP/REG 等控制命令。按钮侧已做禁用/节流。 */
    if (osMessageQueueGet(g_esp_ui_q, &c, NULL, 0U) != osOK)
    {
        ESP_UI_PollAutoRecover();
        return;
    }

    {
        esp_ui_cmd_t cmd = (esp_ui_cmd_t)c;
        switch (cmd)
        {
        case ESP_UI_CMD_WIFI:
        {
            bool ok = ESP_UI_DoWiFi();
            esp_ui_step_done(cmd, ok);
        }
        break;
        case ESP_UI_CMD_TCP:
        {
            bool ok = ESP_UI_DoTCP();
            esp_ui_step_done(cmd, ok);
        }
        break;
        case ESP_UI_CMD_REG:
        {
            bool ok = ESP_UI_DoRegister();
            esp_ui_step_done(cmd, ok);
        }
        break;
        case ESP_UI_CMD_REPORT_TOGGLE:
        {
            bool before = ESP_UI_IsReporting();
            bool toggled_ok = ESP_UI_ToggleReport();
            bool after = ESP_UI_IsReporting();
            bool ok = toggled_ok && (before != after);
            esp_ui_step_done(cmd, ok);
        }
            break;
        case ESP_UI_CMD_AUTO_CONNECT:
            ESP_UI_AutoConnect();
            break;
        case ESP_UI_CMD_APPLY_CONFIG:
        {
            bool ok = ESP_UI_DoApplyConfig();
            esp_ui_step_done(cmd, ok);
        }
            break;
        default:
            break;
        }
    }

    if (osMessageQueueGetCount(g_esp_ui_q) == 0U) {
        ESP_UI_PollAutoRecover();
    }
}
