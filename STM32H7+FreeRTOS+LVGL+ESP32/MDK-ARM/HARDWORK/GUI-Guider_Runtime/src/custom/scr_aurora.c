/**
 * @file scr_aurora.c
 * @brief Aurora glassmorphism main screen for LVGL 9.4
 */

#include "scr_aurora.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../generated/events_init.h"
#include "../../gui_assets.h"
#include "../../gui_assets_sync.h"
#include "../../../EdgeComm/edge_comm.h"
#include "../../../SD_Card/sd_fault_log.h"
#include "../../../SD_Card/sd_time.h"
#include "edgewind_buzzer.h"

/**********************
 * DEFINES
 **********************/
#define AURORA_SCREEN_W   800
#define AURORA_SCREEN_H   480
#define AURORA_HEADER_H   60
#define AURORA_BODY_H     (AURORA_SCREEN_H - AURORA_HEADER_H)
#define AURORA_PAGE_COUNT 3

/* ============ Performance switches ============ */
/* Background floating animation will force large invalidated areas and heavy blending. */
#ifndef EW_AURORA_BG_ANIM
#define EW_AURORA_BG_ANIM 0
#endif

/* Gradients are expensive (often use RGB888 intermediate). Disable by default for FPS. */
#ifndef EW_AURORA_USE_GRADIENT
#define EW_AURORA_USE_GRADIENT 0
#endif

/* Shadows are expensive in SW renderer. Disable by default for FPS. */
#ifndef EW_AURORA_USE_SHADOW
#define EW_AURORA_USE_SHADOW 0
#endif

/* Light premium palette — sky-blue to lavender gradient */
#define COL_BG        lv_color_hex(0xE8F4FD)   /* light sky-blue top */
#define COL_BG_BOT    lv_color_hex(0xEEECFD)   /* light lavender bottom */
#define COL_PANEL     lv_color_hex(0xFFFFFF)   /* white header */
#define COL_CARD      lv_color_hex(0xFFFFFF)   /* white card */
#define COL_CARD_PR   lv_color_hex(0xE8F4FD)   /* light blue on press */
#define COL_ICON_BG   lv_color_hex(0xFFFFFF)   /* base (overridden per-card) */
#define COL_TEXT      lv_color_hex(0x1E293B)   /* dark slate text */
#define COL_TEXT_DIM  lv_color_hex(0x64748B)   /* muted secondary text */
#define COL_DIV       lv_color_hex(0xDDE6F0)   /* subtle divider */
#define COL_TITLE_HL  lv_color_hex(0x0369A1)   /* deep ocean blue title */

/* Accent colors - slightly deeper for legibility on light background */
#define COL_BLUE   lv_color_hex(0x0EA5E9)
#define COL_RED    lv_color_hex(0xEF4444)
#define COL_PURPLE lv_color_hex(0x8B5CF6)
#define COL_GREEN  lv_color_hex(0x10B981)
#define COL_ORANGE lv_color_hex(0xF97316)
#define COL_CYAN   lv_color_hex(0x06B6D4)
#define COL_INDIGO lv_color_hex(0x6366F1)
#define COL_PINK   lv_color_hex(0xEC4899)
#define COL_TEAL   lv_color_hex(0x14B8A6)
#define COL_AMBER  lv_color_hex(0xF59E0B)

/**********************
 * STATIC VARIABLES
 **********************/
static lv_obj_t * ui_AuroraScr = NULL;
static lv_obj_t * ui_Carousel = NULL;
static lv_obj_t * ui_PageIndicator = NULL;
static lv_style_t style_glass_panel;
static lv_style_t style_card;
static lv_style_t style_icon_box;
static lv_style_t style_title;
static bool styles_inited = false;

/* Header status widgets (runtime refreshed) */
static lv_timer_t * s_status_timer = NULL;
static lv_obj_t * s_dot_wifi = NULL;
static lv_obj_t * s_dot_tcp = NULL;
static lv_obj_t * s_dot_reg = NULL;
static lv_obj_t * s_dot_rep = NULL;
static lv_obj_t * s_lbl_node = NULL;
static lv_obj_t * s_lbl_wifi = NULL;
static lv_obj_t * s_lbl_tcp = NULL;
static lv_obj_t * s_lbl_reg = NULL;
static lv_obj_t * s_lbl_rep = NULL;
static lv_timer_t * s_fault_monitor_timer = NULL;

#define FAULT_MONITOR_EVENT_ROWS 4U
static lv_obj_t *s_fault_metric_value[4];
static lv_obj_t *s_fault_metric_sub[4];
static lv_obj_t *s_fault_event_main[FAULT_MONITOR_EVENT_ROWS];
static lv_obj_t *s_fault_event_sub[FAULT_MONITOR_EVENT_ROWS];
static lv_obj_t *s_fault_detail_title = NULL;
static lv_obj_t *s_fault_detail_status = NULL;
static lv_obj_t *s_fault_detail_root = NULL;
static lv_obj_t *s_fault_detail_advice = NULL;
static lv_obj_t *s_fault_detail_time = NULL;

#define HISTORY_RECORD_ROWS 5U
#define HISTORY_RECORD_MAX 16U
static FaultEntry_t s_history_entries[HISTORY_RECORD_MAX];
static uint32_t s_history_count = 0U;
static uint8_t s_history_selected = 0U;
static uint8_t s_history_recent_mode = 1U;
static char s_history_date[16] = "---- -- --";
static lv_obj_t *s_history_row[HISTORY_RECORD_ROWS];
static lv_obj_t *s_history_row_main[HISTORY_RECORD_ROWS];
static lv_obj_t *s_history_row_sub[HISTORY_RECORD_ROWS];
static lv_obj_t *s_history_status = NULL;
static lv_obj_t *s_history_detail_title = NULL;
static lv_obj_t *s_history_detail_status = NULL;
static lv_obj_t *s_history_detail_msg = NULL;
static lv_obj_t *s_history_detail_src = NULL;

static void fault_monitor_refresh(lv_ui *ui);
static void history_record_refresh(lv_ui *ui);

typedef enum {
    AURORA_NAV_NONE = 0,
    AURORA_NAV_REALTIME,
    AURORA_NAV_PARAM,
    AURORA_NAV_WIFI,
    AURORA_NAV_SERVER,
    AURORA_NAV_DEVICE,
    AURORA_NAV_FAULT_MONITOR,
    AURORA_NAV_HISTORY,
    AURORA_NAV_ABOUT
} aurora_nav_target_t;

typedef struct {
    lv_ui * ui;
    aurora_nav_target_t target;
} aurora_nav_ctx_t;

static aurora_nav_ctx_t nav_wifi;
static aurora_nav_ctx_t nav_server;
static aurora_nav_ctx_t nav_device;
static aurora_nav_ctx_t nav_param;
static aurora_nav_ctx_t nav_realtime;
static aurora_nav_ctx_t nav_fault_monitor;
static aurora_nav_ctx_t nav_history;
static aurora_nav_ctx_t nav_about;

/**********************
 * ANIMATION FUNCTIONS
 **********************/
static void anim_float_cb(void * var, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)var, (lv_coord_t)v);
}

static void start_float_anim(lv_obj_t * obj, lv_coord_t delta_y, uint32_t time, uint32_t delay)
{
    if (!obj) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_coord_t base_y = lv_obj_get_y(obj);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, base_y, (lv_coord_t)(base_y + delta_y));
    lv_anim_set_time(&a, time);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_exec_cb(&a, anim_float_cb);
    lv_anim_set_playback_time(&a, time);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/**********************
 * HELPER FUNCTIONS
 **********************/
static void init_aurora_styles(void)
{
    if (styles_inited) {
        return;
    }

    lv_style_init(&style_glass_panel);
    lv_style_set_bg_color(&style_glass_panel, COL_PANEL);
    lv_style_set_bg_opa(&style_glass_panel, 255);
    lv_style_set_border_width(&style_glass_panel, 0);
    lv_style_set_shadow_width(&style_glass_panel, 0);

    /* Solid white card with subtle border */
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, COL_CARD);
    lv_style_set_bg_opa(&style_card, 255);
    lv_style_set_radius(&style_card, 16);
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_color(&style_card, COL_DIV);
    lv_style_set_border_opa(&style_card, 255);
    lv_style_set_shadow_width(&style_card, 0);

    /* Icon box: transparent base, pastel tint applied per-card inline */
    lv_style_init(&style_icon_box);
    lv_style_set_bg_color(&style_icon_box, COL_ICON_BG);
    lv_style_set_bg_opa(&style_icon_box, 0);
    lv_style_set_radius(&style_icon_box, 16);
    lv_style_set_shadow_width(&style_icon_box, 0);

    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, gui_assets_get_font_20());
    lv_style_set_text_color(&style_title, COL_TEXT);

    styles_inited = true;
}

static lv_obj_t * create_status_item(lv_obj_t * parent, const char * text, lv_obj_t ** dot_out, lv_obj_t ** lbl_out, bool big_dot)
{
    if (!parent) {
        return NULL;
    }
    lv_obj_t * item = lv_obj_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(item, 4, 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * dot = lv_obj_create(item);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, big_dot ? 12 : 10, big_dot ? 12 : 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x999999), 0);
    lv_obj_set_style_bg_opa(dot, 255, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * lbl = lv_label_create(item);
    lv_label_set_text(lbl, text ? text : "");
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl, gui_assets_get_font_16(), 0);

    if (dot_out) *dot_out = dot;
    if (lbl_out) *lbl_out = lbl;
    return item;
}

static void status_set(lv_obj_t *dot, bool ok)
{
    if (!dot || !lv_obj_is_valid(dot)) return;
    lv_obj_set_style_bg_color(dot, ok ? COL_GREEN : lv_color_hex(0x999999), 0);
    lv_obj_set_style_bg_opa(dot, 255, 0);
}

static void aurora_status_timer_cb(lv_timer_t * t)
{
    (void)t;

    /* Screen destroyed -> stop timer */
    if (!ui_AuroraScr || !lv_obj_is_valid(ui_AuroraScr)) {
        if (s_status_timer) {
            lv_timer_del(s_status_timer);
            s_status_timer = NULL;
        }
        return;
    }

    bool wifi_ok = ESP_UI_IsWiFiOk();
    bool tcp_ok  = ESP_UI_IsTcpOk();
    bool reg_ok  = ESP_UI_IsRegOk();
    bool rep_ok  = ESP_UI_IsReporting();

    /* reporting implies all previous steps are ok */
    if (rep_ok) {
        wifi_ok = true;
        tcp_ok = true;
        reg_ok = true;
    }

    status_set(s_dot_wifi, wifi_ok);
    status_set(s_dot_tcp, tcp_ok);
    status_set(s_dot_reg, reg_ok);
    status_set(s_dot_rep, rep_ok);

    if (s_lbl_node && lv_obj_is_valid(s_lbl_node)) {
        const char *id = ESP_UI_NodeId();
        lv_label_set_text_fmt(s_lbl_node, "NODE:%s", id ? id : "--");
    }
}

static void create_aurora_background(lv_obj_t * parent)
{
    /* Removed semi-transparent blobs — solid bg set on screen directly */
    (void)parent;
}

static void update_page_indicator(uint32_t page)
{
    if (!ui_PageIndicator) {
        return;
    }
    uint32_t cnt = lv_obj_get_child_cnt(ui_PageIndicator);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t * dot = lv_obj_get_child(ui_PageIndicator, i);
        if (!dot) {
            continue;
        }
        if (i == page) {
            lv_obj_set_width(dot, 24);
            lv_obj_set_style_bg_color(dot, COL_BLUE, 0);
        } else {
            lv_obj_set_width(dot, 8);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x94A3B8), 0);
        }
    }
}

static void carousel_event_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_SCROLL_END) {
        return;
    }
    lv_obj_t * cont = lv_event_get_target(e);
    if (!cont) {
        return;
    }
    lv_coord_t w = lv_obj_get_width(cont);
    if (w <= 0) {
        return;
    }
    lv_coord_t x = lv_obj_get_scroll_x(cont);
    if (x < 0) {
        x = -x;
    }
    uint32_t page = (uint32_t)((x + (w / 2)) / w);
    if (page >= AURORA_PAGE_COUNT) {
        page = AURORA_PAGE_COUNT - 1;
    }
    update_page_indicator(page);
}

static void aurora_nav_event_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    aurora_nav_ctx_t * ctx = (aurora_nav_ctx_t *)lv_event_get_user_data(e);
    if (!ctx || !ctx->ui) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    EdgeWind_Buzzer_Play(EDGEWIND_BUZZER_EVT_UI_CLICK);

    switch (ctx->target) {
    case AURORA_NAV_REALTIME:
        ui_load_scr_animation(ctx->ui, &ctx->ui->RealtimeMonitor, ctx->ui->RealtimeMonitor_del,
                              &ctx->ui->Main_1_del, setup_scr_RealtimeMonitor,
                              LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
        break;
    case AURORA_NAV_PARAM:
        ui_load_scr_animation(ctx->ui, &ctx->ui->ParamConfig, ctx->ui->ParamConfig_del, &ctx->ui->Main_1_del,
                              setup_scr_ParamConfig, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
        break;
    case AURORA_NAV_WIFI:
        ui_load_scr_animation(ctx->ui, &ctx->ui->WifiConfig, ctx->ui->WifiConfig_del, &ctx->ui->Main_1_del,
                              setup_scr_WifiConfig, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
        break;
    case AURORA_NAV_SERVER:
        ui_load_scr_animation(ctx->ui, &ctx->ui->ServerConfig, ctx->ui->ServerConfig_del, &ctx->ui->Main_1_del,
                              setup_scr_ServerConfig, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
        break;
    case AURORA_NAV_DEVICE:
        ui_load_scr_animation(ctx->ui, &ctx->ui->DeviceConnect, ctx->ui->DeviceConnect_del, &ctx->ui->Main_1_del,
                              setup_scr_DeviceConnect, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
        break;
    case AURORA_NAV_FAULT_MONITOR:
        ui_load_scr_animation(ctx->ui, &ctx->ui->FaultMonitor, ctx->ui->FaultMonitor_del, &ctx->ui->Main_1_del,
                              setup_scr_FaultMonitor, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
        break;
    case AURORA_NAV_HISTORY:
        ui_load_scr_animation(ctx->ui, &ctx->ui->HistoryRecord, ctx->ui->HistoryRecord_del, &ctx->ui->Main_1_del,
                              setup_scr_HistoryRecord, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
        break;
    case AURORA_NAV_ABOUT:
        ui_load_scr_animation(ctx->ui, &ctx->ui->About, ctx->ui->About_del, &ctx->ui->Main_1_del,
                              setup_scr_About, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
        break;
    default:
        break;
    }
}

static lv_obj_t * create_app_card(lv_obj_t * parent, const char * name, uint32_t icon_index,
                                  lv_color_t accent_color, aurora_nav_ctx_t * nav_ctx)
{
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_add_style(card, &style_card, 0);
    lv_obj_set_size(card, 220, 150);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_gap(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    /* Pressed: light blue tint on light bg */
    lv_obj_set_style_bg_color(card, COL_CARD_PR, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(card, 255, LV_STATE_PRESSED);
    /* Accent top strip — 4 px, full width, rounded top by card radius */
    lv_obj_t * strip = lv_obj_create(card);
    lv_obj_remove_style_all(strip);
    lv_obj_set_size(strip, 220, 4);
    lv_obj_set_style_bg_color(strip, accent_color, 0);
    lv_obj_set_style_bg_opa(strip, 255, 0);
    lv_obj_set_style_radius(strip, 0, 0);
    lv_obj_align(strip, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(strip, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * icon_box = lv_obj_create(card);
    lv_obj_remove_style_all(icon_box);
    lv_obj_add_style(icon_box, &style_icon_box, 0);
    /* Tint icon box with per-card accent color for visual depth */
    lv_obj_set_style_bg_color(icon_box, accent_color, 0);
    lv_obj_set_style_bg_opa(icon_box, 45, 0);
    lv_obj_set_size(icon_box, 64, 64);
    lv_obj_set_layout(icon_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_align(icon_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * img = lv_image_create(icon_box);
    gui_assets_set_icon(img, icon_index);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * label = lv_label_create(card);
    lv_obj_add_style(label, &style_title, 0);
    lv_label_set_text(label, name);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);

    if (nav_ctx) {
        lv_obj_add_event_cb(card, aurora_nav_event_cb, LV_EVENT_CLICKED, nav_ctx);
    }

    return card;
}

static lv_obj_t * create_grid_page(lv_obj_t * parent)
{
    lv_obj_t * page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, AURORA_SCREEN_W, AURORA_BODY_H);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_obj_set_style_pad_all(page, 18, 0);
    lv_obj_set_style_pad_row(page, 18, 0);
    lv_obj_set_style_pad_column(page, 18, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

static void fault_monitor_back_cb(lv_event_t * e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (!ui) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    EdgeWind_Buzzer_Play(EDGEWIND_BUZZER_EVT_UI_CLICK);
    ui_load_scr_animation(ui, &ui->Main_1, ui->Main_1_del, &ui->FaultMonitor_del,
                          setup_scr_Aurora, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
}

static lv_obj_t * fault_monitor_create_panel(lv_obj_t *parent, int32_t x, int32_t y,
                                             int32_t w, int32_t h, lv_color_t accent,
                                             const char *title)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_bg_color(panel, COL_CARD, 0);
    lv_obj_set_style_bg_opa(panel, 255, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, COL_DIV, 0);

    lv_obj_t *strip = lv_obj_create(panel);
    lv_obj_remove_style_all(strip);
    lv_obj_set_pos(strip, 0, 0);
    lv_obj_set_size(strip, 5, h);
    lv_obj_set_style_bg_color(strip, accent, 0);
    lv_obj_set_style_bg_opa(strip, 255, 0);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(panel);
    lv_label_set_text(lbl, title ? title : "");
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl, gui_assets_get_font_20(), 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, (lv_coord_t)(w - 32));
    lv_obj_set_pos(lbl, 18, 12);
    return panel;
}

static lv_obj_t * fault_monitor_create_label(lv_obj_t *parent, const char *text,
                                             int32_t x, int32_t y, int32_t w,
                                             lv_color_t color, const lv_font_t *font)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text ? text : "");
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl, (lv_coord_t)w);
    lv_obj_set_pos(lbl, x, y);
    return lbl;
}

static lv_obj_t * fault_monitor_create_metric(lv_obj_t *parent, int32_t x,
                                              lv_color_t accent,
                                              const char *title,
                                              lv_obj_t **value_out,
                                              lv_obj_t **sub_out)
{
    lv_obj_t *card = fault_monitor_create_panel(parent, x, 74, 176, 86, accent, title);
    lv_obj_t *value = fault_monitor_create_label(card, "--", 18, 34, 140, accent, gui_assets_get_font_20());
    lv_obj_t *sub = fault_monitor_create_label(card, "--", 18, 60, 140, COL_TEXT_DIM, gui_assets_get_font_12());
    if (value_out) {
        *value_out = value;
    }
    if (sub_out) {
        *sub_out = sub;
    }
    return card;
}

static lv_obj_t * fault_monitor_create_button(lv_obj_t *parent, int32_t x, const char *text,
                                              lv_color_t color, lv_event_cb_t cb, lv_ui *ui)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, 424);
    lv_obj_set_size(btn, 112, 42);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, 235, 0);
    lv_obj_set_style_radius(btn, 22, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ui);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text ? text : "");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, gui_assets_get_font_20(), 0);
    lv_obj_center(lbl);
    return lbl;
}

static void fault_monitor_set_label(lv_obj_t *lbl, const char *text, lv_color_t color)
{
    if (lbl == NULL) {
        return;
    }
    lv_label_set_text(lbl, text ? text : "--");
    lv_obj_set_style_text_color(lbl, color, 0);
}

static void fault_monitor_refresh(lv_ui *ui)
{
    const char *node;
    const char *fault_code;
    const char *fault_name;
    const char *fault_level;
    const char *fault_advice;
    const char *sd_status;
    bool cloud_ok;
    bool local_fault_active;
    bool sd_error;
    char line[160];

    if (ui == NULL || ui->FaultMonitor == NULL) {
        return;
    }

    node = ESP_UI_NodeId();
    cloud_ok = ESP_UI_IsWiFiOk() && ESP_UI_IsTcpOk() && ESP_UI_IsRegOk();
    fault_code = ESP_UI_FaultCode();
    fault_name = ESP_UI_FaultName();
    fault_level = ESP_UI_FaultLevelText();
    fault_advice = ESP_UI_FaultAdvice();
    sd_status = ESP_UI_FaultLogStatus();
    local_fault_active = (fault_code != NULL && strncmp(fault_code, "E00", 3) != 0);
    sd_error = (sd_status != NULL &&
                (strstr(sd_status, "失败") != NULL ||
                 strstr(sd_status, "未就绪") != NULL ||
                 strstr(sd_status, "丢弃") != NULL));

    lv_label_set_text_fmt(ui->FaultMonitor_lbl_status,
                          "NODE:%s  云端:%s  上报:%s",
                          node ? node : "--",
                          cloud_ok ? "OK" : "--",
                          ESP_UI_IsReporting() ? "ON" : "OFF");

    fault_monitor_set_label(s_fault_metric_value[0], fault_code ? fault_code : "E00",
                            local_fault_active ? COL_RED : COL_GREEN);
    fault_monitor_set_label(s_fault_metric_sub[0], local_fault_active ? fault_name : "本机正常", COL_TEXT_DIM);
    fault_monitor_set_label(s_fault_metric_value[1], fault_level, local_fault_active ? COL_AMBER : COL_GREEN);
    fault_monitor_set_label(s_fault_metric_sub[1], "边缘判定", COL_TEXT_DIM);
    fault_monitor_set_label(s_fault_metric_value[2], cloud_ok ? "OK" : "--", cloud_ok ? COL_GREEN : COL_RED);
    fault_monitor_set_label(s_fault_metric_sub[2], ESP_UI_IsReporting() ? "上报中" : "未上报", COL_TEXT_DIM);
    fault_monitor_set_label(s_fault_metric_value[3], sd_error ? "ERR" : (ESP_UI_FaultLogSdOk() ? "OK" : "WAIT"),
                            sd_error ? COL_RED : (ESP_UI_FaultLogSdOk() ? COL_GREEN : COL_AMBER));
    fault_monitor_set_label(s_fault_metric_sub[3], "SD记录", COL_TEXT_DIM);

    snprintf(line, sizeof(line), "%s  %s", fault_code ? fault_code : "E00", fault_name ? fault_name : "--");
    fault_monitor_set_label(s_fault_detail_title, line, local_fault_active ? COL_RED : COL_GREEN);
    fault_monitor_set_label(s_fault_detail_status,
                            local_fault_active ? "状态：本机检测到故障，按现场规程复核" : "状态：当前未检测到本机故障",
                            local_fault_active ? COL_RED : COL_GREEN);
    snprintf(line, sizeof(line), "等级：%s  来源：STM32边缘AI/阈值判定", fault_level ? fault_level : "--");
    fault_monitor_set_label(s_fault_detail_root, line, COL_TEXT);
    snprintf(line, sizeof(line), "建议：%s", fault_advice ? fault_advice : "--");
    fault_monitor_set_label(s_fault_detail_advice, line, COL_TEXT);
    snprintf(line, sizeof(line), "SD：%s", sd_status ? sd_status : "--");
    fault_monitor_set_label(s_fault_detail_time, line, sd_error ? COL_RED : (ESP_UI_FaultLogSdOk() ? COL_GREEN : COL_AMBER));

    snprintf(line, sizeof(line), "云端连接：%s", cloud_ok ? "正常" : "异常/未注册");
    fault_monitor_set_label(s_fault_event_main[0], line, cloud_ok ? COL_GREEN : COL_RED);
    snprintf(line, sizeof(line), "上报状态：%s  节点：%s",
             ESP_UI_IsReporting() ? "正在上报" : "未开启",
             node ? node : "--");
    fault_monitor_set_label(s_fault_event_sub[0], line, COL_TEXT_DIM);
    fault_monitor_set_label(s_fault_event_main[1], "本机历史：写入SD卡，历史记录页查询", COL_TEXT);
    fault_monitor_set_label(s_fault_event_sub[1], "仅记录 E00/E0X 变化，持续同码不重复写入", COL_TEXT_DIM);
    fault_monitor_set_label(s_fault_event_main[2], "云端工单：由Web端处理", COL_TEXT);
    fault_monitor_set_label(s_fault_event_sub[2], "本页不展示最近列表，避免信息重叠", COL_TEXT_DIM);
    fault_monitor_set_label(s_fault_event_main[3], local_fault_active ? "处置提示：先人工确认，再执行控制动作" : "处置提示：持续监测",
                            local_fault_active ? COL_AMBER : COL_GREEN);
    fault_monitor_set_label(s_fault_event_sub[3], "DeepSeek只作为Web端辅助解释，不进入控制闭环", COL_TEXT_DIM);
}

static void fault_monitor_timer_cb(lv_timer_t *timer)
{
    lv_ui *ui = (lv_ui *)lv_timer_get_user_data(timer);
    if (ui == NULL || ui->FaultMonitor == NULL || !lv_obj_is_valid(ui->FaultMonitor)) {
        lv_timer_delete(timer);
        if (s_fault_monitor_timer == timer) {
            s_fault_monitor_timer = NULL;
        }
        return;
    }
    fault_monitor_refresh(ui);
}

static void fault_monitor_refresh_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    EdgeWind_Buzzer_Play(EDGEWIND_BUZZER_EVT_UI_CLICK);
    fault_monitor_refresh(ui);
}

static void fault_monitor_history_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    if (ui == NULL) {
        return;
    }
    if (s_fault_monitor_timer != NULL) {
        lv_timer_delete(s_fault_monitor_timer);
        s_fault_monitor_timer = NULL;
    }
    lv_indev_wait_release(lv_indev_active());
    EdgeWind_Buzzer_Play(EDGEWIND_BUZZER_EVT_UI_CLICK);
    ui_load_scr_animation(ui, &ui->HistoryRecord, ui->HistoryRecord_del, &ui->FaultMonitor_del,
                          setup_scr_HistoryRecord, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
}

void setup_scr_FaultMonitor(lv_ui *ui)
{
    if (!ui) {
        return;
    }
    if (s_fault_monitor_timer != NULL) {
        lv_timer_delete(s_fault_monitor_timer);
        s_fault_monitor_timer = NULL;
    }

    ui->FaultMonitor = lv_obj_create(NULL);
    lv_obj_remove_style_all(ui->FaultMonitor);
    lv_obj_set_size(ui->FaultMonitor, AURORA_SCREEN_W, AURORA_SCREEN_H);
    lv_obj_clear_flag(ui->FaultMonitor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ui->FaultMonitor, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(ui->FaultMonitor, COL_BG, 0);
    lv_obj_set_style_bg_grad_color(ui->FaultMonitor, COL_BG_BOT, 0);
    lv_obj_set_style_bg_grad_dir(ui->FaultMonitor, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(ui->FaultMonitor, 255, 0);

    memset(s_fault_metric_value, 0, sizeof(s_fault_metric_value));
    memset(s_fault_metric_sub, 0, sizeof(s_fault_metric_sub));
    memset(s_fault_event_main, 0, sizeof(s_fault_event_main));
    memset(s_fault_event_sub, 0, sizeof(s_fault_event_sub));
    s_fault_detail_title = NULL;
    s_fault_detail_status = NULL;
    s_fault_detail_root = NULL;
    s_fault_detail_advice = NULL;
    s_fault_detail_time = NULL;

    lv_obj_t *header = lv_obj_create(ui->FaultMonitor);
    lv_obj_remove_style_all(header);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, AURORA_SCREEN_W, AURORA_HEADER_H);
    lv_obj_set_style_bg_color(header, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(header, 255, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    ui->FaultMonitor_lbl_title = lv_label_create(header);
    lv_label_set_text(ui->FaultMonitor_lbl_title, "故障监测");
    lv_obj_set_style_text_color(ui->FaultMonitor_lbl_title, COL_TITLE_HL, 0);
    lv_obj_set_style_text_font(ui->FaultMonitor_lbl_title, gui_assets_get_font_30(), 0);
    lv_obj_align(ui->FaultMonitor_lbl_title, LV_ALIGN_LEFT_MID, 28, 0);

    const char *node = ESP_UI_NodeId();
    ui->FaultMonitor_lbl_status = lv_label_create(header);
    lv_label_set_text_fmt(ui->FaultMonitor_lbl_status, "NODE:%s  云端:%s  上报:%s",
                          node ? node : "--",
                          (ESP_UI_IsTcpOk() && ESP_UI_IsRegOk()) ? "OK" : "--",
                          ESP_UI_IsReporting() ? "ON" : "OFF");
    lv_obj_set_style_text_color(ui->FaultMonitor_lbl_status, COL_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ui->FaultMonitor_lbl_status, gui_assets_get_font_16(), 0);
    lv_label_set_long_mode(ui->FaultMonitor_lbl_status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ui->FaultMonitor_lbl_status, 500);
    lv_obj_align(ui->FaultMonitor_lbl_status, LV_ALIGN_RIGHT_MID, -24, 0);

    fault_monitor_create_metric(ui->FaultMonitor, 24, COL_RED, "本机故障码",
                                &s_fault_metric_value[0], &s_fault_metric_sub[0]);
    fault_monitor_create_metric(ui->FaultMonitor, 216, COL_BLUE, "云同步",
                                &s_fault_metric_value[1], &s_fault_metric_sub[1]);
    fault_monitor_create_metric(ui->FaultMonitor, 408, COL_AMBER, "云端/上报",
                                &s_fault_metric_value[2], &s_fault_metric_sub[2]);
    fault_monitor_create_metric(ui->FaultMonitor, 600, COL_GREEN, "SD记录",
                                &s_fault_metric_value[3], &s_fault_metric_sub[3]);

    lv_obj_t *current = fault_monitor_create_panel(ui->FaultMonitor, 24, 176, 360, 234, COL_RED, "当前本机状态");
    s_fault_detail_title = fault_monitor_create_label(current, "--", 18, 46, 320, COL_TEXT, gui_assets_get_font_20());
    s_fault_detail_status = fault_monitor_create_label(current, "--", 18, 82, 320, COL_TEXT_DIM, gui_assets_get_font_16());
    s_fault_detail_root = fault_monitor_create_label(current, "--", 18, 116, 320, COL_TEXT, gui_assets_get_font_14());
    s_fault_detail_advice = fault_monitor_create_label(current, "--", 18, 150, 320, COL_TEXT, gui_assets_get_font_14());
    s_fault_detail_time = fault_monitor_create_label(current, "--", 18, 190, 320, COL_TEXT_DIM, gui_assets_get_font_12());
    ui->FaultMonitor_lbl_current = s_fault_detail_title;
    ui->FaultMonitor_lbl_sync = s_fault_detail_status;

    lv_obj_t *status = fault_monitor_create_panel(ui->FaultMonitor, 408, 176, 368, 234, COL_BLUE, "记录与同步");
    for (uint8_t i = 0U; i < FAULT_MONITOR_EVENT_ROWS; ++i) {
        s_fault_event_main[i] = fault_monitor_create_label(status, "--", 18, (int32_t)(44 + i * 44), 332,
                                                           COL_TEXT, gui_assets_get_font_14());
        s_fault_event_sub[i] = fault_monitor_create_label(status, "--", 18, (int32_t)(64 + i * 44), 332,
                                                          COL_TEXT_DIM, gui_assets_get_font_12());
    }
    ui->FaultMonitor_lbl_events[0] = s_fault_event_main[0];
    ui->FaultMonitor_lbl_events[1] = s_fault_event_main[1];
    ui->FaultMonitor_lbl_events[2] = s_fault_event_main[2];
    ui->FaultMonitor_lbl_events[3] = s_fault_event_main[3];
    ui->FaultMonitor_lbl_events[4] = s_fault_event_sub[0];

    fault_monitor_create_button(ui->FaultMonitor, 400, "刷新", COL_GREEN, fault_monitor_refresh_cb, ui);
    fault_monitor_create_button(ui->FaultMonitor, 524, "查看历史", COL_AMBER, fault_monitor_history_cb, ui);
    ui->FaultMonitor_lbl_back = fault_monitor_create_button(ui->FaultMonitor, 648, "返回", COL_BLUE, fault_monitor_back_cb, ui);
    ui->FaultMonitor_btn_back = lv_obj_get_parent(ui->FaultMonitor_lbl_back);

    lv_obj_update_layout(ui->FaultMonitor);
    fault_monitor_refresh(ui);
    s_fault_monitor_timer = lv_timer_create(fault_monitor_timer_cb, 1000, ui);
}

static const char *history_record_level_text(uint8_t level)
{
    switch (level) {
    case 0U: return "恢复";
    case 1U: return "低";
    case 2U: return "中";
    case 3U: return "高";
    default: return "--";
    }
}

static lv_color_t history_record_level_color(uint8_t level)
{
    switch (level) {
    case 0U: return COL_GREEN;
    case 1U: return COL_CYAN;
    case 2U: return COL_AMBER;
    case 3U: return COL_RED;
    default: return COL_TEXT_DIM;
    }
}

static void history_record_code_text(uint8_t code, char *buf, size_t len)
{
    if (buf == NULL || len == 0U) {
        return;
    }
    (void)snprintf(buf, len, "E%02u", (unsigned int)code);
}

static void history_record_time_text(uint32_t timestamp, char *buf, size_t len)
{
    uint32_t sec;
    if (buf == NULL || len == 0U) {
        return;
    }
    if (timestamp == 0U) {
        (void)snprintf(buf, len, "--:--:--");
        return;
    }
    sec = timestamp % 86400U;
    (void)snprintf(buf, len, "%02lu:%02lu:%02lu",
                   (unsigned long)(sec / 3600U),
                   (unsigned long)((sec / 60U) % 60U),
                   (unsigned long)(sec % 60U));
}

static bool history_record_parse_date(const char *date, int *year, int *month, int *day)
{
    if (date == NULL || year == NULL || month == NULL || day == NULL) {
        return false;
    }
    return (sscanf(date, "%d-%d-%d", year, month, day) == 3 &&
            *year >= 2000 && *month >= 1 && *month <= 12 && *day >= 1 && *day <= 31);
}

static bool history_record_is_leap(int year)
{
    return (((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0));
}

static int history_record_days_in_month(int year, int month)
{
    static const int days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (month < 1 || month > 12) {
        return 30;
    }
    if (month == 2 && history_record_is_leap(year)) {
        return 29;
    }
    return days[month - 1];
}

static void history_record_set_today(void)
{
    if (!SD_Time_GetDate(s_history_date, sizeof(s_history_date))) {
        (void)snprintf(s_history_date, sizeof(s_history_date), "2026-01-01");
    }
}

static void history_record_shift_date(int delta)
{
    int y, m, d;
    if (!history_record_parse_date(s_history_date, &y, &m, &d)) {
        history_record_set_today();
        (void)history_record_parse_date(s_history_date, &y, &m, &d);
    }
    d += delta;
    while (d < 1) {
        m--;
        if (m < 1) {
            m = 12;
            y--;
        }
        d += history_record_days_in_month(y, m);
    }
    while (d > history_record_days_in_month(y, m)) {
        d -= history_record_days_in_month(y, m);
        m++;
        if (m > 12) {
            m = 1;
            y++;
        }
    }
    (void)snprintf(s_history_date, sizeof(s_history_date), "%04d-%02d-%02d", y, m, d);
}

static bool history_record_load(char *status, size_t status_len)
{
    bool ok = false;
    uint32_t count = 0U;
    if (status != NULL && status_len > 0U) {
        status[0] = '\0';
    }

    if (!EdgeWind_SD_Lock(800U)) {
        if (status != NULL) {
            (void)snprintf(status, status_len, "SD忙，稍后刷新");
        }
        s_history_count = 0U;
        return false;
    }
    if (s_history_recent_mode != 0U) {
        ok = SD_Fault_GetRecent(s_history_entries, HISTORY_RECORD_MAX, &count);
    } else {
        ok = SD_Fault_GetByDate(s_history_date, s_history_entries, HISTORY_RECORD_MAX, &count);
    }
    EdgeWind_SD_Unlock();

    s_history_count = ok ? count : 0U;
    if (s_history_selected >= s_history_count) {
        s_history_selected = 0U;
    }
    if (status != NULL) {
        if (!ok) {
            if (s_history_recent_mode != 0U) {
                (void)snprintf(status, status_len, "SD不可用或本月无记录");
            } else {
                (void)snprintf(status, status_len, "%s 无fault.log", s_history_date);
            }
        } else if (count == 0U) {
            if (s_history_recent_mode != 0U) {
                (void)snprintf(status, status_len, "最近暂无故障记录");
            } else {
                (void)snprintf(status, status_len, "%s 暂无记录", s_history_date);
            }
        } else if (s_history_recent_mode != 0U) {
            (void)snprintf(status, status_len, "最近记录 %lu 条", (unsigned long)count);
        } else {
            (void)snprintf(status, status_len, "%s 记录 %lu 条", s_history_date, (unsigned long)count);
        }
    }
    return ok;
}

static void history_record_show_detail(void)
{
    char code[8];
    char time_txt[16];
    char line[180];
    const FaultEntry_t *entry;

    if (s_history_detail_title == NULL) {
        return;
    }
    if (s_history_count == 0U) {
        fault_monitor_set_label(s_history_detail_title, "暂无故障记录", COL_TEXT_DIM);
        fault_monitor_set_label(s_history_detail_status, "请选择最近/今天或切换日期查询", COL_TEXT_DIM);
        fault_monitor_set_label(s_history_detail_msg, "SD卡无记录、未插卡或当天文件不存在时会显示为空状态。", COL_TEXT_DIM);
        fault_monitor_set_label(s_history_detail_src, "来源：0:/data/YYYY-MM-DD/fault.log / 0:/logs/event_YYYY-MM.log", COL_TEXT_DIM);
        return;
    }

    entry = &s_history_entries[s_history_selected];
    history_record_code_text(entry->code, code, sizeof(code));
    history_record_time_text(entry->timestamp, time_txt, sizeof(time_txt));
    (void)snprintf(line, sizeof(line), "%s  %s", code, history_record_level_text(entry->level));
    fault_monitor_set_label(s_history_detail_title, line, history_record_level_color(entry->level));
    (void)snprintf(line, sizeof(line), "时间：%s  ts:%lu",
                   time_txt, (unsigned long)entry->timestamp);
    fault_monitor_set_label(s_history_detail_status, line, COL_TEXT_DIM);
    fault_monitor_set_label(s_history_detail_msg, entry->message[0] ? entry->message : "记录无详细消息", COL_TEXT);
    (void)snprintf(line, sizeof(line), "来源：%s",
                   s_history_recent_mode ? "0:/logs/event_YYYY-MM.log" : "0:/data/YYYY-MM-DD/fault.log");
    fault_monitor_set_label(s_history_detail_src, line, COL_TEXT_DIM);
}

static void history_record_refresh(lv_ui *ui)
{
    char status[96];
    char code[8];
    char time_txt[16];
    char line[160];
    (void)ui;

    (void)history_record_load(status, sizeof(status));
    if (s_history_status != NULL) {
        fault_monitor_set_label(s_history_status, status, s_history_count > 0U ? COL_GREEN : COL_AMBER);
    }
    for (uint8_t i = 0U; i < HISTORY_RECORD_ROWS; ++i) {
        bool active = (i < s_history_count);
        bool selected = (active && i == s_history_selected);
        if (s_history_row[i] != NULL) {
            lv_obj_set_style_bg_color(s_history_row[i],
                                      selected ? lv_color_hex(0xE0F2FE) : lv_color_hex(0xF8FAFC), 0);
            lv_obj_set_style_border_color(s_history_row[i], selected ? COL_BLUE : COL_DIV, 0);
            lv_obj_set_style_border_width(s_history_row[i], selected ? 2 : 1, 0);
        }
        if (active) {
            const FaultEntry_t *entry = &s_history_entries[i];
            history_record_code_text(entry->code, code, sizeof(code));
            history_record_time_text(entry->timestamp, time_txt, sizeof(time_txt));
            (void)snprintf(line, sizeof(line), "%s  %s  %s",
                           time_txt, code, history_record_level_text(entry->level));
            fault_monitor_set_label(s_history_row_main[i], line, history_record_level_color(entry->level));
            fault_monitor_set_label(s_history_row_sub[i], entry->message[0] ? entry->message : "--", COL_TEXT_DIM);
        } else {
            fault_monitor_set_label(s_history_row_main[i], i == 0U ? "无记录" : "--", COL_TEXT_DIM);
            fault_monitor_set_label(s_history_row_sub[i], i == 0U ? "切换日期或等待故障变化写入SD" : "", COL_TEXT_DIM);
        }
    }
    history_record_show_detail();
}

static void history_record_row_cb(lv_event_t *e)
{
    uint8_t index;
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    index = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (index >= s_history_count) {
        return;
    }
    s_history_selected = index;
    EdgeWind_Buzzer_Play(EDGEWIND_BUZZER_EVT_UI_CLICK);
    history_record_show_detail();
    for (uint8_t i = 0U; i < HISTORY_RECORD_ROWS; ++i) {
        if (s_history_row[i] != NULL) {
            bool selected = (i == s_history_selected && i < s_history_count);
            lv_obj_set_style_bg_color(s_history_row[i],
                                      selected ? lv_color_hex(0xE0F2FE) : lv_color_hex(0xF8FAFC), 0);
            lv_obj_set_style_border_color(s_history_row[i], selected ? COL_BLUE : COL_DIV, 0);
            lv_obj_set_style_border_width(s_history_row[i], selected ? 2 : 1, 0);
        }
    }
}

static void history_record_recent_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    s_history_recent_mode = 1U;
    s_history_selected = 0U;
    EdgeWind_Buzzer_Play(EDGEWIND_BUZZER_EVT_UI_CLICK);
    history_record_refresh((lv_ui *)lv_event_get_user_data(e));
}

static void history_record_today_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    history_record_set_today();
    s_history_recent_mode = 0U;
    s_history_selected = 0U;
    EdgeWind_Buzzer_Play(EDGEWIND_BUZZER_EVT_UI_CLICK);
    history_record_refresh((lv_ui *)lv_event_get_user_data(e));
}

static void history_record_prev_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_history_recent_mode != 0U) {
        history_record_set_today();
    }
    s_history_recent_mode = 0U;
    s_history_selected = 0U;
    history_record_shift_date(-1);
    EdgeWind_Buzzer_Play(EDGEWIND_BUZZER_EVT_UI_CLICK);
    history_record_refresh((lv_ui *)lv_event_get_user_data(e));
}

static void history_record_next_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_history_recent_mode != 0U) {
        history_record_set_today();
    }
    s_history_recent_mode = 0U;
    s_history_selected = 0U;
    history_record_shift_date(1);
    EdgeWind_Buzzer_Play(EDGEWIND_BUZZER_EVT_UI_CLICK);
    history_record_refresh((lv_ui *)lv_event_get_user_data(e));
}

static void history_record_refresh_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    EdgeWind_Buzzer_Play(EDGEWIND_BUZZER_EVT_UI_CLICK);
    history_record_refresh((lv_ui *)lv_event_get_user_data(e));
}

static void history_record_back_cb(lv_event_t *e)
{
    lv_ui *ui;
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    ui = (lv_ui *)lv_event_get_user_data(e);
    if (ui == NULL) {
        return;
    }
    lv_indev_wait_release(lv_indev_active());
    EdgeWind_Buzzer_Play(EDGEWIND_BUZZER_EVT_UI_CLICK);
    ui_load_scr_animation(ui, &ui->Main_1, ui->Main_1_del, &ui->HistoryRecord_del,
                          setup_scr_Aurora, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
}

void setup_scr_HistoryRecord(lv_ui *ui)
{
    lv_obj_t *header;
    lv_obj_t *list;
    lv_obj_t *detail;
    if (ui == NULL) {
        return;
    }

    ui->HistoryRecord = lv_obj_create(NULL);
    lv_obj_remove_style_all(ui->HistoryRecord);
    lv_obj_set_size(ui->HistoryRecord, AURORA_SCREEN_W, AURORA_SCREEN_H);
    lv_obj_clear_flag(ui->HistoryRecord, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ui->HistoryRecord, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(ui->HistoryRecord, COL_BG, 0);
    lv_obj_set_style_bg_grad_color(ui->HistoryRecord, COL_BG_BOT, 0);
    lv_obj_set_style_bg_grad_dir(ui->HistoryRecord, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(ui->HistoryRecord, 255, 0);

    memset(s_history_row, 0, sizeof(s_history_row));
    memset(s_history_row_main, 0, sizeof(s_history_row_main));
    memset(s_history_row_sub, 0, sizeof(s_history_row_sub));
    s_history_status = NULL;
    s_history_detail_title = NULL;
    s_history_detail_status = NULL;
    s_history_detail_msg = NULL;
    s_history_detail_src = NULL;
    s_history_recent_mode = 1U;
    s_history_selected = 0U;
    if (s_history_date[0] == '-' || s_history_date[0] == '\0') {
        history_record_set_today();
    }

    header = lv_obj_create(ui->HistoryRecord);
    lv_obj_remove_style_all(header);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, AURORA_SCREEN_W, AURORA_HEADER_H);
    lv_obj_set_style_bg_color(header, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(header, 255, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    ui->HistoryRecord_lbl_title = lv_label_create(header);
    lv_label_set_text(ui->HistoryRecord_lbl_title, "历史记录");
    lv_obj_set_style_text_color(ui->HistoryRecord_lbl_title, COL_TITLE_HL, 0);
    lv_obj_set_style_text_font(ui->HistoryRecord_lbl_title, gui_assets_get_font_30(), 0);
    lv_obj_align(ui->HistoryRecord_lbl_title, LV_ALIGN_LEFT_MID, 28, 0);

    ui->HistoryRecord_lbl_status = lv_label_create(header);
    lv_label_set_text(ui->HistoryRecord_lbl_status, "SD故障档案");
    lv_obj_set_style_text_color(ui->HistoryRecord_lbl_status, COL_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ui->HistoryRecord_lbl_status, gui_assets_get_font_16(), 0);
    lv_label_set_long_mode(ui->HistoryRecord_lbl_status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ui->HistoryRecord_lbl_status, 420);
    lv_obj_align(ui->HistoryRecord_lbl_status, LV_ALIGN_RIGHT_MID, -24, 0);
    s_history_status = ui->HistoryRecord_lbl_status;

    list = fault_monitor_create_panel(ui->HistoryRecord, 24, 82, 430, 328, COL_GREEN, "本机故障档案");
    for (uint8_t i = 0U; i < HISTORY_RECORD_ROWS; ++i) {
        s_history_row[i] = lv_obj_create(list);
        lv_obj_remove_style_all(s_history_row[i]);
        lv_obj_set_pos(s_history_row[i], 16, (lv_coord_t)(42 + i * 54));
        lv_obj_set_size(s_history_row[i], 398, 48);
        lv_obj_set_style_radius(s_history_row[i], 10, 0);
        lv_obj_set_style_bg_color(s_history_row[i], lv_color_hex(0xF8FAFC), 0);
        lv_obj_set_style_bg_opa(s_history_row[i], 255, 0);
        lv_obj_set_style_border_width(s_history_row[i], 1, 0);
        lv_obj_set_style_border_color(s_history_row[i], COL_DIV, 0);
        lv_obj_add_flag(s_history_row[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(s_history_row[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_history_row[i], history_record_row_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        s_history_row_main[i] = fault_monitor_create_label(s_history_row[i], "--", 12, 5, 374, COL_TEXT,
                                                           gui_assets_get_font_16());
        s_history_row_sub[i] = fault_monitor_create_label(s_history_row[i], "--", 12, 26, 374, COL_TEXT_DIM,
                                                          gui_assets_get_font_12());
    }
    ui->HistoryRecord_lbl_rows[0] = s_history_row_main[0];
    ui->HistoryRecord_lbl_rows[1] = s_history_row_main[1];
    ui->HistoryRecord_lbl_rows[2] = s_history_row_main[2];
    ui->HistoryRecord_lbl_rows[3] = s_history_row_main[3];
    ui->HistoryRecord_lbl_rows[4] = s_history_row_main[4];

    detail = fault_monitor_create_panel(ui->HistoryRecord, 470, 82, 306, 328, COL_BLUE, "记录详情");
    s_history_detail_title = fault_monitor_create_label(detail, "--", 18, 46, 270, COL_TEXT, gui_assets_get_font_20());
    s_history_detail_status = fault_monitor_create_label(detail, "--", 18, 84, 270, COL_TEXT_DIM, gui_assets_get_font_14());
    s_history_detail_msg = fault_monitor_create_label(detail, "--", 18, 122, 270, COL_TEXT, gui_assets_get_font_14());
    lv_label_set_long_mode(s_history_detail_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(s_history_detail_msg, 96);
    s_history_detail_src = fault_monitor_create_label(detail, "--", 18, 242, 270, COL_TEXT_DIM, gui_assets_get_font_12());
    ui->HistoryRecord_lbl_detail = s_history_detail_msg;

    fault_monitor_create_button(ui->HistoryRecord, 24, "最近", COL_GREEN, history_record_recent_cb, ui);
    fault_monitor_create_button(ui->HistoryRecord, 148, "今天", COL_BLUE, history_record_today_cb, ui);
    fault_monitor_create_button(ui->HistoryRecord, 272, "前一天", COL_AMBER, history_record_prev_cb, ui);
    fault_monitor_create_button(ui->HistoryRecord, 396, "后一天", COL_AMBER, history_record_next_cb, ui);
    fault_monitor_create_button(ui->HistoryRecord, 520, "刷新", COL_GREEN, history_record_refresh_cb, ui);
    ui->HistoryRecord_lbl_back = fault_monitor_create_button(ui->HistoryRecord, 644, "返回", COL_BLUE, history_record_back_cb, ui);
    ui->HistoryRecord_btn_back = lv_obj_get_parent(ui->HistoryRecord_lbl_back);

    lv_obj_update_layout(ui->HistoryRecord);
    history_record_refresh(ui);
}

/**********************
 * PUBLIC FUNCTION
 **********************/
void setup_scr_Aurora(lv_ui * ui)
{
    if (!ui) {
        return;
    }

    init_aurora_styles();

    nav_wifi.ui = ui;
    nav_wifi.target = AURORA_NAV_WIFI;
    nav_server.ui = ui;
    nav_server.target = AURORA_NAV_SERVER;
    nav_device.ui = ui;
    nav_device.target = AURORA_NAV_DEVICE;
    nav_param.ui = ui;
    nav_param.target = AURORA_NAV_PARAM;
    nav_realtime.ui = ui;
    nav_realtime.target = AURORA_NAV_REALTIME;
    nav_fault_monitor.ui = ui;
    nav_fault_monitor.target = AURORA_NAV_FAULT_MONITOR;
    nav_history.ui = ui;
    nav_history.target = AURORA_NAV_HISTORY;
    nav_about.ui = ui;
    nav_about.target = AURORA_NAV_ABOUT;

    ui->Main_1 = lv_obj_create(NULL);
    ui_AuroraScr = ui->Main_1;

    lv_obj_remove_style_all(ui_AuroraScr);
    lv_obj_set_size(ui_AuroraScr, AURORA_SCREEN_W, AURORA_SCREEN_H);
    lv_obj_set_style_bg_color(ui_AuroraScr, COL_BG, 0);
    lv_obj_set_style_bg_grad_color(ui_AuroraScr, COL_BG_BOT, 0);
    lv_obj_set_style_bg_grad_dir(ui_AuroraScr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(ui_AuroraScr, 255, 0);
    lv_obj_set_scrollbar_mode(ui_AuroraScr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui_AuroraScr, LV_OBJ_FLAG_SCROLLABLE);

    create_aurora_background(ui_AuroraScr);

    lv_obj_t * header = lv_obj_create(ui_AuroraScr);
    lv_obj_add_style(header, &style_glass_panel, 0);
    lv_obj_set_size(header, AURORA_SCREEN_W, AURORA_HEADER_H);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(header, 20, 0);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * title_box = lv_obj_create(header);
    lv_obj_remove_style_all(title_box);
    lv_obj_set_size(title_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(title_box, 10, 0);

    lv_obj_t * bar = lv_obj_create(title_box);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 4, 20);
    lv_obj_set_style_bg_color(bar, COL_TITLE_HL, 0);
    lv_obj_set_style_radius(bar, 2, 0);

    lv_obj_t * title_lbl = lv_label_create(title_box);
    lv_label_set_text(title_lbl, "WindSight 智风监测");
    lv_obj_set_style_text_color(title_lbl, COL_TITLE_HL, 0);
    lv_obj_set_style_text_font(title_lbl, gui_assets_get_font_30(), 0);

    lv_obj_t * status_pill = lv_obj_create(header);
    lv_obj_remove_style_all(status_pill);
    /* Two-line pill: Row1(WIFI/TCP/REG/REP) + Row2(NODE:xx) */
    lv_obj_set_size(status_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(status_pill, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(status_pill, 255, 0);
    lv_obj_set_style_radius(status_pill, 18, 0);
    lv_obj_set_style_border_width(status_pill, 1, 0);
    lv_obj_set_style_border_color(status_pill, COL_DIV, 0);
    lv_obj_set_style_border_opa(status_pill, 255, 0);
    lv_obj_set_flex_flow(status_pill, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(status_pill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(status_pill, 10, 0);
    lv_obj_set_style_pad_ver(status_pill, 4, 0);
    lv_obj_set_style_pad_gap(status_pill, 4, 0);
    lv_obj_clear_flag(status_pill, LV_OBJ_FLAG_SCROLLABLE);

    /* Row 1: WIFI / TCP / REG / REP */
    lv_obj_t * row1 = lv_obj_create(status_pill);
    lv_obj_remove_style_all(row1);
    lv_obj_set_size(row1, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(row1, 10, 0);
    lv_obj_clear_flag(row1, LV_OBJ_FLAG_SCROLLABLE);

    (void)create_status_item(row1, "WIFI", &s_dot_wifi, &s_lbl_wifi, true /* bigger */);
    (void)create_status_item(row1, "TCP",  &s_dot_tcp,  &s_lbl_tcp,  false);
    (void)create_status_item(row1, "REG",  &s_dot_reg,  &s_lbl_reg,  false);
    (void)create_status_item(row1, "REP",  &s_dot_rep,  &s_lbl_rep,  false);

    /* Row 2: NODE */
    lv_obj_t * row2 = lv_obj_create(status_pill);
    lv_obj_remove_style_all(row2);
    lv_obj_set_size(row2, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row2, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * node_lbl = lv_label_create(row2);
    lv_label_set_text(node_lbl, "NODE:--");
    lv_obj_set_style_text_color(node_lbl, COL_TEXT_DIM, 0);
    lv_obj_set_style_text_font(node_lbl, gui_assets_get_font_16(), 0);
    s_lbl_node = node_lbl;

    /* start timer refresh */
    if (s_status_timer) {
        lv_timer_del(s_status_timer);
        s_status_timer = NULL;
    }
    aurora_status_timer_cb(NULL);
    s_status_timer = lv_timer_create(aurora_status_timer_cb, 500, NULL);

    ui_Carousel = lv_obj_create(ui_AuroraScr);
    lv_obj_remove_style_all(ui_Carousel);
    lv_obj_set_size(ui_Carousel, AURORA_SCREEN_W, AURORA_BODY_H);
    lv_obj_align(ui_Carousel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(ui_Carousel, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(ui_Carousel, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(ui_Carousel, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(ui_Carousel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(ui_Carousel, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_clear_flag(ui_Carousel, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_add_event_cb(ui_Carousel, carousel_event_cb, LV_EVENT_SCROLL_END, NULL);

    lv_obj_t * p1 = create_grid_page(ui_Carousel);
    create_app_card(p1, "实时监控", 0, COL_BLUE, &nav_realtime);
    create_app_card(p1, "故障监测", 1, COL_RED, &nav_fault_monitor);
    create_app_card(p1, "数据分析", 2, COL_PURPLE, NULL);
    create_app_card(p1, "历史记录", 3, COL_GREEN, &nav_history);
    create_app_card(p1, "日志查看", 4, COL_CYAN, NULL);
    create_app_card(p1, "报警设置", 5, COL_ORANGE, NULL);

    lv_obj_t * p2 = create_grid_page(ui_Carousel);
    create_app_card(p2, "通讯参数配置", 6, COL_INDIGO, &nav_param);
    create_app_card(p2, "网络设置", 7, COL_BLUE, &nav_wifi);
    create_app_card(p2, "服务器", 8, COL_CYAN, &nav_server);
    create_app_card(p2, "系统诊断", 9, COL_AMBER, NULL);
    create_app_card(p2, "设备连接", 10, COL_GREEN, &nav_device);
    create_app_card(p2, "用户管理", 11, COL_PINK, NULL);

    lv_obj_t * p3 = create_grid_page(ui_Carousel);
    create_app_card(p3, "固件升级", 12, COL_TEAL, NULL);
    create_app_card(p3, "关于系统", 13, COL_BLUE, &nav_about);

    ui_PageIndicator = lv_obj_create(ui_AuroraScr);
    lv_obj_remove_style_all(ui_PageIndicator);
    lv_obj_set_size(ui_PageIndicator, 200, 20);
    lv_obj_align(ui_PageIndicator, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_flex_flow(ui_PageIndicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_PageIndicator, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(ui_PageIndicator, 10, 0);
    lv_obj_clear_flag(ui_PageIndicator, LV_OBJ_FLAG_SCROLLABLE);

    for (uint32_t i = 0; i < AURORA_PAGE_COUNT; i++) {
        lv_obj_t * dot = lv_obj_create(ui_PageIndicator);
        lv_obj_remove_style_all(dot);
        lv_obj_set_height(dot, 8);
        lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_bg_opa(dot, 255, 0);
        if (i == 0) {
            lv_obj_set_width(dot, 24);
            lv_obj_set_style_bg_color(dot, COL_BLUE, 0);
        } else {
            lv_obj_set_width(dot, 8);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x94A3B8), 0);
        }
    }

    edgewind_ui_attach_buzzer_tree(ui->Main_1);
}
