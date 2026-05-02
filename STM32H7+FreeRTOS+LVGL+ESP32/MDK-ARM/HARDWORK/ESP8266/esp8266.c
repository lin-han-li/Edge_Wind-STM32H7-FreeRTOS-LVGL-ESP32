/**
 ******************************************************************************
 * @file    esp8266.c
 * @author  STM32H7 Optimization Expert
 * @brief   ESP8266 WiFi 模组驱动程序 (STM32H7 专用优化版)
 * @note    核心特性：
 * 1. AXI SRAM + D-Cache 一致性维护 (DMA 必备)
 * 2. 2Mbps 高波特率下的软件容错机制 (抗 ORE/FE 错误)
 * 3. 智能流式接收解析 (滑动窗口)
 * 4. 自动故障恢复与重连逻辑
 ******************************************************************************
 */

#include "esp8266.h"
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
#define ESP32_SPI_STRESS_SPI_LABEL "10MHz"
#endif

#ifndef ESP_LEGACY_UART_LINK_WATCHDOG
#define ESP_LEGACY_UART_LINK_WATCHDOG (!EW_USE_ESP32_SPI_UI)
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
#define ESP32_SPI_FULL_RESULT_TIMEOUT_MS 45000U
#endif
#ifndef ESP32_SPI_FULL_WAIT_LOG_MS
#define ESP32_SPI_FULL_WAIT_LOG_MS 5000U
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
extern UART_HandleTypeDef huart2;
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

// =================================================================================
// 1. STM32H7 特有的内存管理与 Cache 维护
// =================================================================================
// [背景知识]
// STM32H7 的架构非常复杂。普通的 DTCM RAM (0x20000000) 通常连接在 CPU 的紧耦合总线上，
// DMA 控制器（尤其是 DMA1/DMA2）往往无法直接访问 DTCM，或者访问效率极低。
// 必须将用于 DMA 传输的缓冲区（Buffer）放到 AXI SRAM (0x24000000) 或 D2 域的 SRAM1/2/3。
// 此外，H7 默认开启 D-Cache（数据缓存）。CPU 写数据进 Cache 后，物理内存里可能还是旧数据；
// 此时如果 DMA 从内存搬运数据，发出去的就是错的。
// 所以：
// - 发送前：必须 Clean Cache (将 Cache 数据刷入物理内存)。
// - 接收后：必须 Invalidate Cache (废弃 Cache 数据，强迫 CPU 下次从物理内存重读)。

// 定义通过链接器脚本定位的内存段，确保变量存放在 AXI SRAM 中
#define AXI_SRAM_SECTION __attribute__((section(".axi_sram")))
// 强制 32 字节对齐，这是 Cortex-M7 Cache Line 的大小。
// 如果不对齐，Cache 操作可能会意外破坏相邻变量的数据（Cache 伪共享问题）。
#define DMA_ALIGN32 __attribute__((aligned(32)))

/* 辅助内联函数：用于计算对齐地址 */
static inline uint32_t _align_down_32(uint32_t x) { return x & ~31u; }
static inline uint32_t _align_up_32(uint32_t x) { return (x + 31u) & ~31u; }

static inline void ESP_RtosYield(void)
{
    if ((osKernelGetState() == osKernelRunning) && (__get_IPSR() == 0U))
    {
        osDelay(1);
    }
}

/**
 * @brief  Clean D-Cache (将 Cache 中的脏数据写回物理内存)
 * @note   用于 DMA 发送前：CPU 准备好数据 -> Clean -> 内存更新 -> DMA 搬运
 * @param  addr: 缓冲区首地址
 * @param  len:  长度
 */
static void DCache_CleanByAddr_Any(void *addr, uint32_t len)
{
#if defined(SCB_CleanDCache_by_Addr)
    uint32_t a = _align_down_32((uint32_t)addr);
    uint32_t end = _align_up_32(((uint32_t)addr) + len);
    // 调用 CMSIS 标准库函数执行汇编指令
    SCB_CleanDCache_by_Addr((uint32_t *)a, (int32_t)(end - a));
#else
    (void)addr;
    (void)len;
#endif
}

/**
 * @brief  Invalidate D-Cache (使 Cache 失效，强制 CPU 下次从物理内存读取)
 * @note   用于 DMA 接收后：DMA 搬运数据到内存 -> Invalidate -> CPU 读内存新数据
 */
static void DCache_InvalidateByAddr_Any(void *addr, uint32_t len)
{
#if defined(SCB_InvalidateDCache_by_Addr)
    uint32_t a = _align_down_32((uint32_t)addr);
    uint32_t end = _align_up_32(((uint32_t)addr) + len);
    SCB_InvalidateDCache_by_Addr((uint32_t *)a, (int32_t)(end - a));
#else
    (void)addr;
    (void)len;
#endif
}

/* ================= 内存分配 ================= */

/* 发送缓冲区：512KB 放 SDRAM（LVGL 池之后 0xC0600000），支持 4 通道全量上传（4096 波形 + 1024 FFT/通道，step=1），且不占 AXI 避免 UI 卡死。 */
#define HTTP_PACKET_BUF_SIZE      (524288u)
#define HTTP_PACKET_BUF_SDRAM_ADDR ((uint8_t *)0xC0600000)
static uint8_t *http_packet_buf;

static void ensure_http_packet_buf(void)
{
    if (http_packet_buf == NULL)
        http_packet_buf = HTTP_PACKET_BUF_SDRAM_ADDR;
}

/* 简单的接收缓冲，用于 AT 指令阻塞接收 (AT模式下数据量小，且使用轮询，不需要DMA) */
static uint8_t esp_rx_buf[512];

static inline uint8_t ESP_RxBusyDetected(void)
{
    return (strstr((char *)esp_rx_buf, "busy p") != NULL) ||
           (strstr((char *)esp_rx_buf, "busy s") != NULL) ||
           (strstr((char *)esp_rx_buf, "BUSY") != NULL);
}

/* 4 个通道的传感器数据结构体实例，用于存储电压、电流、FFT结果等
 * 4096 点波形 + 2048 点 FFT 时该结构体较大，放 AXI SRAM 避免 DTCM 溢出导致卡死。 */
Channel_Data_t node_channels[4] AXI_SRAM_SECTION;
volatile uint8_t g_esp_ready = 0; // 全局标志：1 表示 WiFi/TCP 就绪，可以发送
/* UI“开始/停止上报”开关：用于门控后台自动重连等行为 */
static volatile uint8_t g_report_enabled = 0;
static volatile uint8_t g_ui_wifi_ok;
static volatile uint8_t g_ui_tcp_ok;
static volatile uint8_t g_ui_reg_ok;
static volatile uint8_t g_ui_auto_recover_want_report;

static SystemConfig_t g_sys_cfg;
static uint8_t g_sys_cfg_loaded = 0;

static void ESP_LoadConfig(void)
{
    if (g_sys_cfg_loaded) {
        return;
    }
    /* ⚠️ 启动阶段不要依赖 SD 卡：
     * - SD 可能未插入/未就绪，FatFs f_mount/f_read 在某些异常场景会阻塞很久
     * - 这里仅加载默认值；真正从 SD 读取/保存由“配置界面”触发
     */
    SD_Config_SetDefaults(&g_sys_cfg);
    g_sys_cfg_loaded = 1;
}

const SystemConfig_t *ESP_Config_Get(void)
{
    if (!g_sys_cfg_loaded) {
        ESP_LoadConfig();
    }
    return &g_sys_cfg;
}

void ESP_Config_Apply(const SystemConfig_t *cfg)
{
    if (!cfg) {
        return;
    }
    g_sys_cfg = *cfg;
    g_sys_cfg_loaded = 1;
}

/* DSP 相关变量：用于 FFT 计算 */
static arm_rfft_fast_instance_f32 S;
static uint8_t fft_initialized = 0;
static uint8_t channels_metadata_initialized = 0;
static float32_t fft_input_buf[WAVEFORM_POINTS] AXI_SRAM_SECTION;  // FFT 输入缓冲（避免破坏原波形）
static float32_t fft_output_buf[WAVEFORM_POINTS] AXI_SRAM_SECTION; // FFT 输出复数数组
static float32_t fft_mag_buf[WAVEFORM_POINTS] AXI_SRAM_SECTION;    // FFT 幅值数组

/* UI 模式/非 ESP_Init 路径也必须初始化通道元数据，否则后端会把 4 个通道都当成 id=0 覆盖成“一个通道” */
static void ESP_Init_Channels_And_DSP(void)
{
    /* FFT init */
    if (!fft_initialized)
    {
        arm_rfft_fast_init_f32(&S, WAVEFORM_POINTS);
        fft_initialized = 1;
    }

    if (channels_metadata_initialized)
    {
        return;
    }

    /* 通道元数据（与后端识别规则对应） */
    memset(node_channels, 0, sizeof(node_channels));
    node_channels[0].id = 0;
    strncpy(node_channels[0].label, "直流母线(+)", 31);
    strncpy(node_channels[0].unit, "V", 7);

    node_channels[1].id = 1;
    strncpy(node_channels[1].label, "直流母线(-)", 31);
    strncpy(node_channels[1].unit, "V", 7);

    node_channels[2].id = 2;
    strncpy(node_channels[2].label, "负载电流", 31);
    strncpy(node_channels[2].unit, "A", 7);

    node_channels[3].id = 3;
    strncpy(node_channels[3].label, "漏电流", 31);
    strncpy(node_channels[3].unit, "mA", 7);
    channels_metadata_initialized = 1;
}

/* 内部函数声明 */
static void ESP_Log(const char *format, ...);
static uint8_t ESP_Send_Cmd(const char *cmd, const char *reply, uint32_t timeout);
static uint8_t ESP_Send_Cmd_Any(const char *cmd, const char *reply1, const char *reply2, uint32_t timeout);
static void ESP_Clear_Error_Flags(void);
static int Helper_FloatArray_To_String(char **pp, const char *end, const float *data, int count, int step);
static int Helper_FloatArray1dp_To_String(char **pp, const char *end, const float *data, int count, int step);
static void ESP_Exit_Transparent_Mode(void);
static uint8_t ESP_Exit_Transparent_Mode_Strict(uint32_t timeout_ms);
static void ESP_Uart2_Drain(uint32_t ms);
static uint8_t ESP_Wait_Keyword(const char *kw, uint32_t timeout_ms);
static void ESP_SoftReconnect(void);
static uint8_t ESP_TryReuseTransparent(void);
static void ESP_HardReset(void);
#if 0
static void Process_Channel_Data(int ch_id, float base_dc, float ripple_amp, float noise_level);
#endif
static UART_HandleTypeDef *ESP_GetLogUart(void);
static void ESP_Log_RxBuf(const char *tag);
static void ESP_SetFaultCode(const char *code);
static void ESP_Console_HandleLine(char *line);
static void StrTrimInPlace(char *s);
static void ESP_StreamRx_Start(void);
static void ESP_StreamRx_Feed(const uint8_t *data, uint16_t len);
void ESP_UI_Internal_OnLog(const char *line);
static void ESP_SetServerReportMode(uint8_t full);
static void ESP_SPI_ResetLocalReportState(const char *reason);
#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
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
#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
static uint8_t ESP_SPI_FullPacketTxBusy(void);
static uint8_t ESP_SPI_FullControlBusy(void);
static void ESP_SPI_QueueCommParamsSync(const ESP_CommParams_t *p);
static void ESP_SPI_QueueReportModeSync(uint8_t full);
static void ESP_SPI_ServiceDeferredSync(void);
#endif

// “核武器”：强制停止 USART2 的 RX DMA/中断状态机，切换到 AT(阻塞收发)前必须调用
static void ESP_ForceStop_DMA(void);
static uint8_t ESP_BufContains(const uint8_t *buf, uint16_t len, const char *needle);

// 当前上报的故障码（默认正常 E00），可通过串口控制台动态修改
static char g_fault_code[4] = "E00";

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

#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
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
// 当从 USART2 收到的 HTTP 响应中提取到 "reset" 时置 1
static volatile uint8_t g_server_reset_pending = 0;
// 服务器请求的上报模式：0=summary, 1=full
static volatile uint8_t g_server_report_full = (ESP32_SPI_STRESS_FULL_UPLOAD != 0) ? 1U : 0U;
static volatile uint8_t g_server_report_full_dirty = 0;
#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
static volatile uint16_t g_spi_full_manual_frames = 0;
static volatile uint8_t g_spi_full_continuous = (ESP32_SPI_FULL_CONTINUOUS_DEFAULT != 0) ? 1U : 0U;
static volatile uint8_t g_spi_full_waiting_result = 0;
static uint32_t g_spi_full_result_ref_seq = 0U;
static uint32_t g_spi_full_result_frame_id = 0U;
static uint32_t g_spi_full_result_start_tick = 0U;
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
#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
static volatile uint8_t g_spi_comm_params_sync_pending = 0U;
static volatile uint8_t g_spi_report_mode_sync_pending = 0U;
static volatile uint8_t g_spi_report_mode_sync_target = 0U;
static ESP_CommParams_t g_spi_pending_comm_params;
#endif
// 当检测到链路异常关键字（CLOSED/ERROR）时置 1，主循环触发软重连
static volatile uint8_t g_link_reconnect_pending = 0;

/* RX DMA 缓冲：用于接收服务器回包 (HTTP响应 / reset命令)。
 * 位于 AXI SRAM，加大到 4096 以降低 IDLE 中断触发频率，减少高负载下的丢包风险。 */
static uint8_t g_stream_rx_buf[4096] AXI_SRAM_SECTION DMA_ALIGN32;

/* 滑动窗口：用于在流式数据中查找关键字。
 * 解决 DMA 分包导致关键字（如 "HTTP/1.1"）被切断的问题。 */
static char g_stream_window[256];
static uint16_t g_stream_window_len = 0;

/* 调试与统计变量 */
static volatile uint32_t g_usart2_rx_events = 0;  // 接收中断次数
static volatile uint32_t g_usart2_rx_bytes = 0;   // 接收总字节数
static volatile uint8_t g_usart2_rx_started = 0;  // DMA 接收开启标志
static volatile uint32_t g_usart2_rx_restart = 0; // 重启次数
static volatile uint32_t g_uart2_err = 0;         // 总错误数
static volatile uint32_t g_uart2_err_ore = 0;     // 溢出错误 (Overrun) - 2Mbps下需重点关注
static volatile uint32_t g_uart2_err_fe = 0;      // 帧错误
static volatile uint32_t g_uart2_err_ne = 0;      // 噪声错误
static volatile uint32_t g_uart2_err_pe = 0;      // 校验错误

/* 链路健康监测 */
static volatile uint32_t g_last_rx_tick = 0;     // 上次收到数据的时间
static volatile uint8_t g_link_reconnecting = 0; // 是否正在重连中
static uint8_t g_boot_hardreset_done = 0;        // 启动时是否已执行过硬复位

/* HTTP 发送流控门控
 * 作用：发送 HTTP 请求后置为 1，收到回复或超时后置为 0。
 * 防止请求发送过快淹没服务器，导致 TCP 拥塞或解析错误。 */
static volatile uint8_t g_waiting_http_response = 0;
static volatile uint32_t g_waiting_http_tick = 0;

/* 分段发送状态（仅用于 ESP_Post_Data 的大包上报） */
typedef struct {
    uint32_t total_len;
    uint32_t offset;
    uint32_t next_tick;
    uint8_t  active;
} esp_tx_chunk_t;
static esp_tx_chunk_t g_tx_chunk = {0};

/* 非分段 HTTP 发送链状态：header DMA -> body DMA */
typedef enum {
    ESP_HTTP_TX_IDLE = 0,
    ESP_HTTP_TX_HEADER_INFLIGHT = 1,
    ESP_HTTP_TX_BODY_INFLIGHT = 2,
} esp_http_tx_phase_t;
static volatile esp_http_tx_phase_t g_http_tx_phase = ESP_HTTP_TX_IDLE;
static uint8_t *g_http_tx_body_ptr = NULL;
static uint32_t g_http_tx_body_len = 0;

/* ================= 通讯参数（运行时缓存） =================
 * 由 SD 文件 0:/config/ui_param.cfg 加载；若未加载则使用宏默认值。
 * 这些值会被 ESP_Post_Data/ESP_Post_Heartbeat/自恢复逻辑实时读取。
 */
static volatile uint32_t g_comm_heartbeat_ms    = (uint32_t)ESP_HEARTBEAT_INTERVAL_MS;
static volatile uint32_t g_comm_min_interval_ms = (uint32_t)ESP_MIN_SEND_INTERVAL_MS;
static volatile uint32_t g_comm_http_timeout_ms = (uint32_t)ESP_HTTP_TIMEOUT_MS_DEFAULT;
static volatile uint32_t g_comm_hardreset_sec   = (uint32_t)ESP_NO_SERVER_RX_HARDRESET_SEC;
static volatile uint32_t g_comm_wave_step       = (uint32_t)WAVEFORM_SEND_STEP;
static volatile uint32_t g_comm_upload_points  = (uint32_t)WAVEFORM_POINTS;
static volatile uint32_t g_comm_chunk_kb        = (uint32_t)ESP_CHUNK_KB_DEFAULT;
static volatile uint32_t g_comm_chunk_delay_ms  = (uint32_t)ESP_CHUNK_DELAY_MS_DEFAULT;

/* USART2 流式接收：DMA Circular + IDLE/TC/HT 回调中按“写指针”增量取数据，避免每次回调停/启 DMA 产生空窗导致 ORE。 */
static volatile uint16_t g_stream_rx_last_pos = 0;
static uint32_t g_last_heartbeat_tick = 0;

/* 关键标志位：指示当前是否处于 AT 命令模式
 * 当处于 AT 模式（使用阻塞式 HAL_UART_Receive）时，必须禁止 DMA 中断回调逻辑介入。
 * 否则 HAL_UARTEx_RxEventCallback 会打断 HAL_UART_Receive，导致状态机错乱死锁。 */
static volatile uint8_t g_uart2_at_mode = 0;

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
    uint32_t cdly  = clamp_u32(p->chunk_delay_ms,  0u,   200u);

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
#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
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
    if (!cfg_ok || !ESP_Config_IsValidForLink(cfg)) {
        ESP_Log("[ESP_CFG] runtime SD load invalid: cfg_ok=%u ssid=%u host=%u port=%u node=%u\r\n",
                cfg_ok ? 1U : 0U,
                (cfg && cfg->wifi_ssid[0]) ? 1U : 0U,
                (cfg && cfg->server_ip[0]) ? 1U : 0U,
                (cfg && cfg->server_port) ? 1U : 0U,
                (cfg && cfg->node_id[0]) ? 1U : 0U);
        return false;
    }

    if (!comm_ok) {
        ESP_Log("[ESP_CFG] ui_param.cfg missing/unreadable; using runtime/default comm params.\r\n");
    }
    return true;
}

// =================================================================================
// 工具函数实现
// =================================================================================

/**
 * @brief  在缓冲区中查找子字符串（朴素匹配算法）
 * @note   用于在 DMA 原始 buffer 中直接搜索关键字
 */
static uint8_t ESP_BufContains(const uint8_t *buf, uint16_t len, const char *needle)
{
    if (!buf || len == 0 || !needle || !*needle)
        return 0;
    uint16_t nlen = (uint16_t)strlen(needle);
    if (nlen == 0 || nlen > len)
        return 0;
    // 朴素扫描：len<=4096，needle 很短，效率足够高
    for (uint16_t i = 0; i + nlen <= len; i++)
    {
        if (memcmp(buf + i, needle, nlen) == 0)
            return 1;
    }
    return 0;
}

/**
 * @brief  【核武器级函数】强制停止 DMA 并复位串口状态机
 * @note   这是解决 STM32 HAL 库混合使用 阻塞模式(AT) 和 DMA模式(透传) 导致死锁的关键。
 * HAL 库如果检测到 DMA 正在运行或 RxState 为 BUSY_RX_DMA，
 * 调用阻塞式的 HAL_UART_Receive 会直接返回 HAL_BUSY。
 * 所以在发送 AT 指令前，必须先调用此函数“杀掉”所有后台 DMA 任务。
 */
static void ESP_ForceStop_DMA(void)
{
    // 1) 关闭 IDLE 中断（ReceiveToIdle 依赖 IDLE，AT 阶段不应触发）
    __HAL_UART_DISABLE_IT(&huart2, UART_IT_IDLE);

    // 2) 停止 DMA（包括 DMAR/DMAT），并强制 Abort UART
    // 这会将 HAL 内部状态重置为 READY
    (void)HAL_UART_DMAStop(&huart2);
    (void)HAL_UART_Abort(&huart2);

    // 3) 暴力复位 HAL 句柄状态 (双重保险，防止 Abort 不彻底)
    /* 注意：这是直接操作 HAL 结构体成员，为了应对极端卡死情况 */
    // huart2.gState = HAL_UART_STATE_READY; // TX 状态
    // huart2.RxState = HAL_UART_STATE_READY; // RX 状态

    // 4) 清除可能残留的硬件错误标志 (ORE/FE等)，避免一开启中断就进 ErrorHandler
    ESP_Clear_Error_Flags();

    // 5) 更新软件状态标志
    g_usart2_rx_started = 0;
    g_waiting_http_response = 0;
    g_stream_rx_last_pos = 0;
    g_http_tx_phase = ESP_HTTP_TX_IDLE;
    g_http_tx_body_ptr = NULL;
    g_http_tx_body_len = 0;
}

/**
 * @brief  获取用于打印日志的串口句柄
 */
static UART_HandleTypeDef *ESP_GetLogUart(void)
{
#if (ESP_LOG_UART_PORT == 1)
    extern UART_HandleTypeDef huart1;
    return &huart1;
#elif (ESP_LOG_UART_PORT == 2)
    return &huart2; // ⚠️一般不建议：USART2 用于 ESP8266 通信，混用会干扰协议
#else
    extern UART_HandleTypeDef huart1;
    return &huart1; // 默认回退 USART1
#endif
}

static void ESP_Log_RxBuf(const char *tag)
{
#if (ESP_DEBUG)
    if (tag == NULL)
        tag = "RX";
    ESP_Log("[ESP 回显 %s] << %s\r\n", tag, esp_rx_buf);
#else
    (void)tag;
#endif
}

/* ================= 核心代码 ================= */

/**
 * @brief  ESP8266 初始化主流程
 * @note   包含：硬复位 -> 复用探测 -> AT初始化 -> WiFi连接 -> TCP连接 -> 透传模式 -> 开启DMA监听
 */
void ESP_Init(void)
{
    char cmd_buf[128];
    g_esp_ready = 0;
    int retry_count = 0;

    // 尝试从 SD 卡加载配置（如果失败则使用默认值）
    /* 启动阶段仅使用默认配置，避免 SD 卡异常导致启动卡死 */
    ESP_LoadConfig();
    ESP_Log("[ESP] Using default config SSID=%s\r\n", g_sys_cfg.wifi_ssid);

    // 步骤 1: 进 AT 模式前，必须清场，确保 UART 处于 READY 状态
    // 否则 HAL_UART_Receive 会直接返回 BUSY，导致初始化失败
    g_uart2_at_mode = 1;
    ESP_ForceStop_DMA();

    // 步骤 2: 启动时强制硬复位一次：保证 ESP 状态干净（用户配置）
#if (ESP_BOOT_HARDRESET_ONCE)
    if (!g_boot_hardreset_done)
    {
        g_boot_hardreset_done = 1;
        ESP_Log("[ESP] 启动硬复位一次...\r\n");
        ESP_HardReset();
    }
#endif

    // 确保 ESP8266 不处于硬件复位状态（RST 低有效，必须拉高）
#ifdef ESP8266_RST_Pin
    HAL_GPIO_WritePin(ESP8266_RST_GPIO_Port, ESP8266_RST_Pin, GPIO_PIN_SET);
#endif

    // 初始化 DSP 库 (FFT)
    if (!fft_initialized)
    {
        arm_rfft_fast_init_f32(&S, WAVEFORM_POINTS);
        fft_initialized = 1;
    }

    ESP_Log("\r\n[ESP] 初始化（4通道模式）...\r\n");
    ESP_Log("[ESP] WiFi 名称(SSID): %s\r\n", g_sys_cfg.wifi_ssid);
    ESP_Log("[ESP] 服务器地址: %s:%d\r\n", g_sys_cfg.server_ip, g_sys_cfg.server_port);
    ESP_Clear_Error_Flags();

    // 步骤 3: 优先尝试复用“透传 + TCP”现有连接
    // 场景：MCU 只是软复位，而 ESP8266 还在透传模式且连接正常，此时无需断电重连，直接发包即可
    if (ESP_TryReuseTransparent())
    {
        g_esp_ready = 1;
        g_uart2_at_mode = 0;
        ESP_Log("[ESP] 复用透传连接成功（无需断电可重连）\r\n");
        return;
    }

    // 步骤 4: 复用失败：必须先确保回到 AT 命令模式
    // 注意：ESP_Send_Cmd 的超时本质不是“等待不够”，而是“没收到任何字节”。
    // 需要先尝试发送 "+++" 退出透传，并确认能收到 "OK"。
    ESP_Uart2_Drain(120);
    uint8_t at_ok = 0;
    for (int k = 0; k < 3; k++)
    {
        if (ESP_Send_Cmd("AT\r\n", "OK", 800))
        {
            at_ok = 1;
            break;
        }
        (void)ESP_Exit_Transparent_Mode_Strict(2000); // 尝试发送 +++ 退出透传
        ESP_Uart2_Drain(120);
    }

    // 如果还是进不去 AT 模式，执行硬复位 (硬件重启模组)
    if (!at_ok)
    {
        ESP_Log("[ESP] AT无回显,执行硬复位ESP8266...\r\n");
        ESP_HardReset();
        // 硬复位后，HAL 库状态可能被中断打断，再次确保处于 AT(阻塞)可用状态
        ESP_ForceStop_DMA();

        // 复位后再探测一次 AT
        if (!ESP_Send_Cmd("AT\r\n", "OK", 1500))
        {
					ESP_Log("[ESP] 致命:硬复位后仍无法进入 AT 模式\r\n");
            return;
        }
    }

    /* 复位流程 */
    ESP_Log("[ESP 指令] >> AT+RST\r\n");
    HAL_UART_Transmit(&huart2, (uint8_t *)"AT+RST\r\n", 8, 100);
    HAL_Delay(3500); // 等待模组启动日志打印完成
    ESP_Clear_Error_Flags();

    // 关闭回显 (ATE0)，方便后续指令解析
    while (!ESP_Send_Cmd("ATE0\r\n", "OK", 500))
    {
        ESP_Log("[ESP] 关闭回显失败,重试 ATE0...\r\n");
        ESP_Clear_Error_Flags();
        HAL_Delay(500);
        retry_count++;
        if (retry_count > 5)
            break;
    }

    // 设置 Station 模式
    ESP_Send_Cmd("AT+CWMODE=1\r\n", "OK", 1000);

    // 连接 WiFi
    sprintf(cmd_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n", g_sys_cfg.wifi_ssid, g_sys_cfg.wifi_password);
    if (!ESP_Send_Cmd(cmd_buf, "GOT IP", 20000)) // 给足 20s 超时
    {
        // 重试一次
        if (!ESP_Send_Cmd(cmd_buf, "GOT IP", 20000))
        {
            ESP_Log("[ESP] WiFi 连接失败。\r\n");
            ESP_Log_RxBuf("WIFI_FAIL");
            return;
        }
    }
    ESP_Log("[ESP] WiFi 连接成功（已获取 IP）。\r\n");
    HAL_Delay(1200);
    ESP_Uart2_Drain(200);
    // 打印 STA IP 信息（便于确认是否进到目标网段）
    for (int k = 0; k < 3; k++)
    {
        if (ESP_Send_Cmd("AT+CIFSR\r\n", "STAIP", 3000))
        {
            break;
        }
        ESP_Log("[ESP] CIFSR 无响应/忙,准备重试...\r\n");
        HAL_Delay(600);
    }
    ESP_Log_RxBuf("CIFSR");

    /* TCP 连接 */
    // 先关闭可能存在的旧连接（无连接时可能返回 ERROR，视为成功）
    (void)ESP_Send_Cmd_Any("AT+CIPCLOSE\r\n", "OK", "ERROR", 500);
    sprintf(cmd_buf, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", g_sys_cfg.server_ip, g_sys_cfg.server_port);
    uint8_t tcp_ok = 0;
    for (int k = 0; k < 3; k++)
    {
        if (ESP_Send_Cmd(cmd_buf, "CONNECT", 10000))
        {
            tcp_ok = 1;
            break;
        }
        // 如果返回 ALREADY，说明连接还在，也算成功
        if (strstr((char *)esp_rx_buf, "ALREADY") != NULL)
        {
            tcp_ok = 1;
            break;
        }
        if (ESP_RxBusyDetected())
        {
            ESP_Log("[ESP] CIPSTART busy，等待后重试...\r\n");
            HAL_Delay(800);
            continue;
        }
        ESP_Log("[ESP] CIPSTART 失败,准备重试...\r\n");
        HAL_Delay(800);
    }
    if (!tcp_ok)
    {
        ESP_Log("[ESP] TCP 连接失败。\r\n");
        ESP_Log_RxBuf("TCP_FAIL");
        return;
    }
    ESP_Log("[ESP] TCP 连接成功（CONNECT）。\r\n");
    (void)ESP_Send_Cmd_Any("AT+CIPSTATUS\r\n", "STATUS:", "OK", 1000);
    ESP_Log_RxBuf("CIPSTATUS");

    // 开启透传模式 (UART <-> WiFi 透明传输)
    ESP_Send_Cmd("AT+CIPMODE=1\r\n", "OK", 1000);
    ESP_Send_Cmd("AT+CIPSEND\r\n", ">", 2000); // 等待出现 '>' 符号
    HAL_Delay(500);

    // 发送注册包 (告诉服务器我是谁)
    ESP_Register();
    g_esp_ready = 1;
    ESP_Log("[ESP] 系统就绪（4通道）,开始上报数据。\r\n");

    // 步骤 5: 启动 USART2 接收（DMA + IDLE）
    // 用于接收 /api/node/heartbeat 的响应中的 command/reset
    // 此时正式退出 AT 模式，进入数据监听模式
    g_uart2_at_mode = 0;
    ESP_StreamRx_Start();

    /* === 初始化 4 个通道的元数据 (与 api.py 逻辑严格对应) === */
    // Channel 0: 直流母线(+) -> api.py 识别 "直流" 且无 "负/-" -> voltage
    node_channels[0].id = 0;
    strncpy(node_channels[0].label, "直流母线(+)", 31);
    strncpy(node_channels[0].unit, "V", 7);

    // Channel 1: 直流母线(-) -> api.py 识别 "直流" 且有 "负/-" -> voltage_neg
    node_channels[1].id = 1;
    strncpy(node_channels[1].label, "直流母线(-)", 31);
    strncpy(node_channels[1].unit, "V", 7);

    // Channel 2: 负载电流 -> api.py 识别 "负载" 或 "电流" -> current
    node_channels[2].id = 2;
    strncpy(node_channels[2].label, "负载电流", 31);
    strncpy(node_channels[2].unit, "A", 7);

    // Channel 3: 漏电流 -> api.py 识别 "漏" -> leakage
    node_channels[3].id = 3;
    strncpy(node_channels[3].label, "漏电流", 31);
    strncpy(node_channels[3].unit, "mA", 7);
}

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
#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
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
#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
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
#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
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

    if (strcmp(line, "fullstat") == 0 || strcmp(line, "FULLSTAT") == 0)
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
#if (EW_USE_ESP32_SPI_UI)
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
#else
    (void)reason;
#endif
}

#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
static void ESP_SPI_FullClearWaitState(void)
{
    g_spi_full_waiting_result = 0U;
    g_spi_full_result_ref_seq = 0U;
    g_spi_full_result_frame_id = 0U;
    g_spi_full_result_start_tick = 0U;
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
    return (g_full_tx_sm.active != 0U || g_spi_full_waiting_result != 0U) ? 1U : 0U;
}

static uint8_t ESP_SPI_FullPacketTxBusy(void)
{
    return (g_full_tx_sm.active != 0U) ? 1U : 0U;
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

    if (!g_esp_ready || ESP_SPI_FullPacketTxBusy()) {
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
            ESP_Log("[服务器命令] ESP32同步通信参数失败，延后重试\r\n");
            ESP_SPI_QueueCommParamsSync(&pending_comm);
            ESP_SPI_FullEnterHoldoff("deferred comm sync failed",
                                     ESP32_SPI_FULL_BUSY_HOLDOFF_MS);
            return;
        }
        ESP_Log("[服务器命令] ESP32同步通信参数成功\r\n");
    }

    if (do_report_sync != 0U && g_report_enabled != 0U) {
        if (g_spi_full_waiting_result == 0U) {
            ESP_SPI_FullResetUploadRuntimeState();
        }
        if (!ESP32_SPI_StartReport(report_target, 3000U)) {
            ESP_Log("[服务器命令] ESP32同步上报模式失败，延后重试\r\n");
            ESP_SPI_QueueReportModeSync(report_target);
            ESP_SPI_FullEnterHoldoff("deferred report sync failed",
                                     ESP32_SPI_FULL_BUSY_HOLDOFF_MS);
            return;
        }
        ESP_Log("[服务器命令] ESP32同步上报模式成功\r\n");
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
#if (EW_USE_ESP32_SPI_UI)
    if (!p || !g_esp_ready) {
        return;
    }
#if (ESP32_SPI_ENABLE_FULL_UPLOAD)
    if (ESP_SPI_FullPacketTxBusy()) {
        ESP_SPI_QueueCommParamsSync(p);
        ESP_Log("[服务器命令] ESP32通信参数同步延后：等待当前SPI full分包结束\r\n");
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
        ESP_Log("[服务器命令] ESP32同步通信参数失败\r\n");
    } else {
        ESP_Log("[服务器命令] ESP32同步通信参数成功\r\n");
    }
#else
    (void)p;
#endif
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
    // 1) 处理“已收到的一整行”控制台指令
    if (g_console_line_ready)
    {
        g_console_line_ready = 0;
        g_console_line[g_console_line_len] = 0;
        ESP_Console_HandleLine(g_console_line);
        g_console_line_len = 0;
    }

#if (EW_USE_ESP32_SPI_UI)
    {
        static uint32_t s_last_async_event_poll_tick = 0U;
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

        if (ESP32_SPI_ConsumeServerCommand(&reset,
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
                ESP_Log("[服务器命令] 收到命令：通信参数 心跳=%lums 间隔=%lums HTTP=%lums 降采样=%lu 上传点数=%lu 分块=%luKB 延时=%lums\r\n",
                        (unsigned long)p.heartbeat_ms,
                        (unsigned long)p.min_interval_ms,
                        (unsigned long)p.http_timeout_ms,
                        (unsigned long)p.wave_step,
                        (unsigned long)p.upload_points,
                        (unsigned long)p.chunk_kb,
                        (unsigned long)p.chunk_delay_ms);
                ESP_CommParams_Apply(&p);
                ESP_Log("[服务器命令] 运行态已应用：通信参数\r\n");
                if (ESP_CommParams_SaveToSD()) {
                    ESP_Log("[服务器命令] SD卡保存成功：通信参数\r\n");
                    ESP_SPI_ApplyCommParamsToCoprocessor(&p);
                } else {
                    ESP_CommParams_Apply(&old_p);
                    ESP_Log("[服务器命令] SD卡保存失败：通信参数，运行态已回滚\r\n");
                }
            }
        }
    }
#endif

    // 2) 处理“服务器下发 reset”指令
    if (g_server_reset_pending)
    {
        g_server_reset_pending = 0;
        ESP_SetFaultCode("E00");
        ESP_Log("[服务器命令] 运行态已应用：故障码清除为E00\r\n");
    }

    // 2.0) Apply server-issued report_mode.
    if (g_server_report_full_dirty)
    {
        uint8_t full = g_server_report_full ? 1U : 0U;
        const char *mode_text = full ? "全量" : "摘要";
        g_server_report_full_dirty = 0;
        ESP_Log("[服务器命令] 开始应用：上报模式=%s\r\n", mode_text);
        if (!ESP_UploadMode_SaveToSD(full)) {
            g_server_report_full = full ? 0U : 1U;
            ESP_Log("[服务器命令] SD卡保存失败：上报模式=%s，运行态未切换\r\n",
                    mode_text);
            return;
        }
        ESP_Log("[服务器命令] SD卡保存成功：上报模式=%s\r\n", mode_text);
#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
        g_spi_full_continuous = full;
        if (!full) {
            g_spi_full_manual_frames = 0U;
        }
#endif
        /* The final clean status log is emitted below.  Avoid an extra
         * user-visible UI log here.
         */
#if (EW_USE_ESP32_SPI_UI)
        if (g_esp_ready && g_report_enabled) {
#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
            if (ESP_SPI_FullPacketTxBusy()) {
                ESP_SPI_QueueReportModeSync(full);
                ESP_Log("[服务器命令] ESP32上报模式同步延后：等待当前SPI full分包结束\r\n");
            } else {
                if (g_spi_full_waiting_result == 0U) {
                    ESP_SPI_FullResetUploadRuntimeState();
                }
                if (ESP32_SPI_StartReport(full, 3000U)) {
                    ESP_Log("[服务器命令] ESP32同步成功：上报模式=%s\r\n",
                            mode_text);
                } else {
                    ESP_Log("[服务器命令] ESP32同步失败：上报模式=%s\r\n",
                            mode_text);
                }
            }
#else
            if (ESP32_SPI_StartReport(full, 3000U)) {
                ESP_Log("[服务器命令] ESP32同步成功：上报模式=%s\r\n",
                        mode_text);
            } else {
                ESP_Log("[服务器命令] ESP32同步失败：上报模式=%s\r\n",
                        mode_text);
            }
#endif
        }
#endif
        ESP_Log("[服务器命令] 运行态已应用：上报模式=%s\r\n", mode_text);
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
        ESP_Log("[服务器命令] 开始应用：降采样步进=%lu\r\n", (unsigned long)target);

        if (p.wave_step != target) {
            p.wave_step = target;
            ESP_CommParams_Apply(&p);
        }
        ESP_Log("[服务器命令] 运行态已应用：降采样步进=%lu\r\n", (unsigned long)target);

        if (ESP_CommParams_SaveToSD()) {
            ESP_Log("[服务器命令] SD卡保存成功：降采样步进=%lu\r\n", (unsigned long)target);
            ESP_SPI_ApplyCommParamsToCoprocessor(&p);
        } else {
            ESP_CommParams_Apply(&old_p);
            ESP_Log("[服务器命令] SD卡保存失败：降采样步进=%lu，运行态已回滚\r\n",
                    (unsigned long)target);
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
        ESP_Log("[服务器命令] 开始应用：上传点数=%lu\r\n", (unsigned long)target);

        if (p.upload_points != target) {
            p.upload_points = target;
            ESP_CommParams_Apply(&p);
        }
        ESP_Log("[服务器命令] 运行态已应用：上传点数=%lu\r\n", (unsigned long)target);

        if (ESP_CommParams_SaveToSD()) {
            ESP_Log("[服务器命令] SD卡保存成功：上传点数=%lu\r\n", (unsigned long)target);
            ESP_SPI_ApplyCommParamsToCoprocessor(&p);
        } else {
            ESP_CommParams_Apply(&old_p);
            ESP_Log("[服务器命令] SD卡保存失败：上传点数=%lu，运行态已回滚\r\n",
                    (unsigned long)target);
        }
    }

#if (ESP_LEGACY_UART_LINK_WATCHDOG)
    if (g_report_enabled && g_link_reconnect_pending && g_esp_ready && !g_link_reconnecting && !g_uart2_at_mode)
    {
        g_link_reconnect_pending = 0;
        ESP_Log("[ESP] 侦测到链路异常关键字,触发软重连...\r\n");
        ESP_SoftReconnect();
    }
#endif

#if (ESP_DEBUG && ESP_DEBUG_STATS)
    // 3) 调试：每 1 秒输出一次统计信息 (确认 RX 是否存活)
    static uint32_t last_dbg = 0;
    if ((now - last_dbg) >= 1000)
    {
        last_dbg = now;
        ESP_Log("[调试] USART2 RX: started=%d, events=%lu, bytes=%lu, restart=%lu\r\n",
                (int)g_usart2_rx_started,
                (unsigned long)g_usart2_rx_events,
                (unsigned long)g_usart2_rx_bytes,
                (unsigned long)g_usart2_rx_restart);
        ESP_Log("[调试] USART2 ERR: total=%lu, ORE=%lu, FE=%lu, NE=%lu, PE=%lu\r\n",
                (unsigned long)g_uart2_err,
                (unsigned long)g_uart2_err_ore,
                (unsigned long)g_uart2_err_fe,
                (unsigned long)g_uart2_err_ne,
                (unsigned long)g_uart2_err_pe);
    }
#endif

    // 4) 链路自恢复：如果长时间收不到服务器任何响应，先软重连（更快）
    // 这是为了应对极端情况下 TCP 假死或模组死机
#if (ESP_LEGACY_UART_LINK_WATCHDOG)
    static uint32_t last_link_check = 0;
    static uint8_t no_rx_miss = 0;
    static uint32_t last_rx_bytes_snapshot = 0;
    if ((now - last_link_check) >= 1000)
    {
        last_link_check = now;
        // 只有当“已连接”、“非重连中”且“非AT模式”时才检查
        if (g_report_enabled && g_esp_ready && !g_link_reconnecting && !g_uart2_at_mode)
        {
            // 正常情况下服务器会对每次心跳返回 HTTP 响应
            uint32_t no_rx_ms = (uint32_t)ESP_CommParams_HardResetSec() * 1000u;
            if (no_rx_ms < 5000u)
                no_rx_ms = 5000u; // 最小阈值 5s（避免过短导致抖动）

            // 如果 RX 字节数在增长，说明链路正常
            if (g_usart2_rx_bytes != last_rx_bytes_snapshot)
            {
                last_rx_bytes_snapshot = g_usart2_rx_bytes;
                g_last_rx_tick = now;
                no_rx_miss = 0;
            }
            // 否则检查是否超时
            else if (g_last_rx_tick != 0 && (now - g_last_rx_tick) > no_rx_ms)
            {
                no_rx_miss++;
                // 连续 3 次检测都超时，才判定为断线
                if (no_rx_miss >= 3)
                {
                    ESP_Log("[ESP] 警告：%lus 无服务器响应(Δt=%lums, miss=%u) -> 软重连\r\n",
                            (unsigned long)(no_rx_ms / 1000u),
                            (unsigned long)(now - g_last_rx_tick),
                            (unsigned)no_rx_miss);
                    no_rx_miss = 0;
                    ESP_SoftReconnect();
                }
            }
        }
        else
        {
            /* 停止上报后：禁止后台自动重连/硬复位重连，仅清空计数避免下次误触发 */
            no_rx_miss = 0;
        }
    }
#endif
#else
    (void)0;
#endif

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
}

#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
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

#if (EW_USE_ESP32_SPI_UI && ESP32_SPI_ENABLE_FULL_UPLOAD)
    ESP_SPI_ServiceDeferredSync();
    /* Full-upload stress mode must not be mixed with summary packets.
       This keeps the 10-minute test as continuous full frames only. */
    if (ESP_ServerReportFull() || g_spi_full_continuous) {
        return;
    }
#endif

#if (EW_USE_ESP32_SPI_UI)
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
#endif

    /* 发送节流统计 */
    static uint32_t last_send_time = 0;
    static uint32_t tx_try = 0, tx_ok = 0, tx_busy = 0, tx_err = 0;
    static uint32_t last_tx_log = 0;

    uint32_t now_tick = HAL_GetTick();

    /* 如果正在分段发送，优先推进下一段 */
    if (g_tx_chunk.active) {
        if (now_tick < g_tx_chunk.next_tick) {
            return;
        }
        HAL_UART_StateTypeDef st_uart = HAL_UART_GetState(&huart2);
        if (st_uart == HAL_UART_STATE_BUSY_TX || st_uart == HAL_UART_STATE_BUSY_TX_RX) {
            return;
        }
        goto send_next_chunk;
    }

    /* 非分段 DMA 链发送期间，禁止再次进入发送流程 */
    if (g_http_tx_phase != ESP_HTTP_TX_IDLE) {
        return;
    }

    /* HTTP 门控：发送后等待回包，避免连续请求淹没服务器；超时后自动放行。 */
    if (g_waiting_http_response)
    {
        uint32_t now_gate = HAL_GetTick();
        uint32_t to_ms = ESP_CommParams_HttpTimeoutMs();
        if ((now_gate - g_waiting_http_tick) < to_ms)
            return;
        g_waiting_http_response = 0;
    }

    // 发送频率限制
    uint32_t min_itv = ESP_CommParams_MinIntervalMs();
    if (min_itv && (now_tick - last_send_time < min_itv))
        return;

    ensure_http_packet_buf();
    const uint32_t header_reserve_len = 256;
    char *body = (char *)http_packet_buf + header_reserve_len;
    char *p = body;
    const char *end = (const char *)http_packet_buf + HTTP_PACKET_BUF_SIZE;
    uint32_t body_len = 0;
    int header_len = 0;
    uint32_t total_len = 0;
    static uint32_t s_seq = 0;
    uint32_t seq = ++s_seq;

    // JSON Header
    if (!ESP_Appendf(&p, end, "{\"node_id\":\"%s\",\"status\":\"online\",\"fault_code\":\"%s\",\"seq\":%lu,\"downsample_step\":%lu,\"upload_points\":%lu,\"report_mode\":\"%s\",\"heartbeat_ms\":%lu,\"min_interval_ms\":%lu,\"http_timeout_ms\":%lu,\"chunk_kb\":%lu,\"chunk_delay_ms\":%lu,\"channels\":[",
                     g_sys_cfg.node_id, g_fault_code, (unsigned long)seq,
                     (unsigned long)ESP_CommParams_WaveStep(),
                     (unsigned long)ESP_CommParams_UploadPoints(),
                     g_server_report_full ? "full" : "summary",
                     (unsigned long)ESP_CommParams_HeartbeatMs(),
                     (unsigned long)ESP_CommParams_MinIntervalMs(),
                     (unsigned long)ESP_CommParams_HttpTimeoutMs(),
                     (unsigned long)ESP_CommParams_ChunkKb(),
                     (unsigned long)ESP_CommParams_ChunkDelayMs()))
        return;

    for (int i = 0; i < 4; i++)
    {
        int32_t cv_i = ESP_FloatToI32Scaled(node_channels[i].current_value);
        if (!ESP_Appendf(&p, end,
                         "{"
                         "\"id\":%d,\"channel_id\":%d,"
                         "\"label\":\"%s\",\"name\":\"%s\","
                         "\"value\":%ld,\"current_value\":%ld,"
                         "\"unit\":\"%s\"}"
                         ,
                         node_channels[i].id, node_channels[i].id,
                         node_channels[i].label, node_channels[i].label,
                         (long)cv_i, (long)cv_i,
                         node_channels[i].unit))
            return;
        if (i < 3)
        {
            if (!ESP_Appendf(&p, end, ","))
                return;
        }
    }

    if (!ESP_Appendf(&p, end, "]}"))
        return;

    body_len = (uint32_t)(p - body);
    if (body_len == 0 || body_len > (HTTP_PACKET_BUF_SIZE - header_reserve_len - 64u))
        return;

    header_len = snprintf((char *)http_packet_buf, header_reserve_len,
                          "POST /api/node/heartbeat HTTP/1.1\r\n"
                          "Host: %s:%d\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: %lu\r\n"
                          "\r\n",
                          g_sys_cfg.server_ip, g_sys_cfg.server_port, (unsigned long)body_len);
    if (header_len <= 0 || (uint32_t)header_len >= header_reserve_len)
        return;

    uint32_t total_len_check = (uint32_t)header_len + body_len;
    if (ESP_CommParams_ChunkKb() == 0u && total_len_check <= 65535u)
    {
        DCache_CleanByAddr_Any(http_packet_buf, (uint32_t)header_len);
        DCache_CleanByAddr_Any(body, body_len);
        HAL_StatusTypeDef st = HAL_UART_Transmit_DMA(&huart2, (uint8_t *)http_packet_buf, (uint16_t)header_len);
        tx_try++;
        if (st == HAL_OK) {
            tx_ok++;
            g_http_tx_body_ptr = (uint8_t *)body;
            g_http_tx_body_len = body_len;
            g_http_tx_phase = ESP_HTTP_TX_HEADER_INFLIGHT;
            last_send_time = now_tick;
        } else if (st == HAL_BUSY) {
            tx_busy++;
        } else {
            tx_err++;
        }
        return;
    }

    memmove(http_packet_buf + header_len, body, body_len);
    total_len = (uint32_t)header_len + body_len;
    if (total_len > HTTP_PACKET_BUF_SIZE)
        return;
    DCache_CleanByAddr_Any(http_packet_buf, total_len);

    g_tx_chunk.total_len = total_len;
    g_tx_chunk.offset = 0;
    g_tx_chunk.next_tick = 0;
    g_tx_chunk.active = 1;
    now_tick = HAL_GetTick();

send_next_chunk:
    if (!g_tx_chunk.active) {
        return;
    }
    uint32_t chunk_bytes = ESP_CommParams_ChunkKb() * 1024u;
    if (chunk_bytes == 0u) chunk_bytes = 65535u;
    uint32_t remain = g_tx_chunk.total_len - g_tx_chunk.offset;
    if (remain == 0u) {
        g_tx_chunk.active = 0;
        return;
    }
    if (chunk_bytes > remain) chunk_bytes = remain;
    if (chunk_bytes > 65535u) chunk_bytes = 65535u;

    HAL_StatusTypeDef st = HAL_UART_Transmit_DMA(&huart2,
                                                 (uint8_t *)http_packet_buf + g_tx_chunk.offset,
                                                 (uint16_t)chunk_bytes);
    tx_try++;
    if (st == HAL_OK) {
        tx_ok++;
        g_tx_chunk.offset += chunk_bytes;
        g_tx_chunk.next_tick = now_tick + ESP_CommParams_ChunkDelayMs();
        if (g_tx_chunk.offset >= g_tx_chunk.total_len) {
            g_tx_chunk.active = 0;
            last_send_time = now_tick;
            g_last_heartbeat_tick = now_tick;
            g_waiting_http_response = 1;
            g_waiting_http_tick = now_tick;
        }
    } else if (st == HAL_BUSY) {
        tx_busy++;
    } else {
        tx_err++;
    }

#if (ESP_DEBUG)
    uint32_t now = HAL_GetTick();
    if ((now - last_tx_log) >= 1000)
    {
        last_tx_log = now;
        ESP_Log("[调试] Summary TX: try=%lu ok=%lu busy=%lu err=%lu total=%lu off=%lu chunk=%lu gState=%d rxStarted=%d\r\n",
                (unsigned long)tx_try, (unsigned long)tx_ok, (unsigned long)tx_busy, (unsigned long)tx_err,
                (unsigned long)g_tx_chunk.total_len,
                (unsigned long)g_tx_chunk.offset,
                (unsigned long)chunk_bytes,
                (int)huart2.gState,
                (int)g_usart2_rx_started);
    }
#endif
}

/**
 * @brief  数据发送主函数
 * @note   负责打包 JSON，通过 DMA 发送
 */
void ESP_Post_Data(void)
{
    if (g_esp_ready == 0)
        return;

#if (EW_USE_ESP32_SPI_UI)
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
#endif

    /* 发送节流统计 */
    static uint32_t last_send_time = 0;
    static uint32_t tx_try = 0, tx_ok = 0, tx_busy = 0, tx_err = 0;
    static uint32_t last_tx_log = 0;

    uint32_t now_tick = HAL_GetTick();

    /* 如果正在分段发送，优先推进下一段 */
    if (g_tx_chunk.active) {
        if (now_tick < g_tx_chunk.next_tick) {
            return;
        }
        HAL_UART_StateTypeDef st_uart = HAL_UART_GetState(&huart2);
        if (st_uart == HAL_UART_STATE_BUSY_TX || st_uart == HAL_UART_STATE_BUSY_TX_RX) {
            return;
        }
        goto send_next_chunk;
    }

    /* 非分段 DMA 链发送期间，禁止再次进入发送流程 */
    if (g_http_tx_phase != ESP_HTTP_TX_IDLE) {
        return;
    }

    /* HTTP 门控：发送后等待回包，避免连续请求淹没服务器；超时后自动放行。 */
    if (g_waiting_http_response)
    {
        uint32_t now_gate = HAL_GetTick();
        uint32_t to_ms = ESP_CommParams_HttpTimeoutMs();
        if ((now_gate - g_waiting_http_tick) < to_ms)
            return;
        g_waiting_http_response = 0;
    }

    // 发送频率限制
    uint32_t min_itv = ESP_CommParams_MinIntervalMs();
    if (min_itv && (now_tick - last_send_time < min_itv))
        return;

    ensure_http_packet_buf();
    /* 开始构建 JSON：把 body 放到偏移处，避免对大 body 做 memmove */
    const uint32_t header_reserve_len = 256;
    char *body = (char *)http_packet_buf + header_reserve_len;
    char *p = body;
    const char *end = (const char *)http_packet_buf + HTTP_PACKET_BUF_SIZE;
    uint32_t body_len = 0;
    int header_len = 0;
    uint32_t total_len = 0;
    static uint32_t s_seq = 0;
    uint32_t seq = ++s_seq;

    /* 限点上传：只上传“降采样后的前 N 点” */
    uint32_t step_u = ESP_CommParams_WaveStep();
    uint32_t limit_u = ESP_CommParams_UploadPoints();
    uint32_t raw_count = (uint32_t)WAVEFORM_POINTS;
    if (raw_count > 0u && step_u > 0u && limit_u > 0u) {
        uint64_t need = ((uint64_t)(limit_u - 1u) * (uint64_t)step_u) + 1u;
        if (need < (uint64_t)raw_count) {
            raw_count = (uint32_t)need;
        }
    }

    // JSON Header
    if (!ESP_Appendf(&p, end, "{\"node_id\":\"%s\",\"status\":\"online\",\"fault_code\":\"%s\",\"seq\":%lu,\"downsample_step\":%lu,\"upload_points\":%lu,\"report_mode\":\"%s\",\"heartbeat_ms\":%lu,\"min_interval_ms\":%lu,\"http_timeout_ms\":%lu,\"chunk_kb\":%lu,\"chunk_delay_ms\":%lu,\"channels\":[",
                    g_sys_cfg.node_id, g_fault_code, (unsigned long)seq,
                    (unsigned long)step_u,
                    (unsigned long)limit_u,
                    g_server_report_full ? "full" : "summary",
                    (unsigned long)ESP_CommParams_HeartbeatMs(),
                    (unsigned long)ESP_CommParams_MinIntervalMs(),
                    (unsigned long)ESP_CommParams_HttpTimeoutMs(),
                    (unsigned long)ESP_CommParams_ChunkKb(),
                    (unsigned long)ESP_CommParams_ChunkDelayMs()))
        return;

    // 循环写入 4 个通道的数据
    for (int i = 0; i < 4; i++)
    {
        int32_t cv_i = ESP_FloatToI32Scaled(node_channels[i].current_value);
        if (!ESP_Appendf(&p, end,
                         "{"
                         "\"id\":%d,\"channel_id\":%d,"
                         "\"label\":\"%s\",\"name\":\"%s\","
                         "\"value\":%ld,\"current_value\":%ld,"
                         "\"unit\":\"%s\","
                         "\"waveform\":[",
                         node_channels[i].id, node_channels[i].id,
                         node_channels[i].label, node_channels[i].label, // name冗余label
                         (long)cv_i, (long)cv_i,
                         node_channels[i].unit))
            return;

        // 波形数据（运行时降采样：step=1全量，step=4每4点取1点）
        if (!Helper_FloatArray_To_String(&p, end, node_channels[i].waveform, (int)raw_count, (int)step_u))
            return;

        // 频谱数据
        if (!ESP_Appendf(&p, end, "],\"fft_spectrum\":["))
            return;
        /* FFT 不乘 200：保持原始数值（1 位小数） */
        if (!Helper_FloatArray1dp_To_String(&p, end, node_channels[i].fft_data, FFT_POINTS, 1))
            return;

        // 结束当前 channel
        if (!ESP_Appendf(&p, end, "]}"))
            return;
        if (i < 3)
        {
            if (!ESP_Appendf(&p, end, ",")) // 逗号分隔
                return;
        }
    }

    if (!ESP_Appendf(&p, end, "]}")) // JSON End
        return;

    body_len = (uint32_t)(p - body);
    if (body_len == 0 || body_len > (HTTP_PACKET_BUF_SIZE - header_reserve_len - 64u))
    {
        // 保护：长度异常直接丢弃，避免 memmove 越界导致后续随机坏帧
        return;
    }

    /* 生成 header 到预留区 */
    header_len = snprintf((char *)http_packet_buf, header_reserve_len,
                          "POST /api/node/heartbeat HTTP/1.1\r\n"
                          "Host: %s:%d\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: %lu\r\n"
                          "\r\n",
                          g_sys_cfg.server_ip, g_sys_cfg.server_port, (unsigned long)body_len);
    if (header_len <= 0 || (uint32_t)header_len >= header_reserve_len)
        return;

    /* 非分段仅当整包 ≤ 64KB 时可用：HAL_UART_Transmit_DMA 长度为 uint16_t，超过 65535 会截断导致服务器收不全。 */
    uint32_t total_len_check = (uint32_t)header_len + body_len;
    if (ESP_CommParams_ChunkKb() == 0u && total_len_check <= 65535u)
    {
        DCache_CleanByAddr_Any(http_packet_buf, (uint32_t)header_len);
        DCache_CleanByAddr_Any(body, body_len);
        HAL_StatusTypeDef st = HAL_UART_Transmit_DMA(&huart2, (uint8_t *)http_packet_buf, (uint16_t)header_len);
        tx_try++;
        if (st == HAL_OK) {
            tx_ok++;
            g_http_tx_body_ptr = (uint8_t *)body;
            g_http_tx_body_len = body_len;
            g_http_tx_phase = ESP_HTTP_TX_HEADER_INFLIGHT;
            last_send_time = now_tick;
        } else if (st == HAL_BUSY) {
            tx_busy++;
        } else {
            tx_err++;
        }
        return;
    }

    /* 分段发送：需要把 header+body 变成连续缓冲（仍会 memmove 一次大包） */
    memmove(http_packet_buf + header_len, body, body_len);
    total_len = (uint32_t)header_len + body_len;
    if (total_len > HTTP_PACKET_BUF_SIZE)
        return;
    DCache_CleanByAddr_Any(http_packet_buf, total_len);

    /* 初始化分段发送上下文 */
    g_tx_chunk.total_len = total_len;
    g_tx_chunk.offset = 0;
    g_tx_chunk.next_tick = 0;
    g_tx_chunk.active = 1;
    now_tick = HAL_GetTick();

send_next_chunk:
    if (!g_tx_chunk.active) {
        return;
    }
    /* 每段 ≤ 65535（DMA 长度 uint16_t）；chunk_kb=0 时仅在全量包>64KB 会走分段，用 64KB 作为默认段大小 */
    uint32_t chunk_bytes = ESP_CommParams_ChunkKb() * 1024u;
    if (chunk_bytes == 0u) chunk_bytes = 65535u;
    uint32_t remain = g_tx_chunk.total_len - g_tx_chunk.offset;
    if (remain == 0u) {
        g_tx_chunk.active = 0;
        return;
    }
    if (chunk_bytes > remain) chunk_bytes = remain;
    if (chunk_bytes > 65535u) chunk_bytes = 65535u;

    HAL_StatusTypeDef st = HAL_UART_Transmit_DMA(&huart2,
                                                 (uint8_t *)http_packet_buf + g_tx_chunk.offset,
                                                 (uint16_t)chunk_bytes);
    tx_try++;
    if (st == HAL_OK) {
        tx_ok++;
        g_tx_chunk.offset += chunk_bytes;
        g_tx_chunk.next_tick = now_tick + ESP_CommParams_ChunkDelayMs();
        if (g_tx_chunk.offset >= g_tx_chunk.total_len) {
            g_tx_chunk.active = 0;
            last_send_time = now_tick;
            g_last_heartbeat_tick = now_tick;
            g_waiting_http_response = 1;
            g_waiting_http_tick = now_tick;
        }
    } else if (st == HAL_BUSY) {
        tx_busy++;
    } else {
        tx_err++;
    }

#if (ESP_DEBUG)
    // 调试日志
    uint32_t now = HAL_GetTick();
    if ((now - last_tx_log) >= 1000)
    {
        last_tx_log = now;
        ESP_Log("[调试] TX: try=%lu ok=%lu busy=%lu err=%lu total=%lu off=%lu chunk=%lu gState=%d rxStarted=%d\r\n",
                (unsigned long)tx_try, (unsigned long)tx_ok, (unsigned long)tx_busy, (unsigned long)tx_err,
                (unsigned long)g_tx_chunk.total_len,
                (unsigned long)g_tx_chunk.offset,
                (unsigned long)chunk_bytes,
                (int)huart2.gState,
                (int)g_usart2_rx_started);
    }
#endif
}

void ESP_Post_Heartbeat(void)
{
    if (g_esp_ready == 0)
        return;

    uint32_t now = HAL_GetTick();
    if (g_waiting_http_response)
    {
        /* 等待回包期间禁止继续发心跳；但超时后自动放行 */
        uint32_t to_ms = ESP_CommParams_HttpTimeoutMs();
        if ((now - g_waiting_http_tick) < to_ms)
            return;
        g_waiting_http_response = 0;
    }

    if (now - g_last_heartbeat_tick < ESP_CommParams_HeartbeatMs())
        return;

    HAL_UART_StateTypeDef st = HAL_UART_GetState(&huart2);
    if (st == HAL_UART_STATE_BUSY_TX || st == HAL_UART_STATE_BUSY_TX_RX)
        return;

    char body[128];
    char req[256];
    int body_len = snprintf(body, sizeof(body),
                            "{\"node_id\":\"%s\",\"status\":\"online\",\"fault_code\":\"%s\",\"channels\":[]}",
                            g_sys_cfg.node_id, g_fault_code);
    if (body_len <= 0 || body_len >= (int)sizeof(body))
        return;
    int req_len = snprintf(req, sizeof(req),
                           "POST /api/node/heartbeat HTTP/1.1\r\n"
                           "Host: %s:%d\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "\r\n"
                           "%s",
                           g_sys_cfg.server_ip, g_sys_cfg.server_port, body_len, body);
    if (req_len <= 0 || req_len >= (int)sizeof(req))
        return;

    if (HAL_UART_Transmit(&huart2, (uint8_t *)req, (uint16_t)req_len, 200) == HAL_OK)
    {
        g_waiting_http_response = 1;
        g_waiting_http_tick = now;
        g_last_heartbeat_tick = now;
#if (ESP_DEBUG)
        ESP_Log("[调试] Heartbeat sent len=%d\r\n", req_len);
#endif
    }
}

// ---------------- USART2 TX DMA 完成回调：Header -> Body 链式发送 ----------------
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (!huart || huart->Instance != USART2)
        return;

    if (g_http_tx_phase == ESP_HTTP_TX_HEADER_INFLIGHT)
    {
        if (!g_http_tx_body_ptr || g_http_tx_body_len == 0)
        {
            g_http_tx_phase = ESP_HTTP_TX_IDLE;
            g_http_tx_body_ptr = NULL;
            g_http_tx_body_len = 0;
            return;
        }
        HAL_StatusTypeDef st = HAL_UART_Transmit_DMA(&huart2,
                                                     g_http_tx_body_ptr,
                                                     (uint16_t)g_http_tx_body_len);
        if (st == HAL_OK)
        {
            g_http_tx_phase = ESP_HTTP_TX_BODY_INFLIGHT;
            uint32_t now = HAL_GetTick();
            g_last_heartbeat_tick = now;
            g_waiting_http_response = 1;
            g_waiting_http_tick = now;
        }
        else
        {
            g_http_tx_phase = ESP_HTTP_TX_IDLE;
            g_http_tx_body_ptr = NULL;
            g_http_tx_body_len = 0;
        }
        return;
    }

    if (g_http_tx_phase == ESP_HTTP_TX_BODY_INFLIGHT)
    {
        g_http_tx_phase = ESP_HTTP_TX_IDLE;
        g_http_tx_body_ptr = NULL;
        g_http_tx_body_len = 0;
        return;
    }
}

// ---------------- 串口 RX 回调：调试串口输入 E01/E00 注入/清除故障 ----------------
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

// ---------------- USART2 流式接收：解析后端 /api/node/heartbeat 响应里的 command=reset ----------------
static void ESP_StreamRx_Start(void)
{
    memset(g_stream_rx_buf, 0, sizeof(g_stream_rx_buf));
    memset(g_stream_window, 0, sizeof(g_stream_window));
    g_stream_window_len = 0;
    g_stream_rx_last_pos = 0;

    // 确保 RX 状态干净（避免因为之前的阻塞接收/异常导致启动失败）
    (void)HAL_UART_AbortReceive(&huart2);

    // 启动 IDLE DMA 接收
    HAL_StatusTypeDef st = HAL_UARTEx_ReceiveToIdle_DMA(&huart2, g_stream_rx_buf, sizeof(g_stream_rx_buf));
    if (st == HAL_OK)
    {
        g_usart2_rx_started = 1;
        g_usart2_rx_restart++;
        g_last_rx_tick = HAL_GetTick();
        // 关闭半传输中断 (HT)，减少一半的中断频率，这对 2Mbps 通信至关重要
        if (huart2.hdmarx)
        {
            __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
        }
        ESP_Log("[ESP] 已开启 USART2 RX(DMA+IDLE):用于接收服务器下发命令\r\n");
    }
    else
    {
        g_usart2_rx_started = 0;
        ESP_Log("[ESP] 开启 USART2 RX(DMA+IDLE) 失败,status=%d（将无法接收 reset 命令）\r\n", (int)st);
    }
}

static void ESP_StreamRx_Feed(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0)
        return;
    g_usart2_rx_bytes += len;

    // ---------------- 关键修复：先扫描“原始数据块” ----------------
    // 避免因为滑动窗口截断（例如 "HTTP" 和 "/1.1" 在两包里）导致漏判

    // 1) HTTP 响应检测：兼容 HTTP/1.0/1.1，出现 "HTTP/" 即认为收到响应头（辅助调试）
    if (g_waiting_http_response && ESP_BufContains(data, len, "HTTP/"))
    {
        g_waiting_http_response = 0;
        g_last_rx_tick = HAL_GetTick();
    }
    // 2) 服务器命令检测：优先在原始数据里扫一遍
    if (ESP_BufContains(data, len, "\"command\"") && ESP_BufContains(data, len, "reset"))
    {
        g_server_reset_pending = 1;
    }
    if (ESP_BufContains(data, len, "\"report_mode\"") && ESP_BufContains(data, len, "full"))
    {
        ESP_SetServerReportMode(1);
    }
    if (ESP_BufContains(data, len, "\"report_mode\"") && ESP_BufContains(data, len, "summary"))
    {
        ESP_SetServerReportMode(0);
    }
    // 3) 链路异常检测：尽早触发软重连，避免长时间“卡住”
    if (g_report_enabled && !g_link_reconnecting &&
        (ESP_BufContains(data, len, "CLOSED") ||
         ESP_BufContains(data, len, "CONNECT FAIL") ||
         ESP_BufContains(data, len, "ERROR") ||
         ESP_BufContains(data, len, "link is not valid")))
    {
        g_link_reconnect_pending = 1;
    }

    // ---------------- 滑动窗口逻辑 ----------------
    // 将新数据追加到窗口，移除旧数据，保持窗口大小恒定。
    // 用于解决关键字跨包的问题。
    const uint16_t cap = (uint16_t)(sizeof(g_stream_window) - 1);
    if (len >= cap)
    {
        // 如果新数据比窗口大，直接保留新数据的最后 cap 字节
        memcpy(g_stream_window, data + (len - cap), cap);
        g_stream_window[cap] = 0;
        g_stream_window_len = cap;
    }
    else
    {
        uint16_t new_len = (uint16_t)(g_stream_window_len + len);
        if (new_len > cap)
        {
            uint16_t drop = (uint16_t)(new_len - cap);
            if (drop >= g_stream_window_len)
            {
                g_stream_window_len = 0;
            }
            else
            {
                // 移动旧数据
                memmove(g_stream_window, g_stream_window + drop, g_stream_window_len - drop);
                g_stream_window_len = (uint16_t)(g_stream_window_len - drop);
            }
        }
        // 追加新数据
        memcpy(g_stream_window + g_stream_window_len, data, len);
        g_stream_window_len = (uint16_t)(g_stream_window_len + len);
        g_stream_window[g_stream_window_len] = 0;
    }

    // 识别 JSON：{"command":"reset"}（也允许出现 reset 关键字）
    if (strstr(g_stream_window, "\"command\"") && strstr(g_stream_window, "reset"))
    {
        g_server_reset_pending = 1;
    }
    if (strstr(g_stream_window, "\"report_mode\"") && strstr(g_stream_window, "full"))
    {
        ESP_SetServerReportMode(1);
    }
    if (strstr(g_stream_window, "\"report_mode\"") && strstr(g_stream_window, "summary"))
    {
        ESP_SetServerReportMode(0);
    }
    // 滑动窗口中检测链路异常关键字
    uint32_t server_downsample_step = 0;
    if (ESP_TryParseDownsampleStep(g_stream_window, &server_downsample_step))
    {
        ESP_SetServerDownsampleStep(server_downsample_step);
    }
    uint32_t server_upload_points = 0;
    if (ESP_TryParseUploadPoints(g_stream_window, &server_upload_points))
    {
        ESP_SetServerUploadPoints(server_upload_points);
    }

    if (!g_link_reconnecting &&
        (strstr(g_stream_window, "CLOSED") ||
         strstr(g_stream_window, "CONNECT FAIL") ||
         strstr(g_stream_window, "ERROR") ||
         strstr(g_stream_window, "link is not valid")))
    {
        g_link_reconnect_pending = 1;
    }

    // 识别 HTTP 响应头（辅助调试，兼容 HTTP/1.0/1.1）
    if (g_waiting_http_response && strstr(g_stream_window, "HTTP/"))
    {
        g_waiting_http_response = 0;
        g_last_rx_tick = HAL_GetTick();
    }
}

/**
 * @brief  UART 接收事件回调 (DMA 满 或 IDLE 空闲时触发)
 * @note   HAL_UARTEx_ReceiveToIdle_DMA 的回调
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart == &huart2)
    {
        // AT 阶段禁止 DMA+IDLE 回调介入（会与阻塞式 HAL_UART_Receive 冲突）
        if (g_uart2_at_mode)
        {
            return;
        }
        g_usart2_rx_events++;
        (void)Size;

        /* DMA Circular 场景：不要在回调里反复 Stop/Start DMA，否则 2Mbps 下极易产生空窗 ORE。
         * 这里按 DMA 写指针(pos)做增量解析，回调触发频率由 IDLE/TC/HT 决定。 */
        if (huart2.hdmarx != NULL)
        {
            const uint16_t buf_sz = (uint16_t)sizeof(g_stream_rx_buf);
            uint16_t pos = (uint16_t)(buf_sz - (uint16_t)__HAL_DMA_GET_COUNTER(huart2.hdmarx));

            if (pos != g_stream_rx_last_pos && pos <= buf_sz)
            {
                uint32_t now = HAL_GetTick();
                g_last_rx_tick = now;

                /* 有新数据到达：直接解除门控。
                 * 说明服务器/链路至少有回包字节到达，继续卡门控只会造成“超时放行刷屏”并降低吞吐。
                 * 更严格的 HTTP 头检测仍由 ESP_StreamRx_Feed 负责（用于调试/统计）。 */
                if (g_waiting_http_response)
                {
                    g_waiting_http_response = 0;
                }

                if (pos > g_stream_rx_last_pos)
                {
                    uint16_t len = (uint16_t)(pos - g_stream_rx_last_pos);
                    DCache_InvalidateByAddr_Any(&g_stream_rx_buf[g_stream_rx_last_pos], len);
                    ESP_StreamRx_Feed(&g_stream_rx_buf[g_stream_rx_last_pos], len);
                }
                else
                {
                    /* wrap-around */
                    uint16_t len1 = (uint16_t)(buf_sz - g_stream_rx_last_pos);
                    if (len1 > 0)
                    {
                        DCache_InvalidateByAddr_Any(&g_stream_rx_buf[g_stream_rx_last_pos], len1);
                        ESP_StreamRx_Feed(&g_stream_rx_buf[g_stream_rx_last_pos], len1);
                    }
                    if (pos > 0)
                    {
                        DCache_InvalidateByAddr_Any(&g_stream_rx_buf[0], pos);
                        ESP_StreamRx_Feed(&g_stream_rx_buf[0], pos);
                    }
                }
                g_stream_rx_last_pos = pos;
            }

            g_usart2_rx_started = 1;
            return;
        }

        /* 兜底：无 DMA 句柄时维持旧逻辑（不建议 2Mbps 下走这里） */
        if (Size > 0 && Size <= sizeof(g_stream_rx_buf))
        {
            uint32_t now = HAL_GetTick();
            g_last_rx_tick = now;
            DCache_InvalidateByAddr_Any(g_stream_rx_buf, Size);
            ESP_StreamRx_Feed(g_stream_rx_buf, Size);
        }
        return;
    }
}

/**
 * @brief  UART 错误回调
 * @note   典型是 ORE（接收溢出）导致后续不再进 RxEventCallback。
 * 这是 2Mbps 无流控通信中最关键的容错逻辑。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2)
    {
        g_uart2_err++;
        uint32_t ec = huart->ErrorCode;
        if (ec & HAL_UART_ERROR_ORE)
            g_uart2_err_ore++;
        if (ec & HAL_UART_ERROR_FE)
            g_uart2_err_fe++;
        if (ec & HAL_UART_ERROR_NE)
            g_uart2_err_ne++;
        if (ec & HAL_UART_ERROR_PE)
            g_uart2_err_pe++;

        // 发生错误时，当前这次 HTTP 回包很可能已经丢了；不要继续门控傻等
        if (g_waiting_http_response)
        {
            g_waiting_http_response = 0;
        }

        // 场景 A: AT(阻塞)阶段
        // 如果此时去重启 ReceiveToIdle_DMA，会把 HAL 的 RxState 锁死为 BUSY_RX，
        // 导致后续 HAL_UART_Receive 直接 HAL_BUSY，从而出现“无限超时/假死”。
        if (g_uart2_at_mode)
        {
            ESP_Clear_Error_Flags();
            (void)HAL_UART_Abort(&huart2);
            g_usart2_rx_started = 0;
            return;
        }

        // 场景 B: 透传阶段
        // 清除错误并重启 DMA 接收，确保能继续收数据
        ESP_Clear_Error_Flags();
        (void)HAL_UART_AbortReceive(&huart2);
        g_stream_rx_last_pos = 0;

        HAL_StatusTypeDef st = HAL_UARTEx_ReceiveToIdle_DMA(&huart2, g_stream_rx_buf, sizeof(g_stream_rx_buf));
        g_usart2_rx_started = (st == HAL_OK) ? 1 : 0;

        if (st == HAL_OK && huart2.hdmarx)
        {
            __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
        }
        return;
    }
}

void ESP_Register(void)
{
    ensure_http_packet_buf();
    char *body_start = (char *)http_packet_buf + 256;
    ESP_Log("[ESP] 正在注册设备...\r\n");
    sprintf(body_start, "{\"device_id\":\"%s\",\"location\":\"%s\",\"hw_version\":\"v1.0_4CH\"}", g_sys_cfg.node_id, g_sys_cfg.node_location);
    uint32_t body_len = strlen(body_start);
    int h_len = sprintf((char *)http_packet_buf,
                        "POST /api/register HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\nContent-Length: %u\r\n\r\n",
                        g_sys_cfg.server_ip, g_sys_cfg.server_port, body_len);
    memmove(http_packet_buf + h_len, body_start, body_len);
    HAL_UART_Transmit(&huart2, http_packet_buf, h_len + body_len, 1000);

    // 关键：读一下服务器 HTTP 响应，确认注册是否真的到达后端
    memset(esp_rx_buf, 0, sizeof(esp_rx_buf));
    uint32_t start = HAL_GetTick();
    uint16_t idx = 0;
    while ((HAL_GetTick() - start) < 3000)
    {
        uint8_t ch;
        if (HAL_UART_Receive(&huart2, &ch, 1, 5) == HAL_OK)
        {
            if (idx < sizeof(esp_rx_buf) - 1)
            {
                esp_rx_buf[idx++] = ch;
                esp_rx_buf[idx] = 0;
            }
            if (strstr((char *)esp_rx_buf, "HTTP/1.1"))
            {
                ESP_Log("[ESP] 注册响应已收到\r\n");
                break;
            }
        }
        else
        {
            ESP_RtosYield();
        }
    }
    if (idx == 0)
    {
        ESP_Log("[ESP] 注册无响应（未收到HTTP头）\r\n");
    }
    HAL_Delay(200);
}

/* 辅助函数 */

// 丢弃 UART2 残留输出（避免污染关键字匹配）
static void ESP_Uart2_Drain(uint32_t ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t ch;
    while ((HAL_GetTick() - start) < ms)
    {
        if (HAL_UART_Receive(&huart2, &ch, 1, 0) == HAL_OK)
        {
            // eat
        }
        else
        {
            HAL_Delay(1);
        }
    }
}

// 阻塞等待关键字（用于透传复用探测 / +++ 等待 OK）
static uint8_t ESP_Wait_Keyword(const char *kw, uint32_t timeout_ms)
{
    if (!kw || !*kw)
        return 0;
    uint32_t start = HAL_GetTick();
    uint16_t idx = 0;
    memset(esp_rx_buf, 0, sizeof(esp_rx_buf));
    while ((HAL_GetTick() - start) < timeout_ms)
    {
        uint8_t ch;
        if (HAL_UART_Receive(&huart2, &ch, 1, 5) == HAL_OK)
        {
            if (idx < sizeof(esp_rx_buf) - 1)
            {
                esp_rx_buf[idx++] = ch;
                esp_rx_buf[idx] = 0;
            }
            if (strstr((char *)esp_rx_buf, kw))
                return 1;
        }
        else
        {
            ESP_RtosYield();
        }
    }
    return 0;
}

// 严格退出透传：满足 guard time，发送 +++，并等待 OK
static uint8_t ESP_Exit_Transparent_Mode_Strict(uint32_t timeout_ms)
{
    // 停 RX DMA，避免与阻塞式接收冲突
    (void)HAL_UART_AbortReceive(&huart2);
    ESP_Clear_Error_Flags();

    // guard time (before)：期间不要向模块发送任何字节 (通常需 > 1s)
    HAL_Delay(1200);
    HAL_UART_Transmit(&huart2, (uint8_t *)"+++", 3, 100);
    // guard time (after)
    HAL_Delay(1200);

    // 等待 OK（有些固件会返回 "OK\r\n"）
    return ESP_Wait_Keyword("OK", timeout_ms);
}

// MCU 复位后：优先复用“现有透传+TCP”连接（无需断电 ESP）
static uint8_t ESP_TryReuseTransparent(void)
{
    // 如果当前就在命令模式，AT 会立刻 OK；这种情况不需要复用，走常规初始化更稳
    if (ESP_Send_Cmd("AT\r\n", "OK", 200))
        return 0;

    // 在透传里：直接发一包最小 heartbeat 探测，看是否收到 HTTP/1.1
    (void)HAL_UART_AbortReceive(&huart2);
    ESP_Uart2_Drain(100);

    ensure_http_packet_buf();
    char *body = (char *)http_packet_buf + 256;
    sprintf(body, "{\"node_id\":\"%s\",\"status\":\"online\",\"fault_code\":\"%s\"}", g_sys_cfg.node_id, g_fault_code);
    uint32_t body_len = (uint32_t)strlen(body);
    int h_len = sprintf((char *)http_packet_buf,
                        "POST /api/node/heartbeat HTTP/1.1\r\n"
                        "Host: %s:%d\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: %lu\r\n"
                        "\r\n",
                        g_sys_cfg.server_ip, g_sys_cfg.server_port, (unsigned long)body_len);
    memmove(http_packet_buf + h_len, body, body_len);

    // 探测包小，阻塞发送即可
    HAL_UART_Transmit(&huart2, http_packet_buf, (uint16_t)(h_len + body_len), 500);
    if (ESP_Wait_Keyword("HTTP/1.1", 800))
    {
        // 复用成功：重启 RX DMA，用于接收 reset 指令 & 更新时间戳
        ESP_StreamRx_Start();
        g_last_rx_tick = HAL_GetTick();
        return 1;
    }
    return 0;
}

// 软重连：不断电、不重置 WiFi，只重建 TCP + 透传
static void ESP_SoftReconnect(void)
{
#if (EW_USE_ESP32_SPI_UI)
    ESP_Log("[ESP32SPI] skip legacy UART soft reconnect in SPI mode.\r\n");
    return;
#endif
    if (g_link_reconnecting)
        return;
    g_link_reconnecting = 1;

    // 暂停一段时间，满足 guard time 才能退出透传
    g_esp_ready = 0;
    g_uart2_at_mode = 1;
    ESP_ForceStop_DMA();
    (void)HAL_UART_AbortReceive(&huart2);

    if (!ESP_Exit_Transparent_Mode_Strict(2000))
    {
			ESP_Log("[ESP] 软重连失败:无法退出透传 -> 硬复位ESP8266\r\n");
        g_link_reconnecting = 0;
        ESP_HardReset();
        // 复位后走完整初始化（会重建 WiFi/TCP/透传）
        ESP_Init();
        return;
    }

    // 重建 TCP 并重新进入透传
    ESP_Send_Cmd("AT+CIPCLOSE\r\n", "OK", 1500);
    char cmd_buf[128];
    sprintf(cmd_buf, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", g_sys_cfg.server_ip, g_sys_cfg.server_port);
    if (!ESP_Send_Cmd(cmd_buf, "CONNECT", 10000))
    {
			ESP_Log("[ESP] 软重连失败:CIPSTART -> 硬复位ESP8266\r\n");
        g_link_reconnecting = 0;
        ESP_HardReset();
        ESP_Init();
        return;
    }
    ESP_Send_Cmd("AT+CIPMODE=1\r\n", "OK", 1000);
    ESP_Send_Cmd("AT+CIPSEND\r\n", ">", 2000);
    HAL_Delay(200);

    ESP_StreamRx_Start();
    g_last_rx_tick = HAL_GetTick();
    g_esp_ready = 1;
    g_uart2_at_mode = 0;
    g_link_reconnecting = 0;
    ESP_Log("[ESP] 软重连完成\r\n");
}

static void ESP_Clear_Error_Flags(void)
{
    volatile uint32_t isr = huart2.Instance->ISR;
    volatile uint32_t rdr = huart2.Instance->RDR;
    (void)isr;
    (void)rdr;
    // 仅清除错误标志，不操作数据
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    __HAL_UART_CLEAR_NEFLAG(&huart2);
    __HAL_UART_CLEAR_FEFLAG(&huart2);
}

// 硬件复位 ESP8266：拉低 RST 一段时间再拉高（不需要断电）
static void ESP_HardReset(void)
{
#ifdef ESP8266_RST_Pin
    // 硬复位前先停掉 UART2 DMA/中断，避免复位过程中输出乱码引发中断风暴
    g_uart2_at_mode = 1;
    ESP_ForceStop_DMA();

    // RST 低有效：低 120ms -> 高，等待启动完成
    HAL_GPIO_WritePin(ESP8266_RST_GPIO_Port, ESP8266_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(120);
    HAL_GPIO_WritePin(ESP8266_RST_GPIO_Port, ESP8266_RST_Pin, GPIO_PIN_SET);
    /* 某些固件在高波特率下上电/复位后需要更久才会响应 AT */
    HAL_Delay(2500);
    ESP_Clear_Error_Flags();
    ESP_Uart2_Drain(400);
#else
    // 若硬件未接 RST 引脚，只能提示用户断电
    ESP_Log("[ESP] 未定义 ESP8266_RST_Pin，无法硬复位（请接 RST/EN 到GPIO）\r\n");
#endif
}

static uint8_t ESP_Send_Cmd(const char *cmd, const char *reply, uint32_t timeout)
{
    uint32_t start;
    uint16_t idx = 0;
    uint8_t busy_hits = 0;
    ESP_Clear_Error_Flags();
    memset(esp_rx_buf, 0, sizeof(esp_rx_buf));

#if (ESP_DEBUG)
    // 打印命令（敏感信息脱敏）
    if (cmd && (strncmp(cmd, "AT+CWJAP=", 9) == 0))
    {
        ESP_Log("[ESP 指令] >> AT+CWJAP=\"%s\",\"******\"\r\n", g_sys_cfg.wifi_ssid);
    }
    else if (cmd)
    {
        ESP_Log("[ESP 指令] >> %s", cmd);
    }
#endif

    /* 发送：若 HAL 状态机异常（BUSY），尝试 Abort 后重发一次 */
    if (HAL_UART_Transmit(&huart2, (uint8_t *)cmd, strlen(cmd), 100) != HAL_OK)
    {
        (void)HAL_UART_Abort(&huart2);
        ESP_Clear_Error_Flags();
        (void)HAL_UART_Transmit(&huart2, (uint8_t *)cmd, strlen(cmd), 200);
    }
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout)
    {
        uint8_t ch;
        // 轮询方式接收一个字节（超时从5ms增加到10ms，减少轮询频率）
        if (HAL_UART_Receive(&huart2, &ch, 1, 10) == HAL_OK)
        {
            if (idx < sizeof(esp_rx_buf) - 1)
            {
                esp_rx_buf[idx++] = ch;
                esp_rx_buf[idx] = 0;
                if (strstr((char *)esp_rx_buf, reply) != NULL)
                {
#if (ESP_DEBUG)
                    ESP_Log("[ESP 期望] << %s\r\n", reply);
#endif
                    return 1;
                }
                /* 早退：出现 ERROR/FAIL 时无需继续等（减少“超时假象”） */
                if (strstr((char *)esp_rx_buf, "ERROR") || strstr((char *)esp_rx_buf, "FAIL"))
                {
#if (ESP_DEBUG)
									ESP_Log("[ESP] 早退:检测到 ERROR/FAIL\r\n");
                    ESP_Log_RxBuf("ERR");
#endif
                    return 0;
                }
                if (ESP_RxBusyDetected())
                {
                    busy_hits++;
#if (ESP_DEBUG)
                    ESP_Log("[ESP] 模组忙(busy)，等待中...\r\n");
#endif
                    memset(esp_rx_buf, 0, sizeof(esp_rx_buf));
                    idx = 0;
                    HAL_Delay(200);
                    if (busy_hits <= 3)
                    {
                        start = HAL_GetTick();
                    }
                }
            }
        }
        else
        {
            HAL_Delay(1);  /* 用HAL_Delay替代ESP_RtosYield，减少任务切换开销 */
        }
    }
#if (ESP_DEBUG)
    ESP_Log("[ESP 超时] 等待关键字: %s\r\n", reply);
    ESP_Log_RxBuf("TIMEOUT");
#endif
    return 0;
}

static uint8_t ESP_Send_Cmd_Any(const char *cmd, const char *reply1, const char *reply2, uint32_t timeout)
{
    uint32_t start;
    uint16_t idx = 0;
    uint8_t busy_hits = 0;
    ESP_Clear_Error_Flags();
    memset(esp_rx_buf, 0, sizeof(esp_rx_buf));

#if (ESP_DEBUG)
    if (cmd && (strncmp(cmd, "AT+CWJAP=", 9) == 0))
    {
        ESP_Log("[ESP 指令] >> AT+CWJAP=\"%s\",\"******\"\r\n", g_sys_cfg.wifi_ssid);
    }
    else if (cmd)
    {
        ESP_Log("[ESP 指令] >> %s", cmd);
    }
#endif

    if (HAL_UART_Transmit(&huart2, (uint8_t *)cmd, strlen(cmd), 100) != HAL_OK)
    {
        (void)HAL_UART_Abort(&huart2);
        ESP_Clear_Error_Flags();
        (void)HAL_UART_Transmit(&huart2, (uint8_t *)cmd, strlen(cmd), 200);
    }
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout)
    {
        uint8_t ch;
        if (HAL_UART_Receive(&huart2, &ch, 1, 10) == HAL_OK)  /* 超时从5ms增加到10ms，减少轮询频率 */
        {
            if (idx < sizeof(esp_rx_buf) - 1)
            {
                esp_rx_buf[idx++] = ch;
                esp_rx_buf[idx] = 0;
                if ((reply1 && strstr((char *)esp_rx_buf, reply1)) ||
                    (reply2 && strstr((char *)esp_rx_buf, reply2)))
                {
#if (ESP_DEBUG)
                    ESP_Log("[ESP 期望] << %s%s\r\n", reply1 ? reply1 : "",
                            reply2 ? " / alt" : "");
#endif
                    return 1;
                }
                if (strstr((char *)esp_rx_buf, "ERROR") || strstr((char *)esp_rx_buf, "FAIL"))
                {
                    /* 允许上层从 esp_rx_buf 里判断具体错误，但不再傻等超时 */
                    return 0;
                }
                if (ESP_RxBusyDetected())
                {
                    busy_hits++;
#if (ESP_DEBUG)
                    ESP_Log("[ESP] 模组忙(busy),等待中...\r\n");
#endif
                    memset(esp_rx_buf, 0, sizeof(esp_rx_buf));
                    idx = 0;
                    HAL_Delay(200);
                    if (busy_hits <= 3)
                    {
                        start = HAL_GetTick();
                    }
                }
            }
        }
        else
        {
            HAL_Delay(1);  /* 用HAL_Delay替代ESP_RtosYield，减少任务切换开销 */
        }
    }
#if (ESP_DEBUG)
    ESP_Log("[ESP 超时] 等待关键字: %s%s%s\r\n", reply1 ? reply1 : "",
            reply2 ? " / " : "", reply2 ? reply2 : "");
    ESP_Log_RxBuf("TIMEOUT");
#endif
    return 0;
}

static inline char *ESP_AppendI32(char *p, const char *end, int32_t x)
{
    if (!p || !end || p >= end)
        return NULL;
    if (x == 0)
    {
        if (p + 1 > end)
            return NULL;
        *p++ = '0';
        return p;
    }
    if (x < 0)
    {
        if (p + 1 > end)
            return NULL;
        *p++ = '-';
        /* INT32_MIN 溢出保护：转无符号处理 */
        uint32_t ux = (uint32_t)(-(x + 1)) + 1u;
        char tmp[11];
        int n = 0;
        while (ux > 0 && n < (int)sizeof(tmp))
        {
            tmp[n++] = (char)('0' + (ux % 10u));
            ux /= 10u;
        }
        if ((end - p) < n)
            return NULL;
        while (n--)
            *p++ = tmp[n];
        return p;
    }
    uint32_t ux = (uint32_t)x;
    char tmp[11];
    int n = 0;
    while (ux > 0 && n < (int)sizeof(tmp))
    {
        tmp[n++] = (char)('0' + (ux % 10u));
        ux /= 10u;
    }
    if ((end - p) < n)
        return NULL;
    while (n--)
        *p++ = tmp[n];
    return p;
}

static int Helper_FloatArray_To_String(char **pp, const char *end, const float *data, int count, int step)
{
    if (!pp || !*pp || !end || *pp >= end || !data || count <= 0 || step <= 0)
        return 0;

    char *p = *pp;

    for (int i = 0; i < count; i += step)
    {
        int32_t x = ESP_FloatToI32Scaled(data[i]);
        char *np = ESP_AppendI32(p, end, x);
        if (!np)
            return 0;
        p = np;
        if (i + step < count)
        {
            if (p + 1 > (char *)end)
                return 0;
            *p++ = ',';
        }
    }

    if (p >= (char *)end)
        return 0;
    *p = 0;
    *pp = p;
    return 1;
}

/* 输出 float 数组（1 位小数），不做 ×200 缩放
 * - 仍不使用 snprintf，避免 CPU 开销
 * - 仅用于 fft_spectrum[]（你要求 FFT 不需要 ×200） */
static int Helper_FloatArray1dp_To_String(char **pp, const char *end, const float *data, int count, int step)
{
    if (!pp || !*pp || !end || *pp >= end || !data || count <= 0 || step <= 0)
        return 0;

    char *p = *pp;
    for (int i = 0; i < count; i += step)
    {
        float vf = ESP_SafeFloat(data[i]);
        int32_t x10 = (int32_t)((vf >= 0.0f) ? (vf * 10.0f + 0.5f) : (vf * 10.0f - 0.5f));
        int32_t ip = x10 / 10;
        int32_t fp = x10 % 10;
        if (fp < 0)
            fp = -fp;

        char *np = ESP_AppendI32(p, end, ip);
        if (!np || (end - np) < 2)
            return 0;
        *np++ = '.';
        *np++ = (char)('0' + (fp % 10));
        p = np;

        if (i + step < count)
        {
            if (p + 1 > (char *)end)
                return 0;
            *p++ = ',';
        }
    }

    if (p >= (char *)end)
        return 0;
    *p = 0;
    *pp = p;
    return 1;
}

static void ESP_Exit_Transparent_Mode(void)
{
    HAL_Delay(200);
    HAL_UART_Transmit(&huart2, (uint8_t *)"+++", 3, 100);
    HAL_Delay(1000);
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
static volatile uint8_t g_ui_wifi_ok = 0;
static volatile uint8_t g_ui_tcp_ok = 0;
static volatile uint8_t g_ui_reg_ok = 0;
static volatile uint8_t g_ui_auto_recover_pending = 0U;
static volatile uint8_t g_ui_auto_recover_want_report = 0U;
static uint32_t g_ui_auto_recover_next_poll_tick = 0U;
static uint32_t g_ui_auto_recover_next_attempt_tick = 0U;
static uint32_t g_ui_last_session_epoch = 0U;
static uint8_t g_ui_last_session_valid = 0U;

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

    /* UI 模式下也建议在任务启动时把模组复位到干净状态，但不做任何连接。
     * 目的：避免用户第一次点“WiFi连接”时 ESP 还在上电启动/透传残留，导致 AT 连续超时。 */
#ifndef EW_ESP_UI_BOOT_HARDRESET_ONCE
#define EW_ESP_UI_BOOT_HARDRESET_ONCE 1
#endif
#if (EW_ESP_UI_BOOT_HARDRESET_ONCE)
#if (EW_USE_ESP32_SPI_UI)
    ESP_Log("[ESP32SPI] UI: SPI mode, skip legacy AT boot reset.\r\n");
#else
    if (!g_boot_hardreset_done)
    {
        g_boot_hardreset_done = 1;
        ESP_Log("[ESP] UI: boot hard reset once (no auto connect)\r\n");
        ESP_HardReset();
    }
#endif
#endif
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

/* 从 ESP_Init 抽取的“进入可用 AT 模式”最小流程 */
static void ESP_UI_SyncLinkFlagsFromStatus(const esp32_spi_status_t *st)
{
#if (EW_USE_ESP32_SPI_UI)
    if (st == NULL) {
        return;
    }
    g_ui_wifi_ok = st->wifi_connected ? 1U : 0U;
    g_ui_tcp_ok = (st->cloud_connected || st->registered_with_cloud || st->reporting_enabled) ? 1U : 0U;
    g_ui_reg_ok = (st->registered_with_cloud || st->reporting_enabled) ? 1U : 0U;
    g_esp_ready = (st->registered_with_cloud || st->reporting_enabled) ? 1U : 0U;
#else
    (void)st;
#endif
}

static void ESP_UI_ScheduleAutoRecover(const char *reason, uint8_t want_report)
{
#if (EW_USE_ESP32_SPI_UI)
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
#else
    (void)reason;
    (void)want_report;
#endif
}

static bool ESP_UI_EnsureReportStarted(uint8_t mode, const char *reason_tag)
{
#if (EW_USE_ESP32_SPI_UI)
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
#else
    (void)mode;
    (void)reason_tag;
    return false;
#endif
}

static void ESP_UI_PollAutoRecover(void)
{
#if (EW_USE_ESP32_SPI_UI)
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
#endif
}

static bool ESP_UI_PrepareAtMode(void)
{
    /* 进 AT 前清场 */
    g_uart2_at_mode = 1;
    ESP_ForceStop_DMA();
    ESP_Clear_Error_Flags();

    /* 确保 RST 拉高 */
#ifdef ESP8266_RST_Pin
    HAL_GPIO_WritePin(ESP8266_RST_GPIO_Port, ESP8266_RST_Pin, GPIO_PIN_SET);
#endif

    /* 先快速探测：若本来就在命令模式，别无谓做 +++（避免大量“OK超时”假象） */
    ESP_Uart2_Drain(60);
    for (int k = 0; k < 2; k++)
    {
        if (ESP_Send_Cmd("AT\r\n", "OK", 300))
        {
            return true;
        }
        ESP_RtosYield();
    }

    /* 如果连续 AT 都“完全没回显”，优先判定为模组未就绪/串口状态机异常，直接硬复位比 +++ 更靠谱 */
    if (esp_rx_buf[0] == 0)
    {
        ESP_Log("[ESP] UI: AT no response, hard reset and retry...\r\n");
        ESP_HardReset();
        ESP_Uart2_Drain(200);
        for (int k = 0; k < 3; k++)
        {
            if (ESP_Send_Cmd("AT\r\n", "OK", 1000))
            {
                return true;
            }
            ESP_RtosYield();
        }
    }

    /* 可能在透传：再严格退出一次（带 guard time） */
    ESP_Log("[ESP] UI:尝试退出透传...\r\n");
    if (ESP_Exit_Transparent_Mode_Strict(2000))
    {
        ESP_Uart2_Drain(80);
        if (ESP_Send_Cmd("AT\r\n", "OK", 800))
        {
            return true;
        }
    }

    for (int k = 0; k < 3; k++)
    {
        if (ESP_Send_Cmd("AT\r\n", "OK", 800))
        {
            return true;
        }
        (void)ESP_Exit_Transparent_Mode_Strict(2000);
        ESP_Uart2_Drain(120);
    }

    /* AT 无回显：硬复位一次 */
    ESP_Log("[ESP] UI: AT无回显,执行硬复位...\r\n");
    ESP_HardReset();
    ESP_ForceStop_DMA();
    if (!ESP_Send_Cmd("AT\r\n", "OK", 1500))
    {
			ESP_Log("[ESP] UI: 致命:硬复位后仍无法进入 AT\r\n");
        return false;
    }
    return true;
}

static bool ESP_UI_IsWifiConnected(void)
{
    if (!ESP_Send_Cmd_Any("AT+CWJAP?\r\n", "+CWJAP:", "No AP", 1500))
    {
        return false;
    }
    return (strstr((char *)esp_rx_buf, "+CWJAP:") != NULL);
}

static bool ESP_UI_IsTcpConnected(void)
{
    /* 专用解析：必须读到 STATUS:x 才判定，避免先匹配到 OK 就返回导致误判 */
    ESP_Clear_Error_Flags();
    memset(esp_rx_buf, 0, sizeof(esp_rx_buf));

    if (HAL_UART_Transmit(&huart2, (uint8_t *)"AT+CIPSTATUS\r\n", 14, 200) != HAL_OK)
    {
        (void)HAL_UART_Abort(&huart2);
        ESP_Clear_Error_Flags();
        (void)HAL_UART_Transmit(&huart2, (uint8_t *)"AT+CIPSTATUS\r\n", 14, 400);
    }

    uint32_t start = HAL_GetTick();
    uint16_t idx = 0;
    while ((HAL_GetTick() - start) < 1500U)
    {
        uint8_t ch;
        if (HAL_UART_Receive(&huart2, &ch, 1, 10) == HAL_OK)  /* 超时从5ms增加到10ms，减少轮询频率 */
        {
            if (idx < sizeof(esp_rx_buf) - 1)
            {
                esp_rx_buf[idx++] = ch;
                esp_rx_buf[idx] = 0;
            }
            /* 读到 STATUS: 就继续收一小段，确保把数字收全 */
            if (strstr((char *)esp_rx_buf, "STATUS:") != NULL)
            {
                /* 再多等最多 150ms 收齐 */
                uint32_t t2 = HAL_GetTick();
                while ((HAL_GetTick() - t2) < 150U)
                {
                    if (HAL_UART_Receive(&huart2, &ch, 1, 10) == HAL_OK)  /* 超时从5ms增加到10ms */
                    {
                        if (idx < sizeof(esp_rx_buf) - 1)
                        {
                            esp_rx_buf[idx++] = ch;
                            esp_rx_buf[idx] = 0;
                        }
                    }
                    else
                    {
                        HAL_Delay(1);  /* 用HAL_Delay替代ESP_RtosYield，减少任务切换开销 */
                    }
                }
                break;
            }
        }
        else
        {
            HAL_Delay(1);  /* 用HAL_Delay替代ESP_RtosYield，减少任务切换开销 */
        }
    }

    char *p = strstr((char *)esp_rx_buf, "STATUS:");
    if (!p)
    {
#if (ESP_DEBUG)
        ESP_Log("[ESP] CIPSTATUS parse fail (no STATUS)\r\n");
        ESP_Log_RxBuf("CIPSTATUS");
#endif
        return false;
    }
    int st = -1;
    /* 兼容 "STATUS:3" 或 "STATUS: 3" */
    p += 7;
    while (*p == ' ' || *p == '\r' || *p == '\n') p++;
    if (*p >= '0' && *p <= '9')
        st = *p - '0';

    /* ESP AT: STATUS:3 表示已建立 TCP 连接 */
    return (st == 3);
}

#if (EW_USE_ESP32_SPI_UI)
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
#endif

static bool ESP_UI_DoWiFi(void)
{
#if (EW_USE_ESP32_SPI_UI)
    ESP_Log("[UI] Executing WIFI via ESP32 SPI...\r\n");
    g_esp_ready = 0;
    g_report_enabled = 0;
    g_ui_wifi_ok = 0;
    g_ui_tcp_ok = 0;
    g_ui_reg_ok = 0;
    g_uart2_at_mode = 0;
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
#else
    char cmd_buf[128];

    ESP_LoadConfig();
    ESP_Log("[UI] Executing WIFI...\r\n");

    /* 用户要求：每次点击 WiFi 连接，都先硬件复位一次（保证状态干净） */
    g_esp_ready = 0;
    g_ui_wifi_ok = 0;
    g_ui_tcp_ok = 0;
    g_ui_reg_ok = 0;
    ESP_HardReset();

    if (!ESP_UI_PrepareAtMode())
        return false;

    int retry_count = 0;
    while (!ESP_Send_Cmd("ATE0\r\n", "OK", 500))
    {
        ESP_Log("[ESP] UI: ATE0 失败,重试...\r\n");
        ESP_Clear_Error_Flags();
        HAL_Delay(500);
        retry_count++;
        if (retry_count > 5)
            break;
    }

    ESP_Send_Cmd("AT+CWMODE=1\r\n", "OK", 1000);
    (void)ESP_Send_Cmd_Any("AT+CWQAP\r\n", "OK", "ERROR", 1000); /* 断开旧 AP（若无连接返回 ERROR 也可忽略） */

    sprintf(cmd_buf, "AT+CWJAP=\"%s\",\"%s\"\r\n", g_sys_cfg.wifi_ssid, g_sys_cfg.wifi_password);
    if (!ESP_Send_Cmd(cmd_buf, "GOT IP", 20000))
    {
        /* 第一次失败：硬复位一次再试（比“无限重试”更可控） */
        ESP_Log("[ESP] WiFi 连接失败,硬复位后重试...\r\n");
        ESP_HardReset();
        if (!ESP_UI_PrepareAtMode())
            return false;
        ESP_Send_Cmd("ATE0\r\n", "OK", 800);
        ESP_Send_Cmd("AT+CWMODE=1\r\n", "OK", 1000);
        if (!ESP_Send_Cmd(cmd_buf, "GOT IP", 20000))
        {
            ESP_Log("[ESP] WiFi 连接失败。\r\n");
            ESP_Log_RxBuf("WIFI_FAIL");
            g_ui_wifi_ok = 0;
            return false;
        }
    }

    ESP_Log("[ESP] WiFi 连接成功（已获取 IP）。\r\n");
    HAL_Delay(1200);
    ESP_Uart2_Drain(200);
    (void)ESP_Send_Cmd("AT+CIFSR\r\n", "STAIP", 3000);
    ESP_Log_RxBuf("CIFSR");
    g_ui_wifi_ok = 1;
    return true;
#endif
}

static bool ESP_UI_DoTCP(void)
{
#if (EW_USE_ESP32_SPI_UI)
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
#else
    char cmd_buf[128];
    ESP_LoadConfig();
    ESP_Log("[UI] Executing TCP...\r\n");

    if (!ESP_UI_PrepareAtMode())
        return false;

    if (!ESP_UI_IsWifiConnected())
    {
			ESP_Log("[ESP] TCP 前置条件失败:WiFi 未连接\r\n");
        g_ui_tcp_ok = 0;
        return false;
    }

    if (ESP_UI_IsTcpConnected())
    {
        ESP_Log("[ESP] TCP 已连接,跳过重连。\r\n");
        g_ui_tcp_ok = 1;
        return true;
    }

    (void)ESP_Send_Cmd_Any("AT+CIPCLOSE\r\n", "OK", "ERROR", 500);
    sprintf(cmd_buf, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", g_sys_cfg.server_ip, g_sys_cfg.server_port);

    uint8_t tcp_ok = 0;
    for (int k = 0; k < 3; k++)
    {
        if (ESP_Send_Cmd(cmd_buf, "CONNECT", 10000))
        {
            tcp_ok = 1;
            break;
        }
        if (strstr((char *)esp_rx_buf, "ALREADY") != NULL)
        {
            tcp_ok = 1;
            break;
        }
        if (ESP_RxBusyDetected())
        {
            ESP_Log("[ESP] CIPSTART busy，等待后重试...\r\n");
            HAL_Delay(800);
            continue;
        }
        ESP_Log("[ESP] CIPSTART 失败,准备重试...\r\n");
        HAL_Delay(800);
    }

    if (!tcp_ok)
    {
        ESP_Log("[ESP] TCP 连接失败。\r\n");
        ESP_Log_RxBuf("TCP_FAIL");
        g_ui_tcp_ok = 0;
        return false;
    }
    ESP_Log("[ESP] TCP 连接成功（CONNECT）。\r\n");
    (void)ESP_Send_Cmd_Any("AT+CIPSTATUS\r\n", "STATUS:", "OK", 1000);
    ESP_Log_RxBuf("CIPSTATUS");
    g_ui_tcp_ok = 1;
    return true;
#endif
}

static bool ESP_UI_DoRegister(void)
{
#if (EW_USE_ESP32_SPI_UI)
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
    g_uart2_at_mode = 0;
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
#else
    ESP_Log("[UI] Executing REG...\r\n");

    if (!ESP_UI_PrepareAtMode())
        return false;

    if (!ESP_UI_IsTcpConnected())
    {
			ESP_Log("[ESP] 注册前置条件失败:TCP 未连接\r\n");
        g_ui_reg_ok = 0;
        return false;
    }

    ESP_Send_Cmd("AT+CIPMODE=1\r\n", "OK", 1000);
    if (!ESP_Send_Cmd("AT+CIPSEND\r\n", ">", 2000))
    {
        ESP_Log("[ESP] 进入透传发送失败（未出现 > ）\r\n");
        g_ui_reg_ok = 0;
        return false;
    }
    HAL_Delay(500);
    ESP_Register();

    /* 关键：进入“可上报”前初始化 4 通道与 FFT，否则后端只会看到 1 个通道 */
    ESP_Init_Channels_And_DSP();

    /* 注册完成后，切换到数据监听模式 */
    g_esp_ready = 1;
    g_uart2_at_mode = 0;
    ESP_StreamRx_Start();
    ESP_Log("[ESP] 注册完成,链路就绪.\r\n");
    g_ui_reg_ok = 1;
    return true;
#endif
}

static bool ESP_UI_ToggleReport(void)
{
#if (EW_USE_ESP32_SPI_UI)
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
#else
    /* 只允许在链路就绪后开启上报，避免“未注册就上报”造成大量超时 */
    if (!g_report_enabled)
    {
        if (!g_esp_ready)
        {
            ESP_Log("[UI] Report denied: link not ready (need REG first)\r\n");
            return false;
        }
        g_report_enabled = 1U;
        ESP_Log("[UI] Started sensor data upload loop.\r\n");
        /* 记录上次上电前上报状态：开启 */
        (void)ESP_AutoReconnect_SetLastReporting(true);
        return true;
    }
    else
    {
        g_report_enabled = 0U;
        ESP_Log("[UI] Data upload stopped.\r\n");
        /* 记录上次上电前上报状态：关闭 */
        (void)ESP_AutoReconnect_SetLastReporting(false);
        return true;
    }
#endif
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
#if (EW_USE_ESP32_SPI_UI)
            bool ok = ESP_UI_DoApplyConfig();
#else
            bool ok = ESP_Config_LoadRuntimeFromSD();
#endif
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
