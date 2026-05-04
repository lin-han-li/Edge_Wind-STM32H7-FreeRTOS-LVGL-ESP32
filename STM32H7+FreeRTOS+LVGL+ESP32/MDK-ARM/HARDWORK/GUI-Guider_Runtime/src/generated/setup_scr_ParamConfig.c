/*
* Copyright 2026 NXP
* Replica of GUI Guider Generated Code for Gemini Project
*/

#include "lvgl.h"
#include <stdio.h>
#include "gui_guider.h"
#include "events_init.h"
#include "widgets_init.h"
#include "custom.h"
#include "../../gui_assets.h"

#define PC_W 800
#define PC_H 480
#define PC_COL_SCREEN  0x65ADFF
#define PC_COL_TOPBAR  0xD7F1FF
#define PC_COL_TITLE   0x0F4C81
#define PC_COL_TEXT    0x334155
#define PC_COL_MUTED   0x64748B
#define PC_COL_CARD    0xFFFFFF
#define PC_COL_FIELD   0xF8FAFC
#define PC_COL_BORDER  0xB7E4F8
#define PC_COL_BLUE    0x2563EB
#define PC_COL_CYAN    0x0891B2
#define PC_COL_GREEN   0x059669
#define PC_COL_AMBER   0xD97706
#define PC_COL_GRAY    0x64748B
#define PC_COL_WARN_BG 0xECFEFF

static void pc_style_panel(lv_obj_t *obj, uint32_t color, lv_opa_t opa)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(PC_COL_BORDER), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, 220, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 12, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void pc_style_label(lv_obj_t *obj, int32_t w)
{
    lv_obj_set_width(obj, w);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(obj, lv_color_hex(PC_COL_TITLE), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, gui_assets_get_font_16(), LV_PART_MAIN|LV_STATE_DEFAULT);
}

static void pc_style_ta(lv_obj_t *obj, uint32_t max_len)
{
    lv_textarea_set_one_line(obj, true);
    lv_textarea_set_accepted_chars(obj, "0123456789");
    if (max_len > 0U) {
        lv_textarea_set_max_length(obj, max_len);
    }
    lv_obj_set_style_bg_color(obj, lv_color_hex(PC_COL_FIELD), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(obj, lv_color_hex(PC_COL_TEXT), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(PC_COL_BORDER), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(PC_COL_BLUE), LV_PART_MAIN|LV_STATE_FOCUSED);
    lv_obj_set_style_radius(obj, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(obj, 8, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(obj, 6, LV_PART_MAIN|LV_STATE_DEFAULT);
}

static void pc_style_btn(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 240, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 18, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
}

static void pc_style_btn_label(lv_obj_t *obj)
{
    lv_obj_set_style_text_color(obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, gui_assets_get_font_16(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_center(obj);
}

void setup_scr_ParamConfig(lv_ui *ui)
{
    ui->ParamConfig = lv_obj_create(NULL);
    lv_obj_remove_style_all(ui->ParamConfig);
    lv_obj_set_size(ui->ParamConfig, PC_W, PC_H);
    lv_obj_set_scrollbar_mode(ui->ParamConfig, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->ParamConfig, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ui->ParamConfig, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->ParamConfig, lv_color_hex(PC_COL_SCREEN), LV_PART_MAIN|LV_STATE_DEFAULT);

    lv_obj_t *header = lv_obj_create(ui->ParamConfig);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, PC_W, 60);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(PC_COL_TOPBAR), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(header, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(header);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, 4, 18);
    lv_obj_set_style_bg_color(accent, lv_color_hex(PC_COL_CYAN), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(accent, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(accent, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(accent, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(accent, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);

    ui->ParamConfig_lbl_title = lv_label_create(header);
    lv_obj_set_width(ui->ParamConfig_lbl_title, 300);
    lv_label_set_long_mode(ui->ParamConfig_lbl_title, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ui->ParamConfig_lbl_title, "通讯参数配置");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_title, lv_color_hex(PC_COL_TITLE), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_title, gui_assets_get_font_30(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(ui->ParamConfig_lbl_title, LV_ALIGN_LEFT_MID, 34, 0);

    lv_obj_t *header_note = lv_label_create(header);
    lv_obj_set_width(header_note, 240);
    lv_label_set_long_mode(header_note, LV_LABEL_LONG_CLIP);
    lv_label_set_text(header_note, "上传链路 / 时序 / 分段策略");
    lv_obj_set_style_text_color(header_note, lv_color_hex(PC_COL_MUTED), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(header_note, gui_assets_get_font_16(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(header_note, LV_ALIGN_LEFT_MID, 332, 0);

    lv_obj_t *preset_pill = lv_obj_create(header);
    lv_obj_remove_style_all(preset_pill);
    lv_obj_set_size(preset_pill, 188, 42);
    lv_obj_align(preset_pill, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(preset_pill, lv_color_hex(0xFFFFFF), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(preset_pill, 190, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(preset_pill, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(preset_pill, 21, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(preset_pill, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(preset_pill, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(preset_pill, LV_OBJ_FLAG_SCROLLABLE);

    ui->ParamConfig_btn_lan = lv_button_create(preset_pill);
    lv_obj_set_pos(ui->ParamConfig_btn_lan, 10, 4);
    lv_obj_set_size(ui->ParamConfig_btn_lan, 74, 34);
    pc_style_btn(ui->ParamConfig_btn_lan, PC_COL_GREEN);

    ui->ParamConfig_lbl_lan = lv_label_create(ui->ParamConfig_btn_lan);
    lv_label_set_text(ui->ParamConfig_lbl_lan, "局域网");
    pc_style_btn_label(ui->ParamConfig_lbl_lan);

    ui->ParamConfig_btn_wan = lv_button_create(preset_pill);
    lv_obj_set_pos(ui->ParamConfig_btn_wan, 102, 4);
    lv_obj_set_size(ui->ParamConfig_btn_wan, 74, 34);
    pc_style_btn(ui->ParamConfig_btn_wan, PC_COL_AMBER);

    ui->ParamConfig_lbl_wan = lv_label_create(ui->ParamConfig_btn_wan);
    lv_label_set_text(ui->ParamConfig_lbl_wan, "公网");
    pc_style_btn_label(ui->ParamConfig_lbl_wan);

    ui->ParamConfig_cont_panel = lv_obj_create(ui->ParamConfig);
    lv_obj_remove_style_all(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_cont_panel, 24, 68);
    lv_obj_set_size(ui->ParamConfig_cont_panel, 744, 304);
    pc_style_panel(ui->ParamConfig_cont_panel, PC_COL_CARD, 240);
    lv_obj_set_scroll_dir(ui->ParamConfig_cont_panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui->ParamConfig_cont_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(ui->ParamConfig_cont_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel_title = lv_label_create(ui->ParamConfig_cont_panel);
    lv_label_set_text(panel_title, "参数设置");
    lv_obj_set_style_text_color(panel_title, lv_color_hex(PC_COL_TITLE), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(panel_title, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_pos(panel_title, 20, 16);

    lv_obj_t *panel_note = lv_label_create(ui->ParamConfig_cont_panel);
    lv_label_set_text(panel_note, "数值保存到 SD 后由 ESP32 侧同步应用");
    lv_obj_set_style_text_color(panel_note, lv_color_hex(PC_COL_MUTED), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(panel_note, gui_assets_get_font_12(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_pos(panel_note, 132, 22);

    ui->ParamConfig_btn_nochunk = lv_button_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_btn_nochunk, 574, 12);
    lv_obj_set_size(ui->ParamConfig_btn_nochunk, 150, 34);
    pc_style_btn(ui->ParamConfig_btn_nochunk, PC_COL_GRAY);

    ui->ParamConfig_lbl_nochunk = lv_label_create(ui->ParamConfig_btn_nochunk);
    lv_label_set_text(ui->ParamConfig_lbl_nochunk, "一键无分段");
    pc_style_btn_label(ui->ParamConfig_lbl_nochunk);

    const int32_t col_x[4] = {20, 197, 374, 551};
    const int32_t label_y[2] = {64, 148};
    const int32_t input_y[2] = {90, 174};
    const int32_t item_w = 165;
    const int32_t item_h = 40;

    ui->ParamConfig_lbl_heartbeat = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_lbl_heartbeat, col_x[0], label_y[0]);
    lv_label_set_text(ui->ParamConfig_lbl_heartbeat, "心跳间隔 ms");
    pc_style_label(ui->ParamConfig_lbl_heartbeat, item_w);

    ui->ParamConfig_ta_heartbeat = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_ta_heartbeat, col_x[0], input_y[0]);
    lv_obj_set_size(ui->ParamConfig_ta_heartbeat, item_w, item_h);
    lv_textarea_set_text(ui->ParamConfig_ta_heartbeat, "5000");
    pc_style_ta(ui->ParamConfig_ta_heartbeat, 6);

    ui->ParamConfig_lbl_sendlimit = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_lbl_sendlimit, col_x[1], label_y[0]);
    lv_label_set_text(ui->ParamConfig_lbl_sendlimit, "发包限频 ms");
    pc_style_label(ui->ParamConfig_lbl_sendlimit, item_w);

    ui->ParamConfig_ta_sendlimit = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_ta_sendlimit, col_x[1], input_y[0]);
    lv_obj_set_size(ui->ParamConfig_ta_sendlimit, item_w, item_h);
    lv_textarea_set_text(ui->ParamConfig_ta_sendlimit, "200");
    pc_style_ta(ui->ParamConfig_ta_sendlimit, 5);

    ui->ParamConfig_lbl_httptimeout = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_lbl_httptimeout, col_x[2], label_y[0]);
    lv_label_set_text(ui->ParamConfig_lbl_httptimeout, "回包超时 ms");
    pc_style_label(ui->ParamConfig_lbl_httptimeout, item_w);

    ui->ParamConfig_ta_httptimeout = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_ta_httptimeout, col_x[2], input_y[0]);
    lv_obj_set_size(ui->ParamConfig_ta_httptimeout, item_w, item_h);
    lv_textarea_set_text(ui->ParamConfig_ta_httptimeout, "1200");
    pc_style_ta(ui->ParamConfig_ta_httptimeout, 5);

    ui->ParamConfig_lbl_hardreset = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_lbl_hardreset, col_x[3], label_y[0]);
    lv_label_set_text(ui->ParamConfig_lbl_hardreset, "复位阈值 s");
    pc_style_label(ui->ParamConfig_lbl_hardreset, item_w);

    ui->ParamConfig_ta_hardreset = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_ta_hardreset, col_x[3], input_y[0]);
    lv_obj_set_size(ui->ParamConfig_ta_hardreset, item_w, item_h);
    lv_textarea_set_text(ui->ParamConfig_ta_hardreset, "60");
    pc_style_ta(ui->ParamConfig_ta_hardreset, 4);

    ui->ParamConfig_lbl_chunkkb = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_lbl_chunkkb, col_x[0], label_y[1]);
    lv_label_set_text(ui->ParamConfig_lbl_chunkkb, "分段大小 KB");
    pc_style_label(ui->ParamConfig_lbl_chunkkb, item_w);

    ui->ParamConfig_ta_chunkkb = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_ta_chunkkb, col_x[0], input_y[1]);
    lv_obj_set_size(ui->ParamConfig_ta_chunkkb, item_w, item_h);
    lv_textarea_set_text(ui->ParamConfig_ta_chunkkb, "4");
    pc_style_ta(ui->ParamConfig_ta_chunkkb, 2);

    ui->ParamConfig_lbl_chunkdelay = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_lbl_chunkdelay, col_x[1], label_y[1]);
    lv_label_set_text(ui->ParamConfig_lbl_chunkdelay, "分段间隔 ms");
    pc_style_label(ui->ParamConfig_lbl_chunkdelay, item_w);

    ui->ParamConfig_ta_chunkdelay = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_ta_chunkdelay, col_x[1], input_y[1]);
    lv_obj_set_size(ui->ParamConfig_ta_chunkdelay, item_w, item_h);
    lv_textarea_set_text(ui->ParamConfig_ta_chunkdelay, "10");
    pc_style_ta(ui->ParamConfig_ta_chunkdelay, 3);

    ui->ParamConfig_lbl_downsample = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_lbl_downsample, col_x[2], label_y[1]);
    lv_label_set_text(ui->ParamConfig_lbl_downsample, "降采样 step");
    pc_style_label(ui->ParamConfig_lbl_downsample, item_w);

    ui->ParamConfig_ta_downsample = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_ta_downsample, col_x[2], input_y[1]);
    lv_obj_set_size(ui->ParamConfig_ta_downsample, item_w, item_h);
    lv_textarea_set_text(ui->ParamConfig_ta_downsample, "1");
    pc_style_ta(ui->ParamConfig_ta_downsample, 2);

    ui->ParamConfig_lbl_uploadpoints = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_lbl_uploadpoints, col_x[3], label_y[1]);
    lv_label_set_text(ui->ParamConfig_lbl_uploadpoints, "上传点数");
    pc_style_label(ui->ParamConfig_lbl_uploadpoints, item_w);

    ui->ParamConfig_ta_uploadpoints = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_ta_uploadpoints, col_x[3], input_y[1]);
    lv_obj_set_size(ui->ParamConfig_ta_uploadpoints, item_w, item_h);
    lv_textarea_set_text(ui->ParamConfig_ta_uploadpoints, "4096");
    pc_style_ta(ui->ParamConfig_ta_uploadpoints, 4);

    lv_obj_t *tip_box = lv_obj_create(ui->ParamConfig_cont_panel);
    lv_obj_remove_style_all(tip_box);
    lv_obj_set_pos(tip_box, 20, 244);
    lv_obj_set_size(tip_box, 696, 96);
    pc_style_panel(tip_box, PC_COL_WARN_BG, 255);

    ui->ParamConfig_lbl_tips = lv_label_create(tip_box);
    lv_obj_set_pos(ui->ParamConfig_lbl_tips, 12, 10);
    lv_obj_set_size(ui->ParamConfig_lbl_tips, 670, 72);
    lv_label_set_long_mode(ui->ParamConfig_lbl_tips, LV_LABEL_LONG_WRAP);
    lv_label_set_text(ui->ParamConfig_lbl_tips,
                      "建议：心跳5000ms，限频200ms，回包1200ms，复位60s；分段4KB/10ms。回包超时必须小于复位阈值。");
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_tips, gui_assets_get_font_12(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_tips, lv_color_hex(PC_COL_TEXT), LV_PART_MAIN|LV_STATE_DEFAULT);

    ui->ParamConfig_lbl_status = lv_label_create(ui->ParamConfig);
    lv_obj_set_pos(ui->ParamConfig_lbl_status, 40, 372);
    lv_obj_set_size(ui->ParamConfig_lbl_status, 720, 22);
    lv_label_set_long_mode(ui->ParamConfig_lbl_status, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->ParamConfig_lbl_status, "状态：默认参数已加载");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_status, lv_color_hex(0xEAF6FF), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_status, gui_assets_get_font_16(), LV_PART_MAIN|LV_STATE_DEFAULT);

    ui->ParamConfig_btn_back = lv_button_create(ui->ParamConfig);
    lv_obj_set_pos(ui->ParamConfig_btn_back, 50, 418);
    lv_obj_set_size(ui->ParamConfig_btn_back, 150, 44);
    pc_style_btn(ui->ParamConfig_btn_back, PC_COL_GRAY);

    ui->ParamConfig_btn_back_label = lv_label_create(ui->ParamConfig_btn_back);
    lv_label_set_text(ui->ParamConfig_btn_back_label, "返回");
    pc_style_btn_label(ui->ParamConfig_btn_back_label);

    ui->ParamConfig_btn_load = lv_button_create(ui->ParamConfig);
    lv_obj_set_pos(ui->ParamConfig_btn_load, 325, 418);
    lv_obj_set_size(ui->ParamConfig_btn_load, 150, 44);
    pc_style_btn(ui->ParamConfig_btn_load, PC_COL_AMBER);

    ui->ParamConfig_btn_load_label = lv_label_create(ui->ParamConfig_btn_load);
    lv_label_set_text(ui->ParamConfig_btn_load_label, "读取配置");
    pc_style_btn_label(ui->ParamConfig_btn_load_label);

    ui->ParamConfig_btn_save = lv_button_create(ui->ParamConfig);
    lv_obj_set_pos(ui->ParamConfig_btn_save, 600, 418);
    lv_obj_set_size(ui->ParamConfig_btn_save, 150, 44);
    pc_style_btn(ui->ParamConfig_btn_save, PC_COL_BLUE);

    ui->ParamConfig_btn_save_label = lv_label_create(ui->ParamConfig_btn_save);
    lv_label_set_text(ui->ParamConfig_btn_save_label, "保存配置");
    pc_style_btn_label(ui->ParamConfig_btn_save_label);

    ui->ParamConfig_kb = lv_keyboard_create(ui->ParamConfig);
    lv_obj_set_size(ui->ParamConfig_kb, PC_W, 200);
    lv_obj_align(ui->ParamConfig_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(ui->ParamConfig_kb, LV_OBJ_FLAG_HIDDEN);

    lv_obj_update_layout(ui->ParamConfig);
    events_init_ParamConfig(ui);
}
