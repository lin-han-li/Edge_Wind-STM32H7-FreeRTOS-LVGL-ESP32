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

#define WF_W 800
#define WF_H 480
#define WF_COL_SCREEN   0xF0F9FF
#define WF_COL_TOPBAR   0xD7F1FF
#define WF_COL_TITLE    0x0F4C81
#define WF_COL_TEXT     0x334155
#define WF_COL_MUTED    0x64748B
#define WF_COL_CARD     0xFFFFFF
#define WF_COL_FIELD    0xF8FAFC
#define WF_COL_BORDER   0xB7E4F8
#define WF_COL_PRIMARY  0x2563EB
#define WF_COL_CYAN     0x0891B2
#define WF_COL_AMBER    0xD97706
#define WF_COL_GRAY     0x64748B
#define WF_COL_INFO_BG  0xECFEFF

static void wifi_cfg_nav_server_event_handler(lv_event_t *e)
{
    lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!ui) {
        return;
    }
    if (indev) {
        lv_indev_wait_release(indev);
    }
    if (ui->WifiConfig_kb) {
        lv_obj_add_flag(ui->WifiConfig_kb, LV_OBJ_FLAG_HIDDEN);
    }
    ui_load_scr_animation(&guider_ui, &guider_ui.ServerConfig, guider_ui.ServerConfig_del, &guider_ui.WifiConfig_del,
                          setup_scr_ServerConfig, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, false, false);
}

static void wifi_cfg_style_card(lv_obj_t *obj, uint32_t bg, lv_opa_t opa)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(WF_COL_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, 220, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void wifi_cfg_style_btn(lv_obj_t *obj, uint32_t bg, uint32_t border)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, border ? 1 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(border ? border : bg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void wifi_cfg_style_btn_label(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_text_color(obj, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(obj);
}

static void wifi_cfg_style_input(lv_obj_t *obj)
{
    lv_textarea_set_one_line(obj, true);
    lv_obj_set_style_bg_color(obj, lv_color_hex(WF_COL_FIELD), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(obj, lv_color_hex(WF_COL_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, gui_assets_get_font_20(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(WF_COL_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(WF_COL_PRIMARY), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_radius(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void setup_scr_WifiConfig(lv_ui *ui)
{
    ui->WifiConfig = lv_obj_create(NULL);
    lv_obj_remove_style_all(ui->WifiConfig);
    lv_obj_set_size(ui->WifiConfig, WF_W, WF_H);
    lv_obj_set_scrollbar_mode(ui->WifiConfig, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->WifiConfig, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui->WifiConfig, lv_color_hex(WF_COL_SCREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->WifiConfig, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *header = lv_obj_create(ui->WifiConfig);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, WF_W, 60);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(WF_COL_TOPBAR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(header, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *accent = lv_obj_create(header);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, 4, 18);
    lv_obj_set_style_bg_color(accent, lv_color_hex(WF_COL_CYAN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(accent, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(accent, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(accent, LV_ALIGN_LEFT_MID, 20, 0);

    ui->WifiConfig_lbl_title = lv_label_create(header);
    lv_obj_set_width(ui->WifiConfig_lbl_title, 250);
    lv_label_set_long_mode(ui->WifiConfig_lbl_title, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ui->WifiConfig_lbl_title, "网络设置");
    lv_obj_set_style_text_color(ui->WifiConfig_lbl_title, lv_color_hex(WF_COL_TITLE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->WifiConfig_lbl_title, gui_assets_get_font_30(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui->WifiConfig_lbl_title, LV_ALIGN_LEFT_MID, 34, 0);

    lv_obj_t *header_note = lv_label_create(header);
    lv_obj_set_width(header_note, 230);
    lv_label_set_long_mode(header_note, LV_LABEL_LONG_CLIP);
    lv_label_set_text(header_note, "WiFi 接入 / 热点 / 凭据");
    lv_obj_set_style_text_color(header_note, lv_color_hex(WF_COL_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(header_note, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(header_note, LV_ALIGN_LEFT_MID, 330, 0);

    lv_obj_t *tab_pill = lv_obj_create(header);
    wifi_cfg_style_card(tab_pill, 0xFFFFFF, 190);
    lv_obj_set_size(tab_pill, 170, 42);
    lv_obj_align(tab_pill, LV_ALIGN_RIGHT_MID, -20, 0);

    lv_obj_t *btn_wifi = lv_button_create(tab_pill);
    lv_obj_set_pos(btn_wifi, 10, 4);
    lv_obj_set_size(btn_wifi, 68, 34);
    wifi_cfg_style_btn(btn_wifi, WF_COL_PRIMARY, 0);
    lv_obj_t *lbl_wifi = lv_label_create(btn_wifi);
    lv_label_set_text(lbl_wifi, "WiFi");
    wifi_cfg_style_btn_label(lbl_wifi, 0xFFFFFF);

    lv_obj_t *btn_server = lv_button_create(tab_pill);
    lv_obj_set_pos(btn_server, 92, 4);
    lv_obj_set_size(btn_server, 68, 34);
    wifi_cfg_style_btn(btn_server, 0xEEF6FF, WF_COL_BORDER);
    lv_obj_t *lbl_server = lv_label_create(btn_server);
    lv_label_set_text(lbl_server, "服务器");
    wifi_cfg_style_btn_label(lbl_server, WF_COL_TITLE);
    lv_obj_add_event_cb(btn_server, wifi_cfg_nav_server_event_handler, LV_EVENT_CLICKED, ui);

    ui->WifiConfig_cont_panel = lv_obj_create(ui->WifiConfig);
    wifi_cfg_style_card(ui->WifiConfig_cont_panel, WF_COL_CARD, 240);
    lv_obj_set_pos(ui->WifiConfig_cont_panel, 24, 78);
    lv_obj_set_size(ui->WifiConfig_cont_panel, 752, 308);

    lv_obj_t *panel_title = lv_label_create(ui->WifiConfig_cont_panel);
    lv_label_set_text(panel_title, "无线接入");
    lv_obj_set_style_text_color(panel_title, lv_color_hex(WF_COL_TITLE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(panel_title, gui_assets_get_font_20(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(panel_title, 20, 16);

    lv_obj_t *panel_note = lv_label_create(ui->WifiConfig_cont_panel);
    lv_label_set_text(panel_note, "保存到 SD 后由 ESP32 同步应用");
    lv_obj_set_style_text_color(panel_note, lv_color_hex(WF_COL_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(panel_note, gui_assets_get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(panel_note, 108, 22);

    lv_obj_t *info_card = lv_obj_create(ui->WifiConfig_cont_panel);
    wifi_cfg_style_card(info_card, WF_COL_INFO_BG, 255);
    lv_obj_set_pos(info_card, 510, 18);
    lv_obj_set_size(info_card, 222, 118);

    lv_obj_t *info_title = lv_label_create(info_card);
    lv_label_set_text(info_title, "接入建议");
    lv_obj_set_style_text_color(info_title, lv_color_hex(WF_COL_CYAN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(info_title, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(info_title, 14, 12);

    lv_obj_t *info_body = lv_label_create(info_card);
    lv_obj_set_width(info_body, 194);
    lv_label_set_long_mode(info_body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(info_body, "1. 推荐 2.4GHz 热点\n2. 密码建议 8 位以上\n3. 读取配置会从 SD 回填");
    lv_obj_set_style_text_color(info_body, lv_color_hex(WF_COL_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(info_body, gui_assets_get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(info_body, 14, 36);

    ui->WifiConfig_lbl_ssid = lv_label_create(ui->WifiConfig_cont_panel);
    lv_label_set_text(ui->WifiConfig_lbl_ssid, "WiFi SSID");
    lv_obj_set_style_text_color(ui->WifiConfig_lbl_ssid, lv_color_hex(WF_COL_TITLE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->WifiConfig_lbl_ssid, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(ui->WifiConfig_lbl_ssid, 20, 70);

    ui->WifiConfig_ta_ssid = lv_textarea_create(ui->WifiConfig_cont_panel);
    lv_obj_set_pos(ui->WifiConfig_ta_ssid, 20, 96);
    lv_obj_set_size(ui->WifiConfig_ta_ssid, 468, 44);
    lv_textarea_set_placeholder_text(ui->WifiConfig_ta_ssid, "输入热点名称...");
    wifi_cfg_style_input(ui->WifiConfig_ta_ssid);

    ui->WifiConfig_lbl_pwd = lv_label_create(ui->WifiConfig_cont_panel);
    lv_label_set_text(ui->WifiConfig_lbl_pwd, "WiFi 密码");
    lv_obj_set_style_text_color(ui->WifiConfig_lbl_pwd, lv_color_hex(WF_COL_TITLE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->WifiConfig_lbl_pwd, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(ui->WifiConfig_lbl_pwd, 20, 156);

    ui->WifiConfig_ta_pwd = lv_textarea_create(ui->WifiConfig_cont_panel);
    lv_obj_set_pos(ui->WifiConfig_ta_pwd, 20, 182);
    lv_obj_set_size(ui->WifiConfig_ta_pwd, 468, 44);
    lv_textarea_set_password_mode(ui->WifiConfig_ta_pwd, true);
    lv_textarea_set_placeholder_text(ui->WifiConfig_ta_pwd, "输入热点密码...");
    wifi_cfg_style_input(ui->WifiConfig_ta_pwd);

    lv_obj_t *status_card = lv_obj_create(ui->WifiConfig_cont_panel);
    wifi_cfg_style_card(status_card, WF_COL_FIELD, 255);
    lv_obj_set_pos(status_card, 20, 252);
    lv_obj_set_size(status_card, 712, 40);

    ui->WifiConfig_lbl_status = lv_label_create(status_card);
    lv_obj_set_width(ui->WifiConfig_lbl_status, 688);
    lv_label_set_long_mode(ui->WifiConfig_lbl_status, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->WifiConfig_lbl_status, "状态：等待操作");
    lv_obj_set_style_text_color(ui->WifiConfig_lbl_status, lv_color_hex(WF_COL_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->WifiConfig_lbl_status, gui_assets_get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(ui->WifiConfig_lbl_status, LV_ALIGN_LEFT_MID, 12, 0);

    ui->WifiConfig_btn_back = lv_button_create(ui->WifiConfig);
    lv_obj_set_pos(ui->WifiConfig_btn_back, 40, 404);
    lv_obj_set_size(ui->WifiConfig_btn_back, 168, 48);
    wifi_cfg_style_btn(ui->WifiConfig_btn_back, WF_COL_GRAY, 0);
    ui->WifiConfig_btn_back_label = lv_label_create(ui->WifiConfig_btn_back);
    lv_label_set_text(ui->WifiConfig_btn_back_label, "返回");
    wifi_cfg_style_btn_label(ui->WifiConfig_btn_back_label, 0xFFFFFF);

    ui->WifiConfig_btn_scan = lv_button_create(ui->WifiConfig);
    lv_obj_set_pos(ui->WifiConfig_btn_scan, 316, 404);
    lv_obj_set_size(ui->WifiConfig_btn_scan, 168, 48);
    wifi_cfg_style_btn(ui->WifiConfig_btn_scan, WF_COL_AMBER, 0);
    ui->WifiConfig_btn_scan_label = lv_label_create(ui->WifiConfig_btn_scan);
    lv_label_set_text(ui->WifiConfig_btn_scan_label, "读取配置");
    wifi_cfg_style_btn_label(ui->WifiConfig_btn_scan_label, 0xFFFFFF);

    ui->WifiConfig_btn_save = lv_button_create(ui->WifiConfig);
    lv_obj_set_pos(ui->WifiConfig_btn_save, 592, 404);
    lv_obj_set_size(ui->WifiConfig_btn_save, 168, 48);
    wifi_cfg_style_btn(ui->WifiConfig_btn_save, WF_COL_PRIMARY, 0);
    ui->WifiConfig_btn_save_label = lv_label_create(ui->WifiConfig_btn_save);
    lv_label_set_text(ui->WifiConfig_btn_save_label, "保存配置");
    wifi_cfg_style_btn_label(ui->WifiConfig_btn_save_label, 0xFFFFFF);

    ui->WifiConfig_kb = lv_keyboard_create(ui->WifiConfig);
    lv_obj_set_size(ui->WifiConfig_kb, 800, 200);
    lv_obj_align(ui->WifiConfig_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(ui->WifiConfig_kb, LV_OBJ_FLAG_HIDDEN);

    lv_obj_update_layout(ui->WifiConfig);
    events_init_WifiConfig(ui);
}
