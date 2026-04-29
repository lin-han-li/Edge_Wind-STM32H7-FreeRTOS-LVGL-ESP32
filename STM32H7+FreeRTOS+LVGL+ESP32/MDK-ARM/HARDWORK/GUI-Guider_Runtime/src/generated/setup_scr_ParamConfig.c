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

void setup_scr_ParamConfig(lv_ui *ui)
{
    //Write codes ParamConfig (Screen)
    ui->ParamConfig = lv_obj_create(NULL);
    lv_obj_set_size(ui->ParamConfig, 800, 480);
    lv_obj_set_scrollbar_mode(ui->ParamConfig, LV_SCROLLBAR_MODE_OFF);

    //Write style for ParamConfig
    lv_obj_set_style_bg_opa(ui->ParamConfig, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->ParamConfig, lv_color_hex(0x65adff), LV_PART_MAIN|LV_STATE_DEFAULT);

    //Header (match Main_x header)
    lv_obj_t * header = lv_obj_create(ui->ParamConfig);
    lv_obj_set_size(header, 800, 50);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_opa(header, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    //Write codes ParamConfig_lbl_title (Header)
    ui->ParamConfig_lbl_title = lv_label_create(header);
    /* 右上角新增两枚按钮，缩短标题宽度避免遮挡 */
    lv_obj_set_width(ui->ParamConfig_lbl_title, 520);
    lv_label_set_long_mode(ui->ParamConfig_lbl_title, LV_LABEL_LONG_CLIP);
    lv_label_set_text(ui->ParamConfig_lbl_title, "通讯参数配置 (Comm_Params)");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_title, gui_assets_get_font_30(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_align(ui->ParamConfig_lbl_title, LV_ALIGN_LEFT_MID, 20, 0);

    /* Header quick presets: LAN / WAN */
    ui->ParamConfig_btn_wan = lv_button_create(header);
    lv_obj_set_size(ui->ParamConfig_btn_wan, 110, 34);
    lv_obj_align(ui->ParamConfig_btn_wan, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_radius(ui->ParamConfig_btn_wan, 17, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->ParamConfig_btn_wan, lv_color_hex(0xFFA500), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->ParamConfig_btn_wan, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    ui->ParamConfig_lbl_wan = lv_label_create(ui->ParamConfig_btn_wan);
    lv_label_set_text(ui->ParamConfig_lbl_wan, "公网");
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_wan, gui_assets_get_font_16(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_wan, lv_color_hex(0x000000), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_center(ui->ParamConfig_lbl_wan);

    ui->ParamConfig_btn_lan = lv_button_create(header);
    lv_obj_set_size(ui->ParamConfig_btn_lan, 110, 34);
    lv_obj_align_to(ui->ParamConfig_btn_lan, ui->ParamConfig_btn_wan, LV_ALIGN_OUT_LEFT_MID, -10, 0);
    lv_obj_set_style_radius(ui->ParamConfig_btn_lan, 17, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->ParamConfig_btn_lan, lv_color_hex(0x3dfb00), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->ParamConfig_btn_lan, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    ui->ParamConfig_lbl_lan = lv_label_create(ui->ParamConfig_btn_lan);
    lv_label_set_text(ui->ParamConfig_lbl_lan, "局域网");
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_lan, gui_assets_get_font_16(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_lan, lv_color_hex(0x000000), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_center(ui->ParamConfig_lbl_lan);

    //Container
    ui->ParamConfig_cont_panel = lv_obj_create(ui->ParamConfig);
    lv_obj_set_pos(ui->ParamConfig_cont_panel, 50, 60);
    /* panel 加高：容纳新增“分段发送”与更大的 tips */
    lv_obj_set_size(ui->ParamConfig_cont_panel, 700, 290);
    lv_obj_set_scrollbar_mode(ui->ParamConfig_cont_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(ui->ParamConfig_cont_panel, lv_color_hex(0xFFFFFF), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->ParamConfig_cont_panel, 230, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->ParamConfig_cont_panel, 10, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->ParamConfig_cont_panel, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    // --- Row 1: Heartbeat & Send Limit ---

    // Heartbeat Label
    ui->ParamConfig_lbl_heartbeat = lv_label_create(ui->ParamConfig_cont_panel);
    /* 上移：给下方参数+tips留空间，避免与键盘遮挡重叠 */
    lv_obj_set_pos(ui->ParamConfig_lbl_heartbeat, 20, 0);
    lv_label_set_text(ui->ParamConfig_lbl_heartbeat, "Heartbeat (心跳间隔 ms)");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_heartbeat, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_heartbeat, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);

    // Heartbeat TextArea
    ui->ParamConfig_ta_heartbeat = lv_textarea_create(ui->ParamConfig_cont_panel);
    /* 与第二行一致：输入框始终在标签下方固定间距，避免不同字号/基线导致间距不一致 */
    lv_obj_align_to(ui->ParamConfig_ta_heartbeat, ui->ParamConfig_lbl_heartbeat, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_set_size(ui->ParamConfig_ta_heartbeat, 320, 40);
    lv_textarea_set_one_line(ui->ParamConfig_ta_heartbeat, true);
    lv_textarea_set_accepted_chars(ui->ParamConfig_ta_heartbeat, "0123456789");
    lv_textarea_set_text(ui->ParamConfig_ta_heartbeat, "5000");
    lv_obj_set_style_text_font(ui->ParamConfig_ta_heartbeat, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->ParamConfig_ta_heartbeat, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->ParamConfig_ta_heartbeat, lv_color_hex(0xCCCCCC), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ParamConfig() 里绑定事件 */

    // Send Limit Label
    ui->ParamConfig_lbl_sendlimit = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_set_pos(ui->ParamConfig_lbl_sendlimit, 360, 0);
    lv_label_set_text(ui->ParamConfig_lbl_sendlimit, "Min Interval (发包限频 ms)");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_sendlimit, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_sendlimit, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);

    // Send Limit TextArea
    ui->ParamConfig_ta_sendlimit = lv_textarea_create(ui->ParamConfig_cont_panel);
    /* 与第二行一致：输入框始终在标签下方固定间距 */
    lv_obj_align_to(ui->ParamConfig_ta_sendlimit, ui->ParamConfig_lbl_sendlimit, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_set_size(ui->ParamConfig_ta_sendlimit, 320, 40);
    lv_textarea_set_one_line(ui->ParamConfig_ta_sendlimit, true);
    lv_textarea_set_accepted_chars(ui->ParamConfig_ta_sendlimit, "0123456789");
    lv_textarea_set_text(ui->ParamConfig_ta_sendlimit, "200");
    lv_obj_set_style_text_font(ui->ParamConfig_ta_sendlimit, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->ParamConfig_ta_sendlimit, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->ParamConfig_ta_sendlimit, lv_color_hex(0xCCCCCC), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ParamConfig() 里绑定事件 */

    // --- Row 2: HTTP Timeout & Hard Reset ---

    // HTTP Timeout Label
    ui->ParamConfig_lbl_httptimeout = lv_label_create(ui->ParamConfig_cont_panel);
    /* 采用相对对齐：避免不同字体高度导致压线重叠 */
    lv_obj_align_to(ui->ParamConfig_lbl_httptimeout, ui->ParamConfig_ta_heartbeat, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_label_set_text(ui->ParamConfig_lbl_httptimeout, "HTTP Timeout (回包超时 ms)");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_httptimeout, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_httptimeout, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);

    // HTTP Timeout TextArea
    ui->ParamConfig_ta_httptimeout = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_align_to(ui->ParamConfig_ta_httptimeout, ui->ParamConfig_lbl_httptimeout, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_set_size(ui->ParamConfig_ta_httptimeout, 320, 40);
    lv_textarea_set_one_line(ui->ParamConfig_ta_httptimeout, true);
    lv_textarea_set_accepted_chars(ui->ParamConfig_ta_httptimeout, "0123456789");
    lv_textarea_set_text(ui->ParamConfig_ta_httptimeout, "1200");
    lv_obj_set_style_text_font(ui->ParamConfig_ta_httptimeout, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->ParamConfig_ta_httptimeout, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->ParamConfig_ta_httptimeout, lv_color_hex(0xCCCCCC), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ParamConfig() 里绑定事件 */

    // Hard Reset Label
    ui->ParamConfig_lbl_hardreset = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_align_to(ui->ParamConfig_lbl_hardreset, ui->ParamConfig_ta_sendlimit, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_label_set_text(ui->ParamConfig_lbl_hardreset, "Reset Thresh (无响应复位 s)");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_hardreset, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_hardreset, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);

    // Hard Reset TextArea
    ui->ParamConfig_ta_hardreset = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_align_to(ui->ParamConfig_ta_hardreset, ui->ParamConfig_lbl_hardreset, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_set_size(ui->ParamConfig_ta_hardreset, 320, 40);
    lv_textarea_set_one_line(ui->ParamConfig_ta_hardreset, true);
    lv_textarea_set_accepted_chars(ui->ParamConfig_ta_hardreset, "0123456789");
    lv_textarea_set_text(ui->ParamConfig_ta_hardreset, "60");
    lv_obj_set_style_text_font(ui->ParamConfig_ta_hardreset, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->ParamConfig_ta_hardreset, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->ParamConfig_ta_hardreset, lv_color_hex(0xCCCCCC), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ParamConfig() 里绑定事件 */

    // --- Row 3: Chunked send ---
    ui->ParamConfig_lbl_chunkkb = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_align_to(ui->ParamConfig_lbl_chunkkb, ui->ParamConfig_ta_httptimeout, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_label_set_text(ui->ParamConfig_lbl_chunkkb, "Chunk (每段 KB)");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_chunkkb, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_chunkkb, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);

    ui->ParamConfig_ta_chunkkb = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_align_to(ui->ParamConfig_ta_chunkkb, ui->ParamConfig_lbl_chunkkb, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_obj_set_size(ui->ParamConfig_ta_chunkkb, 320, 32);
    lv_textarea_set_one_line(ui->ParamConfig_ta_chunkkb, true);
    lv_textarea_set_accepted_chars(ui->ParamConfig_ta_chunkkb, "0123456789");
    lv_textarea_set_max_length(ui->ParamConfig_ta_chunkkb, 2);
    lv_textarea_set_text(ui->ParamConfig_ta_chunkkb, "4");
    lv_obj_set_style_text_font(ui->ParamConfig_ta_chunkkb, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->ParamConfig_ta_chunkkb, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->ParamConfig_ta_chunkkb, lv_color_hex(0xCCCCCC), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ParamConfig() 里绑定事件 */

    ui->ParamConfig_lbl_chunkdelay = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_align_to(ui->ParamConfig_lbl_chunkdelay, ui->ParamConfig_ta_hardreset, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_label_set_text(ui->ParamConfig_lbl_chunkdelay, "Delay (每段间隔 ms)");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_chunkdelay, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_chunkdelay, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);

    ui->ParamConfig_ta_chunkdelay = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_align_to(ui->ParamConfig_ta_chunkdelay, ui->ParamConfig_lbl_chunkdelay, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_obj_set_size(ui->ParamConfig_ta_chunkdelay, 320, 32);
    lv_textarea_set_one_line(ui->ParamConfig_ta_chunkdelay, true);
    lv_textarea_set_accepted_chars(ui->ParamConfig_ta_chunkdelay, "0123456789");
    lv_textarea_set_max_length(ui->ParamConfig_ta_chunkdelay, 3);
    lv_textarea_set_text(ui->ParamConfig_ta_chunkdelay, "10");
    lv_obj_set_style_text_font(ui->ParamConfig_ta_chunkdelay, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->ParamConfig_ta_chunkdelay, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->ParamConfig_ta_chunkdelay, lv_color_hex(0xCCCCCC), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ParamConfig() 里绑定事件 */

    // Quick action: disable chunking (one click)
    ui->ParamConfig_btn_nochunk = lv_button_create(ui->ParamConfig_cont_panel);
    lv_obj_align_to(ui->ParamConfig_btn_nochunk, ui->ParamConfig_ta_chunkdelay, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_obj_set_size(ui->ParamConfig_btn_nochunk, 150, 24);
    lv_obj_set_style_radius(ui->ParamConfig_btn_nochunk, 12, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->ParamConfig_btn_nochunk, lv_color_hex(0x6C757D), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->ParamConfig_btn_nochunk, 200, LV_PART_MAIN|LV_STATE_DEFAULT);

    ui->ParamConfig_lbl_nochunk = lv_label_create(ui->ParamConfig_btn_nochunk);
    lv_label_set_text(ui->ParamConfig_lbl_nochunk, "一键无分段");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_nochunk, lv_color_hex(0xFFFFFF), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_nochunk, gui_assets_get_font_16(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_center(ui->ParamConfig_lbl_nochunk);

    // --- Row 4 (Right): Downsample ---
    ui->ParamConfig_lbl_downsample = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_align_to(ui->ParamConfig_lbl_downsample, ui->ParamConfig_btn_nochunk, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_label_set_text(ui->ParamConfig_lbl_downsample, "Downsample (降采样 step)");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_downsample, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_downsample, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);

    ui->ParamConfig_ta_downsample = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_align_to(ui->ParamConfig_ta_downsample, ui->ParamConfig_lbl_downsample, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_obj_set_size(ui->ParamConfig_ta_downsample, 320, 32);
    lv_textarea_set_one_line(ui->ParamConfig_ta_downsample, true);
    lv_textarea_set_accepted_chars(ui->ParamConfig_ta_downsample, "0123456789");
    lv_textarea_set_max_length(ui->ParamConfig_ta_downsample, 2);
    lv_textarea_set_text(ui->ParamConfig_ta_downsample, "1");
    lv_obj_set_style_text_font(ui->ParamConfig_ta_downsample, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->ParamConfig_ta_downsample, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->ParamConfig_ta_downsample, lv_color_hex(0xCCCCCC), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ParamConfig() 里绑定事件 */

    // --- Row 5 (Right): Upload Points ---
    ui->ParamConfig_lbl_uploadpoints = lv_label_create(ui->ParamConfig_cont_panel);
    lv_obj_align_to(ui->ParamConfig_lbl_uploadpoints, ui->ParamConfig_ta_downsample, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_label_set_text(ui->ParamConfig_lbl_uploadpoints, "Upload Points (降采样后, 256步进)");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_uploadpoints, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_uploadpoints, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);

    ui->ParamConfig_ta_uploadpoints = lv_textarea_create(ui->ParamConfig_cont_panel);
    lv_obj_align_to(ui->ParamConfig_ta_uploadpoints, ui->ParamConfig_lbl_uploadpoints, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_obj_set_size(ui->ParamConfig_ta_uploadpoints, 320, 32);
    lv_textarea_set_one_line(ui->ParamConfig_ta_uploadpoints, true);
    lv_textarea_set_accepted_chars(ui->ParamConfig_ta_uploadpoints, "0123456789");
    lv_textarea_set_max_length(ui->ParamConfig_ta_uploadpoints, 4);
    lv_textarea_set_text(ui->ParamConfig_ta_uploadpoints, "4096");
    lv_obj_set_style_text_font(ui->ParamConfig_ta_uploadpoints, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->ParamConfig_ta_uploadpoints, 2, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->ParamConfig_ta_uploadpoints, lv_color_hex(0xCCCCCC), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ParamConfig() 里绑定事件 */

    /* Tips / validation area (inside panel)
     * - Show recommended values and warnings to avoid misconfiguration causing reconnect loops.
     */
    ui->ParamConfig_lbl_tips = lv_label_create(ui->ParamConfig_cont_panel);
    /* tips 相对放在 Chunk(KB) 下方：避免重叠 */
    lv_obj_align_to(ui->ParamConfig_lbl_tips, ui->ParamConfig_ta_chunkkb, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    /* 左下区域：给右下“降采样”留出空间 */
    /* tips 文案变长（含 step->points），加高以免显示不全 */
    lv_obj_set_size(ui->ParamConfig_lbl_tips, 340, 120);
    lv_label_set_long_mode(ui->ParamConfig_lbl_tips, LV_LABEL_LONG_WRAP);
    lv_label_set_text(ui->ParamConfig_lbl_tips,
                      "建议值：心跳5000ms，限频200ms，回包1200ms，复位60s\n"
                      "降采样：step=1(全量)，4(推荐)\n"
                      "分段：4KB / 10ms\n"
                      "规则：回包超时 < 复位阈值×1000；复位≥30s更稳");
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_tips, gui_assets_get_font_12(), LV_PART_MAIN|LV_STATE_DEFAULT);
    /* 正常提示用黑色（警告/错误由运行时逻辑改色） */
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_tips, lv_color_hex(0x111111), LV_PART_MAIN|LV_STATE_DEFAULT);

    // --- Buttons ---

    // Back Button
    ui->ParamConfig_btn_back = lv_button_create(ui->ParamConfig);
    lv_obj_set_pos(ui->ParamConfig_btn_back, 50, 360);
    lv_obj_set_size(ui->ParamConfig_btn_back, 150, 50);
    lv_obj_set_style_bg_color(ui->ParamConfig_btn_back, lv_color_hex(0x999999), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->ParamConfig_btn_back, 25, LV_PART_MAIN|LV_STATE_DEFAULT);
    /* events_init_ParamConfig() 里绑定事件 */

    ui->ParamConfig_btn_back_label = lv_label_create(ui->ParamConfig_btn_back);
    lv_label_set_text(ui->ParamConfig_btn_back_label, "返回");
    lv_obj_set_style_text_font(ui->ParamConfig_btn_back_label, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_center(ui->ParamConfig_btn_back_label);

    // Load Button (Orange)
    ui->ParamConfig_btn_load = lv_button_create(ui->ParamConfig);
    lv_obj_set_pos(ui->ParamConfig_btn_load, 325, 360);
    lv_obj_set_size(ui->ParamConfig_btn_load, 150, 50);
    lv_obj_set_style_bg_color(ui->ParamConfig_btn_load, lv_color_hex(0xFFA500), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->ParamConfig_btn_load, 25, LV_PART_MAIN|LV_STATE_DEFAULT);

    ui->ParamConfig_btn_load_label = lv_label_create(ui->ParamConfig_btn_load);
    lv_label_set_text(ui->ParamConfig_btn_load_label, "读取配置");
    lv_obj_set_style_text_font(ui->ParamConfig_btn_load_label, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_center(ui->ParamConfig_btn_load_label);

    // Save Button (Green)
    ui->ParamConfig_btn_save = lv_button_create(ui->ParamConfig);
    lv_obj_set_pos(ui->ParamConfig_btn_save, 600, 360);
    lv_obj_set_size(ui->ParamConfig_btn_save, 150, 50);
    lv_obj_set_style_bg_color(ui->ParamConfig_btn_save, lv_color_hex(0x3dfb00), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->ParamConfig_btn_save, 25, LV_PART_MAIN|LV_STATE_DEFAULT);

    ui->ParamConfig_btn_save_label = lv_label_create(ui->ParamConfig_btn_save);
    lv_label_set_text(ui->ParamConfig_btn_save_label, "保存配置");
    lv_obj_set_style_text_color(ui->ParamConfig_btn_save_label, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_btn_save_label, gui_assets_get_font_20(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_center(ui->ParamConfig_btn_save_label);

    // Status Label
    ui->ParamConfig_lbl_status = lv_label_create(ui->ParamConfig);
    lv_obj_set_pos(ui->ParamConfig_lbl_status, 50, 420);
    lv_label_set_text(ui->ParamConfig_lbl_status, "状态：默认参数已加载");
    lv_obj_set_style_text_color(ui->ParamConfig_lbl_status, lv_color_hex(0xFFFFFF), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->ParamConfig_lbl_status, gui_assets_get_font_16(), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_width(ui->ParamConfig_lbl_status, 700);

    // Keyboard
    ui->ParamConfig_kb = lv_keyboard_create(ui->ParamConfig);
    lv_obj_set_size(ui->ParamConfig_kb, 800, 200);
    lv_obj_align(ui->ParamConfig_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(ui->ParamConfig_kb, LV_OBJ_FLAG_HIDDEN);
    /* events_init_ParamConfig() 里绑定事件 */

    //Update layout
    lv_obj_update_layout(ui->ParamConfig);
    events_init_ParamConfig(ui);
}
