/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "events_init.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h> /* atoi */
#include "lvgl.h"
#include "../../gui_assets.h"
#include "../../gui_ime_pinyin.h"
#include "../custom/scr_aurora.h"
#include "cmsis_os.h"
#include "main.h"
#include "../../../EdgeComm/edge_comm.h"
#include "fatfs.h"
#include "diskio.h"
#include "bsp_driver_sd.h"

#if LV_USE_GUIDER_SIMULATOR && LV_USE_FREEMASTER
#include "freemaster_client.h"
#endif


static void Main_1_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_GESTURE:
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        switch(dir) {
        case LV_DIR_LEFT:
        {
            lv_indev_wait_release(lv_indev_active());
            ui_load_scr_animation(&guider_ui, &guider_ui.Main_2, guider_ui.Main_2_del, &guider_ui.Main_1_del, setup_scr_Main_2, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

static void Main_1_dot_2_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    if (ui->Main_2 == NULL) {
        setup_scr_Main_2(ui);
    }
    gui_assets_patch_images(ui);
    lv_screen_load_anim(ui->Main_2, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

static void Main_1_dot_3_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    if (ui->Main_3 == NULL) {
        setup_scr_Main_3(ui);
    }
    gui_assets_patch_images(ui);
    lv_screen_load_anim(ui->Main_3, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void events_init_Main_1 (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->Main_1, Main_1_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_1_dot_2, Main_1_dot_2_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->Main_1_dot_3, Main_1_dot_3_event_handler, LV_EVENT_CLICKED, ui);
}

static void Main_2_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_GESTURE:
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        switch(dir) {
        case LV_DIR_LEFT:
        {
            lv_indev_wait_release(lv_indev_active());
            ui_load_scr_animation(&guider_ui, &guider_ui.Main_3, guider_ui.Main_3_del, &guider_ui.Main_2_del, setup_scr_Main_3, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false, false);
            break;
        }
        case LV_DIR_RIGHT:
    {
            lv_indev_wait_release(lv_indev_active());
            ui_load_scr_animation(&guider_ui, &guider_ui.Main_1, guider_ui.Main_1_del, &guider_ui.Main_2_del, setup_scr_Main_1, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false, false);
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

static void Main_2_tile_3_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        lv_indev_wait_release(lv_indev_active());
        ui_load_scr_animation(&guider_ui, &guider_ui.ServerConfig, guider_ui.ServerConfig_del, &guider_ui.Main_2_del,
                              setup_scr_ServerConfig, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
        break;
    }
    default:
        break;
    }
}

static void Main_2_tile_1_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        lv_indev_wait_release(lv_indev_active());
        ui_load_scr_animation(&guider_ui, &guider_ui.ParamConfig, guider_ui.ParamConfig_del, &guider_ui.Main_2_del,
                              setup_scr_ParamConfig, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
        break;
    }
    default:
        break;
    }
}

static void Main_2_tile_2_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        lv_indev_wait_release(lv_indev_active());
        ui_load_scr_animation(&guider_ui, &guider_ui.WifiConfig, guider_ui.WifiConfig_del, &guider_ui.Main_2_del,
                              setup_scr_WifiConfig, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
        break;
    }
    default:
        break;
    }
}

static void Main_2_tile_5_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        lv_indev_wait_release(lv_indev_active());
        ui_load_scr_animation(&guider_ui, &guider_ui.DeviceConnect, guider_ui.DeviceConnect_del, &guider_ui.Main_2_del,
                              setup_scr_DeviceConnect, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
        break;
    }
    default:
        break;
    }
}

static void Main_2_dot_1_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    if (ui->Main_1 == NULL) {
        setup_scr_Main_1(ui);
    }
    gui_assets_patch_images(ui);
    lv_screen_load_anim(ui->Main_1, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    }

static void Main_2_dot_3_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    if (ui->Main_3 == NULL) {
        setup_scr_Main_3(ui);
    }
    gui_assets_patch_images(ui);
    lv_screen_load_anim(ui->Main_3, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void events_init_Main_2 (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->Main_2, Main_2_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_2_tile_1, Main_2_tile_1_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->Main_2_tile_2, Main_2_tile_2_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->Main_2_tile_3, Main_2_tile_3_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->Main_2_tile_5, Main_2_tile_5_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->Main_2_dot_1, Main_2_dot_1_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->Main_2_dot_3, Main_2_dot_3_event_handler, LV_EVENT_CLICKED, ui);
}

static void Main_3_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_GESTURE:
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        switch(dir) {
        case LV_DIR_RIGHT:
        {
            lv_indev_wait_release(lv_indev_active());
            ui_load_scr_animation(&guider_ui, &guider_ui.Main_2, guider_ui.Main_2_del, &guider_ui.Main_3_del, setup_scr_Main_2, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false, false);
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

static void Main_3_dot_1_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    if (ui->Main_1 == NULL) {
        setup_scr_Main_1(ui);
    }
    gui_assets_patch_images(ui);
    lv_screen_load_anim(ui->Main_1, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

static void Main_3_dot_2_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    if (ui->Main_2 == NULL) {
        setup_scr_Main_2(ui);
    }
    gui_assets_patch_images(ui);
    lv_screen_load_anim(ui->Main_2, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

static void WifiConfig_ta_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        if (ui->WifiConfig_kb != NULL) {
            lv_textarea_clear_selection(ta);
            lv_keyboard_set_textarea(ui->WifiConfig_kb, ta);
#if LV_USE_IME_PINYIN
            (void)gui_ime_pinyin_attach(ui->WifiConfig_kb);
#endif
            lv_obj_remove_flag(ui->WifiConfig_kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void WifiConfig_kb_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(ui->WifiConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ================= WifiConfig: 仅 UI 保存/读取（不改 EdgeComm 配置） =================
 * 文件位置：SD(0:) -> 0:/config/ui_wifi.cfg
 * 格式（纯文本）：
 *   SSID=xxxx
 *   PWD=yyyy
 */
#define UI_WIFI_CFG_DIR  "0:/config"
#define UI_WIFI_CFG_FILE "0:/config/ui_wifi.cfg"

static void ui_wifi_cfg_set_status(lv_ui *ui, const char *text, uint32_t color_hex)
{
    if (!ui || !ui->WifiConfig_lbl_status || !lv_obj_is_valid(ui->WifiConfig_lbl_status))
        return;
    lv_label_set_text(ui->WifiConfig_lbl_status, text ? text : "");
    lv_obj_set_style_text_color(ui->WifiConfig_lbl_status, lv_color_hex(color_hex), LV_PART_MAIN);
}

static void ui_wifi_cfg_rstrip(char *s)
{
    if (!s)
        return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t'))
    {
        s[n - 1] = '\0';
        n--;
    }
}

/* UI 配置共用的 SD 挂载状态（简化版，直接在 LVGL 线程同步执行，彻底避免死锁） */
static volatile uint8_t g_ui_sd_mounted = 0;
static volatile FRESULT g_ui_sd_last_err = FR_OK;
static uint32_t g_ui_sd_last_err_tick = 0;

/* 统一 SD 错误码到 UI 状态文本的映射 */
static void ui_sd_result_to_status(lv_ui *ui, FRESULT res, 
                                    void (*set_status_fn)(lv_ui *, const char *, uint32_t),
                                    const char *success_text)
{
    switch (res) {
        case FR_OK:
            set_status_fn(ui, success_text, 0x3dfb00);
            break;
        case FR_NO_FILE:
            set_status_fn(ui, "未找到配置文件", 0xFFD000);
            break;
        case FR_NO_FILESYSTEM:
            set_status_fn(ui, "SD未格式化/格式化失败", 0xFF5555);
            break;
        case FR_NOT_READY:
            set_status_fn(ui, "SD未就绪", 0xFF5555);
            break;
        case FR_DISK_ERR:
            set_status_fn(ui, "SD读写错误(可重试)", 0xFF5555);
            break;
        case FR_TIMEOUT:
            set_status_fn(ui, "操作超时", 0xFF5555);
            break;
        default: {
            char buf[40];
            snprintf(buf, sizeof(buf), "SD错误: %d", (int)res);
            set_status_fn(ui, buf, 0xFF5555);
        break;
    }
}
}

/* 增强的 SD mount：支持自动 mkfs + 创建 config 目录 */
static FRESULT ui_sd_mount_with_mkfs(void)
{
    /* 检查是否正在 QSPI 同步（防止竞争） */
    extern volatile uint8_t g_qspi_sd_sync_in_progress;
    if (g_qspi_sd_sync_in_progress) {
        printf("[UI_SD] QSPI sync in progress, skip mount\r\n");
        return FR_DISK_ERR;
    }

    /* 即使已经 mount，也需要验证 SD 卡状态（可能在前一次写入后变为 BUSY/ERROR） */
    if (g_ui_sd_mounted) {
        uint8_t card_state = BSP_SD_GetCardState();
        DSTATUS disk_st = disk_status(0);
        printf("[UI_SD] recheck mounted: card_state=%u disk_st=0x%02X\r\n", 
               (unsigned)card_state, (unsigned)disk_st);
        
        /* 若状态正常，直接返回 */
        if (card_state == SD_TRANSFER_OK && !(disk_st & STA_NOINIT)) {
            return FR_OK;
        }
        
        /* 状态异常，重新初始化 */
        printf("[UI_SD] SD state abnormal, remount needed\r\n");
        g_ui_sd_mounted = 0;
        g_ui_sd_last_err = FR_NOT_READY;
    }

    /* 缓存 FR_NO_FILESYSTEM：5 秒内不再重复尝试（避免格式化失败后反复卡死） */
    if (g_ui_sd_last_err == FR_NO_FILESYSTEM) {
        if ((osKernelGetTickCount() - g_ui_sd_last_err_tick) < 5000U) {
            printf("[UI_SD] skip mount (cached NO_FILESYSTEM)\r\n");
            return FR_NO_FILESYSTEM;
        }
    }

    /* 确保驱动已链接 */
    if (SDPath[0] == '\0') {
        MX_FATFS_Init();
    }

    /* 先快速判定是否插卡 */
    if (BSP_SD_IsDetected() != SD_PRESENT)
    {
        g_ui_sd_mounted = 0;
        g_ui_sd_last_err = FR_NOT_READY;
        g_ui_sd_last_err_tick = osKernelGetTickCount();
        printf("[UI_SD] SD not present\r\n");
        return FR_NOT_READY;
    }

    /* 关键修复：UI 层不直接 BSP_SD_Init（避免与 diskio 内部初始化/状态机冲突）
     * 统一走 disk_initialize(0) + f_mount。
     */
    FRESULT res = FR_DISK_ERR;

    DSTATUS st_init = disk_initialize(0);
    DSTATUS st_stat = disk_status(0);
    printf("[UI_SD] disk_init=0x%02X status=0x%02X\r\n",
           (unsigned)st_init, (unsigned)st_stat);
    if (st_init & STA_NOINIT)
    {
        res = FR_NOT_READY;
        g_ui_sd_last_err = res;
        g_ui_sd_last_err_tick = osKernelGetTickCount();
        return res;
    }

    /* 等待卡进入 TRANSFER_OK（短超时） */
    uint32_t t0 = osKernelGetTickCount();
    while ((osKernelGetTickCount() - t0) < 200U)
    {
        if (BSP_SD_GetCardState() == SD_TRANSFER_OK)
        break;
        osDelay(5);
    }
    printf("[UI_SD] state=%u (0=OK,1=BUSY)\r\n", (unsigned)BSP_SD_GetCardState());

    res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
    printf("[UI_SD] mount %s -> %d\r\n", SDPath, (int)res);
    if (res == FR_OK)
    {
        g_ui_sd_mounted = 1;
        g_ui_sd_last_err = FR_OK;
        /* 确保 config 目录存在 */
        FRESULT mkdir_res = f_mkdir("0:/config");
        if (mkdir_res != FR_OK && mkdir_res != FR_EXIST) {
            printf("[UI_SD] mkdir 0:/config -> %d (non-fatal)\r\n", (int)mkdir_res);
        }
        return FR_OK;
    }

    /* 处理 FR_NO_FILESYSTEM：自动格式化 */
    if (res == FR_NO_FILESYSTEM)
{
        printf("[UI_SD] no filesystem, mkfs disabled in UI path\r\n");
        g_ui_sd_last_err = FR_NO_FILESYSTEM;
        g_ui_sd_last_err_tick = osKernelGetTickCount();
        return FR_NO_FILESYSTEM;
#if 0
        printf("[UI_SD] no filesystem, formatting (FAT32)...\r\n");
        static uint8_t mkfs_work[4096];
        FRESULT mkfs_res = f_mkfs((TCHAR const *)SDPath, FM_FAT32, 0, mkfs_work, sizeof(mkfs_work));
        printf("[UI_SD] mkfs -> %d\r\n", (int)mkfs_res);
        if (mkfs_res != FR_OK)
        {
            printf("[UI_SD] mkfs failed\r\n");
            g_ui_sd_last_err = FR_NO_FILESYSTEM;
            g_ui_sd_last_err_tick = osKernelGetTickCount();
            return FR_NO_FILESYSTEM;
        }
        /* 格式化成功，重新 mount */
        res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
        printf("[UI_SD] remount after mkfs -> %d\r\n", (int)res);
        if (res == FR_OK)
        {
            g_ui_sd_mounted = 1;
            g_ui_sd_last_err = FR_OK;
            FRESULT mkdir_res = f_mkdir("0:/config");
            printf("[UI_SD] mkdir 0:/config -> %d\r\n", (int)mkdir_res);
            return FR_OK;
    }
#endif
    }

    g_ui_sd_mounted = 0;
    g_ui_sd_last_err = res;
    g_ui_sd_last_err_tick = osKernelGetTickCount();
    return res;
}

static FRESULT ui_wifi_cfg_ensure_dir(void)
{
    FILINFO fno;
    FRESULT res = f_stat(UI_WIFI_CFG_DIR, &fno);
    printf("[UI_CFG] stat dir %s -> %d\r\n", UI_WIFI_CFG_DIR, (int)res);
    if (res == FR_OK)
    {
        if (fno.fattrib & AM_DIR)
            return FR_OK;
        printf("[UI_CFG] path exists but not dir: %s\r\n", UI_WIFI_CFG_DIR);
        return FR_INVALID_NAME;
    }
    /* FR_NO_FILE(4) 或 FR_NO_PATH(5) 都表示目录不存在，需要创建 */
    if (res != FR_NO_FILE && res != FR_NO_PATH)
{
        printf("[UI_CFG] stat dir unexpected fail: %s res=%d\r\n", UI_WIFI_CFG_DIR, (int)res);
        return res;
    }
    printf("[UI_CFG] creating dir %s ...\r\n", UI_WIFI_CFG_DIR);
    res = f_mkdir(UI_WIFI_CFG_DIR);
    printf("[UI_CFG] mkdir %s -> %d\r\n", UI_WIFI_CFG_DIR, (int)res);
    /* FR_EXIST 也算成功（目录已存在） */
    if (res == FR_EXIST)
        return FR_OK;
    return res;
}

static FRESULT ui_wifi_cfg_read_file(char *ssid, size_t ssid_len, char *pwd, size_t pwd_len)
{
    if (!ssid || !pwd || ssid_len == 0 || pwd_len == 0)
        return FR_INVALID_OBJECT;
    ssid[0] = '\0';
    pwd[0] = '\0';

    FRESULT res = ui_sd_mount_with_mkfs();
    if (res != FR_OK)
    {
        printf("[WIFI_UI_CFG] load skipped: SD not ready (res=%d)\r\n", (int)res);
        return res;
    }

    FIL fil;
    for (int attempt = 1; attempt <= 2; ++attempt) {
        res = f_open(&fil, UI_WIFI_CFG_FILE, FA_READ);
        if (res == FR_OK) {
        break;
    }
        printf("[WIFI_UI_CFG] open for read fail: %s res=%d attempt=%d\r\n",
               UI_WIFI_CFG_FILE, (int)res, attempt);
        /* 磁盘错误：强制 remount 后重试一次 */
        if (res == FR_DISK_ERR && attempt == 1) {
            g_ui_sd_mounted = 0;
            (void)ui_sd_mount_with_mkfs();
            continue;
        }
        return res;
    }

    char line[160];
    while (f_gets(line, sizeof(line), &fil))
    {
        ui_wifi_cfg_rstrip(line);
        if (strncmp(line, "SSID=", 5) == 0)
        {
            strncpy(ssid, line + 5, ssid_len - 1);
            ssid[ssid_len - 1] = '\0';
        }
        else if (strncmp(line, "PWD=", 4) == 0)
        {
            strncpy(pwd, line + 4, pwd_len - 1);
            pwd[pwd_len - 1] = '\0';
        }
    }
    (void)f_close(&fil);

    printf("[WIFI_UI_CFG] loaded: ssid_len=%u pwd_len=%u\r\n", (unsigned)strlen(ssid), (unsigned)strlen(pwd));
    return FR_OK;
}

static void ui_wifi_cfg_apply_to_ui(lv_ui *ui, const char *ssid, const char *pwd)
{
    if (!ui)
        return;
    /* 允许覆盖为空串：用户清空后再次进入界面也能恢复（或显示空） */
    if (ui->WifiConfig_ta_ssid && lv_obj_is_valid(ui->WifiConfig_ta_ssid))
        lv_textarea_set_text(ui->WifiConfig_ta_ssid, ssid ? ssid : "");
    if (ui->WifiConfig_ta_pwd && lv_obj_is_valid(ui->WifiConfig_ta_pwd))
        lv_textarea_set_text(ui->WifiConfig_ta_pwd, pwd ? pwd : "");
}

static FRESULT ui_wifi_cfg_write_file(const char *ssid, const char *pwd)
{
    printf("[WIFI_UI_CFG] write_file: start\r\n");
    FRESULT res = ui_sd_mount_with_mkfs();
    if (res != FR_OK)
    {
        printf("[WIFI_UI_CFG] save failed: SD not ready (res=%d)\r\n", (int)res);
        return res;
    }
    printf("[WIFI_UI_CFG] write_file: SD mounted\r\n");

    res = ui_wifi_cfg_ensure_dir();
    printf("[WIFI_UI_CFG] write_file: ensure_dir res=%d\r\n", (int)res);
    if (res != FR_OK)
    {
        printf("[WIFI_UI_CFG] ensure dir fail: res=%d\r\n", (int)res);
        return res;
    }

    /* 原子写入：先写临时文件，再 rename 覆盖 */
    const char *tmp_path = "0:/config/.ui_wifi.cfg.tmp";

    printf("[WIFI_UI_CFG] write_file: opening tmp...\r\n");
    FIL fil;
    res = f_open(&fil, tmp_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (res != FR_OK)
    {
        printf("[WIFI_UI_CFG] open for write fail: %s res=%d\r\n", tmp_path, (int)res);
        /* 若遇到磁盘错误，强制卸载/重挂载后再重试一次 */
        if (res == FR_DISK_ERR) {
            g_ui_sd_mounted = 0;
            (void)f_mount(NULL, (TCHAR const *)SDPath, 0);
            osDelay(10);
            (void)ui_sd_mount_with_mkfs();
            res = f_open(&fil, tmp_path, FA_CREATE_ALWAYS | FA_WRITE);
            printf("[WIFI_UI_CFG] retry open tmp -> %d\r\n", (int)res);
        }
        if (res != FR_OK) {
            g_ui_sd_last_err = res;
            g_ui_sd_last_err_tick = osKernelGetTickCount();
            return res;
        }
    }

    printf("[WIFI_UI_CFG] write_file: writing...\r\n");
    char buf[220];
    UINT bw = 0;
    int n = snprintf(buf, sizeof(buf), "SSID=%s\r\nPWD=%s\r\n", ssid ? ssid : "", pwd ? pwd : "");
    if (n < 0)
        n = 0;
    if ((size_t)n > sizeof(buf))
        n = (int)sizeof(buf);
    res = f_write(&fil, buf, (UINT)n, &bw);
    printf("[WIFI_UI_CFG] write_file: f_write res=%d bw=%u\r\n", (int)res, (unsigned)bw);
    
    /* 仅在写入成功时才 sync，且添加短延迟避免 SD 卡 BUSY 卡死 */
    if (res == FR_OK) {
        osDelay(5); /* 短延迟让 SD 卡缓冲稳定 */
        FRESULT sync_res = f_sync(&fil);
        printf("[WIFI_UI_CFG] write_file: f_sync res=%d\r\n", (int)sync_res);
        if (sync_res != FR_OK) {
            res = sync_res;
        }
    }
    
    FRESULT close_res = f_close(&fil);
    printf("[WIFI_UI_CFG] write_file: f_close res=%d\r\n", (int)close_res);
    if (close_res != FR_OK && res == FR_OK) {
        res = close_res;
    }
    
    /* 关闭后短暂等待 SD 进入就绪状态，避免立即切换界面时读取遇到 BUSY */
    if (res == FR_OK) {
        uint32_t t0 = osKernelGetTickCount();
        while ((osKernelGetTickCount() - t0) < 100U) {
            if (BSP_SD_GetCardState() == SD_TRANSFER_OK)
        break;
            osDelay(5);
        }
        printf("[WIFI_UI_CFG] post-close wait: card_state=%u\r\n", (unsigned)BSP_SD_GetCardState());
    }

    /* 如果文件内容未成功落盘，清理临时文件并让下次强制 remount */
    if (res != FR_OK) {
        printf("[WIFI_UI_CFG] write_file failed, unlink tmp\r\n");
        (void)f_unlink(tmp_path);
        g_ui_sd_mounted = 0;
        g_ui_sd_last_err = res;
        g_ui_sd_last_err_tick = osKernelGetTickCount();
        return res;
    }

    /* 覆盖写入：先删旧文件，再 rename */
    (void)f_unlink(UI_WIFI_CFG_FILE);
    FRESULT rn = f_rename(tmp_path, UI_WIFI_CFG_FILE);
    printf("[WIFI_UI_CFG] rename tmp -> cfg res=%d\r\n", (int)rn);
    if (rn != FR_OK) {
        (void)f_unlink(tmp_path);
        g_ui_sd_mounted = 0;
        g_ui_sd_last_err = rn;
        g_ui_sd_last_err_tick = osKernelGetTickCount();
        return rn;
    }

    printf("[WIFI_UI_CFG] saved: final_res=%d bytes=%u ssid_len=%u pwd_len=%u\r\n",
           (int)res, (unsigned)bw, (unsigned)(ssid ? strlen(ssid) : 0U), (unsigned)(pwd ? strlen(pwd) : 0U));
    return FR_OK;
}

/* ============ WiFi 配置 SD 同步操作（直接在 LVGL 线程执行，彻底避免死锁） ============ */

static void ui_wifi_cfg_do_load_sync(lv_ui *ui)
{
    if (!ui) return;
    char ssid[64] = {0};
    char pwd[64]  = {0};
    FRESULT res = ui_wifi_cfg_read_file(ssid, sizeof(ssid), pwd, sizeof(pwd));
    if (res == FR_OK) {
        ui_wifi_cfg_apply_to_ui(ui, ssid, pwd);
    }
    ui_sd_result_to_status(ui, res, ui_wifi_cfg_set_status, "加载完成");
}

static void ui_wifi_cfg_do_save_sync(lv_ui *ui)
{
    if (!ui) return;
    const char *ssid = (ui->WifiConfig_ta_ssid) ? lv_textarea_get_text(ui->WifiConfig_ta_ssid) : "";
    const char *pwd  = (ui->WifiConfig_ta_pwd) ? lv_textarea_get_text(ui->WifiConfig_ta_pwd) : "";
    FRESULT res = ui_wifi_cfg_write_file(ssid ? ssid : "", pwd ? pwd : "");
    
    if (res == FR_OK) {
        char r_ssid[64] = {0}, r_pwd[64] = {0};
        if (ui_wifi_cfg_read_file(r_ssid, sizeof(r_ssid), r_pwd, sizeof(r_pwd)) == FR_OK) {
            ui_wifi_cfg_apply_to_ui(ui, r_ssid, r_pwd);
        }
    }
    
    bool runtime_ok = (res == FR_OK) ? ESP_Config_LoadRuntimeFromSD() : false;
    ui_sd_result_to_status(ui, res, ui_wifi_cfg_set_status, "Save OK");
    if (res == FR_OK) {
        if (ESP_UI_IsReporting()) {
            ui_wifi_cfg_set_status(ui, "Saved; reporting active, reconnect to apply", 0xFFA500);
        } else if (!runtime_ok) {
            ui_wifi_cfg_set_status(ui, "Saved; SD config incomplete, not synced", 0xFFA500);
        } else {
            (void)ESP_UI_SendCmd(ESP_UI_CMD_APPLY_CONFIG);
            ui_wifi_cfg_set_status(ui, "Saved; ESP32 sync queued", 0x3dfb00);
        }
    }
    
    if (ui->WifiConfig_btn_save && lv_obj_is_valid(ui->WifiConfig_btn_save))
        lv_obj_clear_state(ui->WifiConfig_btn_save, LV_STATE_DISABLED);
}


/* 进入 WifiConfig 界面后自动加载（延迟一帧执行，保证屏幕已渲染完成） */
static void WifiConfig_load_timer_cb(lv_timer_t *t)
{
    lv_ui *ui = (lv_ui *)lv_timer_get_user_data(t);
    lv_timer_del(t);
    if (!ui) return;
    ui_wifi_cfg_set_status(ui, "正在加载...", 0xFFA500);
    /* 立即刷新一次，让用户看到"正在加载"提示 */
    lv_obj_update_layout(ui->WifiConfig);
    lv_refr_now(NULL);
    /* 同步执行 SD 读取（在 LVGL 线程内，不会死锁） */
    ui_wifi_cfg_do_load_sync(ui);
}

/* 每次屏幕被加载（切换进入）都触发一次自动加载 */
static void WifiConfig_screen_event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SCREEN_LOADED) {
        return;
    }
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) return;
    ui_wifi_cfg_set_status(ui, "正在加载...", 0xFFA500);
    lv_timer_create(WifiConfig_load_timer_cb, 100, ui);
}

static void WifiConfig_save_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) return;
    lv_indev_wait_release(lv_indev_active());
    if (ui->WifiConfig_kb) {
        lv_obj_add_flag(ui->WifiConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui->WifiConfig_btn_save && lv_obj_is_valid(ui->WifiConfig_btn_save))
        lv_obj_add_state(ui->WifiConfig_btn_save, LV_STATE_DISABLED);
    ui_wifi_cfg_set_status(ui, "正在保存...", 0xFFA500);
    lv_obj_update_layout(ui->WifiConfig);
    lv_refr_now(NULL);
    /* 同步执行 SD 写入（在 LVGL 线程内，不会死锁） */
    ui_wifi_cfg_do_save_sync(ui);
}

static void WifiConfig_scan_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) return;
    lv_indev_wait_release(lv_indev_active());
    if (ui->WifiConfig_kb) {
        lv_obj_add_flag(ui->WifiConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
    ui_wifi_cfg_set_status(ui, "正在加载...", 0xFFA500);
    lv_obj_update_layout(ui->WifiConfig);
    lv_refr_now(NULL);
    /* 同步执行 SD 读取（在 LVGL 线程内，不会死锁） */
    ui_wifi_cfg_do_load_sync(ui);
}

static void WifiConfig_back_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    if (ui->WifiConfig_kb) {
        lv_obj_add_flag(ui->WifiConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
    ui_load_scr_animation(&guider_ui, &guider_ui.Main_1, guider_ui.Main_1_del, &guider_ui.WifiConfig_del,
                          setup_scr_Aurora, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
}

static void ServerConfig_ta_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        if (ui->ServerConfig_kb != NULL) {
            lv_textarea_clear_selection(ta);
            lv_keyboard_set_textarea(ui->ServerConfig_kb, ta);
#if LV_USE_IME_PINYIN
            (void)gui_ime_pinyin_attach(ui->ServerConfig_kb);
#endif
            lv_obj_remove_flag(ui->ServerConfig_kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ServerConfig_kb_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(ui->ServerConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ServerConfig_back_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
            lv_indev_wait_release(lv_indev_active());
    if (ui->ServerConfig_kb) {
        lv_obj_add_flag(ui->ServerConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
    ui_load_scr_animation(&guider_ui, &guider_ui.Main_1, guider_ui.Main_1_del, &guider_ui.ServerConfig_del,
                          setup_scr_Aurora, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
}

/* ============ 服务器配置 SD 持久化（直接在 LVGL 线程同步执行） ============ */

#define UI_SERVER_CFG_FILE "0:/config/ui_server.cfg"

static void ui_server_cfg_set_status(lv_ui *ui, const char *text, uint32_t color_hex)
{
    if (!ui || !ui->ServerConfig_lbl_status || !lv_obj_is_valid(ui->ServerConfig_lbl_status))
        return;
    lv_label_set_text(ui->ServerConfig_lbl_status, text ? text : "");
    lv_obj_set_style_text_color(ui->ServerConfig_lbl_status, lv_color_hex(color_hex), LV_PART_MAIN);
        }

static void ui_server_cfg_apply_to_ui(lv_ui *ui, const char *ip, const char *port, const char *id, const char *loc)
{
    if (!ui) return;
    /* 允许覆盖为空串：用户清空后再次进入界面也能恢复（或显示空） */
    if (ui->ServerConfig_ta_ip && lv_obj_is_valid(ui->ServerConfig_ta_ip))
        lv_textarea_set_text(ui->ServerConfig_ta_ip, ip ? ip : "");
    if (ui->ServerConfig_ta_port && lv_obj_is_valid(ui->ServerConfig_ta_port))
        lv_textarea_set_text(ui->ServerConfig_ta_port, port ? port : "");
    if (ui->ServerConfig_ta_id && lv_obj_is_valid(ui->ServerConfig_ta_id))
        lv_textarea_set_text(ui->ServerConfig_ta_id, id ? id : "");
    if (ui->ServerConfig_ta_loc && lv_obj_is_valid(ui->ServerConfig_ta_loc))
        lv_textarea_set_text(ui->ServerConfig_ta_loc, loc ? loc : "");
}

static FRESULT ui_server_cfg_read_file(char *ip, size_t ip_len, char *port, size_t port_len,
                                       char *id, size_t id_len, char *loc, size_t loc_len)
{
    if (!ip || !port || !id || !loc) return FR_INVALID_OBJECT;
    ip[0] = port[0] = id[0] = loc[0] = '\0';

    FRESULT res = ui_sd_mount_with_mkfs();
    if (res != FR_OK) {
        printf("[SERVER_UI_CFG] load skipped: SD not ready (res=%d)\r\n", (int)res);
        return res;
    }

    FIL fil;
    for (int attempt = 1; attempt <= 2; ++attempt) {
        res = f_open(&fil, UI_SERVER_CFG_FILE, FA_READ);
        if (res == FR_OK) {
            break;
        }
        printf("[SERVER_UI_CFG] open for read fail: %s res=%d attempt=%d\r\n",
               UI_SERVER_CFG_FILE, (int)res, attempt);
        if (res == FR_DISK_ERR && attempt == 1) {
            g_ui_sd_mounted = 0;
            (void)ui_sd_mount_with_mkfs();
            continue;
        }
        return res;
    }

    char line[160];
    while (f_gets(line, sizeof(line), &fil)) {
        ui_wifi_cfg_rstrip(line);
        if (strncmp(line, "IP=", 3) == 0) {
            strncpy(ip, line + 3, ip_len - 1);
            ip[ip_len - 1] = '\0';
        } else if (strncmp(line, "PORT=", 5) == 0) {
            strncpy(port, line + 5, port_len - 1);
            port[port_len - 1] = '\0';
        } else if (strncmp(line, "ID=", 3) == 0) {
            strncpy(id, line + 3, id_len - 1);
            id[id_len - 1] = '\0';
        } else if (strncmp(line, "LOC=", 4) == 0) {
            strncpy(loc, line + 4, loc_len - 1);
            loc[loc_len - 1] = '\0';
        }
    }
    (void)f_close(&fil);

    printf("[SERVER_UI_CFG] loaded: ip=%s port=%s id=%s loc=%s\r\n", ip, port, id, loc);
    return FR_OK;
}

static bool ui_server_cfg_parse_port_local(const char *s, uint32_t *out)
{
    if (!s || !out) return false;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') return false;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s) return false;
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\0') return false;
    *out = (uint32_t)v;
    return true;
}

static bool ui_server_cfg_validate_and_warn(lv_ui *ui, const char *ip, const char *port, const char *id)
{
    uint32_t port_u = 0;
    if (!ip || ip[0] == '\0') {
        ui_server_cfg_set_status(ui, "Server host is empty", 0xFF4444);
        return false;
    }
    if (!id || id[0] == '\0') {
        ui_server_cfg_set_status(ui, "Node ID is empty", 0xFF4444);
        return false;
    }
    if (!ui_server_cfg_parse_port_local(port, &port_u) || port_u == 0U || port_u > 65535U) {
        ui_server_cfg_set_status(ui, "Port must be 1..65535", 0xFF4444);
        return false;
    }
    return true;
}

static FRESULT ui_server_cfg_write_file(const char *ip, const char *port, const char *id, const char *loc)
{
    printf("[SERVER_UI_CFG] write_file: start\r\n");
    FRESULT res = ui_sd_mount_with_mkfs();
    if (res != FR_OK) {
        printf("[SERVER_UI_CFG] save failed: SD not ready (res=%d)\r\n", (int)res);
        return res;
    }
    printf("[SERVER_UI_CFG] write_file: SD mounted\r\n");

    res = ui_wifi_cfg_ensure_dir();
    printf("[SERVER_UI_CFG] write_file: ensure_dir res=%d\r\n", (int)res);
    if (res != FR_OK) {
        printf("[SERVER_UI_CFG] ensure dir fail: res=%d\r\n", (int)res);
        return res;
    }

    /* 原子写入：先写临时文件，再 rename 覆盖 */
    const char *tmp_path = "0:/config/.ui_server.cfg.tmp";

    printf("[SERVER_UI_CFG] write_file: opening tmp...\r\n");
    FIL fil;
    res = f_open(&fil, tmp_path, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        printf("[SERVER_UI_CFG] open for write fail: %s res=%d\r\n", tmp_path, (int)res);
        /* 若遇到磁盘错误，强制卸载/重挂载后再重试一次 */
        if (res == FR_DISK_ERR) {
            g_ui_sd_mounted = 0;
            (void)f_mount(NULL, (TCHAR const *)SDPath, 0);
            osDelay(10);
            (void)ui_sd_mount_with_mkfs();
            res = f_open(&fil, tmp_path, FA_WRITE | FA_CREATE_ALWAYS);
            printf("[SERVER_UI_CFG] retry open tmp -> %d\r\n", (int)res);
        }
        if (res != FR_OK) {
            g_ui_sd_last_err = res;
            g_ui_sd_last_err_tick = osKernelGetTickCount();
            return res;
        }
    }

    printf("[SERVER_UI_CFG] write_file: writing...\r\n");
    char buf[320];
    int n = snprintf(buf, sizeof(buf), "IP=%s\nPORT=%s\nID=%s\nLOC=%s\n",
                     ip ? ip : "", port ? port : "", id ? id : "", loc ? loc : "");
    UINT bw = 0;
    res = f_write(&fil, buf, (UINT)n, &bw);
    printf("[SERVER_UI_CFG] write_file: f_write res=%d bw=%u\r\n", (int)res, (unsigned)bw);
    
    /* 仅在写入成功时才 sync，且添加短延迟避免 SD 卡 BUSY 卡死 */
    if (res == FR_OK) {
        osDelay(5); /* 短延迟让 SD 卡缓冲稳定 */
        FRESULT sync_res = f_sync(&fil);
        printf("[SERVER_UI_CFG] write_file: f_sync res=%d\r\n", (int)sync_res);
        if (sync_res != FR_OK) {
            res = sync_res;
        }
    }
    
    FRESULT close_res = f_close(&fil);
    printf("[SERVER_UI_CFG] write_file: f_close res=%d\r\n", (int)close_res);
    if (close_res != FR_OK && res == FR_OK) {
        res = close_res;
    }
    
    /* 关闭后短暂等待 SD 进入就绪状态，避免立即切换界面时读取遇到 BUSY */
    if (res == FR_OK) {
        uint32_t t0 = osKernelGetTickCount();
        while ((osKernelGetTickCount() - t0) < 100U) {
            if (BSP_SD_GetCardState() == SD_TRANSFER_OK)
        break;
            osDelay(5);
        }
        printf("[SERVER_UI_CFG] post-close wait: card_state=%u\r\n", (unsigned)BSP_SD_GetCardState());
    }

    if (res != FR_OK) {
        printf("[SERVER_UI_CFG] write_file failed, unlink tmp\r\n");
        (void)f_unlink(tmp_path);
        g_ui_sd_mounted = 0;
        g_ui_sd_last_err = res;
        g_ui_sd_last_err_tick = osKernelGetTickCount();
        return res;
    }

    (void)f_unlink(UI_SERVER_CFG_FILE);
    FRESULT rn = f_rename(tmp_path, UI_SERVER_CFG_FILE);
    printf("[SERVER_UI_CFG] rename tmp -> cfg res=%d\r\n", (int)rn);
    if (rn != FR_OK) {
        (void)f_unlink(tmp_path);
        g_ui_sd_mounted = 0;
        g_ui_sd_last_err = rn;
        g_ui_sd_last_err_tick = osKernelGetTickCount();
        return rn;
    }

    printf("[SERVER_UI_CFG] saved: final_res=%d bytes=%u\r\n", (int)res, (unsigned)bw);
    return FR_OK;
}

static void ui_server_cfg_do_load_sync(lv_ui *ui)
{
    if (!ui) return;
    char ip[64] = {0}, port[16] = {0}, id[80] = {0}, loc[80] = {0};
    FRESULT res = ui_server_cfg_read_file(ip, sizeof(ip), port, sizeof(port), id, sizeof(id), loc, sizeof(loc));
    if (res == FR_OK) {
        ui_server_cfg_apply_to_ui(ui, ip, port, id, loc);
    }
    ui_sd_result_to_status(ui, res, ui_server_cfg_set_status, "加载完成");
}

static void ui_server_cfg_do_save_sync(lv_ui *ui)
{
    if (!ui) return;
    const char *ip   = (ui->ServerConfig_ta_ip)   ? lv_textarea_get_text(ui->ServerConfig_ta_ip)   : "";
    const char *port = (ui->ServerConfig_ta_port) ? lv_textarea_get_text(ui->ServerConfig_ta_port) : "";
    const char *id   = (ui->ServerConfig_ta_id)   ? lv_textarea_get_text(ui->ServerConfig_ta_id)   : "";
    const char *loc  = (ui->ServerConfig_ta_loc)  ? lv_textarea_get_text(ui->ServerConfig_ta_loc)  : "";

    if (!ui_server_cfg_validate_and_warn(ui, ip, port, id)) {
        if (ui->ServerConfig_btn_save && lv_obj_is_valid(ui->ServerConfig_btn_save))
            lv_obj_clear_state(ui->ServerConfig_btn_save, LV_STATE_DISABLED);
        return;
    }

    FRESULT res = ui_server_cfg_write_file(ip, port, id, loc);
    
    bool runtime_ok = (res == FR_OK) ? ESP_Config_LoadRuntimeFromSD() : false;
    ui_sd_result_to_status(ui, res, ui_server_cfg_set_status, "Save OK");
    if (res == FR_OK) {
        if (ESP_UI_IsReporting()) {
            ui_server_cfg_set_status(ui, "Saved; reporting active, reconnect to apply", 0xFFA500);
        } else if (!runtime_ok) {
            ui_server_cfg_set_status(ui, "Saved; SD config incomplete, not synced", 0xFFA500);
        } else {
            (void)ESP_UI_SendCmd(ESP_UI_CMD_APPLY_CONFIG);
            ui_server_cfg_set_status(ui, "Saved; ESP32 sync queued", 0x3dfb00);
        }
    }
    
    if (ui->ServerConfig_btn_save && lv_obj_is_valid(ui->ServerConfig_btn_save))
        lv_obj_clear_state(ui->ServerConfig_btn_save, LV_STATE_DISABLED);
}


static void ServerConfig_load_timer_cb(lv_timer_t *t)
{
    lv_ui *ui = (lv_ui *)lv_timer_get_user_data(t);
    lv_timer_del(t);
    if (!ui) return;
    ui_server_cfg_set_status(ui, "正在加载...", 0xFFA500);
    lv_obj_update_layout(ui->ServerConfig);
    lv_refr_now(NULL);
    ui_server_cfg_do_load_sync(ui);
}

/* 每次屏幕被加载（切换进入）都触发一次自动加载 */
static void ServerConfig_screen_event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SCREEN_LOADED) {
        return;
    }
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) return;
    ui_server_cfg_set_status(ui, "正在加载...", 0xFFA500);
    lv_timer_create(ServerConfig_load_timer_cb, 100, ui);
}

static void ServerConfig_save_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) return;
    lv_indev_wait_release(lv_indev_active());
    if (ui->ServerConfig_kb) {
        lv_obj_add_flag(ui->ServerConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui->ServerConfig_btn_save && lv_obj_is_valid(ui->ServerConfig_btn_save))
        lv_obj_add_state(ui->ServerConfig_btn_save, LV_STATE_DISABLED);
    ui_server_cfg_set_status(ui, "正在保存...", 0xFFA500);
    lv_obj_update_layout(ui->ServerConfig);
    lv_refr_now(NULL);
    ui_server_cfg_do_save_sync(ui);
}

static void ServerConfig_load_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) return;
    lv_indev_wait_release(lv_indev_active());
    if (ui->ServerConfig_kb) {
        lv_obj_add_flag(ui->ServerConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
    ui_server_cfg_set_status(ui, "正在加载...", 0xFFA500);
    lv_obj_update_layout(ui->ServerConfig);
    lv_refr_now(NULL);
    ui_server_cfg_do_load_sync(ui);
}

/* ============ 通讯参数配置（ParamConfig）SD 持久化（仅 UI 验证，不影响实际参数） ============ */

#define UI_PARAM_CFG_FILE "0:/config/ui_param.cfg"

static void ui_param_cfg_set_status(lv_ui *ui, const char *text, uint32_t color_hex)
{
    if (!ui || !ui->ParamConfig_lbl_status || !lv_obj_is_valid(ui->ParamConfig_lbl_status))
        return;
    lv_label_set_text(ui->ParamConfig_lbl_status, text ? text : "");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_status, lv_color_hex(color_hex), LV_PART_MAIN);
}

static void ui_param_cfg_set_tips(lv_ui *ui, const char *text, uint32_t color_hex)
{
    if (!ui || !ui->ParamConfig_lbl_tips || !lv_obj_is_valid(ui->ParamConfig_lbl_tips))
        return;
    lv_label_set_text(ui->ParamConfig_lbl_tips, text ? text : "");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_tips, lv_color_hex(color_hex), LV_PART_MAIN);
}

static bool ui_param_cfg_parse_u32(const char *s, uint32_t *out)
{
    if (!out) return false;
    if (!s) return false;
    /* 容错：允许值前面出现多余的 '=' 或空白（例如文件被写成 HARDRESET_S==60） */
    while (*s == '=' || *s == ' ' || *s == '\t')
        s++;
    if (!s[0]) return false;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s) return false;
    while (end && (*end == ' ' || *end == '\t')) end++;
    if (end && *end != '\0') return false;
    *out = (uint32_t)v;
    return true;
}

/* 输入校验（防止误设导致一直重连）
 * - strict=true：用于保存时，遇到错误直接阻止保存
 * - strict=false：用于输入过程实时提示（不阻止）
 */
static bool ui_param_cfg_validate_and_warn(lv_ui *ui, bool strict)
{
    if (!ui) return true;

    const char *hb_s    = (ui->ParamConfig_ta_heartbeat)   ? lv_textarea_get_text(ui->ParamConfig_ta_heartbeat)   : "";
    const char *send_s  = (ui->ParamConfig_ta_sendlimit)   ? lv_textarea_get_text(ui->ParamConfig_ta_sendlimit)   : "";
    const char *http_s  = (ui->ParamConfig_ta_httptimeout) ? lv_textarea_get_text(ui->ParamConfig_ta_httptimeout) : "";
    const char *rst_s   = (ui->ParamConfig_ta_hardreset)   ? lv_textarea_get_text(ui->ParamConfig_ta_hardreset)   : "";
    const char *ds_s    = (ui->ParamConfig_ta_downsample)  ? lv_textarea_get_text(ui->ParamConfig_ta_downsample)  : "";
    const char *up_s    = (ui->ParamConfig_ta_uploadpoints) ? lv_textarea_get_text(ui->ParamConfig_ta_uploadpoints) : "";
    const char *ckb_s   = (ui->ParamConfig_ta_chunkkb)     ? lv_textarea_get_text(ui->ParamConfig_ta_chunkkb)     : "";
    const char *cdly_s  = (ui->ParamConfig_ta_chunkdelay)  ? lv_textarea_get_text(ui->ParamConfig_ta_chunkdelay)  : "";

    uint32_t hb=0, send=0, http=0, rst=0, ds=1, up=4096, ckb=0, cdly=0;
    bool ok_hb   = ui_param_cfg_parse_u32(hb_s, &hb);
    bool ok_send = ui_param_cfg_parse_u32(send_s, &send);
    bool ok_http = ui_param_cfg_parse_u32(http_s, &http);
    bool ok_rst  = ui_param_cfg_parse_u32(rst_s, &rst);
    bool ok_ds   = ui_param_cfg_parse_u32(ds_s, &ds);
    bool ok_up   = ui_param_cfg_parse_u32(up_s, &up);
    bool ok_ckb  = ui_param_cfg_parse_u32(ckb_s, &ckb);
    bool ok_cdly = ui_param_cfg_parse_u32(cdly_s, &cdly);

    if (!ok_hb || !ok_send || !ok_http || !ok_rst || !ok_ds || !ok_up || !ok_ckb || !ok_cdly) {
        ui_param_cfg_set_tips(ui,
                              "提示：请输入纯数字。\n"
                              "建议值：心跳5000ms，限频200ms，回包1200ms，复位60s，降采样step=4，上传点数4096，分段4KB/10ms",
                              0xFFA500);
        return !strict;
    }

    /* 动态信息：step 对应发送点数（4096/step，与当前采样点数一致） */
    uint32_t pts = 0;
    if (ds >= 1u) {
        /* ceil(4096/ds) = (4096 + ds - 1) / ds */
        pts = (4096u + ds - 1u) / ds;
        if (pts == 0u) pts = 1u;
    }
    uint32_t actual_up = pts;
    if (up < actual_up) actual_up = up;
    char chunk_info[32];
    if (ckb == 0u) {
        (void)snprintf(chunk_info, sizeof(chunk_info), "关闭(单包)");
    } else {
        (void)snprintf(chunk_info, sizeof(chunk_info), "%luKB/%lums",
                       (unsigned long)ckb, (unsigned long)cdly);
    }

    /* 硬范围（错误） */
    if (hb < 200u || hb > 600000u) {
        ui_param_cfg_set_tips(ui, "错误：心跳间隔建议范围 200..600000 ms（例如 5000）", 0xFF4444);
        return !strict;
    }
    if (hb >= 55000u) {
        ui_param_cfg_set_tips(ui, "Error: heartbeat must stay below server NODE_TIMEOUT safety window (<55s).", 0xFF4444);
        return !strict;
    }
    if (send > 600000u) {
        ui_param_cfg_set_tips(ui, "错误：发包限频过大（建议 0..600000 ms，常用 200）", 0xFF4444);
        return !strict;
    }
    if (http < 1000u || http > 600000u) {
        ui_param_cfg_set_tips(ui, "Error: HTTP timeout valid range is 1000..600000 ms.", 0xFF4444);
        return !strict;
    }
    if (rst < 5u || rst > 3600u) {
        ui_param_cfg_set_tips(ui, "错误：复位阈值建议范围 5..3600 s（例如 60）", 0xFF4444);
        return !strict;
    }
    if (ds < 1u || ds > 64u) {
        ui_param_cfg_set_tips(ui, "错误：降采样 step 建议范围 1..64（例如 1=全量，4=推荐）", 0xFF4444);
        return !strict;
    }
    if (up < 256u || up > 4096u || (up % 256u) != 0u) {
        ui_param_cfg_set_tips(ui, "错误：上传点数需为 256..4096 且 256 步进（例如 1024/2048/4096）", 0xFF4444);
        return !strict;
    }
    if (ckb > 16u) {
        ui_param_cfg_set_tips(ui, "错误：分段大小建议范围 0..16 KB（0=不分段）", 0xFF4444);
        return !strict;
    }
    if (cdly > 200u) {
        ui_param_cfg_set_tips(ui, "错误：分段间隔建议范围 0..200 ms（例如 10）", 0xFF4444);
        return !strict;
    }

    /* 关键约束（错误）：回包超时必须小于复位阈值（否则容易频繁重连） */
    if (http >= (rst * 1000u)) {
        ui_param_cfg_set_tips(ui,
                              "错误：回包超时必须 < 复位阈值×1000。\n"
                              "否则门控还没放行就触发无响应重连，可能一直重连。",
                              0xFF4444);
        return !strict;
    }

    /* 软建议（警告） */
    if (rst < 30u) {
        ui_param_cfg_set_tips(ui,
                              "警告：复位阈值 < 30s 可能在网络抖动/服务器忙时频繁重连。\n"
                              "建议：复位60s、回包1200ms、限频200ms。",
                              0xFFA500);
        return true;
    }

    if (send && send < 50u) {
        char tips[220];
        (void)snprintf(tips, sizeof(tips),
                       "警告：限频过小会提高带宽/CPU/串口压力，可能导致掉帧或丢包。\n"
                       "降采样：step=%lu -> points=%lu (4096/step)\n"
                       "上传点数：%lu -> 实际上传=%lu\n"
                       "分段：%s\n"
                       "建议：限频≥50ms（推荐200ms）。",
                       (unsigned long)ds, (unsigned long)pts,
                       (unsigned long)up, (unsigned long)actual_up,
                       chunk_info);
        ui_param_cfg_set_tips(ui, tips, 0xFFA500);
        return true;
    }

    if (ds > 16u) {
        char tips[220];
        (void)snprintf(tips, sizeof(tips),
                       "警告：降采样 step 过大可能导致波形细节丢失。\n"
                       "当前：step=%lu -> points=%lu (4096/step)\n"
                       "上传点数：%lu -> 实际上传=%lu\n"
                       "分段：%s\n"
                       "建议：step=4 或 8（1=全量）。",
                       (unsigned long)ds, (unsigned long)pts,
                       (unsigned long)up, (unsigned long)actual_up,
                       chunk_info);
        ui_param_cfg_set_tips(ui, tips, 0xFFA500);
        return true;
    }

    /* OK */
    {
        char tips[220];
        (void)snprintf(tips, sizeof(tips),
                       "参数看起来合理。\n"
                       "降采样：step=%lu -> points=%lu (4096/step)\n"
                       "上传点数：%lu -> 实际上传=%lu\n"
                       "分段：%s\n"
                       "建议：step=4，分段4KB/10ms（可一键关闭分段）",
                       (unsigned long)ds, (unsigned long)pts,
                       (unsigned long)up, (unsigned long)actual_up,
                       chunk_info);
        ui_param_cfg_set_tips(ui, tips, 0x111111);
    }
    return true;
}

static void ui_param_cfg_apply_to_ui(lv_ui *ui, const char *heartbeat_ms, const char *sendlimit_ms,
                                     const char *http_timeout_ms, const char *hardreset_s,
                                     const char *downsample_step, const char *upload_points,
                                     const char *chunk_kb, const char *chunk_delay)
{
    if (!ui) return;
    if (ui->ParamConfig_ta_heartbeat && lv_obj_is_valid(ui->ParamConfig_ta_heartbeat))
        lv_textarea_set_text(ui->ParamConfig_ta_heartbeat, heartbeat_ms ? heartbeat_ms : "");
    if (ui->ParamConfig_ta_sendlimit && lv_obj_is_valid(ui->ParamConfig_ta_sendlimit))
        lv_textarea_set_text(ui->ParamConfig_ta_sendlimit, sendlimit_ms ? sendlimit_ms : "");
    if (ui->ParamConfig_ta_httptimeout && lv_obj_is_valid(ui->ParamConfig_ta_httptimeout))
        lv_textarea_set_text(ui->ParamConfig_ta_httptimeout, http_timeout_ms ? http_timeout_ms : "");
    if (ui->ParamConfig_ta_hardreset && lv_obj_is_valid(ui->ParamConfig_ta_hardreset))
        lv_textarea_set_text(ui->ParamConfig_ta_hardreset, hardreset_s ? hardreset_s : "");
    if (ui->ParamConfig_ta_downsample && lv_obj_is_valid(ui->ParamConfig_ta_downsample))
        lv_textarea_set_text(ui->ParamConfig_ta_downsample, downsample_step ? downsample_step : "");
    if (ui->ParamConfig_ta_uploadpoints && lv_obj_is_valid(ui->ParamConfig_ta_uploadpoints))
        lv_textarea_set_text(ui->ParamConfig_ta_uploadpoints, upload_points ? upload_points : "");
    if (ui->ParamConfig_ta_chunkkb && lv_obj_is_valid(ui->ParamConfig_ta_chunkkb))
        lv_textarea_set_text(ui->ParamConfig_ta_chunkkb, chunk_kb ? chunk_kb : "");
    if (ui->ParamConfig_ta_chunkdelay && lv_obj_is_valid(ui->ParamConfig_ta_chunkdelay))
        lv_textarea_set_text(ui->ParamConfig_ta_chunkdelay, chunk_delay ? chunk_delay : "");
}

static FRESULT ui_param_cfg_read_file(char *heartbeat_ms, size_t heartbeat_len,
                                      char *sendlimit_ms, size_t sendlimit_len,
                                      char *http_timeout_ms, size_t http_timeout_len,
                                      char *hardreset_s, size_t hardreset_len,
                                      char *downsample_step, size_t downsample_len,
                                      char *upload_points, size_t upload_points_len,
                                      char *chunk_kb, size_t chunk_kb_len,
                                      char *chunk_delay, size_t chunk_delay_len)
{
    if (!heartbeat_ms || !sendlimit_ms || !http_timeout_ms || !hardreset_s || !downsample_step || !upload_points || !chunk_kb || !chunk_delay)
        return FR_INVALID_OBJECT;
    heartbeat_ms[0] = sendlimit_ms[0] = http_timeout_ms[0] = hardreset_s[0] = downsample_step[0] = '\0';
    upload_points[0] = '\0';
    chunk_kb[0] = chunk_delay[0] = '\0';

    FRESULT res = ui_sd_mount_with_mkfs();
    if (res != FR_OK) {
        printf("[PARAM_UI_CFG] load skipped: SD not ready (res=%d)\r\n", (int)res);
        return res;
    }

    FIL fil;
    for (int attempt = 1; attempt <= 2; ++attempt) {
        res = f_open(&fil, UI_PARAM_CFG_FILE, FA_READ);
        if (res == FR_OK) {
            break;
        }
        printf("[PARAM_UI_CFG] open for read fail: %s res=%d attempt=%d\r\n",
               UI_PARAM_CFG_FILE, (int)res, attempt);
        if (res == FR_DISK_ERR && attempt == 1) {
            g_ui_sd_mounted = 0;
            (void)ui_sd_mount_with_mkfs();
            continue;
        }
        return res;
    }

    char line[160];
    while (f_gets(line, sizeof(line), &fil)) {
        ui_wifi_cfg_rstrip(line);
        if (strncmp(line, "HEARTBEAT_MS=", 13) == 0) {
            strncpy(heartbeat_ms, line + 13, heartbeat_len - 1);
            heartbeat_ms[heartbeat_len - 1] = '\0';
        } else if (strncmp(line, "SENDLIMIT_MS=", 13) == 0) {
            strncpy(sendlimit_ms, line + 13, sendlimit_len - 1);
            sendlimit_ms[sendlimit_len - 1] = '\0';
        } else if (strncmp(line, "HTTP_TIMEOUT_MS=", 16) == 0) {
            strncpy(http_timeout_ms, line + 16, http_timeout_len - 1);
            http_timeout_ms[http_timeout_len - 1] = '\0';
        } else if (strncmp(line, "HARDRESET_S=", 11) == 0) {
            strncpy(hardreset_s, line + 11, hardreset_len - 1);
            hardreset_s[hardreset_len - 1] = '\0';
        } else if (strncmp(line, "DOWNSAMPLE_STEP=", 16) == 0) {
            strncpy(downsample_step, line + 16, downsample_len - 1);
            downsample_step[downsample_len - 1] = '\0';
        } else if (strncmp(line, "UPLOAD_POINTS=", 14) == 0) {
            strncpy(upload_points, line + 14, upload_points_len - 1);
            upload_points[upload_points_len - 1] = '\0';
        } else if (strncmp(line, "CHUNK_KB=", 9) == 0) {
            strncpy(chunk_kb, line + 9, chunk_kb_len - 1);
            chunk_kb[chunk_kb_len - 1] = '\0';
        } else if (strncmp(line, "CHUNK_DELAY_MS=", 15) == 0) {
            strncpy(chunk_delay, line + 15, chunk_delay_len - 1);
            chunk_delay[chunk_delay_len - 1] = '\0';
        }
    }
    (void)f_close(&fil);

    printf("[PARAM_UI_CFG] loaded: hb=%s send=%s http=%s reset=%s ds=%s up=%s chunk=%s delay=%s\r\n",
           heartbeat_ms, sendlimit_ms, http_timeout_ms, hardreset_s, downsample_step, upload_points, chunk_kb, chunk_delay);
    return FR_OK;
}

static FRESULT ui_param_cfg_write_file(const char *heartbeat_ms, const char *sendlimit_ms,
                                       const char *http_timeout_ms, const char *hardreset_s,
                                       const char *downsample_step, const char *upload_points,
                                       const char *chunk_kb, const char *chunk_delay)
{
    printf("[PARAM_UI_CFG] write_file: start\r\n");
    FRESULT res = ui_sd_mount_with_mkfs();
    if (res != FR_OK) {
        printf("[PARAM_UI_CFG] save failed: SD not ready (res=%d)\r\n", (int)res);
        return res;
    }
    printf("[PARAM_UI_CFG] write_file: SD mounted\r\n");

    res = ui_wifi_cfg_ensure_dir();
    printf("[PARAM_UI_CFG] write_file: ensure_dir res=%d\r\n", (int)res);
    if (res != FR_OK) {
        printf("[PARAM_UI_CFG] ensure dir fail: res=%d\r\n", (int)res);
        return res;
    }

    /* 原子写入：先写临时文件，再 rename 覆盖 */
    const char *tmp_path = "0:/config/.ui_param.cfg.tmp";

    printf("[PARAM_UI_CFG] write_file: opening tmp...\r\n");
    FIL fil;
    res = f_open(&fil, tmp_path, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        printf("[PARAM_UI_CFG] open for write fail: %s res=%d\r\n", tmp_path, (int)res);
        if (res == FR_DISK_ERR) {
            g_ui_sd_mounted = 0;
            (void)f_mount(NULL, (TCHAR const *)SDPath, 0);
            osDelay(10);
            (void)ui_sd_mount_with_mkfs();
            res = f_open(&fil, tmp_path, FA_WRITE | FA_CREATE_ALWAYS);
            printf("[PARAM_UI_CFG] retry open tmp -> %d\r\n", (int)res);
        }
        if (res != FR_OK) {
            g_ui_sd_last_err = res;
            g_ui_sd_last_err_tick = osKernelGetTickCount();
            return res;
        }
    }

    printf("[PARAM_UI_CFG] write_file: writing...\r\n");
    char buf[320];
    /* 只写入纯数字，避免出现 HARDRESET_S==60 这种“脏文件” */
    uint32_t hb_u = 0, send_u = 0, http_u = 0, rst_u = 0, ds_u = 1, up_u = 4096, ckb_u = 4, cdly_u = 10;
    (void)ui_param_cfg_parse_u32(heartbeat_ms, &hb_u);
    (void)ui_param_cfg_parse_u32(sendlimit_ms, &send_u);
    (void)ui_param_cfg_parse_u32(http_timeout_ms, &http_u);
    (void)ui_param_cfg_parse_u32(hardreset_s, &rst_u);
    (void)ui_param_cfg_parse_u32(downsample_step, &ds_u);
    (void)ui_param_cfg_parse_u32(upload_points, &up_u);
    (void)ui_param_cfg_parse_u32(chunk_kb, &ckb_u);
    (void)ui_param_cfg_parse_u32(chunk_delay, &cdly_u);
    if (ds_u < 1u) ds_u = 1u;
    /* upload_points：256..4096 且 256 步进（validate 已保证，这里做兜底夹逼） */
    if (up_u < 256u) up_u = 256u;
    if (up_u > 4096u) up_u = 4096u;
    if ((up_u % 256u) != 0u) {
        up_u = (up_u / 256u) * 256u;
        if (up_u < 256u) up_u = 256u;
    }
    /* 允许 ckb=0 表示关闭分段 */
    int n = snprintf(buf, sizeof(buf),
                     "HEARTBEAT_MS=%lu\nSENDLIMIT_MS=%lu\nHTTP_TIMEOUT_MS=%lu\nHARDRESET_S=%lu\nDOWNSAMPLE_STEP=%lu\nUPLOAD_POINTS=%lu\nCHUNK_KB=%lu\nCHUNK_DELAY_MS=%lu\n",
                     (unsigned long)hb_u,
                     (unsigned long)send_u,
                     (unsigned long)http_u,
                     (unsigned long)rst_u,
                     (unsigned long)ds_u,
                     (unsigned long)up_u,
                     (unsigned long)ckb_u,
                     (unsigned long)cdly_u);
    UINT bw = 0;
    res = f_write(&fil, buf, (UINT)n, &bw);
    printf("[PARAM_UI_CFG] write_file: f_write res=%d bw=%u\r\n", (int)res, (unsigned)bw);

    if (res == FR_OK) {
        osDelay(5);
        FRESULT sync_res = f_sync(&fil);
        printf("[PARAM_UI_CFG] write_file: f_sync res=%d\r\n", (int)sync_res);
        if (sync_res != FR_OK) {
            res = sync_res;
        }
    }

    FRESULT close_res = f_close(&fil);
    printf("[PARAM_UI_CFG] write_file: f_close res=%d\r\n", (int)close_res);
    if (close_res != FR_OK && res == FR_OK) {
        res = close_res;
    }

    if (res == FR_OK) {
        uint32_t t0 = osKernelGetTickCount();
        while ((osKernelGetTickCount() - t0) < 100U) {
            if (BSP_SD_GetCardState() == SD_TRANSFER_OK)
                break;
            osDelay(5);
        }
        printf("[PARAM_UI_CFG] post-close wait: card_state=%u\r\n", (unsigned)BSP_SD_GetCardState());
    }

    if (res != FR_OK) {
        printf("[PARAM_UI_CFG] write_file failed, unlink tmp\r\n");
        (void)f_unlink(tmp_path);
        g_ui_sd_mounted = 0;
        g_ui_sd_last_err = res;
        g_ui_sd_last_err_tick = osKernelGetTickCount();
        return res;
    }

    (void)f_unlink(UI_PARAM_CFG_FILE);
    FRESULT rn = f_rename(tmp_path, UI_PARAM_CFG_FILE);
    printf("[PARAM_UI_CFG] rename tmp -> cfg res=%d\r\n", (int)rn);
    if (rn != FR_OK) {
        (void)f_unlink(tmp_path);
        g_ui_sd_mounted = 0;
        g_ui_sd_last_err = rn;
        g_ui_sd_last_err_tick = osKernelGetTickCount();
        return rn;
    }

    printf("[PARAM_UI_CFG] saved: final_res=%d bytes=%u\r\n", (int)res, (unsigned)bw);
    return FR_OK;
}

static void ui_param_cfg_do_load_sync(lv_ui *ui)
{
    if (!ui) return;
    char hb[24] = {0}, send[24] = {0}, http[24] = {0}, reset[24] = {0}, ds[24] = {0}, up[24] = {0}, ckb[24] = {0}, cdly[24] = {0};
    FRESULT res = ui_param_cfg_read_file(hb, sizeof(hb), send, sizeof(send), http, sizeof(http), reset, sizeof(reset),
                                         ds, sizeof(ds), up, sizeof(up), ckb, sizeof(ckb), cdly, sizeof(cdly));
    if (res == FR_OK) {
        /* 读取时就“净化”一次：把 =60 / ==60 之类的值纠正为纯数字回写到输入框 */
        uint32_t hb_u = 0, send_u = 0, http_u = 0, rst_u = 0, ds_u = 1, up_u = 4096, ckb_u = 4, cdly_u = 10;
        bool ok_hb = ui_param_cfg_parse_u32(hb, &hb_u);
        bool ok_send = ui_param_cfg_parse_u32(send, &send_u);
        bool ok_http = ui_param_cfg_parse_u32(http, &http_u);
        bool ok_rst = ui_param_cfg_parse_u32(reset, &rst_u);
        bool ok_ds = ui_param_cfg_parse_u32(ds, &ds_u);
        bool ok_up = ui_param_cfg_parse_u32(up, &up_u);
        bool ok_ckb = ui_param_cfg_parse_u32(ckb, &ckb_u);
        bool ok_cdly = ui_param_cfg_parse_u32(cdly, &cdly_u);
        if (ok_hb && ui->ParamConfig_ta_heartbeat && lv_obj_is_valid(ui->ParamConfig_ta_heartbeat)) {
            char tmp[16]; (void)snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)hb_u);
            lv_textarea_set_text(ui->ParamConfig_ta_heartbeat, tmp);
        }
        if (ok_send && ui->ParamConfig_ta_sendlimit && lv_obj_is_valid(ui->ParamConfig_ta_sendlimit)) {
            char tmp[16]; (void)snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)send_u);
            lv_textarea_set_text(ui->ParamConfig_ta_sendlimit, tmp);
        }
        if (ok_http && ui->ParamConfig_ta_httptimeout && lv_obj_is_valid(ui->ParamConfig_ta_httptimeout)) {
            char tmp[16]; (void)snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)http_u);
            lv_textarea_set_text(ui->ParamConfig_ta_httptimeout, tmp);
        }
        if (ok_rst && ui->ParamConfig_ta_hardreset && lv_obj_is_valid(ui->ParamConfig_ta_hardreset)) {
            char tmp[16]; (void)snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)rst_u);
            lv_textarea_set_text(ui->ParamConfig_ta_hardreset, tmp);
        }
        if (ok_ds && ui->ParamConfig_ta_downsample && lv_obj_is_valid(ui->ParamConfig_ta_downsample)) {
            char tmp[16]; (void)snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)ds_u);
            lv_textarea_set_text(ui->ParamConfig_ta_downsample, tmp);
        }
        if (ok_up && ui->ParamConfig_ta_uploadpoints && lv_obj_is_valid(ui->ParamConfig_ta_uploadpoints)) {
            char tmp[16]; (void)snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)up_u);
            lv_textarea_set_text(ui->ParamConfig_ta_uploadpoints, tmp);
        }
        if (ok_ckb && ui->ParamConfig_ta_chunkkb && lv_obj_is_valid(ui->ParamConfig_ta_chunkkb)) {
            char tmp[16]; (void)snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)ckb_u);
            lv_textarea_set_text(ui->ParamConfig_ta_chunkkb, tmp);
        }
        if (ok_cdly && ui->ParamConfig_ta_chunkdelay && lv_obj_is_valid(ui->ParamConfig_ta_chunkdelay)) {
            char tmp[16]; (void)snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)cdly_u);
            lv_textarea_set_text(ui->ParamConfig_ta_chunkdelay, tmp);
        }

        /* 影响实际参数：将 SD 读到的值写入 ESP 通讯参数缓存（不改 WiFi/Server/SystemConfig_t） */
        ESP_CommParams_t p;
        ESP_CommParams_Get(&p); /* 先取当前缓存作为兜底（避免文件缺字段时把值变成 0） */
        if (ok_hb)   p.heartbeat_ms    = hb_u;
        if (ok_send) p.min_interval_ms = send_u;
        if (ok_http) p.http_timeout_ms = http_u;
        if (ok_rst)  p.hardreset_sec   = rst_u;
        if (ok_ds)   p.wave_step       = ds_u;
        if (ok_up)   p.upload_points   = up_u;
        if (ok_ckb)  p.chunk_kb        = ckb_u;
        if (ok_cdly) p.chunk_delay_ms  = cdly_u;
        ESP_CommParams_Apply(&p);
    }
    ui_sd_result_to_status(ui, res, ui_param_cfg_set_status, "加载完成");
    (void)ui_param_cfg_validate_and_warn(ui, false);
}

static void ui_param_cfg_do_save_sync(lv_ui *ui)
{
    if (!ui) return;
    if (!ui_param_cfg_validate_and_warn(ui, true)) {
        ui_param_cfg_set_status(ui, "参数错误：已阻止保存（请按提示修改）", 0xFF4444);
        if (ui->ParamConfig_btn_save && lv_obj_is_valid(ui->ParamConfig_btn_save))
            lv_obj_clear_state(ui->ParamConfig_btn_save, LV_STATE_DISABLED);
        return;
    }
    const char *hb    = (ui->ParamConfig_ta_heartbeat)  ? lv_textarea_get_text(ui->ParamConfig_ta_heartbeat)  : "";
    const char *send  = (ui->ParamConfig_ta_sendlimit) ? lv_textarea_get_text(ui->ParamConfig_ta_sendlimit) : "";
    const char *http  = (ui->ParamConfig_ta_httptimeout) ? lv_textarea_get_text(ui->ParamConfig_ta_httptimeout) : "";
    const char *reset = (ui->ParamConfig_ta_hardreset) ? lv_textarea_get_text(ui->ParamConfig_ta_hardreset) : "";
    const char *ds    = (ui->ParamConfig_ta_downsample) ? lv_textarea_get_text(ui->ParamConfig_ta_downsample) : "";
    const char *up    = (ui->ParamConfig_ta_uploadpoints) ? lv_textarea_get_text(ui->ParamConfig_ta_uploadpoints) : "";
    const char *ckb   = (ui->ParamConfig_ta_chunkkb) ? lv_textarea_get_text(ui->ParamConfig_ta_chunkkb) : "";
    const char *cdly  = (ui->ParamConfig_ta_chunkdelay) ? lv_textarea_get_text(ui->ParamConfig_ta_chunkdelay) : "";

    /* 注意：这里仅做 UI->SD 的保存/回读验证，不写入任何实际运行参数。 */
    FRESULT res = ui_param_cfg_write_file(hb, send, http, reset, ds, up, ckb, cdly);

    if (res == FR_OK) {
        /* 保存后立即回读一次，验证保存成功并回显 */
        ui_param_cfg_do_load_sync(ui);
        /* ui_param_cfg_do_load_sync 已经会设置状态，这里覆盖为“保存成功”更直观 */
        bool runtime_ok = ESP_Config_LoadRuntimeFromSD();
        if (ESP_UI_IsReporting()) {
            ui_param_cfg_set_status(ui, "Saved; reporting active, reconnect to apply", 0xFFA500);
        } else if (!runtime_ok) {
            ui_param_cfg_set_status(ui, "Saved; SD config incomplete, not synced", 0xFFA500);
        } else {
            (void)ESP_UI_SendCmd(ESP_UI_CMD_APPLY_CONFIG);
            ui_param_cfg_set_status(ui, "Saved; ESP32 sync queued", 0x3dfb00);
        }
    } else {
        ui_sd_result_to_status(ui, res, ui_param_cfg_set_status, "保存成功");
    }

    if (ui->ParamConfig_btn_save && lv_obj_is_valid(ui->ParamConfig_btn_save))
        lv_obj_clear_state(ui->ParamConfig_btn_save, LV_STATE_DISABLED);
}

static void ParamConfig_load_timer_cb(lv_timer_t *t)
{
    lv_ui *ui = (lv_ui *)lv_timer_get_user_data(t);
    lv_timer_del(t);
    if (!ui) return;
    ui_param_cfg_set_status(ui, "正在加载...", 0xFFA500);
    lv_obj_update_layout(ui->ParamConfig);
    lv_refr_now(NULL);
    ui_param_cfg_do_load_sync(ui);
}

static void ParamConfig_screen_event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SCREEN_LOADED) {
        return;
    }
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) return;
    ui_param_cfg_set_status(ui, "正在加载...", 0xFFA500);
    lv_timer_create(ParamConfig_load_timer_cb, 100, ui);
}

static void ParamConfig_ta_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        if (ui->ParamConfig_kb != NULL) {
            lv_textarea_clear_selection(ta);
            lv_keyboard_set_textarea(ui->ParamConfig_kb, ta);
            lv_obj_remove_flag(ui->ParamConfig_kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
    else if (code == LV_EVENT_VALUE_CHANGED) {
        (void)ui_param_cfg_validate_and_warn(ui, false);
    }
}

static void ParamConfig_kb_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(ui->ParamConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ParamConfig_apply_preset(lv_ui *ui,
                                     const char *hb, const char *send, const char *http,
                                     const char *reset, const char *chunkkb, const char *chunkdelay,
                                     const char *downsample, const char *upload_points,
                                     const char *status_text)
{
    if (!ui) return;
    lv_indev_wait_release(lv_indev_active());
    if (ui->ParamConfig_kb) {
        lv_obj_add_flag(ui->ParamConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }

    if (ui->ParamConfig_ta_heartbeat && lv_obj_is_valid(ui->ParamConfig_ta_heartbeat))
        lv_textarea_set_text(ui->ParamConfig_ta_heartbeat, hb ? hb : "");
    if (ui->ParamConfig_ta_sendlimit && lv_obj_is_valid(ui->ParamConfig_ta_sendlimit))
        lv_textarea_set_text(ui->ParamConfig_ta_sendlimit, send ? send : "");
    if (ui->ParamConfig_ta_httptimeout && lv_obj_is_valid(ui->ParamConfig_ta_httptimeout))
        lv_textarea_set_text(ui->ParamConfig_ta_httptimeout, http ? http : "");
    if (ui->ParamConfig_ta_hardreset && lv_obj_is_valid(ui->ParamConfig_ta_hardreset))
        lv_textarea_set_text(ui->ParamConfig_ta_hardreset, reset ? reset : "");
    if (ui->ParamConfig_ta_chunkkb && lv_obj_is_valid(ui->ParamConfig_ta_chunkkb))
        lv_textarea_set_text(ui->ParamConfig_ta_chunkkb, chunkkb ? chunkkb : "");
    if (ui->ParamConfig_ta_chunkdelay && lv_obj_is_valid(ui->ParamConfig_ta_chunkdelay))
        lv_textarea_set_text(ui->ParamConfig_ta_chunkdelay, chunkdelay ? chunkdelay : "");
    if (ui->ParamConfig_ta_downsample && lv_obj_is_valid(ui->ParamConfig_ta_downsample))
        lv_textarea_set_text(ui->ParamConfig_ta_downsample, downsample ? downsample : "");
    if (ui->ParamConfig_ta_uploadpoints && lv_obj_is_valid(ui->ParamConfig_ta_uploadpoints))
        lv_textarea_set_text(ui->ParamConfig_ta_uploadpoints, upload_points ? upload_points : "");

    ui_param_cfg_set_status(ui, status_text ? status_text : "正在保存...", 0xFFA500);
    lv_obj_update_layout(ui->ParamConfig);
    lv_refr_now(NULL);
    ui_param_cfg_do_save_sync(ui);
}

static void ParamConfig_preset_lan_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    ParamConfig_apply_preset(ui,
                             "5000", "200", "1200", "60",
                             "0", "0", "1",
                             "4096",
                             "已应用局域网参数，正在保存...");
}

static void ParamConfig_preset_wan_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    ParamConfig_apply_preset(ui,
                             "30000", "1000", "8000", "60",
                             "1", "160", "1",
                             "4096",
                             "已应用公网参数，正在保存...");
}

static void ParamConfig_nochunk_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) return;
    lv_indev_wait_release(lv_indev_active());
    if (ui->ParamConfig_kb) {
        lv_obj_add_flag(ui->ParamConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }

    if (ui->ParamConfig_ta_chunkkb && lv_obj_is_valid(ui->ParamConfig_ta_chunkkb)) {
        lv_textarea_set_text(ui->ParamConfig_ta_chunkkb, "0");
    }
    if (ui->ParamConfig_ta_chunkdelay && lv_obj_is_valid(ui->ParamConfig_ta_chunkdelay)) {
        lv_textarea_set_text(ui->ParamConfig_ta_chunkdelay, "0");
    }

    ui_param_cfg_set_status(ui, "已设置无分段，正在保存...", 0xFFA500);
    lv_obj_update_layout(ui->ParamConfig);
    lv_refr_now(NULL);
    ui_param_cfg_do_save_sync(ui);
}

static void ParamConfig_back_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    if (ui->ParamConfig_kb) {
        lv_obj_add_flag(ui->ParamConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
    /*
     * 统一返回规则（强约定，避免界面层级混乱）：
     * - 所有“图标进入的功能页/配置页”的【返回】一律回 Aurora 主界面（setup_scr_Aurora / Main_1）。
     * - Main_2（配置中心）只是 Aurora 的“配置页/二级入口”，不作为全局返回目标。
     */
    ui_load_scr_animation(&guider_ui, &guider_ui.Main_1, guider_ui.Main_1_del, &guider_ui.ParamConfig_del,
                          setup_scr_Aurora, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
}

static void ParamConfig_load_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) return;
    lv_indev_wait_release(lv_indev_active());
    if (ui->ParamConfig_kb) {
        lv_obj_add_flag(ui->ParamConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
    ui_param_cfg_set_status(ui, "正在加载...", 0xFFA500);
    lv_obj_update_layout(ui->ParamConfig);
    lv_refr_now(NULL);
    ui_param_cfg_do_load_sync(ui);
}

static void ParamConfig_save_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) return;
    lv_indev_wait_release(lv_indev_active());
    if (ui->ParamConfig_kb) {
        lv_obj_add_flag(ui->ParamConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui->ParamConfig_btn_save && lv_obj_is_valid(ui->ParamConfig_btn_save))
        lv_obj_add_state(ui->ParamConfig_btn_save, LV_STATE_DISABLED);
    ui_param_cfg_set_status(ui, "正在保存...", 0xFFA500);
    lv_obj_update_layout(ui->ParamConfig);
    lv_refr_now(NULL);
    ui_param_cfg_do_save_sync(ui);
}

void events_init_Main_3 (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->Main_3, Main_3_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_3_dot_1, Main_3_dot_1_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->Main_3_dot_2, Main_3_dot_2_event_handler, LV_EVENT_CLICKED, ui);
}

void events_init_WifiConfig(lv_ui *ui)
{
    lv_obj_add_event_cb(ui->WifiConfig_ta_ssid, WifiConfig_ta_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->WifiConfig_ta_pwd, WifiConfig_ta_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->WifiConfig_kb, WifiConfig_kb_event_handler, LV_EVENT_READY, ui);
    lv_obj_add_event_cb(ui->WifiConfig_kb, WifiConfig_kb_event_handler, LV_EVENT_CANCEL, ui);
    lv_obj_add_event_cb(ui->WifiConfig_btn_back, WifiConfig_back_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->WifiConfig_btn_save, WifiConfig_save_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->WifiConfig_btn_scan, WifiConfig_scan_event_handler, LV_EVENT_CLICKED, ui);

    /* 每次切换进入屏幕都自动加载 */
    lv_obj_add_event_cb(ui->WifiConfig, WifiConfig_screen_event_handler, LV_EVENT_SCREEN_LOADED, ui);
}

void events_init_ServerConfig(lv_ui *ui)
{
    lv_obj_add_event_cb(ui->ServerConfig_ta_ip, ServerConfig_ta_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->ServerConfig_ta_port, ServerConfig_ta_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->ServerConfig_ta_id, ServerConfig_ta_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->ServerConfig_ta_loc, ServerConfig_ta_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->ServerConfig_kb, ServerConfig_kb_event_handler, LV_EVENT_READY, ui);
    lv_obj_add_event_cb(ui->ServerConfig_kb, ServerConfig_kb_event_handler, LV_EVENT_CANCEL, ui);
    lv_obj_add_event_cb(ui->ServerConfig_btn_back, ServerConfig_back_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->ServerConfig_btn_save, ServerConfig_save_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->ServerConfig_btn_load, ServerConfig_load_event_handler, LV_EVENT_CLICKED, ui);

    /* 每次切换进入屏幕都自动加载 */
    lv_obj_add_event_cb(ui->ServerConfig, ServerConfig_screen_event_handler, LV_EVENT_SCREEN_LOADED, ui);
}

void events_init_ParamConfig(lv_ui *ui)
{
    lv_obj_add_event_cb(ui->ParamConfig_ta_heartbeat, ParamConfig_ta_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->ParamConfig_ta_sendlimit, ParamConfig_ta_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->ParamConfig_ta_httptimeout, ParamConfig_ta_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->ParamConfig_ta_hardreset, ParamConfig_ta_event_handler, LV_EVENT_ALL, ui);
    if (ui->ParamConfig_ta_downsample) {
        lv_obj_add_event_cb(ui->ParamConfig_ta_downsample, ParamConfig_ta_event_handler, LV_EVENT_ALL, ui);
    }
    if (ui->ParamConfig_ta_uploadpoints) {
        lv_obj_add_event_cb(ui->ParamConfig_ta_uploadpoints, ParamConfig_ta_event_handler, LV_EVENT_ALL, ui);
    }
    if (ui->ParamConfig_ta_chunkkb) {
        lv_obj_add_event_cb(ui->ParamConfig_ta_chunkkb, ParamConfig_ta_event_handler, LV_EVENT_ALL, ui);
    }
    if (ui->ParamConfig_ta_chunkdelay) {
        lv_obj_add_event_cb(ui->ParamConfig_ta_chunkdelay, ParamConfig_ta_event_handler, LV_EVENT_ALL, ui);
    }
    lv_obj_add_event_cb(ui->ParamConfig_kb, ParamConfig_kb_event_handler, LV_EVENT_READY, ui);
    lv_obj_add_event_cb(ui->ParamConfig_kb, ParamConfig_kb_event_handler, LV_EVENT_CANCEL, ui);
    lv_obj_add_event_cb(ui->ParamConfig_btn_back, ParamConfig_back_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->ParamConfig_btn_load, ParamConfig_load_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->ParamConfig_btn_save, ParamConfig_save_event_handler, LV_EVENT_CLICKED, ui);
    if (ui->ParamConfig_btn_nochunk) {
        lv_obj_add_event_cb(ui->ParamConfig_btn_nochunk, ParamConfig_nochunk_event_handler, LV_EVENT_CLICKED, ui);
    }
    if (ui->ParamConfig_btn_lan) {
        lv_obj_add_event_cb(ui->ParamConfig_btn_lan, ParamConfig_preset_lan_event_handler, LV_EVENT_CLICKED, ui);
    }
    if (ui->ParamConfig_btn_wan) {
        lv_obj_add_event_cb(ui->ParamConfig_btn_wan, ParamConfig_preset_wan_event_handler, LV_EVENT_CLICKED, ui);
    }

    /* 每次切换进入屏幕都自动加载（仅 UI 读写验证，不影响实际参数） */
    lv_obj_add_event_cb(ui->ParamConfig, ParamConfig_screen_event_handler, LV_EVENT_SCREEN_LOADED, ui);

    /* 初始提示一次 */
    (void)ui_param_cfg_validate_and_warn(ui, false);
}

static void DeviceConnect_back_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    ui_load_scr_animation(&guider_ui, &guider_ui.Main_1, guider_ui.Main_1_del, &guider_ui.DeviceConnect_del,
                          setup_scr_Aurora, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
}

/* ================= DeviceConnect: EdgeComm UI 交互（对齐 EdgeComm操作.HTML） ================= */

typedef enum
{
    DC_MSG_LOG = 0,
    DC_MSG_STEP_DONE,
} dc_msg_type_t;

typedef struct
{
    dc_msg_type_t type;
    uint8_t step; /* esp_ui_cmd_t */
    uint8_t ok;
    char text[128];
} dc_msg_t;

static osMessageQueueId_t g_dc_q = NULL;
static lv_timer_t *g_dc_timer = NULL;
static lv_ui *g_dc_ui = NULL;
static uint8_t g_dc_auto_running = 0;
static uint32_t g_dc_report_stop_tick = 0;
static uint8_t g_dc_reg_dimmed = 0;
static lv_obj_t *g_dc_lbl_reg_countdown = NULL;
static uint32_t g_dc_console_len = 0;
/* DeviceConnect console is intentionally tiny and curated.
 * Full upload / DSP / SPI debug logs can arrive for hours. Feeding those
 * lines into an LVGL textarea repeatedly causes large text relayouts and can
 * make the touch UI look frozen. Keep only short human-action/status lines
 * here; the complete diagnostic stream remains on the UART console. */
#ifndef EW_DEVICECONNECT_CONSOLE_MAX_CHARS
#define EW_DEVICECONNECT_CONSOLE_MAX_CHARS 1024u
#endif
#ifndef EW_DEVICECONNECT_CONSOLE_MAX_LINES
#define EW_DEVICECONNECT_CONSOLE_MAX_LINES 16u
#endif
static char g_dc_console_text[EW_DEVICECONNECT_CONSOLE_MAX_CHARS];
static uint8_t g_dc_console_lines = 0;

/* DeviceConnect 进入时是否已从 SD 加载并应用到 ESP 配置缓冲区 */
static uint8_t g_dc_cfg_loaded = 0;
/* 进入 DeviceConnect 时的配置加载 timer（去重，避免重复创建导致 UI 抖动/刷屏） */
static lv_timer_t *g_dc_cfg_timer = NULL;
/* 解决“Processing 一闪就回 Idle”：步骤执行中不允许 dc_sync_reporting_ui 覆盖行状态 */
static uint8_t g_dc_step_busy_wifi = 0;
static uint8_t g_dc_step_busy_tcp  = 0;
static uint8_t g_dc_step_busy_reg  = 0;
static uint8_t g_dc_step_busy_rep  = 0;

typedef struct
{
    char ssid[64];
    char pwd[64];
    char ip[32];
    char port_s[16];
    char node_id[64];
    char node_loc[64];
} dc_cfg_buf_t;

/* 避免在 LVGL 线程上使用大量栈空间，改为静态缓冲区 */
static dc_cfg_buf_t g_dc_cfg_buf;

/* 停止上报后超过该时间，设备注册变暗并提示重新点击（单位 ms） */
#ifndef EW_DEVICECONNECT_REG_DIM_MS
#define EW_DEVICECONNECT_REG_DIM_MS 60000u
#endif

static void dc_set_buttons_enabled(lv_ui *ui, bool enabled)
{
    if (!ui)
        return;
    /* “开始上报”时，禁止反复重连操作；否则保持随时可点 */
    lv_obj_t *btns[] = {
        ui->DeviceConnect_btn_wifi,
        ui->DeviceConnect_btn_tcp,
        ui->DeviceConnect_btn_reg,
        ui->DeviceConnect_btn_auto,
    };
    for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); i++)
    {
        if (btns[i] && lv_obj_is_valid(btns[i]))
        {
            if (enabled)
                lv_obj_clear_state(btns[i], LV_STATE_DISABLED);
            else
                lv_obj_add_state(btns[i], LV_STATE_DISABLED);
        }
    }
}

static void dc_set_reg_dim(lv_ui *ui, bool dim)
{
    if (!ui)
        return;
    if (ui->DeviceConnect_btn_reg && lv_obj_is_valid(ui->DeviceConnect_btn_reg))
    {
        if (dim)
        {
            lv_obj_set_style_bg_color(ui->DeviceConnect_btn_reg, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(ui->DeviceConnect_btn_reg, 160, LV_PART_MAIN);
        }
        else
        {
            lv_obj_set_style_bg_color(ui->DeviceConnect_btn_reg, lv_color_hex(0xFFA500), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(ui->DeviceConnect_btn_reg, 255, LV_PART_MAIN);
        }
    }
}

static void dc_reg_countdown_set_visible(bool visible)
{
    if (g_dc_lbl_reg_countdown && lv_obj_is_valid(g_dc_lbl_reg_countdown))
    {
        if (visible)
            lv_obj_clear_flag(g_dc_lbl_reg_countdown, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(g_dc_lbl_reg_countdown, LV_OBJ_FLAG_HIDDEN);
    }
}

static void dc_reg_countdown_update(lv_ui *ui)
{
    if (!ui)
        return;
    if (!g_dc_lbl_reg_countdown || !lv_obj_is_valid(g_dc_lbl_reg_countdown))
        return;

    /* 仅在“停止上报后已开始计时”且未过期时显示倒计时 */
    if (ESP_UI_IsReporting() || g_dc_report_stop_tick == 0 || g_dc_reg_dimmed)
    {
        dc_reg_countdown_set_visible(false);
        return;
    }

    uint32_t now = lv_tick_get();
    uint32_t elapsed = now - g_dc_report_stop_tick;
    if (elapsed >= EW_DEVICECONNECT_REG_DIM_MS)
    {
        dc_reg_countdown_set_visible(false);
        return;
    }

    uint32_t remain_ms = EW_DEVICECONNECT_REG_DIM_MS - elapsed;
    uint32_t remain_s = (remain_ms + 999u) / 1000u;
    char buf[16];
    (void)snprintf(buf, sizeof(buf), "%lus", (unsigned long)remain_s);
    lv_label_set_text(g_dc_lbl_reg_countdown, buf);
    dc_reg_countdown_set_visible(true);

    /* 跟随“注册状态文本”位置放在右侧（旁边） */
    if (ui->DeviceConnect_lbl_stat_reg && lv_obj_is_valid(ui->DeviceConnect_lbl_stat_reg))
    {
        int32_t x = lv_obj_get_x(ui->DeviceConnect_lbl_stat_reg) + lv_obj_get_width(ui->DeviceConnect_lbl_stat_reg) + 8;
        int32_t y = lv_obj_get_y(ui->DeviceConnect_lbl_stat_reg);
        lv_obj_set_pos(g_dc_lbl_reg_countdown, x, y);
    }
}

static void dc_queue_log_line(const char *line)
{
    if (!g_dc_q || !line)
        return;
    dc_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = DC_MSG_LOG;
    /* 截断并去掉末尾 \r */
    size_t n = strlen(line);
    if (n >= sizeof(m.text))
        n = sizeof(m.text) - 1;
    memcpy(m.text, line, n);
    m.text[n] = 0;
    /* 将 \r\n 统一成 \n，避免 textarea 出现空行 */
    for (size_t i = 0; m.text[i]; ++i)
    {
        if (m.text[i] == '\r')
            m.text[i] = '\n';
    }
    (void)osMessageQueuePut(g_dc_q, &m, 0U, 0U);
}

static bool dc_line_has(const char *line, const char *needle)
{
    return (line != NULL && needle != NULL && strstr(line, needle) != NULL);
}

static bool dc_log_line_should_show(const char *line)
{
    if (!line || line[0] == '\0') {
        return false;
    }

    if (dc_line_has(line, "[SERVER_CMD]")) {
        return true;
    }
    if (dc_line_has(line, "[服务器命令]")) {
        return false;
    }

    /* High-rate diagnostics: debug console only, never DeviceConnect textarea. */
    if (dc_line_has(line, "[DSP]") ||
        dc_line_has(line, "[AD7606]") ||
        dc_line_has(line, "[PARAM]") ||
        dc_line_has(line, "[Debug]") ||
        dc_line_has(line, "[debug]") ||
        dc_line_has(line, "full tx progress") ||
        dc_line_has(line, "full tx done") ||
        dc_line_has(line, "full waiting") ||
        dc_line_has(line, "full status") ||
        dc_line_has(line, "full http done") ||
        dc_line_has(line, "[ESP32SPI] TX ") ||
        dc_line_has(line, "[ESP32SPI] RX ") ||
        dc_line_has(line, "TX REPORT_") ||
        dc_line_has(line, "RX STATUS_RESP") ||
        dc_line_has(line, "[ESP32SPI] STATUS ready=") ||
        dc_line_has(line, "[ESP32SPI] EVENT type=") ||
        dc_line_has(line, "[ESP32SPI] RESP type=") ||
        dc_line_has(line, "RX EVENT") ||
        dc_line_has(line, "RX TX_RESULT") ||
        dc_line_has(line, "TX_RESULT ref_seq") ||
        dc_line_has(line, "NACK ref_seq")) {
        return false;
    }

    /* Keep explicit user actions, connection steps and real problems. */
    if (dc_line_has(line, "[UI]") ||
        dc_line_has(line, "Executing") ||
        dc_line_has(line, "AutoReconnect") ||
        dc_line_has(line, "Config") ||
        dc_line_has(line, "config") ||
        dc_line_has(line, "WiFi") ||
        dc_line_has(line, "TCP") ||
        dc_line_has(line, "REG") ||
        dc_line_has(line, "Register") ||
        dc_line_has(line, "register") ||
        dc_line_has(line, "report_mode") ||
        dc_line_has(line, "upload_points") ||
        dc_line_has(line, "downsample_step") ||
        dc_line_has(line, "server command") ||
        dc_line_has(line, "failed") ||
        dc_line_has(line, "Failed") ||
        dc_line_has(line, "fail") ||
        dc_line_has(line, "FAIL") ||
        dc_line_has(line, "ERROR") ||
        dc_line_has(line, "error") ||
        dc_line_has(line, "timeout") ||
        dc_line_has(line, "Timeout") ||
        dc_line_has(line, "not ready") ||
        dc_line_has(line, "invalid") ||
        dc_line_has(line, "denied") ||
        dc_line_has(line, "reset") ||
        dc_line_has(line, "ready")) {
        return true;
    }

    return false;
}

static void dc_post_log_from_esp(const char *line, void *ctx)
{
    (void)ctx;
    if (!g_dc_q || !line)
        return;
    if (!dc_log_line_should_show(line)) {
        return;
    }
    bool is_server_cmd = dc_line_has(line, "[SERVER_CMD]");
    /* Throttle visible UI logs. Do not print "dropped N lines" into the
     * textarea; that message itself becomes high-rate noise during full upload. */
    static uint32_t last_log_tick = 0;
    uint32_t now = lv_tick_get();
    if (!is_server_cmd && last_log_tick != 0U && (now - last_log_tick) < 500U) {
        return;
    }
    if (!is_server_cmd) {
        last_log_tick = now;
    }
    dc_queue_log_line(line);
}

static void dc_post_step_from_esp(esp_ui_cmd_t step, bool ok, void *ctx)
{
    (void)ctx;
    if (!g_dc_q)
        return;
    dc_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = DC_MSG_STEP_DONE;
    m.step = (uint8_t)step;
    m.ok = ok ? 1U : 0U;
    (void)osMessageQueuePut(g_dc_q, &m, 0U, 0U);
}

static void dc_console_append(lv_ui *ui, const char *line)
{
    if (!ui || !ui->DeviceConnect_ta_console || !line)
        return;
    if (!lv_obj_is_valid(ui->DeviceConnect_ta_console))
        return;
    if (line[0] == '\0')
        return;

    char one[160];
    size_t n = 0;
    if (line[0] != '>') {
        one[n++] = '>';
        one[n++] = ' ';
    }
    while (*line != '\0' && n < sizeof(one) - 2U) {
        char c = *line++;
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            break;
        }
        one[n++] = c;
    }
    one[n++] = '\n';
    one[n] = '\0';

    while ((g_dc_console_lines >= EW_DEVICECONNECT_CONSOLE_MAX_LINES) ||
           (g_dc_console_len + n >= EW_DEVICECONNECT_CONSOLE_MAX_CHARS)) {
        char *first_nl = strchr(g_dc_console_text, '\n');
        if (!first_nl) {
            g_dc_console_text[0] = '\0';
            g_dc_console_len = 0;
            g_dc_console_lines = 0;
            break;
        }
        size_t drop = (size_t)(first_nl - g_dc_console_text) + 1U;
        memmove(g_dc_console_text, g_dc_console_text + drop, g_dc_console_len - drop + 1U);
        g_dc_console_len -= (uint32_t)drop;
        if (g_dc_console_lines > 0U) {
            g_dc_console_lines--;
        }
    }

    if (n < EW_DEVICECONNECT_CONSOLE_MAX_CHARS) {
        memcpy(g_dc_console_text + g_dc_console_len, one, n + 1U);
        g_dc_console_len += (uint32_t)n;
        g_dc_console_lines++;
    }

    lv_textarea_set_text(ui->DeviceConnect_ta_console, g_dc_console_text);
    lv_textarea_set_cursor_pos(ui->DeviceConnect_ta_console, LV_TEXTAREA_CURSOR_LAST);
}

static void dc_led_set_state(lv_obj_t *led, uint32_t color_hex, bool on)
{
    if (!led || !lv_obj_is_valid(led))
        return;
    lv_led_set_color(led, lv_color_hex(color_hex));
    if (on)
        lv_led_on(led);
    else
        lv_led_off(led);
}

static void dc_step_reset_ui(lv_obj_t *led, lv_obj_t *lbl_stat, lv_obj_t *btn, const char *stat_text)
{
    if (led && lv_obj_is_valid(led))
        dc_led_set_state(led, 0x999999, false);
    if (lbl_stat && lv_obj_is_valid(lbl_stat))
        lv_label_set_text(lbl_stat, stat_text ? stat_text : "未连接 (Idle)");
    /* 未上报时：按钮应该随时可点。这里仅清掉 disabled（若正在上报，外层会拦截点击） */
    if (btn && lv_obj_is_valid(btn))
        lv_obj_clear_state(btn, LV_STATE_DISABLED);
}

/* 强制把“数据上报”行的 UI 同步到实际上报状态。
 * 解决问题：实际在上报(g_report_enabled=1)但界面因重新进入/重建而显示 Idle，且提示 stop reporting first。
 * 规则：上报开启时，锁定 WiFi/TCP/REG/AUTO；仅允许点击“停止上报”。
 */
static void dc_sync_reporting_ui(lv_ui *ui)
{
    if (!ui) return;
    static int last_reporting = -1;
    static int last_wifi_ok = -1;
    static int last_tcp_ok = -1;
    static int last_reg_ok = -1;

    bool reporting = ESP_UI_IsReporting();
    bool wifi_ok = ESP_UI_IsWiFiOk();
    bool tcp_ok = ESP_UI_IsTcpOk();
    bool reg_ok = ESP_UI_IsRegOk();

    /* 上报中必然意味着 WiFi/TCP/REG 已 OK（否则 ESP 侧会拒绝开启上报）。
     * 自动重连发生在进入界面前时，步骤消息可能丢失，这里必须用“真实状态”强制刷新。
     */
    if (reporting) {
        wifi_ok = true;
        tcp_ok = true;
        reg_ok = true;
    }

    /* 任何情况下都不要让 report 按钮卡在 disabled（否则用户无法停止上报） */
    if (ui->DeviceConnect_btn_report && lv_obj_is_valid(ui->DeviceConnect_btn_report)) {
        if (lv_obj_has_state(ui->DeviceConnect_btn_report, LV_STATE_DISABLED)) {
            lv_obj_clear_state(ui->DeviceConnect_btn_report, LV_STATE_DISABLED);
        }
    }

    if (last_reporting != (int)reporting) {
        if (ui->DeviceConnect_led_report && lv_obj_is_valid(ui->DeviceConnect_led_report)) {
            dc_led_set_state(ui->DeviceConnect_led_report, reporting ? 0x3dfb00 : 0x999999, reporting);
        }
        if (ui->DeviceConnect_lbl_stat_report && lv_obj_is_valid(ui->DeviceConnect_lbl_stat_report)) {
            lv_label_set_text(ui->DeviceConnect_lbl_stat_report, reporting ? "Uploading..." : "未连接 (Idle)");
        }
        if (ui->DeviceConnect_lbl_btn_report && lv_obj_is_valid(ui->DeviceConnect_lbl_btn_report)) {
            lv_label_set_text(ui->DeviceConnect_lbl_btn_report, reporting ? "停止上报" : "开始上报");
            lv_obj_set_style_text_color(ui->DeviceConnect_lbl_btn_report,
                                        reporting ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x2F35DA),
                                        LV_PART_MAIN);
        }
        if (ui->DeviceConnect_btn_report && lv_obj_is_valid(ui->DeviceConnect_btn_report)) {
            lv_obj_set_style_bg_color(ui->DeviceConnect_btn_report,
                                      reporting ? lv_color_hex(0xff4444) : lv_color_hex(0x3dfb00),
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(ui->DeviceConnect_btn_report, 255, LV_PART_MAIN);
        }
        /* 上报开启：锁定连接按钮；停止上报：恢复 */
        dc_set_buttons_enabled(ui, !reporting);
        last_reporting = reporting ? 1 : 0;
    }

    /* 同步 WiFi/TCP/REG 三步的真实状态（不要依赖历史 step 消息） */
    if (!g_dc_step_busy_wifi) {
        if (last_wifi_ok != (int)wifi_ok) {
            if (ui->DeviceConnect_led_wifi && lv_obj_is_valid(ui->DeviceConnect_led_wifi)) {
                dc_led_set_state(ui->DeviceConnect_led_wifi, wifi_ok ? 0x3dfb00 : 0x999999, wifi_ok);
            }
            if (ui->DeviceConnect_lbl_stat_wifi && lv_obj_is_valid(ui->DeviceConnect_lbl_stat_wifi)) {
                lv_label_set_text(ui->DeviceConnect_lbl_stat_wifi, wifi_ok ? "WiFi Connected" : "未连接 (Idle)");
            }
            last_wifi_ok = wifi_ok ? 1 : 0;
        }
    }
    if (!g_dc_step_busy_tcp) {
        if (last_tcp_ok != (int)tcp_ok) {
            if (ui->DeviceConnect_led_tcp && lv_obj_is_valid(ui->DeviceConnect_led_tcp)) {
                dc_led_set_state(ui->DeviceConnect_led_tcp, tcp_ok ? 0x3dfb00 : 0x999999, tcp_ok);
            }
            if (ui->DeviceConnect_lbl_stat_tcp && lv_obj_is_valid(ui->DeviceConnect_lbl_stat_tcp)) {
                lv_label_set_text(ui->DeviceConnect_lbl_stat_tcp, tcp_ok ? "TCP Linked" : "未连接 (Idle)");
            }
            last_tcp_ok = tcp_ok ? 1 : 0;
        }
    }
    if (!g_dc_step_busy_reg) {
        if (last_reg_ok != (int)reg_ok) {
            if (ui->DeviceConnect_led_reg && lv_obj_is_valid(ui->DeviceConnect_led_reg)) {
                dc_led_set_state(ui->DeviceConnect_led_reg, reg_ok ? 0x3dfb00 : 0x999999, reg_ok);
            }
            if (ui->DeviceConnect_lbl_stat_reg && lv_obj_is_valid(ui->DeviceConnect_lbl_stat_reg)) {
                lv_label_set_text(ui->DeviceConnect_lbl_stat_reg, reg_ok ? "Registered" : "未连接 (Idle)");
            }
            last_reg_ok = reg_ok ? 1 : 0;
        }
    }

    if (reporting) {
        /* 上报中：不显示“注册过期倒计时/变暗” */
        if (last_reporting != 1 || g_dc_report_stop_tick != 0 || g_dc_reg_dimmed) {
            g_dc_report_stop_tick = 0;
            g_dc_reg_dimmed = 0;
            dc_reg_countdown_set_visible(false);
        }
    }
}

/* ============ DeviceConnect: 进入界面时从 SD 加载 WiFi/Server 配置，并写入 ESP 缓冲区 ============ */
static bool dc_apply_cfg_to_esp_from_files(lv_ui *ui)
{
    memset(&g_dc_cfg_buf, 0, sizeof(g_dc_cfg_buf));

    FRESULT r1 = ui_wifi_cfg_read_file(g_dc_cfg_buf.ssid, sizeof(g_dc_cfg_buf.ssid),
                                       g_dc_cfg_buf.pwd, sizeof(g_dc_cfg_buf.pwd));
    FRESULT r2 = ui_server_cfg_read_file(g_dc_cfg_buf.ip, sizeof(g_dc_cfg_buf.ip),
                                         g_dc_cfg_buf.port_s, sizeof(g_dc_cfg_buf.port_s),
                                         g_dc_cfg_buf.node_id, sizeof(g_dc_cfg_buf.node_id),
                                         g_dc_cfg_buf.node_loc, sizeof(g_dc_cfg_buf.node_loc));
    if (r1 != FR_OK || r2 != FR_OK)
    {
        /* 不在这里大量刷日志，避免进入界面时频繁重绘导致“乱飞”观感 */
        printf("[DC_CFG] load fail: wifi=%d server=%d\r\n", (int)r1, (int)r2);
        return false;
    }

    uint32_t port_u = 0;
    if (g_dc_cfg_buf.port_s[0])
        port_u = (uint32_t)atoi(g_dc_cfg_buf.port_s);
    if (port_u == 0 || port_u > 65535U)
        port_u = 0;

    SystemConfig_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.wifi_ssid, g_dc_cfg_buf.ssid, sizeof(cfg.wifi_ssid) - 1);
    strncpy(cfg.wifi_password, g_dc_cfg_buf.pwd, sizeof(cfg.wifi_password) - 1);
    strncpy(cfg.server_ip, g_dc_cfg_buf.ip, sizeof(cfg.server_ip) - 1);
    cfg.server_port = (uint16_t)port_u;
    strncpy(cfg.node_id, g_dc_cfg_buf.node_id, sizeof(cfg.node_id) - 1);
    strncpy(cfg.node_location, g_dc_cfg_buf.node_loc, sizeof(cfg.node_location) - 1);

    ESP_Config_Apply(&cfg);
    printf("[DC_CFG] applied: ssid=%s ip=%s port=%u id=%s loc=%s\r\n",
           cfg.wifi_ssid, cfg.server_ip, (unsigned)cfg.server_port, cfg.node_id, cfg.node_location);

    /* 进入设备连接界面也读取一次通讯参数：写入 ESP 通讯参数缓存，让 ESP 运行时使用 */
    (void)ESP_CommParams_LoadFromSD();
    return true;
}

static void DeviceConnect_cfg_load_timer_cb(lv_timer_t *t)
{
    lv_ui *ui = (lv_ui *)lv_timer_get_user_data(t);
    lv_timer_del(t);
    g_dc_cfg_timer = NULL;
    if (!ui) return;
    g_dc_cfg_loaded = dc_apply_cfg_to_esp_from_files(ui) ? 1U : 0U;
    if (!g_dc_cfg_loaded) {
        dc_console_append(ui, "Config not ready. Please enter WiFi/Server config and save.");
    }
}

static void DeviceConnect_screen_event_handler(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_SCREEN_LOADED)
        return;
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) return;
    g_dc_cfg_loaded = 0;
    /* 进入界面立即同步一次“上报 UI 状态”，避免实际在上报但 UI 显示 Idle */
    dc_sync_reporting_ui(ui);

    /* 右上角：断电重连开关（从 SD 读取并刷新 UI） */
    if (ui->DeviceConnect_btn_autorec && lv_obj_is_valid(ui->DeviceConnect_btn_autorec) &&
        ui->DeviceConnect_lbl_autorec && lv_obj_is_valid(ui->DeviceConnect_lbl_autorec))
    {
        bool en = true, last = false;
        (void)ESP_AutoReconnect_Read(&en, &last);
        lv_label_set_text(ui->DeviceConnect_lbl_autorec, en ? "断电重连: 是" : "断电重连: 否");
        lv_obj_set_style_bg_color(ui->DeviceConnect_btn_autorec,
                                  lv_color_hex(en ? 0x3dfb00 : 0x999999),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ui->DeviceConnect_btn_autorec, 255, LV_PART_MAIN);
        lv_obj_set_style_text_color(ui->DeviceConnect_lbl_autorec,
                                    lv_color_hex(en ? 0x2F35DA : 0xFFFFFF),
                                    LV_PART_MAIN);
    }
    /* 不在这里刷控制台，避免进入界面时大量文本更新导致重绘异常观感 */
    /* 去重：若已存在 timer，先删除再创建 */
    if (g_dc_cfg_timer) {
        lv_timer_del(g_dc_cfg_timer);
        g_dc_cfg_timer = NULL;
    }
    /* 延迟一帧，避免首帧渲染被 SD IO 阻塞 */
    g_dc_cfg_timer = lv_timer_create(DeviceConnect_cfg_load_timer_cb, 120, ui);
}

static void DeviceConnect_autorec_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) return;
    lv_indev_wait_release(lv_indev_active());

    bool en = true, last = false;
    (void)ESP_AutoReconnect_Read(&en, &last);
    en = !en;
    bool ok = ESP_AutoReconnect_SetEnabled(en);

    if (ui->DeviceConnect_lbl_autorec && lv_obj_is_valid(ui->DeviceConnect_lbl_autorec)) {
        lv_label_set_text(ui->DeviceConnect_lbl_autorec, en ? "断电重连: 是" : "断电重连: 否");
        lv_obj_set_style_text_color(ui->DeviceConnect_lbl_autorec,
                                    lv_color_hex(en ? 0x2F35DA : 0xFFFFFF),
                                    LV_PART_MAIN);
    }
    if (ui->DeviceConnect_btn_autorec && lv_obj_is_valid(ui->DeviceConnect_btn_autorec)) {
        lv_obj_set_style_bg_color(ui->DeviceConnect_btn_autorec,
                                  lv_color_hex(en ? 0x3dfb00 : 0x999999),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ui->DeviceConnect_btn_autorec, 255, LV_PART_MAIN);
    }

    if (!ok) {
        dc_console_append(ui, "AutoReconnect save failed (SD not ready?)");
    } else {
        dc_console_append(ui, en ? "AutoReconnect: ON (saved)" : "AutoReconnect: OFF (saved)");
    }
}

static bool dc_require_cfg_loaded(lv_ui *ui)
{
    if (g_dc_cfg_loaded)
        return true;
    dc_console_append(ui, "Config not loaded. Go to WiFi/Server config and save first.");
    return false;
}

/* 规则：点击某一步时，其后的步骤需要全部“重新执行”——LED 灭掉、状态恢复 Idle、清掉相关就绪标志 */
static void dc_reset_following_steps(lv_ui *ui, esp_ui_cmd_t clicked_step)
{
    if (!ui)
        return;

    /* 任何“重新走流程”的动作，都应终止“注册过期倒计时/变暗”UI 状态 */
    g_dc_reg_dimmed = 0;
    g_dc_report_stop_tick = 0;
    dc_reg_countdown_set_visible(false);
    dc_set_reg_dim(ui, false);

    /* Report 步骤：恢复按钮文案/配色（避免残留“停止上报/红色”） */
    if (ui->DeviceConnect_lbl_btn_report && lv_obj_is_valid(ui->DeviceConnect_lbl_btn_report))
        lv_label_set_text(ui->DeviceConnect_lbl_btn_report, "开始上报");
    if (ui->DeviceConnect_btn_report && lv_obj_is_valid(ui->DeviceConnect_btn_report))
    {
        lv_obj_set_style_bg_color(ui->DeviceConnect_btn_report, lv_color_hex(0x3dfb00), LV_PART_MAIN);
    }
    if (ui->DeviceConnect_lbl_btn_report && lv_obj_is_valid(ui->DeviceConnect_lbl_btn_report))
    {
        lv_obj_set_style_text_color(ui->DeviceConnect_lbl_btn_report, lv_color_hex(0x2F35DA), LV_PART_MAIN);
    }

    switch (clicked_step)
    {
    case ESP_UI_CMD_WIFI:
        /* WiFi 之后：TCP/REG/REPORT 必须重做 */
        g_dc_step_busy_tcp = 0;
        g_dc_step_busy_reg = 0;
        g_dc_step_busy_rep = 0;
        dc_step_reset_ui(ui->DeviceConnect_led_tcp, ui->DeviceConnect_lbl_stat_tcp, ui->DeviceConnect_btn_tcp, "未连接 (Idle)");
        dc_step_reset_ui(ui->DeviceConnect_led_reg, ui->DeviceConnect_lbl_stat_reg, ui->DeviceConnect_btn_reg, "未连接 (Idle)");
        dc_step_reset_ui(ui->DeviceConnect_led_report, ui->DeviceConnect_lbl_stat_report, ui->DeviceConnect_btn_report, "未连接 (Idle)");
        break;
    case ESP_UI_CMD_TCP:
        /* TCP 之后：REG/REPORT 必须重做 */
        g_dc_step_busy_reg = 0;
        g_dc_step_busy_rep = 0;
        dc_step_reset_ui(ui->DeviceConnect_led_reg, ui->DeviceConnect_lbl_stat_reg, ui->DeviceConnect_btn_reg, "未连接 (Idle)");
        dc_step_reset_ui(ui->DeviceConnect_led_report, ui->DeviceConnect_lbl_stat_report, ui->DeviceConnect_btn_report, "未连接 (Idle)");
        /* 清掉“可上报就绪”标志，强制重新注册 */
        ESP_UI_InvalidateReg();
        break;
    case ESP_UI_CMD_REG:
        /* REG 之后：REPORT 必须重做（但 REG 本身会在 ESP 侧重新设置就绪） */
        g_dc_step_busy_rep = 0;
        dc_step_reset_ui(ui->DeviceConnect_led_report, ui->DeviceConnect_lbl_stat_report, ui->DeviceConnect_btn_report, "未连接 (Idle)");
        break;
    case ESP_UI_CMD_AUTO_CONNECT:
        /* 一键连接：从头开始，清空所有步骤显示 */
        g_dc_step_busy_wifi = 0;
        g_dc_step_busy_tcp = 0;
        g_dc_step_busy_reg = 0;
        g_dc_step_busy_rep = 0;
        dc_step_reset_ui(ui->DeviceConnect_led_wifi, ui->DeviceConnect_lbl_stat_wifi, ui->DeviceConnect_btn_wifi, "未连接 (Idle)");
        dc_step_reset_ui(ui->DeviceConnect_led_tcp, ui->DeviceConnect_lbl_stat_tcp, ui->DeviceConnect_btn_tcp, "未连接 (Idle)");
        dc_step_reset_ui(ui->DeviceConnect_led_reg, ui->DeviceConnect_lbl_stat_reg, ui->DeviceConnect_btn_reg, "未连接 (Idle)");
        dc_step_reset_ui(ui->DeviceConnect_led_report, ui->DeviceConnect_lbl_stat_report, ui->DeviceConnect_btn_report, "未连接 (Idle)");
        /* 清掉“可上报就绪”标志，避免复用旧注册 */
        ESP_UI_InvalidateReg();
        break;
    default:
        break;
    }
}

static void dc_set_processing(lv_ui *ui, esp_ui_cmd_t step, const char *text)
{
    if (!ui)
        return;
    switch (step)
    {
    case ESP_UI_CMD_WIFI:
        g_dc_step_busy_wifi = 1;
        dc_led_set_state(ui->DeviceConnect_led_wifi, 0xFFA500, true);
        lv_label_set_text(ui->DeviceConnect_lbl_stat_wifi, text ? text : "Processing...");
        break;
    case ESP_UI_CMD_TCP:
        g_dc_step_busy_tcp = 1;
        dc_led_set_state(ui->DeviceConnect_led_tcp, 0xFFA500, true);
        lv_label_set_text(ui->DeviceConnect_lbl_stat_tcp, text ? text : "Processing...");
        break;
    case ESP_UI_CMD_REG:
        g_dc_step_busy_reg = 1;
        dc_led_set_state(ui->DeviceConnect_led_reg, 0xFFA500, true);
        lv_label_set_text(ui->DeviceConnect_lbl_stat_reg, text ? text : "Processing...");
        break;
    case ESP_UI_CMD_REPORT_TOGGLE:
        g_dc_step_busy_rep = 1;
        dc_led_set_state(ui->DeviceConnect_led_report, 0xFFA500, true);
        lv_label_set_text(ui->DeviceConnect_lbl_stat_report, text ? text : "Uploading...");
        break;
    default:
        break;
    }
}

static void dc_set_done(lv_ui *ui, esp_ui_cmd_t step, bool ok)
{
    if (!ui)
        return;

    uint32_t c_ok = 0x3dfb00;
    uint32_t c_err = 0xff4444;

    switch (step)
    {
    case ESP_UI_CMD_WIFI:
        g_dc_step_busy_wifi = 0;
        dc_led_set_state(ui->DeviceConnect_led_wifi, ok ? c_ok : c_err, true);
        lv_label_set_text(ui->DeviceConnect_lbl_stat_wifi, ok ? "WiFi Connected" : "WiFi Failed");
        /* WiFi 按钮永远可再次点击（除非已开始上报） */
        lv_obj_clear_state(ui->DeviceConnect_btn_wifi, LV_STATE_DISABLED);
        break;
    case ESP_UI_CMD_TCP:
        g_dc_step_busy_tcp = 0;
        dc_led_set_state(ui->DeviceConnect_led_tcp, ok ? c_ok : c_err, true);
        lv_label_set_text(ui->DeviceConnect_lbl_stat_tcp, ok ? "TCP Linked" : "TCP Failed");
        lv_obj_clear_state(ui->DeviceConnect_btn_tcp, LV_STATE_DISABLED);
        break;
    case ESP_UI_CMD_REG:
        g_dc_step_busy_reg = 0;
        dc_led_set_state(ui->DeviceConnect_led_reg, ok ? c_ok : c_err, true);
        lv_label_set_text(ui->DeviceConnect_lbl_stat_reg, ok ? "Registered" : "Register Failed");
        lv_obj_clear_state(ui->DeviceConnect_btn_reg, LV_STATE_DISABLED);
        if (ok)
        {
            /* 用户新需求：
             * 注册成功后即开始倒计时；若不点击“开始上报”，到期需重新注册。
             * 复用 g_dc_report_stop_tick 作为“注册有效期倒计时起点”（开始上报会清零；停止上报会重置为停止时刻）。
             */
            if (!ESP_UI_IsReporting())
            {
                g_dc_report_stop_tick = lv_tick_get();
                g_dc_reg_dimmed = 0;
                dc_set_reg_dim(ui, false);
                dc_reg_countdown_update(ui);
            }
        }
        break;
    case ESP_UI_CMD_REPORT_TOGGLE:
    {
        g_dc_step_busy_rep = 0;
        bool reporting = ESP_UI_IsReporting();
        if (!ok && !reporting)
        {
            dc_led_set_state(ui->DeviceConnect_led_report, c_err, true);
            lv_label_set_text(ui->DeviceConnect_lbl_stat_report, "Need REG first");
        }
        else
        {
            dc_led_set_state(ui->DeviceConnect_led_report, c_ok, reporting);
            lv_label_set_text(ui->DeviceConnect_lbl_stat_report, reporting ? "Uploading..." : "Paused");
        }

        if (reporting)
        {
            lv_label_set_text(ui->DeviceConnect_lbl_btn_report, "停止上报");
            lv_obj_set_style_bg_color(ui->DeviceConnect_btn_report, lv_color_hex(0xff4444), LV_PART_MAIN);
            lv_obj_set_style_text_color(ui->DeviceConnect_lbl_btn_report, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        }
        else
        {
            lv_label_set_text(ui->DeviceConnect_lbl_btn_report, "开始上报");
            lv_obj_set_style_bg_color(ui->DeviceConnect_btn_report, lv_color_hex(0x3dfb00), LV_PART_MAIN);
            lv_obj_set_style_text_color(ui->DeviceConnect_lbl_btn_report, lv_color_hex(0x2F35DA), LV_PART_MAIN);
        }

        lv_obj_clear_state(ui->DeviceConnect_btn_report, LV_STATE_DISABLED);

        /* 上报开始后，锁定连接按钮；停止上报后恢复 */
        dc_set_buttons_enabled(ui, !reporting);

        if (reporting)
        {
            /* 开始上报：清除“注册过期”状态 */
            g_dc_report_stop_tick = 0;
            g_dc_reg_dimmed = 0;
            dc_reg_countdown_set_visible(false);
        }
        else
        {
            /* 停止上报：启动计时 */
            g_dc_report_stop_tick = lv_tick_get();
            dc_reg_countdown_update(ui);
        }
    }
    break;
    default:
        break;
    }
}

static void dc_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_dc_q || !g_dc_ui)
        return;

    /* 停止上报后的倒计时：每次都刷新一次显示；到期后触发“需重新注册” */
    if (!ESP_UI_IsReporting() && g_dc_report_stop_tick != 0 && !g_dc_reg_dimmed)
    {
        dc_reg_countdown_update(g_dc_ui);

        uint32_t now = lv_tick_get();
        if ((now - g_dc_report_stop_tick) >= EW_DEVICECONNECT_REG_DIM_MS)
        {
            g_dc_reg_dimmed = 1;
            if (g_dc_ui->DeviceConnect_lbl_stat_reg && lv_obj_is_valid(g_dc_ui->DeviceConnect_lbl_stat_reg))
            {
                lv_label_set_text(g_dc_ui->DeviceConnect_lbl_stat_reg, "需重新注册");
            }
            /* 注册过期：LED 需要灭掉（变灰），提示必须重新注册；上报也显示停止 */
            dc_led_set_state(g_dc_ui->DeviceConnect_led_reg, 0x999999, false);
            dc_led_set_state(g_dc_ui->DeviceConnect_led_report, 0x999999, false);
            if (g_dc_ui->DeviceConnect_lbl_stat_report && lv_obj_is_valid(g_dc_ui->DeviceConnect_lbl_stat_report))
            {
                lv_label_set_text(g_dc_ui->DeviceConnect_lbl_stat_report, "Stopped");
            }
            if (g_dc_ui->DeviceConnect_lbl_btn_report && lv_obj_is_valid(g_dc_ui->DeviceConnect_lbl_btn_report))
            {
                lv_label_set_text(g_dc_ui->DeviceConnect_lbl_btn_report, "开始上报");
            }
            dc_console_append(g_dc_ui, "Registration expired, click REG again.");
            ESP_UI_InvalidateReg();
            dc_reg_countdown_set_visible(false);
        }
    }
    else
    {
        /* 非倒计时阶段：隐藏倒计时 */
        dc_reg_countdown_set_visible(false);
    }

    dc_msg_t m;
    /* 每次最多处理 N 条消息，避免某次日志爆发导致 LVGL 卡顿/假死 */
    uint32_t budget = g_dc_auto_running ? 40u : 16u;
    while (budget-- && osMessageQueueGet(g_dc_q, &m, NULL, 0U) == osOK)
    {
        if (m.type == DC_MSG_LOG)
        {
            /* 自动流程中优先处理“步骤结果”，日志会明显拖慢状态刷新；这里丢弃日志以保证 UI 及时推进。 */
            if (!g_dc_auto_running || dc_line_has(m.text, "[SERVER_CMD]")) {
                dc_console_append(g_dc_ui, m.text);
            }
        }
        else if (m.type == DC_MSG_STEP_DONE)
        {
            esp_ui_cmd_t step = (esp_ui_cmd_t)m.step;
            bool ok = (m.ok != 0U);
            dc_set_done(g_dc_ui, step, ok);

            /* 自动流程：成功后把下一步置为 processing（更像 HTML 的“串行执行”效果） */
            if (g_dc_auto_running)
            {
                if (!ok)
                {
                    g_dc_auto_running = 0;
                    /* 自动流程失败：恢复按钮（若未上报） */
                    dc_set_buttons_enabled(g_dc_ui, !ESP_UI_IsReporting());
                }
                else
                {
                    if (step == ESP_UI_CMD_WIFI)
                        dc_set_processing(g_dc_ui, ESP_UI_CMD_TCP, "Processing...");
                    else if (step == ESP_UI_CMD_TCP)
                        dc_set_processing(g_dc_ui, ESP_UI_CMD_REG, "Processing...");
                    else if (step == ESP_UI_CMD_REG)
                        dc_set_processing(g_dc_ui, ESP_UI_CMD_REPORT_TOGGLE, "Uploading...");
                    else if (step == ESP_UI_CMD_REPORT_TOGGLE)
                    {
                        g_dc_auto_running = 0;
                        dc_set_buttons_enabled(g_dc_ui, !ESP_UI_IsReporting());
                    }
                }
            }
        }
    }

    /* 兜底：即使步骤消息被日志淹没或丢失，也保证“上报状态”能实时反映到 UI 上 */
    dc_sync_reporting_ui(g_dc_ui);
}

static void DeviceConnect_auto_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui || !ui->DeviceConnect_ta_console) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    if (!dc_require_cfg_loaded(ui)) {
        return;
    }
    if (ESP_UI_IsReporting()) {
        /* 若 UI 与实际上报状态不同步，先同步，确保用户能看到“停止上报”按钮 */
        dc_sync_reporting_ui(ui);
        dc_console_append(ui, "Reporting active, stop reporting first.");
        return;
    }
    g_dc_auto_running = 1;

    /* 点击“一键连接”：后续步骤全部重新执行（从头开始） */
    dc_reset_following_steps(ui, ESP_UI_CMD_AUTO_CONNECT);

    /* 自动流程中仅临时禁用连接按钮，结束后恢复 */
    dc_set_buttons_enabled(ui, false);
    lv_obj_add_state(ui->DeviceConnect_btn_report, LV_STATE_DISABLED);

    dc_console_append(ui, "Auto sequence started...");
    dc_set_processing(ui, ESP_UI_CMD_WIFI, "Processing...");
    /* 关键：先强制刷新一帧，让“Processing...”立刻可见，再交给 ESP 任务执行耗时步骤 */
    lv_obj_update_layout(ui->DeviceConnect);
    lv_refr_now(NULL);
    (void)ESP_UI_SendCmd(ESP_UI_CMD_AUTO_CONNECT);
}

static void DeviceConnect_wifi_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui)
        return;
    lv_indev_wait_release(lv_indev_active());
    if (!dc_require_cfg_loaded(ui)) {
        return;
    }
    if (ESP_UI_IsReporting()) {
        dc_sync_reporting_ui(ui);
        dc_console_append(ui, "Reporting active, stop reporting first.");
        return;
    }
    g_dc_auto_running = 0;

    /* 点击 WiFi：其后的 TCP/REG/REPORT 全部重置，强制重新执行 */
    dc_reset_following_steps(ui, ESP_UI_CMD_WIFI);

    lv_obj_add_state(ui->DeviceConnect_btn_wifi, LV_STATE_DISABLED);
    dc_set_processing(ui, ESP_UI_CMD_WIFI, "Processing...");
    dc_console_append(ui, "Executing WIFI...");
    lv_obj_update_layout(ui->DeviceConnect);
    lv_refr_now(NULL);
    (void)ESP_UI_SendCmd(ESP_UI_CMD_WIFI);
}

static void DeviceConnect_tcp_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui)
        return;
    lv_indev_wait_release(lv_indev_active());
    if (!dc_require_cfg_loaded(ui)) {
        return;
    }
    if (ESP_UI_IsReporting()) {
        dc_sync_reporting_ui(ui);
        dc_console_append(ui, "Reporting active, stop reporting first.");
        return;
    }
    g_dc_auto_running = 0;

    /* 点击 TCP：其后的 REG/REPORT 全部重置，强制重新执行 */
    dc_reset_following_steps(ui, ESP_UI_CMD_TCP);

    lv_obj_add_state(ui->DeviceConnect_btn_tcp, LV_STATE_DISABLED);
    dc_set_processing(ui, ESP_UI_CMD_TCP, "Processing...");
    dc_console_append(ui, "Executing TCP...");
    lv_obj_update_layout(ui->DeviceConnect);
    lv_refr_now(NULL);
    (void)ESP_UI_SendCmd(ESP_UI_CMD_TCP);
}

static void DeviceConnect_reg_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui)
        return;
    lv_indev_wait_release(lv_indev_active());
    if (!dc_require_cfg_loaded(ui)) {
        return;
    }
    if (ESP_UI_IsReporting()) {
        dc_sync_reporting_ui(ui);
        dc_console_append(ui, "Reporting active, stop reporting first.");
        return;
    }
    g_dc_auto_running = 0;
    if (g_dc_reg_dimmed) {
        g_dc_reg_dimmed = 0;
        g_dc_report_stop_tick = 0;
        dc_reg_countdown_set_visible(false);
    }

    /* 点击 REG：其后的 REPORT 全部重置，强制重新执行 */
    dc_reset_following_steps(ui, ESP_UI_CMD_REG);

    lv_obj_add_state(ui->DeviceConnect_btn_reg, LV_STATE_DISABLED);
    dc_set_processing(ui, ESP_UI_CMD_REG, "Processing...");
    dc_console_append(ui, "Executing REG...");
    lv_obj_update_layout(ui->DeviceConnect);
    lv_refr_now(NULL);
    (void)ESP_UI_SendCmd(ESP_UI_CMD_REG);
}

static void DeviceConnect_report_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui)
        return;
    lv_indev_wait_release(lv_indev_active());
    if (!dc_require_cfg_loaded(ui)) {
        return;
    }
    lv_obj_add_state(ui->DeviceConnect_btn_report, LV_STATE_DISABLED);
    dc_set_processing(ui, ESP_UI_CMD_REPORT_TOGGLE, "Uploading...");
    lv_obj_update_layout(ui->DeviceConnect);
    lv_refr_now(NULL);
    (void)ESP_UI_SendCmd(ESP_UI_CMD_REPORT_TOGGLE);
}

void events_init_DeviceConnect(lv_ui *ui)
{
    lv_obj_add_event_cb(ui->DeviceConnect_btn_back, DeviceConnect_back_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->DeviceConnect_btn_auto, DeviceConnect_auto_event_handler, LV_EVENT_CLICKED, ui);

    if (ui->DeviceConnect_btn_autorec) {
        lv_obj_add_event_cb(ui->DeviceConnect_btn_autorec, DeviceConnect_autorec_event_handler, LV_EVENT_CLICKED, ui);
    }

    lv_obj_add_event_cb(ui->DeviceConnect_btn_wifi, DeviceConnect_wifi_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->DeviceConnect_btn_tcp, DeviceConnect_tcp_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->DeviceConnect_btn_reg, DeviceConnect_reg_event_handler, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(ui->DeviceConnect_btn_report, DeviceConnect_report_event_handler, LV_EVENT_CLICKED, ui);

    /* Reset the small ring-style console each time the screen is rebuilt.
     * The backing textarea has max_length=2048, but we keep our own 1KB/16-line
     * buffer to avoid long-text relayout stalls from high-rate SPI/DSP logs. */
    strncpy(g_dc_console_text, "> System Ready.\n> Waiting for user action...\n", sizeof(g_dc_console_text) - 1U);
    g_dc_console_text[sizeof(g_dc_console_text) - 1U] = '\0';
    g_dc_console_len = (uint32_t)strlen(g_dc_console_text);
    g_dc_console_lines = 2U;
    if (ui->DeviceConnect_ta_console && lv_obj_is_valid(ui->DeviceConnect_ta_console)) {
        lv_textarea_set_text(ui->DeviceConnect_ta_console, g_dc_console_text);
        lv_textarea_set_cursor_pos(ui->DeviceConnect_ta_console, LV_TEXTAREA_CURSOR_LAST);
    }

    /* 每次进入 DeviceConnect 界面都自动从 SD 加载配置并应用到 ESP 缓冲区 */
    lv_obj_add_event_cb(ui->DeviceConnect, DeviceConnect_screen_event_handler, LV_EVENT_SCREEN_LOADED, ui);

    /* 注册 hook：ESP 任务会把日志/步骤结果通过消息队列投递给 LVGL 线程 */
    g_dc_ui = ui;
    if (!g_dc_q)
        g_dc_q = osMessageQueueNew(24, sizeof(dc_msg_t), NULL);
    ESP_UI_SetHooks(dc_post_log_from_esp, NULL, dc_post_step_from_esp, NULL);

    if (!g_dc_timer)
        g_dc_timer = lv_timer_create(dc_timer_cb, 200, NULL);

    /* “重新注册倒计时”标签：显示在注册状态右侧 */
    if (!g_dc_lbl_reg_countdown && ui->DeviceConnect_cont_panel && lv_obj_is_valid(ui->DeviceConnect_cont_panel))
    {
        g_dc_lbl_reg_countdown = lv_label_create(ui->DeviceConnect_cont_panel);
        lv_label_set_text(g_dc_lbl_reg_countdown, "");
        lv_obj_set_style_text_font(g_dc_lbl_reg_countdown, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_set_style_text_color(g_dc_lbl_reg_countdown, lv_color_hex(0x666666), LV_PART_MAIN);
        lv_obj_add_flag(g_dc_lbl_reg_countdown, LV_OBJ_FLAG_HIDDEN);
        dc_reg_countdown_update(ui);
    }
}


void events_init(lv_ui *ui)
{

}
