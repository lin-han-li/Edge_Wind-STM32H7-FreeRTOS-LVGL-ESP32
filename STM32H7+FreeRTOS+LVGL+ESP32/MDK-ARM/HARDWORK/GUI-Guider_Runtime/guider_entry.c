/**
 * @file guider_entry.c
 * @brief GUI Guider 入口：进入系统后加载 Main_1
 */

#include "../EdgeWind_UI/edgewind_ui.h"
#include "gui_assets.h"
#include "src/generated/gui_guider.h"
#include "esp8266.h"

lv_ui guider_ui;

static bool guider_initialized = false;

/* ================= 断电重连：开机动画结束前自动上报 ================= */
static lv_timer_t *g_boot_autostart_timer = NULL;
static uint8_t g_boot_autostart_done = 0;
static uint8_t g_boot_autostart_tries = 0;

static void boot_autostart_try_cb(lv_timer_t *t)
{
    (void)t;

    if (g_boot_autostart_done) {
        if (g_boot_autostart_timer) {
            lv_timer_del(g_boot_autostart_timer);
            g_boot_autostart_timer = NULL;
        }
        return;
    }

    if (g_boot_autostart_tries++ > 30U) { /* ~6s 超时：避免异常场景无限尝试 */
        g_boot_autostart_done = 1;
        if (g_boot_autostart_timer) {
            lv_timer_del(g_boot_autostart_timer);
            g_boot_autostart_timer = NULL;
        }
        return;
    }

    bool en = true;
    bool last = false;
    (void)ESP_AutoReconnect_Read(&en, &last);
    if (!en || !last) {
        g_boot_autostart_done = 1;
        if (g_boot_autostart_timer) {
            lv_timer_del(g_boot_autostart_timer);
            g_boot_autostart_timer = NULL;
        }
        return;
    }

    /* 自动连接前，先把 UI 保存的 WiFi/Server 配置与通讯参数加载到 ESP 缓存 */
    (void)ESP_Config_LoadFromSD_UIFiles();
    (void)ESP_CommParams_LoadFromSD();

    /* 触发“一键连接 + 开始上报”（ESP 侧 AutoConnect 内部会确保最后开启上报） */
    if (ESP_UI_SendCmd(ESP_UI_CMD_AUTO_CONNECT)) {
        g_boot_autostart_done = 1;
        if (g_boot_autostart_timer) {
            lv_timer_del(g_boot_autostart_timer);
            g_boot_autostart_timer = NULL;
        }
    }
}

void edgewind_ui_on_before_enter_button(void)
{
    /* 开机动画结束但按钮尚未显示：若上次上电前处于上报状态，则自动重连并开始上报 */
    if (g_boot_autostart_done) {
        return;
    }
    if (!g_boot_autostart_timer) {
        g_boot_autostart_timer = lv_timer_create(boot_autostart_try_cb, 200, NULL);
    }
    /* 立刻尝试一次，提升响应速度 */
    boot_autostart_try_cb(g_boot_autostart_timer);
}

void edgewind_ui_on_enter_system(void)
{
    if (!guider_initialized) {
        gui_assets_init();
        setup_ui(&guider_ui);
        gui_assets_patch_images(&guider_ui);
        /* 上电读取一次通讯参数（仅加载到 ESP 缓存；若无文件则保持默认值） */
        (void)ESP_CommParams_LoadFromSD();
        guider_initialized = true;
        return;
    }

    if (guider_ui.Main_1) {
        lv_screen_load(guider_ui.Main_1);
    }
}
