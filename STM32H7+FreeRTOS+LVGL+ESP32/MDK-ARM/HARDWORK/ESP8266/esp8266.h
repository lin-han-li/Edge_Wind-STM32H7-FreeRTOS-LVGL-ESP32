#ifndef __ESP8266_H
#define __ESP8266_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>
#include "sd_config.h"

/* ================= 用户配置区 ================= */
// 引入用户具体的 WiFi 和服务器配置
#include "esp8266_config.h"

// 兜底宏定义：如果用户没有在 config.h 中定义，则使用这里的默认值，防止编译报错
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif
#ifndef SERVER_IP
#define SERVER_IP "192.168.10.43"
#endif
#ifndef SERVER_PORT
#define SERVER_PORT 5000
#endif
#ifndef NODE_ID
#define NODE_ID "STM32_H7_Node"
#endif
#ifndef NODE_LOCATION
#define NODE_LOCATION "Lab_Test"
#endif

/* ================= 调试输出配置 =================
 * 控制日志打印的行为。
 * ESP8266 连接在 USART2，因此调试日志(printf)不能往 USART2 打，否则会干扰 AT 指令。
 * 通常调试日志输出到 USART1 (连接电脑 USB 转串口)。
 */
#ifndef ESP_DEBUG
#define ESP_DEBUG 1 // 1: 开启调试日志，0: 关闭
#endif

/* 是否周期性输出 USART2 RX/ERR 统计（默认关闭，避免未上报时刷屏） */
#ifndef ESP_DEBUG_STATS
#define ESP_DEBUG_STATS 0
#endif

#ifndef ESP_LOG_UART_PORT
#define ESP_LOG_UART_PORT 1 // 指定调试日志使用的串口号 (1 -> huart1)
#endif

/* ================= 串口命令（故障注入）=================
 * 这是一个非常实用的功能：通过串口助手输入指令来模拟故障。
 * 例如输入 "E01" 可以让 STM32 假装发生了故障，从而测试后端的报警功能。
 */
#ifndef ESP_CONSOLE_ENABLE
#define ESP_CONSOLE_ENABLE 1 // 1: 启用串口控制台功能
#endif

/* ================= 链路自恢复配置 =================
 * - 启动强制硬复位：保证每次上电/复位都从干净的 ESP 状态开始
 * - 无服务器响应硬复位阈值：连续 N 秒收不到服务器任何响应（HTTP 回包）则直接硬复位并重连
 */
#ifndef ESP_BOOT_HARDRESET_ONCE
#define ESP_BOOT_HARDRESET_ONCE 1
#endif

#ifndef ESP_NO_SERVER_RX_HARDRESET_SEC
#define ESP_NO_SERVER_RX_HARDRESET_SEC 6
#endif

#ifndef ESP_HEARTBEAT_INTERVAL_MS
#define ESP_HEARTBEAT_INTERVAL_MS 5000
#endif

/* ================= 通讯参数（运行时可配置） =================
 * 这些参数默认由宏兜底（保持兼容），但实际运行会优先使用“运行时缓存值”，
 * 缓存值由 SD 文件 0:/config/ui_param.cfg 加载并应用（见 ESP_CommParams_* API）。
 */
#ifndef ESP_MIN_SEND_INTERVAL_MS
#define ESP_MIN_SEND_INTERVAL_MS 200
#endif

#ifndef ESP_HTTP_TIMEOUT_MS_DEFAULT
#define ESP_HTTP_TIMEOUT_MS_DEFAULT 1200
#endif

#ifndef ESP_CHUNK_KB_DEFAULT
#define ESP_CHUNK_KB_DEFAULT 4
#endif

#ifndef ESP_CHUNK_DELAY_MS_DEFAULT
#define ESP_CHUNK_DELAY_MS_DEFAULT 10
#endif

typedef struct
{
    uint32_t heartbeat_ms;      /* 心跳间隔 ms */
    uint32_t min_interval_ms;   /* 发包限频 ms */
    uint32_t http_timeout_ms;   /* 回包超时 ms（用于 HTTP 门控超时放行） */
    uint32_t hardreset_sec;     /* 无响应复位阈值 s */
    uint32_t wave_step;         /* 波形降采样步进：1=全量，4=每4点取1点 */
    uint32_t upload_points;     /* 降采样后的最多上传点数：256..4096 且 256 步进 */
    uint32_t chunk_kb;          /* 分段发送：每段 KB（0=关闭分段） */
    uint32_t chunk_delay_ms;    /* 分段发送：每段后延时 ms */
} ESP_CommParams_t;

/* 读取/写入运行时缓存（线程安全：内部使用 32-bit 原子写） */
void ESP_CommParams_Get(ESP_CommParams_t *out);
void ESP_CommParams_Apply(const ESP_CommParams_t *p);

/* 从 SD 加载并应用（不会影响 WiFi/Server 等 SystemConfig_t） */
bool ESP_CommParams_LoadFromSD(void);
bool ESP_CommParams_SaveRuntimeToSD(void);

/* 快捷 getter：给内部发送/心跳/自恢复逻辑使用 */
uint32_t ESP_CommParams_HeartbeatMs(void);
uint32_t ESP_CommParams_MinIntervalMs(void);
uint32_t ESP_CommParams_HttpTimeoutMs(void);
uint32_t ESP_CommParams_HardResetSec(void);
uint32_t ESP_CommParams_WaveStep(void);
uint32_t ESP_CommParams_UploadPoints(void);
uint32_t ESP_CommParams_ChunkKb(void);
uint32_t ESP_CommParams_ChunkDelayMs(void);

/* ================= 断电重连/上报状态持久化（SD 标志位） =================
 * 文件：0:/config/ui_autoreport.cfg
 *   AUTO_RECONNECT=0/1   （用户开关）
 *   LAST_REPORTING=0/1   （上次上电前是否处于上报状态）
 */
bool ESP_AutoReconnect_Read(bool *auto_reconnect_en, bool *last_reporting);
bool ESP_AutoReconnect_SetEnabled(bool auto_reconnect_en);
bool ESP_AutoReconnect_SetLastReporting(bool last_reporting);

/* 启动前置：从 SD 读取 WiFi/Server 配置（UI 保存的文件）并应用到 ESP_Config */
bool ESP_Config_LoadFromSD_UIFiles(void);
bool ESP_Config_LoadRuntimeFromSD(void);

// 数据采样与发送参数
/* 波形点数：必须与采样双缓冲长度一致（否则 src[][] 步长不一致会读错内存） */
#ifndef WAVEFORM_POINTS
#ifdef AD_ACQ_POINTS
#define WAVEFORM_POINTS AD_ACQ_POINTS
#else
#define WAVEFORM_POINTS 1024
#endif
#endif
/* * ⚠️ 关键参数 WAVEFORM_SEND_STEP:
 * 浮点数转成字符串通过 UART 发送，数据量较大，容易造成阻塞或溢出。
 * 这里 step=4 表示每隔 4 个点取 1 个点发送（如 4096/4=1024 点），
 * 既保证了波形大致形状，又显著降低了数据传输量。
 */
// 压力测试：不降采样（用户要求）
#ifndef WAVEFORM_SEND_STEP
#define WAVEFORM_SEND_STEP 1
#endif

#ifndef FFT_POINTS
/* FFT 点数固定为采样点数的 1/2（4096->2048，实 FFT 输出 N/2 个幅值） */
#define FFT_POINTS (WAVEFORM_POINTS / 2)
#endif

    /* ================= 数据结构定义 ================= */

    /* 单个通道的数据结构 */
    typedef struct
    {
        uint8_t id;                      // 通道编号 (0~3)
        char label[32];                  // 标签 (如 "直流母线(+)")，后端依据此字段识别数据含义
        char unit[8];                    // 单位 (V, A, mA)
        char type[16];                   // 类型 (预留字段)
        float current_value;             // 当前显示的有效值/瞬时值
        float waveform[WAVEFORM_POINTS]; // 时域波形数组 (源数据)
        float fft_data[FFT_POINTS];      // 频域数据数组 (FFT 计算结果)
    } Channel_Data_t;

    /* 全局变量声明 */
    extern Channel_Data_t node_channels[4]; // 4个通道实例：母线+, 母线-, 电流, 漏电
    extern volatile uint8_t g_esp_ready;    // 标志位：1表示WiFi/TCP已连接，可以发送数据

    /* ================= 函数声明 ================= */
    void ESP_Init(void);                // 初始化 ESP8266 (AT指令序列)
    void ESP_Update_Data_And_FFT(void); // 更新模拟数据并计算 FFT
    void ESP_Post_Data(void);           // 打包 JSON 并通过 HTTP POST 发送
    void ESP_Post_Summary(void);        // 发送轻量数据（无波形/FFT）
    void ESP_Post_Heartbeat(void);      // 发送最小心跳包（保活）
    void ESP_Register(void);            // 向服务器注册节点信息
    void ESP_Console_Init(void);        // 初始化调试控制台中断
    void ESP_Console_Poll(void);        // 在主循环中轮询控制台输入
    const SystemConfig_t *ESP_Config_Get(void);
    void ESP_Config_Apply(const SystemConfig_t *cfg);

    /* ================= DeviceConnect(UI) 驱动接口 =================
     * 目标：ESP8266 不再开机自启动连接，由 UI 的“WiFi/TCP/注册/上报/一键连接”按钮触发
     */
    typedef enum
    {
        ESP_UI_CMD_WIFI = 0,
        ESP_UI_CMD_TCP,
        ESP_UI_CMD_REG,
        ESP_UI_CMD_REPORT_TOGGLE,
        ESP_UI_CMD_AUTO_CONNECT,
        ESP_UI_CMD_APPLY_CONFIG,
    } esp_ui_cmd_t;

    typedef void (*esp_ui_log_hook_t)(const char *line, void *ctx);
    typedef void (*esp_ui_step_hook_t)(esp_ui_cmd_t step, bool ok, void *ctx);

    void ESP_UI_SetHooks(esp_ui_log_hook_t log_hook, void *log_ctx,
                         esp_ui_step_hook_t step_hook, void *step_ctx);
    void ESP_UI_TaskInit(void);                 // 在 ESP8266_Task 里调用一次（创建消息队列）
    bool ESP_UI_SendCmd(esp_ui_cmd_t cmd);      // UI 线程调用（不阻塞）
    void ESP_UI_TaskPoll(void);                 // 在 ESP8266_Task 循环里调用（处理UI命令）
    bool ESP_UI_IsReporting(void);              // 查询当前是否在上报状态
    bool ESP_ServerReportFull(void);            // 查询服务器是否请求全量
    bool ESP_UI_IsWiFiOk(void);                 // 查询 WiFi 步骤是否已成功
    bool ESP_UI_IsTcpOk(void);                  // 查询 TCP 步骤是否已成功
    bool ESP_UI_IsRegOk(void);                  // 查询 REG 步骤是否已成功
    const char *ESP_UI_NodeId(void);            // 查询当前 NodeId（若为空返回 "--"）
    void ESP_UI_InvalidateReg(void);            // UI 用：注册过期/停止上报过久时清除就绪标志，要求重新注册

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_H */
