/*
 * Copyright 2026 NXP
 * EdgeWind Device Connection Screen - LVGL 9.4.0
 */

#include "lvgl.h"
#include "gui_guider.h"
#include "events_init.h"
#include "../../gui_assets.h"

#define DC_W 800
#define DC_H 480
#define DC_HEADER_H 60

#define DC_COL_SCREEN   0xEEF8FC
#define DC_COL_TOPBAR   0xCFEFFF
#define DC_COL_TITLE    0x0B4F71
#define DC_COL_TEXT     0x1F3A4D
#define DC_COL_MUTED    0x5E7686
#define DC_COL_BORDER   0xB9DCEB
#define DC_COL_CARD     0xFFFFFF
#define DC_COL_ROW      0xF8FAFC
#define DC_COL_BLUE     0x2563EB
#define DC_COL_CYAN     0x0891B2
#define DC_COL_GREEN    0x059669
#define DC_COL_AMBER    0xF59E0B
#define DC_COL_RED      0xE11D48
#define DC_COL_GRAY     0x64748B
#define DC_COL_PURPLE   0x7C3AED
#define DC_COL_SOFT_BLUE   0xEFF6FF
#define DC_COL_SOFT_CYAN   0xECFEFF
#define DC_COL_SOFT_PURPLE 0xF5F3FF
#define DC_COL_SOFT_GREEN  0xECFDF5
#define DC_COL_LOG_BG   0x1E3A4D
#define DC_COL_LOG_TXT  0xE0F2FE

static void dc_style_panel(lv_obj_t *obj, uint32_t bg, lv_opa_t opa, int32_t radius)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(DC_COL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, 170, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void dc_style_button(lv_obj_t *btn, uint32_t bg, int32_t radius)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, radius, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
}

static void dc_style_button_label(lv_obj_t *lbl, uint32_t color)
{
    lv_obj_set_style_text_font(lbl, gui_assets_get_font_16(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_center(lbl);
}

static lv_obj_t *dc_create_label(lv_obj_t *parent, const char *text, int32_t x, int32_t y,
                                 int32_t width, uint32_t color, const lv_font_t *font)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text ? text : "");
    if (width > 0) {
        lv_obj_set_width(lbl, width);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    }
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_pos(lbl, x, y);
    return lbl;
}

static void create_step_row(lv_obj_t *parent, int32_t y,
                            const char *icon_text, const char *label_text,
                            uint32_t accent_color, uint32_t soft_color,
                            lv_obj_t **led_ptr, lv_obj_t **stat_lbl_ptr,
                            lv_obj_t **btn_ptr, lv_obj_t **btn_lbl_ptr)
{
    lv_obj_t *row = lv_obj_create(parent);
    dc_style_panel(row, soft_color, 255, 12);
    lv_obj_set_style_border_color(row, lv_color_hex(accent_color), LV_PART_MAIN);
    lv_obj_set_style_border_opa(row, 70, LV_PART_MAIN);
    lv_obj_set_pos(row, 14, y);
    lv_obj_set_size(row, 492, 48);

    lv_obj_t *icon = lv_obj_create(row);
    dc_style_panel(icon, accent_color, 255, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN);
    lv_obj_set_pos(icon, 12, 9);
    lv_obj_set_size(icon, 30, 30);

    lv_obj_t *icon_lbl = lv_label_create(icon);
    lv_label_set_text(icon_lbl, icon_text ? icon_text : "");
    lv_obj_set_style_text_font(icon_lbl, gui_assets_get_font_16(), LV_PART_MAIN);
    lv_obj_set_style_text_color(icon_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(icon_lbl);

    dc_create_label(row, label_text, 54, 13, 140, accent_color, gui_assets_get_font_16());

    *led_ptr = lv_led_create(row);
    lv_obj_set_pos(*led_ptr, 206, 17);
    lv_obj_set_size(*led_ptr, 14, 14);
    lv_led_set_color(*led_ptr, lv_color_hex(0x999999));
    lv_led_off(*led_ptr);

    *stat_lbl_ptr = dc_create_label(row, "未连接 (Idle)", 228, 13, 148,
                                    DC_COL_MUTED, gui_assets_get_font_16());

    *btn_ptr = lv_button_create(row);
    lv_obj_set_pos(*btn_ptr, 388, 7);
    lv_obj_set_size(*btn_ptr, 90, 34);
    dc_style_button(*btn_ptr, accent_color, 17);

    *btn_lbl_ptr = lv_label_create(*btn_ptr);
    lv_label_set_text(*btn_lbl_ptr, "连接");
    dc_style_button_label(*btn_lbl_ptr, 0xFFFFFF);
}

void setup_scr_DeviceConnect(lv_ui *ui)
{
    ui->DeviceConnect = lv_obj_create(NULL);
    lv_obj_remove_style_all(ui->DeviceConnect);
    lv_obj_set_size(ui->DeviceConnect, DC_W, DC_H);
    lv_obj_set_scrollbar_mode(ui->DeviceConnect, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(ui->DeviceConnect, lv_color_hex(DC_COL_SCREEN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->DeviceConnect, 255, LV_PART_MAIN);

    lv_obj_t *header = lv_obj_create(ui->DeviceConnect);
    dc_style_panel(header, DC_COL_TOPBAR, 255, 0);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, DC_W, DC_HEADER_H);

    lv_obj_t *accent = lv_obj_create(header);
    dc_style_panel(accent, DC_COL_TITLE, 255, 2);
    lv_obj_set_style_border_width(accent, 0, LV_PART_MAIN);
    lv_obj_set_pos(accent, 38, 21);
    lv_obj_set_size(accent, 4, 20);

    ui->DeviceConnect_lbl_title = dc_create_label(header, "设备连接", 56, 11, 230,
                                                  DC_COL_TITLE, gui_assets_get_font_30());

    dc_create_label(header, "WiFi / TCP / REG / REP", 296, 18, 250,
                    DC_COL_TEXT, gui_assets_get_font_20());

    ui->DeviceConnect_btn_autorec = lv_button_create(header);
    lv_obj_set_pos(ui->DeviceConnect_btn_autorec, 600, 12);
    lv_obj_set_size(ui->DeviceConnect_btn_autorec, 176, 36);
    dc_style_button(ui->DeviceConnect_btn_autorec, 0xFFFFFF, 18);
    lv_obj_set_style_border_width(ui->DeviceConnect_btn_autorec, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ui->DeviceConnect_btn_autorec, lv_color_hex(DC_COL_BORDER), LV_PART_MAIN);

    ui->DeviceConnect_lbl_autorec = lv_label_create(ui->DeviceConnect_btn_autorec);
    lv_label_set_text(ui->DeviceConnect_lbl_autorec, "断电重连: 是");
    dc_style_button_label(ui->DeviceConnect_lbl_autorec, DC_COL_TITLE);

    ui->DeviceConnect_cont_panel = lv_obj_create(ui->DeviceConnect);
    dc_style_panel(ui->DeviceConnect_cont_panel, DC_COL_CARD, 235, 16);
    lv_obj_set_pos(ui->DeviceConnect_cont_panel, 24, 76);
    lv_obj_set_size(ui->DeviceConnect_cont_panel, 536, 260);

    dc_create_label(ui->DeviceConnect_cont_panel, "EdgeComm Flow", 18, 14, 210,
                    DC_COL_TITLE, gui_assets_get_font_20());
    dc_create_label(ui->DeviceConnect_cont_panel, "Manual steps keep upload state visible",
                    222, 18, 280, DC_COL_MUTED, gui_assets_get_font_12());

    create_step_row(ui->DeviceConnect_cont_panel, 48, "W", "1. WiFi 连接",
                    DC_COL_BLUE, DC_COL_SOFT_BLUE,
                    &ui->DeviceConnect_led_wifi, &ui->DeviceConnect_lbl_stat_wifi,
                    &ui->DeviceConnect_btn_wifi, &ui->DeviceConnect_lbl_btn_wifi);

    create_step_row(ui->DeviceConnect_cont_panel, 100, "T", "2. TCP 服务",
                    DC_COL_CYAN, DC_COL_SOFT_CYAN,
                    &ui->DeviceConnect_led_tcp, &ui->DeviceConnect_lbl_stat_tcp,
                    &ui->DeviceConnect_btn_tcp, &ui->DeviceConnect_lbl_btn_tcp);

    create_step_row(ui->DeviceConnect_cont_panel, 152, "R", "3. 设备注册",
                    DC_COL_PURPLE, DC_COL_SOFT_PURPLE,
                    &ui->DeviceConnect_led_reg, &ui->DeviceConnect_lbl_stat_reg,
                    &ui->DeviceConnect_btn_reg, &ui->DeviceConnect_lbl_btn_reg);

    create_step_row(ui->DeviceConnect_cont_panel, 204, "D", "4. 数据上报",
                    DC_COL_GREEN, DC_COL_SOFT_GREEN,
                    &ui->DeviceConnect_led_report, &ui->DeviceConnect_lbl_stat_report,
                    &ui->DeviceConnect_btn_report, &ui->DeviceConnect_lbl_btn_report);

    lv_label_set_text(ui->DeviceConnect_lbl_btn_report, "开始上报");
    lv_obj_set_style_bg_color(ui->DeviceConnect_btn_report, lv_color_hex(DC_COL_GREEN), LV_PART_MAIN);
    lv_obj_set_style_text_color(ui->DeviceConnect_lbl_btn_report, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    lv_obj_t *action_card = lv_obj_create(ui->DeviceConnect);
    dc_style_panel(action_card, 0xF8FBFD, 245, 16);
    lv_obj_set_pos(action_card, 580, 76);
    lv_obj_set_size(action_card, 196, 260);

    dc_create_label(action_card, "AUTO", 18, 14, 100, DC_COL_TITLE, gui_assets_get_font_20());
    dc_create_label(action_card, "Run connection steps in order", 18, 44, 160,
                    DC_COL_MUTED, gui_assets_get_font_12());

    ui->DeviceConnect_btn_auto = lv_button_create(action_card);
    lv_obj_set_pos(ui->DeviceConnect_btn_auto, 18, 86);
    lv_obj_set_size(ui->DeviceConnect_btn_auto, 160, 48);
    dc_style_button(ui->DeviceConnect_btn_auto, DC_COL_AMBER, 24);
    lv_obj_set_style_shadow_width(ui->DeviceConnect_btn_auto, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(ui->DeviceConnect_btn_auto, 35, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(ui->DeviceConnect_btn_auto, lv_color_hex(0xB45309), LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(ui->DeviceConnect_btn_auto, 2, LV_PART_MAIN);

    ui->DeviceConnect_lbl_auto = lv_label_create(ui->DeviceConnect_btn_auto);
    lv_label_set_text(ui->DeviceConnect_lbl_auto, "一键连接");
    lv_obj_set_style_text_font(ui->DeviceConnect_lbl_auto, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_set_style_text_color(ui->DeviceConnect_lbl_auto, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(ui->DeviceConnect_lbl_auto);

    ui->DeviceConnect_btn_back = lv_button_create(action_card);
    lv_obj_set_pos(ui->DeviceConnect_btn_back, 18, 182);
    lv_obj_set_size(ui->DeviceConnect_btn_back, 160, 42);
    dc_style_button(ui->DeviceConnect_btn_back, 0x475569, 21);

    ui->DeviceConnect_lbl_back = lv_label_create(ui->DeviceConnect_btn_back);
    lv_label_set_text(ui->DeviceConnect_lbl_back, "返回");
    lv_obj_set_style_text_font(ui->DeviceConnect_lbl_back, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_set_style_text_color(ui->DeviceConnect_lbl_back, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(ui->DeviceConnect_lbl_back);

    ui->DeviceConnect_ta_console = lv_textarea_create(ui->DeviceConnect);
    lv_obj_set_pos(ui->DeviceConnect_ta_console, 24, 350);
    lv_obj_set_size(ui->DeviceConnect_ta_console, 752, 106);
    lv_obj_clear_flag(ui->DeviceConnect_ta_console, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_textarea_set_cursor_click_pos(ui->DeviceConnect_ta_console, false);
    lv_textarea_set_text_selection(ui->DeviceConnect_ta_console, false);
    lv_obj_set_style_bg_color(ui->DeviceConnect_ta_console, lv_color_hex(DC_COL_LOG_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->DeviceConnect_ta_console, 245, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui->DeviceConnect_ta_console, lv_color_hex(DC_COL_LOG_TXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->DeviceConnect_ta_console, gui_assets_get_font_12(), LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->DeviceConnect_ta_console, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ui->DeviceConnect_ta_console, lv_color_hex(0x7DD3FC), LV_PART_MAIN);
    lv_obj_set_style_radius(ui->DeviceConnect_ta_console, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_left(ui->DeviceConnect_ta_console, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(ui->DeviceConnect_ta_console, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(ui->DeviceConnect_ta_console, 8, LV_PART_MAIN);
    lv_textarea_set_max_length(ui->DeviceConnect_ta_console, 2048);
    lv_textarea_set_text(ui->DeviceConnect_ta_console, "> System Ready.\n> Waiting for user action...\n");

    lv_obj_update_layout(ui->DeviceConnect);
    events_init_DeviceConnect(ui);
}
