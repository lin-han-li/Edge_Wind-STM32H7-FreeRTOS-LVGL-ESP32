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

void setup_scr_ServerConfig(lv_ui *ui)
{
    //Write codes ServerConfig (Screen)
    ui->ServerConfig = lv_obj_create(NULL);
    lv_obj_set_size(ui->ServerConfig, 800, 480);
    lv_obj_set_scrollbar_mode(ui->ServerConfig, LV_SCROLLBAR_MODE_OFF);

    //Write style for ServerConfig
    lv_obj_set_style_bg_opa(ui->ServerConfig, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->ServerConfig, lv_color_hex(0x65adff), LV_PART_MAIN|LV_STATE_DEFAULT);

    //Header (match Main_x header)
    lv_obj_t * header = lv_obj_create(ui->ServerConfig);
    lv_obj_set_size(header, 800, 50);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_opa(header, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    ui->ServerConfig_lbl_title = lv_label_create(header);
    /* 注意：滚动标题会持续重绘，字号大时会明显掉帧；这里改为静态裁切显示 */
    lv_obj_set_width(ui->ServerConfig_lbl_title, 760);
    lv_label_set_long_mode(ui->ServerConfig_lbl_title, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ui->ServerConfig_lbl_title, "服务器配置(Server_Configuration)");
    lv_obj_set_style_text_color(ui->ServerConfig_lbl_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_lbl_title, gui_assets_get_font_30(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(ui->ServerConfig_lbl_title, LV_ALIGN_LEFT_MID, 20, 0);

    //Container
    ui->ServerConfig_cont_panel = lv_obj_create(ui->ServerConfig);
    lv_obj_set_pos(ui->ServerConfig_cont_panel, 50, 60);
    lv_obj_set_size(ui->ServerConfig_cont_panel, 700, 260);
    lv_obj_set_style_bg_color(ui->ServerConfig_cont_panel, lv_color_hex(0xFFFFFF), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->ServerConfig_cont_panel, 230, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->ServerConfig_cont_panel, 10, LV_PART_MAIN|LV_STATE_DEFAULT);

    // --- Row 1: IP & Port ---
    
    // IP Label
    ui->ServerConfig_lbl_ip = lv_label_create(ui->ServerConfig_cont_panel);
    lv_obj_set_pos(ui->ServerConfig_lbl_ip, 20, 20);
    lv_label_set_text(ui->ServerConfig_lbl_ip, "Server IP");
    lv_obj_set_style_text_color(ui->ServerConfig_lbl_ip, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_lbl_ip, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);

    // IP TextArea
    ui->ServerConfig_ta_ip = lv_textarea_create(ui->ServerConfig_cont_panel);
    lv_obj_set_pos(ui->ServerConfig_ta_ip, 20, 50);
    lv_obj_set_size(ui->ServerConfig_ta_ip, 320, 40);
    lv_textarea_set_one_line(ui->ServerConfig_ta_ip, true);
    lv_textarea_set_text(ui->ServerConfig_ta_ip, "192.168.10.43"); // Default
    lv_obj_set_style_text_font(ui->ServerConfig_ta_ip, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->ServerConfig_ta_ip, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->ServerConfig_ta_ip, lv_color_hex(0xCCCCCC), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ServerConfig() 里绑定事件 */

    // Port Label
    ui->ServerConfig_lbl_port = lv_label_create(ui->ServerConfig_cont_panel);
    lv_obj_set_pos(ui->ServerConfig_lbl_port, 360, 20);
    lv_label_set_text(ui->ServerConfig_lbl_port, "Port");
    lv_obj_set_style_text_color(ui->ServerConfig_lbl_port, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_lbl_port, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);

    // Port TextArea
    ui->ServerConfig_ta_port = lv_textarea_create(ui->ServerConfig_cont_panel);
    lv_obj_set_pos(ui->ServerConfig_ta_port, 360, 50);
    lv_obj_set_size(ui->ServerConfig_ta_port, 300, 40);
    lv_textarea_set_one_line(ui->ServerConfig_ta_port, true);
    lv_textarea_set_text(ui->ServerConfig_ta_port, "5000"); // Default
    lv_obj_set_style_text_font(ui->ServerConfig_ta_port, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->ServerConfig_ta_port, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->ServerConfig_ta_port, lv_color_hex(0xCCCCCC), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ServerConfig() 里绑定事件 */

    // --- Row 2: Node ID & Location (2x2 layout) ---

    // ID Label (left)
    ui->ServerConfig_lbl_id = lv_label_create(ui->ServerConfig_cont_panel);
    lv_obj_set_pos(ui->ServerConfig_lbl_id, 20, 110);
    lv_obj_set_size(ui->ServerConfig_lbl_id, 320, LV_SIZE_CONTENT);
    lv_label_set_long_mode(ui->ServerConfig_lbl_id, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ui->ServerConfig_lbl_id, "Device ID (节点名称)");
    lv_obj_set_style_text_color(ui->ServerConfig_lbl_id, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_lbl_id, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);

    // ID TextArea (left)
    ui->ServerConfig_ta_id = lv_textarea_create(ui->ServerConfig_cont_panel);
    lv_obj_set_pos(ui->ServerConfig_ta_id, 20, 140);
    lv_obj_set_size(ui->ServerConfig_ta_id, 320, 40);
    lv_textarea_set_one_line(ui->ServerConfig_ta_id, true);
    lv_textarea_set_text(ui->ServerConfig_ta_id, "STM32_H7_Node"); // Default
    lv_obj_set_style_text_font(ui->ServerConfig_ta_id, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->ServerConfig_ta_id, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->ServerConfig_ta_id, lv_color_hex(0xCCCCCC), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ServerConfig() 里绑定事件 */

    // Location Label (right)
    ui->ServerConfig_lbl_loc = lv_label_create(ui->ServerConfig_cont_panel);
    lv_obj_set_pos(ui->ServerConfig_lbl_loc, 360, 110);
    lv_obj_set_size(ui->ServerConfig_lbl_loc, 300, LV_SIZE_CONTENT);
    lv_label_set_long_mode(ui->ServerConfig_lbl_loc, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ui->ServerConfig_lbl_loc, "Node Location (节点位置)");
    lv_obj_set_style_text_color(ui->ServerConfig_lbl_loc, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_lbl_loc, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);

    // Location TextArea (right)
    ui->ServerConfig_ta_loc = lv_textarea_create(ui->ServerConfig_cont_panel);
    lv_obj_set_pos(ui->ServerConfig_ta_loc, 360, 140);
    lv_obj_set_size(ui->ServerConfig_ta_loc, 300, 40);
    lv_textarea_set_one_line(ui->ServerConfig_ta_loc, true);
    lv_textarea_set_placeholder_text(ui->ServerConfig_ta_loc, "Enter Location...");
    lv_obj_set_style_text_font(ui->ServerConfig_ta_loc, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->ServerConfig_ta_loc, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->ServerConfig_ta_loc, lv_color_hex(0xCCCCCC), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ServerConfig() 里绑定事件 */

    // --- Buttons ---

    // Back Button
    ui->ServerConfig_btn_back = lv_button_create(ui->ServerConfig);
    lv_obj_set_pos(ui->ServerConfig_btn_back, 50, 340);
    lv_obj_set_size(ui->ServerConfig_btn_back, 150, 50);
    lv_obj_set_style_bg_color(ui->ServerConfig_btn_back, lv_color_hex(0x999999), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->ServerConfig_btn_back, 25, LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ServerConfig() 里绑定事件 */

    ui->ServerConfig_btn_back_label = lv_label_create(ui->ServerConfig_btn_back);
    lv_label_set_text(ui->ServerConfig_btn_back_label, "返回");
    lv_obj_set_style_text_font(ui->ServerConfig_btn_back_label, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_center(ui->ServerConfig_btn_back_label);

    // Load Button (Orange)
    ui->ServerConfig_btn_load = lv_button_create(ui->ServerConfig);
    lv_obj_set_pos(ui->ServerConfig_btn_load, 325, 340);
    lv_obj_set_size(ui->ServerConfig_btn_load, 150, 50);
    lv_obj_set_style_bg_color(ui->ServerConfig_btn_load, lv_color_hex(0xFFA500), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->ServerConfig_btn_load, 25, LV_PART_MAIN|LV_STATE_DEFAULT);

    ui->ServerConfig_btn_load_label = lv_label_create(ui->ServerConfig_btn_load);
    lv_label_set_text(ui->ServerConfig_btn_load_label, "读取配置");
    lv_obj_set_style_text_color(ui->ServerConfig_btn_load_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_btn_load_label, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_center(ui->ServerConfig_btn_load_label);

    // Save Button (Green)
    ui->ServerConfig_btn_save = lv_button_create(ui->ServerConfig);
    lv_obj_set_pos(ui->ServerConfig_btn_save, 600, 340);
    lv_obj_set_size(ui->ServerConfig_btn_save, 150, 50);
    lv_obj_set_style_bg_color(ui->ServerConfig_btn_save, lv_color_hex(0x3dfb00), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->ServerConfig_btn_save, 25, LV_PART_MAIN|LV_STATE_DEFAULT);

    ui->ServerConfig_btn_save_label = lv_label_create(ui->ServerConfig_btn_save);
    lv_label_set_text(ui->ServerConfig_btn_save_label, "保存配置");
    lv_obj_set_style_text_color(ui->ServerConfig_btn_save_label, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_btn_save_label, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_center(ui->ServerConfig_btn_save_label);

    // Status Label
    ui->ServerConfig_lbl_status = lv_label_create(ui->ServerConfig);
    lv_obj_set_pos(ui->ServerConfig_lbl_status, 50, 400);
    lv_label_set_text(ui->ServerConfig_lbl_status, "状态：等待操作");
    lv_obj_set_style_text_color(ui->ServerConfig_lbl_status, lv_color_hex(0xFFFFFF), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ServerConfig_lbl_status, gui_assets_get_font_16(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_width(ui->ServerConfig_lbl_status, 700);

    // Keyboard
    ui->ServerConfig_kb = lv_keyboard_create(ui->ServerConfig);
    lv_obj_set_size(ui->ServerConfig_kb, 800, 200);
    lv_obj_align(ui->ServerConfig_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(ui->ServerConfig_kb, LV_OBJ_FLAG_HIDDEN);
    /* events_init_ServerConfig() 里绑定事件 */

    //Update layout
    lv_obj_update_layout(ui->ServerConfig);
    events_init_ServerConfig(ui);
}
