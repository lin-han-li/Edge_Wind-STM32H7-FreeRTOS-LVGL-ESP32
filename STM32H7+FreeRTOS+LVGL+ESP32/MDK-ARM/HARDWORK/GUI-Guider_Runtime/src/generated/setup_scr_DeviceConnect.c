/*
* Copyright 2026 NXP
* EdgeWind Device Connection Screen - LVGL 9.4.0
*/

#include "lvgl.h"
#include "gui_guider.h"
#include "events_init.h"
#include "../../gui_assets.h"

/* 辅助宏：定义行高和布局参数 */
#define STEP_ROW_HEIGHT 55
#define PANEL_PAD_TOP   20
#define PANEL_PAD_LEFT  20

static void create_step_row(lv_obj_t *parent, int32_t y_offset,
                            const char *icon_text, const char *label_text,
                            lv_obj_t **led_ptr, lv_obj_t **stat_lbl_ptr,
                            lv_obj_t **btn_ptr, lv_obj_t **btn_lbl_ptr)
{
    lv_obj_t *icon = lv_label_create(parent);
    lv_label_set_text(icon, icon_text);
    lv_obj_set_pos(icon, PANEL_PAD_LEFT, y_offset + 12);
    lv_obj_set_style_text_font(icon, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x2F35DA), LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_pos(lbl, PANEL_PAD_LEFT + 40, y_offset + 12);
    lv_obj_set_style_text_font(lbl, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x2F35DA), LV_PART_MAIN);

    *led_ptr = lv_led_create(parent);
    lv_obj_set_pos(*led_ptr, 300, y_offset + 15);
    lv_obj_set_size(*led_ptr, 14, 14);
    lv_led_set_color(*led_ptr, lv_color_hex(0x999999));
    lv_led_off(*led_ptr);

    *stat_lbl_ptr = lv_label_create(parent);
    lv_label_set_text(*stat_lbl_ptr, "未连接 (Idle)");
    lv_obj_set_pos(*stat_lbl_ptr, 325, y_offset + 12);
    lv_obj_set_style_text_color(*stat_lbl_ptr, lv_color_hex(0x666666), LV_PART_MAIN);
    /* 状态文本包含中文：使用 16px 小字（若无则回落到 20px） */
    lv_obj_set_style_text_font(*stat_lbl_ptr, gui_assets_get_font_16(), LV_PART_MAIN);

    *btn_ptr = lv_button_create(parent);
    lv_obj_set_pos(*btn_ptr, 550, y_offset + 5);
    lv_obj_set_size(*btn_ptr, 120, 40);
    lv_obj_set_style_bg_color(*btn_ptr, lv_color_hex(0xFFA500), LV_PART_MAIN);
    lv_obj_set_style_radius(*btn_ptr, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(*btn_ptr, 5, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(*btn_ptr, lv_color_hex(0xDDDDDD), LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(*btn_ptr, 2, LV_PART_MAIN);

    *btn_lbl_ptr = lv_label_create(*btn_ptr);
    lv_label_set_text(*btn_lbl_ptr, "连接");
    lv_obj_set_style_text_font(*btn_lbl_ptr, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_center(*btn_lbl_ptr);

    if (y_offset < (PANEL_PAD_TOP + STEP_ROW_HEIGHT * 3)) {
        lv_obj_t *line = lv_obj_create(parent);
        lv_obj_set_size(line, 660, 1);
        lv_obj_set_pos(line, 20, y_offset + STEP_ROW_HEIGHT - 2);
        lv_obj_set_style_bg_color(line, lv_color_hex(0xEEEEEE), LV_PART_MAIN);
        lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
    }
}

void setup_scr_DeviceConnect(lv_ui *ui)
{
    ui->DeviceConnect = lv_obj_create(NULL);
    lv_obj_set_size(ui->DeviceConnect, 800, 480);
    lv_obj_set_scrollbar_mode(ui->DeviceConnect, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(ui->DeviceConnect, lv_color_hex(0x65adff), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->DeviceConnect, 255, LV_PART_MAIN);

    /* Header (match Main_x header): fixed 50px, title vertically centered */
    lv_obj_t * header = lv_obj_create(ui->DeviceConnect);
    lv_obj_set_size(header, 800, 50);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_opa(header, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    ui->DeviceConnect_lbl_title = lv_label_create(header);
    lv_obj_set_width(ui->DeviceConnect_lbl_title, 760);
    lv_label_set_long_mode(ui->DeviceConnect_lbl_title, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ui->DeviceConnect_lbl_title, "设备连接 (Device Connection)");
    lv_obj_set_style_text_color(ui->DeviceConnect_lbl_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->DeviceConnect_lbl_title, gui_assets_get_font_30(), LV_PART_MAIN);
    lv_obj_align(ui->DeviceConnect_lbl_title, LV_ALIGN_LEFT_MID, 20, 0);

    /* 右上角：断电重连 是否（设置按钮） */
    ui->DeviceConnect_btn_autorec = lv_button_create(header);
    lv_obj_set_size(ui->DeviceConnect_btn_autorec, 180, 36);
    lv_obj_align(ui->DeviceConnect_btn_autorec, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_radius(ui->DeviceConnect_btn_autorec, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->DeviceConnect_btn_autorec, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->DeviceConnect_btn_autorec, 200, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->DeviceConnect_btn_autorec, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ui->DeviceConnect_btn_autorec, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(ui->DeviceConnect_btn_autorec, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(ui->DeviceConnect_btn_autorec, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(ui->DeviceConnect_btn_autorec, 2, LV_PART_MAIN);

    ui->DeviceConnect_lbl_autorec = lv_label_create(ui->DeviceConnect_btn_autorec);
    lv_label_set_text(ui->DeviceConnect_lbl_autorec, "断电重连: 是");
    lv_obj_set_style_text_font(ui->DeviceConnect_lbl_autorec, gui_assets_get_font_16(), LV_PART_MAIN);
    lv_obj_set_style_text_color(ui->DeviceConnect_lbl_autorec, lv_color_hex(0x2F35DA), LV_PART_MAIN);
    lv_obj_center(ui->DeviceConnect_lbl_autorec);

    ui->DeviceConnect_cont_panel = lv_obj_create(ui->DeviceConnect);
    lv_obj_set_pos(ui->DeviceConnect_cont_panel, 50, 60);
    lv_obj_set_size(ui->DeviceConnect_cont_panel, 700, 260);
    lv_obj_set_scrollbar_mode(ui->DeviceConnect_cont_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(ui->DeviceConnect_cont_panel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->DeviceConnect_cont_panel, 230, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->DeviceConnect_cont_panel, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->DeviceConnect_cont_panel, 0, LV_PART_MAIN);

    create_step_row(ui->DeviceConnect_cont_panel, PANEL_PAD_TOP,
                    "W", "1. WiFi 连接",
                    &ui->DeviceConnect_led_wifi, &ui->DeviceConnect_lbl_stat_wifi,
                    &ui->DeviceConnect_btn_wifi, &ui->DeviceConnect_lbl_btn_wifi);

    create_step_row(ui->DeviceConnect_cont_panel, PANEL_PAD_TOP + STEP_ROW_HEIGHT,
                    "T", "2. TCP 服务",
                    &ui->DeviceConnect_led_tcp, &ui->DeviceConnect_lbl_stat_tcp,
                    &ui->DeviceConnect_btn_tcp, &ui->DeviceConnect_lbl_btn_tcp);

    create_step_row(ui->DeviceConnect_cont_panel, PANEL_PAD_TOP + STEP_ROW_HEIGHT * 2,
                    "R", "3. 设备注册",
                    &ui->DeviceConnect_led_reg, &ui->DeviceConnect_lbl_stat_reg,
                    &ui->DeviceConnect_btn_reg, &ui->DeviceConnect_lbl_btn_reg);

    create_step_row(ui->DeviceConnect_cont_panel, PANEL_PAD_TOP + STEP_ROW_HEIGHT * 3,
                    "D", "4. 数据上报",
                    &ui->DeviceConnect_led_report, &ui->DeviceConnect_lbl_stat_report,
                    &ui->DeviceConnect_btn_report, &ui->DeviceConnect_lbl_btn_report);
    lv_label_set_text(ui->DeviceConnect_lbl_btn_report, "开始上报");
    lv_obj_set_style_bg_color(ui->DeviceConnect_btn_report, lv_color_hex(0x3dfb00), LV_PART_MAIN);
    lv_obj_set_style_text_color(ui->DeviceConnect_lbl_btn_report, lv_color_hex(0x2F35DA), LV_PART_MAIN);

    ui->DeviceConnect_btn_back = lv_button_create(ui->DeviceConnect);
    lv_obj_set_pos(ui->DeviceConnect_btn_back, 50, 340);
    lv_obj_set_size(ui->DeviceConnect_btn_back, 150, 50);
    lv_obj_set_style_bg_color(ui->DeviceConnect_btn_back, lv_color_hex(0x999999), LV_PART_MAIN);
    lv_obj_set_style_radius(ui->DeviceConnect_btn_back, 25, LV_PART_MAIN);

    ui->DeviceConnect_lbl_back = lv_label_create(ui->DeviceConnect_btn_back);
    lv_label_set_text(ui->DeviceConnect_lbl_back, "返回");
    lv_obj_set_style_text_font(ui->DeviceConnect_lbl_back, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_center(ui->DeviceConnect_lbl_back);

    ui->DeviceConnect_ta_console = lv_textarea_create(ui->DeviceConnect);
    lv_obj_set_pos(ui->DeviceConnect_ta_console, 220, 340);
    lv_obj_set_size(ui->DeviceConnect_ta_console, 360, 120);
    /* LVGL9: textarea 无 readonly API。这里通过禁止点击聚焦来避免用户编辑 */
    lv_obj_clear_flag(ui->DeviceConnect_ta_console, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_textarea_set_cursor_click_pos(ui->DeviceConnect_ta_console, false);
    lv_textarea_set_text_selection(ui->DeviceConnect_ta_console, false);
    lv_obj_set_style_bg_color(ui->DeviceConnect_ta_console, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->DeviceConnect_ta_console, 60, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui->DeviceConnect_ta_console, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    /* 日志区域需要中文：使用 12px 中文字库（缺失则自动回落到 16/20） */
    lv_obj_set_style_text_font(ui->DeviceConnect_ta_console, gui_assets_get_font_12(), LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->DeviceConnect_ta_console, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->DeviceConnect_ta_console, 10, LV_PART_MAIN);
    lv_textarea_set_max_length(ui->DeviceConnect_ta_console, 2048);
    lv_textarea_set_text(ui->DeviceConnect_ta_console, "> System Ready.\n> Waiting for user action...\n");

    ui->DeviceConnect_btn_auto = lv_button_create(ui->DeviceConnect);
    lv_obj_set_pos(ui->DeviceConnect_btn_auto, 600, 340);
    lv_obj_set_size(ui->DeviceConnect_btn_auto, 150, 50);
    lv_obj_set_style_bg_color(ui->DeviceConnect_btn_auto, lv_color_hex(0xFFA500), LV_PART_MAIN);
    lv_obj_set_style_radius(ui->DeviceConnect_btn_auto, 25, LV_PART_MAIN);

    ui->DeviceConnect_lbl_auto = lv_label_create(ui->DeviceConnect_btn_auto);
    lv_label_set_text(ui->DeviceConnect_lbl_auto, "一键连接");
    lv_obj_set_style_text_font(ui->DeviceConnect_lbl_auto, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_center(ui->DeviceConnect_lbl_auto);

    lv_obj_update_layout(ui->DeviceConnect);
    events_init_DeviceConnect(ui);
}

