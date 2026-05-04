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

#define SV_W 800
#define SV_H 480
#define SV_COL_SCREEN   0xF0F9FF
#define SV_COL_TOPBAR   0xD7F1FF
#define SV_COL_TITLE    0x0F4C81
#define SV_COL_TEXT     0x334155
#define SV_COL_MUTED    0x64748B
#define SV_COL_CARD     0xFFFFFF
#define SV_COL_FIELD    0xF8FAFC
#define SV_COL_BORDER   0xB7E4F8
#define SV_COL_PRIMARY  0x2563EB
#define SV_COL_CYAN     0x0891B2
#define SV_COL_AMBER    0xD97706
#define SV_COL_GRAY     0x64748B
#define SV_COL_INFO_BG  0xEFF6FF

static void server_cfg_nav_wifi_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!ui) {
        return;
    }
    if (indev) {
        lv_indev_wait_release(indev);
    }
    if (ui->ServerConfig_kb) {
        lv_obj_add_flag(ui->ServerConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
    ui_load_scr_animation(&guider_ui, &guider_ui.WifiConfig, guider_ui.WifiConfig_del, &guider_ui.ServerConfig_del,
                          setup_scr_WifiConfig, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
}

static void server_cfg_style_card(lv_obj_t *obj, uint32_t bg, lv_opa_t opa)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(SV_COL_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, 220, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void server_cfg_style_btn(lv_obj_t *obj, uint32_t bg, uint32_t border)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, border ? 1 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(border ? border : bg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void server_cfg_style_btn_label(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_text_color(obj, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(obj);
}

static void server_cfg_style_input(lv_obj_t *obj)
{
    lv_textarea_set_one_line(obj, true);
    lv_obj_set_style_bg_color(obj, lv_color_hex(SV_COL_FIELD), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(obj, lv_color_hex(SV_COL_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, gui_assets_get_font_20(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(SV_COL_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(SV_COL_PRIMARY), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_radius(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void setup_scr_ServerConfig(lv_ui *ui)
{
    ui->ServerConfig = lv_obj_create(NULL);
    lv_obj_remove_style_all(ui->ServerConfig);
    lv_obj_set_size(ui->ServerConfig, SV_W, SV_H);
    lv_obj_set_scrollbar_mode(ui->ServerConfig, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->ServerConfig, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui->ServerConfig, lv_color_hex(SV_COL_SCREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->ServerConfig, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *header = lv_obj_create(ui->ServerConfig);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, SV_W, 60);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(SV_COL_TOPBAR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(header, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(header);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, 4, 18);
    lv_obj_set_style_bg_color(accent, lv_color_hex(SV_COL_CYAN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(accent, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(accent, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(accent, LV_ALIGN_LEFT_MID, 20, 0);

    ui->ServerConfig_lbl_title = lv_label_create(header);
    lv_obj_set_width(ui->ServerConfig_lbl_title, 260);
    lv_label_set_long_mode(ui->ServerConfig_lbl_title, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ui->ServerConfig_lbl_title, "服务器配置");
    lv_obj_set_style_text_color(ui->ServerConfig_lbl_title, lv_color_hex(SV_COL_TITLE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_lbl_title, gui_assets_get_font_30(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui->ServerConfig_lbl_title, LV_ALIGN_LEFT_MID, 34, 0);

    lv_obj_t *header_note = lv_label_create(header);
    lv_obj_set_width(header_note, 240);
    lv_label_set_long_mode(header_note, LV_LABEL_LONG_CLIP);
    lv_label_set_text(header_note, "云端 / 地址 / 节点身份");
    lv_obj_set_style_text_color(header_note, lv_color_hex(SV_COL_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(header_note, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(header_note, LV_ALIGN_LEFT_MID, 330, 0);

    lv_obj_t *tab_pill = lv_obj_create(header);
    server_cfg_style_card(tab_pill, 0xFFFFFF, 190);
    lv_obj_set_size(tab_pill, 170, 42);
    lv_obj_align(tab_pill, LV_ALIGN_RIGHT_MID, -20, 0);

    lv_obj_t *btn_wifi = lv_button_create(tab_pill);
    lv_obj_set_pos(btn_wifi, 10, 4);
    lv_obj_set_size(btn_wifi, 68, 34);
    server_cfg_style_btn(btn_wifi, 0xEEF6FF, SV_COL_BORDER);
    lv_obj_t *lbl_wifi = lv_label_create(btn_wifi);
    lv_label_set_text(lbl_wifi, "WiFi");
    server_cfg_style_btn_label(lbl_wifi, SV_COL_TITLE);
    lv_obj_add_event_cb(btn_wifi, server_cfg_nav_wifi_event_handler, LV_EVENT_CLICKED, ui);

    lv_obj_t *btn_server = lv_button_create(tab_pill);
    lv_obj_set_pos(btn_server, 92, 4);
    lv_obj_set_size(btn_server, 68, 34);
    server_cfg_style_btn(btn_server, SV_COL_PRIMARY, 0);
    lv_obj_t *lbl_server = lv_label_create(btn_server);
    lv_label_set_text(lbl_server, "服务器");
    server_cfg_style_btn_label(lbl_server, 0xFFFFFF);

    ui->ServerConfig_cont_panel = lv_obj_create(ui->ServerConfig);
    server_cfg_style_card(ui->ServerConfig_cont_panel, SV_COL_CARD, 240);
    lv_obj_set_pos(ui->ServerConfig_cont_panel, 24, 78);
    lv_obj_set_size(ui->ServerConfig_cont_panel, 752, 300);

    lv_obj_t *panel_title = lv_label_create(ui->ServerConfig_cont_panel);
    lv_label_set_text(panel_title, "服务接入");
    lv_obj_set_style_text_color(panel_title, lv_color_hex(SV_COL_TITLE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(panel_title, gui_assets_get_font_20(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(panel_title, 20, 16);

    lv_obj_t *panel_note = lv_label_create(ui->ServerConfig_cont_panel);
    lv_label_set_text(panel_note, "地址保存到 SD 后由 ESP32 同步应用");
    lv_obj_set_style_text_color(panel_note, lv_color_hex(SV_COL_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(panel_note, gui_assets_get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(panel_note, 108, 22);

    lv_obj_t *info_card = lv_obj_create(ui->ServerConfig_cont_panel);
    server_cfg_style_card(info_card, SV_COL_INFO_BG, 255);
    lv_obj_set_pos(info_card, 530, 18);
    lv_obj_set_size(info_card, 202, 94);

    lv_obj_t *info_title = lv_label_create(info_card);
    lv_label_set_text(info_title, "接入建议");
    lv_obj_set_style_text_color(info_title, lv_color_hex(SV_COL_CYAN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(info_title, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(info_title, 14, 12);

    lv_obj_t *info_body = lv_label_create(info_card);
    lv_obj_set_width(info_body, 172);
    lv_label_set_long_mode(info_body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(info_body, "IP 与端口决定上传目标\n节点 ID 用于设备唯一标识");
    lv_obj_set_style_text_color(info_body, lv_color_hex(SV_COL_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(info_body, gui_assets_get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(info_body, 14, 38);

    ui->ServerConfig_lbl_ip = lv_label_create(ui->ServerConfig_cont_panel);
    lv_label_set_text(ui->ServerConfig_lbl_ip, "服务器 IP");
    lv_obj_set_style_text_color(ui->ServerConfig_lbl_ip, lv_color_hex(SV_COL_TITLE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_lbl_ip, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(ui->ServerConfig_lbl_ip, 20, 72);

    ui->ServerConfig_ta_ip = lv_textarea_create(ui->ServerConfig_cont_panel);
    lv_obj_set_pos(ui->ServerConfig_ta_ip, 20, 98);
    lv_obj_set_size(ui->ServerConfig_ta_ip, 330, 44);
    lv_textarea_set_text(ui->ServerConfig_ta_ip, "192.168.10.43");
    server_cfg_style_input(ui->ServerConfig_ta_ip);

    ui->ServerConfig_lbl_port = lv_label_create(ui->ServerConfig_cont_panel);
    lv_label_set_text(ui->ServerConfig_lbl_port, "端口");
    lv_obj_set_style_text_color(ui->ServerConfig_lbl_port, lv_color_hex(SV_COL_TITLE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_lbl_port, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(ui->ServerConfig_lbl_port, 376, 72);

    ui->ServerConfig_ta_port = lv_textarea_create(ui->ServerConfig_cont_panel);
    lv_obj_set_pos(ui->ServerConfig_ta_port, 376, 98);
    lv_obj_set_size(ui->ServerConfig_ta_port, 130, 44);
    lv_textarea_set_text(ui->ServerConfig_ta_port, "5000");
    server_cfg_style_input(ui->ServerConfig_ta_port);

    ui->ServerConfig_lbl_id = lv_label_create(ui->ServerConfig_cont_panel);
    lv_label_set_text(ui->ServerConfig_lbl_id, "设备 ID");
    lv_obj_set_style_text_color(ui->ServerConfig_lbl_id, lv_color_hex(SV_COL_TITLE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_lbl_id, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(ui->ServerConfig_lbl_id, 20, 154);

    ui->ServerConfig_ta_id = lv_textarea_create(ui->ServerConfig_cont_panel);
    lv_obj_set_pos(ui->ServerConfig_ta_id, 20, 180);
    lv_obj_set_size(ui->ServerConfig_ta_id, 330, 44);
    lv_textarea_set_text(ui->ServerConfig_ta_id, "STM32_H7_Node");
    server_cfg_style_input(ui->ServerConfig_ta_id);

    ui->ServerConfig_lbl_loc = lv_label_create(ui->ServerConfig_cont_panel);
    lv_label_set_text(ui->ServerConfig_lbl_loc, "安装位置");
    lv_obj_set_style_text_color(ui->ServerConfig_lbl_loc, lv_color_hex(SV_COL_TITLE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_lbl_loc, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(ui->ServerConfig_lbl_loc, 376, 154);

    ui->ServerConfig_ta_loc = lv_textarea_create(ui->ServerConfig_cont_panel);
    lv_obj_set_pos(ui->ServerConfig_ta_loc, 376, 180);
    lv_obj_set_size(ui->ServerConfig_ta_loc, 330, 44);
    lv_textarea_set_placeholder_text(ui->ServerConfig_ta_loc, "例如：风机箱 A01");
    server_cfg_style_input(ui->ServerConfig_ta_loc);

    lv_obj_t *status_card = lv_obj_create(ui->ServerConfig_cont_panel);
    server_cfg_style_card(status_card, SV_COL_FIELD, 255);
    lv_obj_set_pos(status_card, 20, 244);
    lv_obj_set_size(status_card, 712, 40);

    ui->ServerConfig_lbl_status = lv_label_create(status_card);
    lv_obj_set_width(ui->ServerConfig_lbl_status, 688);
    lv_label_set_long_mode(ui->ServerConfig_lbl_status, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->ServerConfig_lbl_status, "状态：等待操作");
    lv_obj_set_style_text_color(ui->ServerConfig_lbl_status, lv_color_hex(SV_COL_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_lbl_status, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui->ServerConfig_lbl_status, LV_ALIGN_LEFT_MID, 12, 0);

    ui->ServerConfig_btn_back = lv_button_create(ui->ServerConfig);
    lv_obj_set_pos(ui->ServerConfig_btn_back, 40, 404);
    lv_obj_set_size(ui->ServerConfig_btn_back, 168, 48);
    server_cfg_style_btn(ui->ServerConfig_btn_back, SV_COL_GRAY, 0);
    ui->ServerConfig_btn_back_label = lv_label_create(ui->ServerConfig_btn_back);
    lv_label_set_text(ui->ServerConfig_btn_back_label, "返回");
    server_cfg_style_btn_label(ui->ServerConfig_btn_back_label, 0xFFFFFF);

    ui->ServerConfig_btn_load = lv_button_create(ui->ServerConfig);
    lv_obj_set_pos(ui->ServerConfig_btn_load, 316, 404);
    lv_obj_set_size(ui->ServerConfig_btn_load, 168, 48);
    server_cfg_style_btn(ui->ServerConfig_btn_load, SV_COL_AMBER, 0);
    ui->ServerConfig_btn_load_label = lv_label_create(ui->ServerConfig_btn_load);
    lv_label_set_text(ui->ServerConfig_btn_load_label, "读取配置");
    server_cfg_style_btn_label(ui->ServerConfig_btn_load_label, 0xFFFFFF);

    ui->ServerConfig_btn_save = lv_button_create(ui->ServerConfig);
    lv_obj_set_pos(ui->ServerConfig_btn_save, 592, 404);
    lv_obj_set_size(ui->ServerConfig_btn_save, 168, 48);
    server_cfg_style_btn(ui->ServerConfig_btn_save, SV_COL_PRIMARY, 0);
    ui->ServerConfig_btn_save_label = lv_label_create(ui->ServerConfig_btn_save);
    lv_label_set_text(ui->ServerConfig_btn_save_label, "保存配置");
    server_cfg_style_btn_label(ui->ServerConfig_btn_save_label, 0xFFFFFF);

    ui->ServerConfig_kb = lv_keyboard_create(ui->ServerConfig);
    lv_obj_set_size(ui->ServerConfig_kb, 800, 200);
    lv_obj_align(ui->ServerConfig_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(ui->ServerConfig_kb, LV_OBJ_FLAG_HIDDEN);

    lv_obj_update_layout(ui->ServerConfig);
    events_init_ServerConfig(ui);
}
