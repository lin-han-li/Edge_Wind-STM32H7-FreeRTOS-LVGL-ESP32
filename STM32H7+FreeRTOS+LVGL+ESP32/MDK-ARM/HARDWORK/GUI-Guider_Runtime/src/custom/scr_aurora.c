/**
 * @file scr_aurora.c
 * @brief Aurora glassmorphism main screen for LVGL 9.4
 */

#include "scr_aurora.h"
#include <stdbool.h>
#include <stddef.h>
#include "../../gui_assets.h"
#include "../../../ESP8266/esp8266.h"

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

/* Tailwind-like palette */
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

typedef enum {
    AURORA_NAV_NONE = 0,
    AURORA_NAV_PARAM,
    AURORA_NAV_WIFI,
    AURORA_NAV_SERVER,
    AURORA_NAV_DEVICE
} aurora_nav_target_t;

typedef struct {
    lv_ui * ui;
    aurora_nav_target_t target;
} aurora_nav_ctx_t;

static aurora_nav_ctx_t nav_wifi;
static aurora_nav_ctx_t nav_server;
static aurora_nav_ctx_t nav_device;
static aurora_nav_ctx_t nav_param;

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
    lv_style_set_bg_color(&style_glass_panel, lv_color_white());
    lv_style_set_bg_opa(&style_glass_panel, 180);
    lv_style_set_border_width(&style_glass_panel, 1);
    lv_style_set_border_color(&style_glass_panel, lv_color_white());
#if EW_AURORA_USE_SHADOW
    lv_style_set_shadow_width(&style_glass_panel, 8);
    lv_style_set_shadow_color(&style_glass_panel, lv_color_make(0, 0, 0));
    lv_style_set_shadow_opa(&style_glass_panel, 20);
#else
    lv_style_set_shadow_width(&style_glass_panel, 0);
#endif

    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, lv_color_white());
    lv_style_set_bg_opa(&style_card, 200);
    lv_style_set_radius(&style_card, 16);
    lv_style_set_border_width(&style_card, 1);
    lv_style_set_border_color(&style_card, lv_color_white());
#if EW_AURORA_USE_SHADOW
    lv_style_set_shadow_width(&style_card, 16);
    lv_style_set_shadow_offset_y(&style_card, 6);
    lv_style_set_shadow_color(&style_card, lv_color_make(0, 0, 0));
    lv_style_set_shadow_opa(&style_card, 30);
#else
    lv_style_set_shadow_width(&style_card, 0);
#endif

    lv_style_init(&style_icon_box);
    lv_style_set_bg_color(&style_icon_box, lv_color_white());
    lv_style_set_bg_opa(&style_icon_box, 255);
    lv_style_set_radius(&style_icon_box, 14);
#if EW_AURORA_USE_SHADOW
    lv_style_set_shadow_width(&style_icon_box, 10);
    lv_style_set_shadow_color(&style_icon_box, lv_color_hex(0xE2E8F0));
    lv_style_set_shadow_opa(&style_icon_box, 140);
#else
    lv_style_set_shadow_width(&style_icon_box, 0);
#endif

    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, gui_assets_get_font_20());
    lv_style_set_text_color(&style_title, lv_color_hex(0x334155));

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
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x334155), 0);
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
    lv_obj_t * blob1 = lv_obj_create(parent);
    lv_obj_remove_style_all(blob1);
    lv_obj_set_size(blob1, 420, 420);
    lv_obj_set_style_bg_color(blob1, COL_BLUE, 0);
    lv_obj_set_style_bg_opa(blob1, 80, 0);
#if EW_AURORA_USE_GRADIENT
    lv_obj_set_style_bg_grad_color(blob1, lv_color_white(), 0);
    lv_obj_set_style_bg_grad_dir(blob1, LV_GRAD_DIR_VER, 0);
#endif
    lv_obj_set_style_radius(blob1, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(blob1, LV_ALIGN_TOP_LEFT, -140, -140);
    lv_obj_clear_flag(blob1, LV_OBJ_FLAG_SCROLLABLE);
#if EW_AURORA_BG_ANIM
    start_float_anim(blob1, 18, 7000, 0);
#endif

    lv_obj_t * blob2 = lv_obj_create(parent);
    lv_obj_remove_style_all(blob2);
    lv_obj_set_size(blob2, 340, 340);
    lv_obj_set_style_bg_color(blob2, COL_PINK, 0);
    lv_obj_set_style_bg_opa(blob2, 70, 0);
    lv_obj_set_style_radius(blob2, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(blob2, LV_ALIGN_BOTTOM_RIGHT, 60, 60);
    lv_obj_clear_flag(blob2, LV_OBJ_FLAG_SCROLLABLE);
#if EW_AURORA_BG_ANIM
    start_float_anim(blob2, -14, 8000, 800);
#endif

    lv_obj_t * blob3 = lv_obj_create(parent);
    lv_obj_remove_style_all(blob3);
    lv_obj_set_size(blob3, 260, 260);
    lv_obj_set_style_bg_color(blob3, COL_AMBER, 0);
    lv_obj_set_style_bg_opa(blob3, 60, 0);
    lv_obj_set_style_radius(blob3, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(blob3, LV_ALIGN_CENTER, 40, -10);
    lv_obj_clear_flag(blob3, LV_OBJ_FLAG_SCROLLABLE);
#if EW_AURORA_BG_ANIM
    start_float_anim(blob3, 12, 9000, 1400);
#endif
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
            lv_obj_set_style_bg_color(dot, lv_color_hex(0xCBD5E1), 0);
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

    switch (ctx->target) {
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
    default:
        break;
    }
}

static lv_obj_t * create_app_card(lv_obj_t * parent, const char * name, uint32_t icon_index,
                                  lv_color_t glow_color, aurora_nav_ctx_t * nav_ctx)
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
    lv_obj_set_style_transform_pivot_x(card, lv_pct(50), LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(card, lv_pct(50), LV_PART_MAIN);
    lv_obj_set_style_transform_scale(card, 240, LV_STATE_PRESSED);

    lv_obj_t * glow = lv_obj_create(card);
    lv_obj_remove_style_all(glow);
    lv_obj_set_size(glow, 220, 70);
    lv_obj_set_style_bg_color(glow, glow_color, 0);
    lv_obj_set_style_bg_opa(glow, 26, 0);
#if EW_AURORA_USE_GRADIENT
    lv_obj_set_style_bg_grad_color(glow, lv_color_white(), 0);
    lv_obj_set_style_bg_grad_dir(glow, LV_GRAD_DIR_VER, 0);
#endif
    lv_obj_set_style_radius(glow, 16, 0);
    lv_obj_align(glow, LV_ALIGN_TOP_MID, 0, -10);
    lv_obj_add_flag(glow, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_move_background(glow);
    /* 让点击始终命中 card（避免子对象吞掉点击导致“只能点到字才进”） */
    lv_obj_clear_flag(glow, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * icon_box = lv_obj_create(card);
    lv_obj_remove_style_all(icon_box);
    lv_obj_add_style(icon_box, &style_icon_box, 0);
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

    ui->Main_1 = lv_obj_create(NULL);
    ui_AuroraScr = ui->Main_1;

    lv_obj_remove_style_all(ui_AuroraScr);
    lv_obj_set_size(ui_AuroraScr, AURORA_SCREEN_W, AURORA_SCREEN_H);
    lv_obj_set_style_bg_color(ui_AuroraScr, lv_color_hex(0xF0F9FF), 0);
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
    lv_obj_set_size(bar, 4, 18);
    lv_obj_set_style_bg_color(bar, COL_BLUE, 0);
    lv_obj_set_style_radius(bar, 2, 0);

    lv_obj_t * title_lbl = lv_label_create(title_box);
    lv_label_set_text(title_lbl, "WindSight 智风监测");
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x334155), 0);
    lv_obj_set_style_text_font(title_lbl, gui_assets_get_font_30(), 0);

    lv_obj_t * status_pill = lv_obj_create(header);
    lv_obj_remove_style_all(status_pill);
    /* Two-line pill: Row1(WIFI/TCP/REG/REP) + Row2(NODE:xx) */
    lv_obj_set_size(status_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(status_pill, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(status_pill, 120, 0);
    lv_obj_set_style_radius(status_pill, 18, 0);
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
    lv_obj_set_style_text_color(node_lbl, lv_color_hex(0x334155), 0);
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
    create_app_card(p1, "实时监控", 0, COL_BLUE, NULL);
    create_app_card(p1, "故障监测", 1, COL_RED, NULL);
    create_app_card(p1, "数据分析", 2, COL_PURPLE, NULL);
    create_app_card(p1, "历史记录", 3, COL_GREEN, NULL);
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
    create_app_card(p3, "关于系统", 13, COL_BLUE, NULL);

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
            lv_obj_set_style_bg_color(dot, lv_color_hex(0xCBD5E1), 0);
        }
    }
}
