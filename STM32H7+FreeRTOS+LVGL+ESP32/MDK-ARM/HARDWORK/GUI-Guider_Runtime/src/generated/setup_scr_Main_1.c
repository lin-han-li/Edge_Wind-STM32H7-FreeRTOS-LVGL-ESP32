/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "lvgl.h"
#include "gui_guider.h"
#include "../../gui_assets.h"
#include "events_init.h"

static void create_tile(lv_obj_t * parent, lv_obj_t ** tile_ptr, lv_obj_t ** img_ptr, lv_obj_t ** lbl_ptr,
                        const char * icon_path, const char * label_text, int32_t col, int32_t row)
{
    *tile_ptr = lv_obj_create(parent);
    lv_obj_set_size(*tile_ptr, 220, 160);
    lv_obj_set_grid_cell(*tile_ptr, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
    lv_obj_set_style_bg_color(*tile_ptr, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(*tile_ptr, 20, LV_PART_MAIN);
    lv_obj_set_style_border_width(*tile_ptr, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(*tile_ptr, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_border_opa(*tile_ptr, 30, LV_PART_MAIN);
    lv_obj_set_style_radius(*tile_ptr, 12, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(*tile_ptr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(*tile_ptr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(*tile_ptr, LV_OBJ_FLAG_CLICKABLE);

    *img_ptr = lv_image_create(*tile_ptr);
    lv_image_set_src(*img_ptr, icon_path);
    lv_obj_align(*img_ptr, LV_ALIGN_CENTER, 0, -15);

    *lbl_ptr = lv_label_create(*tile_ptr);
    lv_label_set_text(*lbl_ptr, label_text);
    lv_obj_set_style_text_font(*lbl_ptr, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_set_style_text_color(*lbl_ptr, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
    lv_obj_align(*lbl_ptr, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void setup_scr_Main_1(lv_ui *ui)
{
    ui->Main_1 = lv_obj_create(NULL);
    lv_obj_set_size(ui->Main_1, 800, 480);
    lv_obj_set_scrollbar_mode(ui->Main_1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_grad_dir(ui->Main_1, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(ui->Main_1, lv_color_hex(0x1e293b), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui->Main_1, lv_color_hex(0x0f172a), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->Main_1, 255, LV_PART_MAIN);

    ui->Main_1_header = lv_obj_create(ui->Main_1);
    lv_obj_set_size(ui->Main_1_header, 800, 50);
    lv_obj_set_pos(ui->Main_1_header, 0, 0);
    lv_obj_set_style_bg_color(ui->Main_1_header, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->Main_1_header, 50, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->Main_1_header, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->Main_1_header, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(ui->Main_1_header, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->Main_1_header, LV_OBJ_FLAG_SCROLLABLE);

    ui->Main_1_title = lv_label_create(ui->Main_1_header);
    lv_label_set_text(ui->Main_1_title, "WindSight 智风监测");
    lv_obj_set_style_text_color(ui->Main_1_title, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->Main_1_title, gui_assets_get_font_30(), LV_PART_MAIN);
    lv_obj_align(ui->Main_1_title, LV_ALIGN_LEFT_MID, 20, 0);

    ui->Main_1_status = lv_label_create(ui->Main_1_header);
    lv_label_set_text(ui->Main_1_status, "WiFi: ON   Node: 01");
    lv_obj_set_style_text_color(ui->Main_1_status, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->Main_1_status, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_align(ui->Main_1_status, LV_ALIGN_RIGHT_MID, -20, 0);

    ui->Main_1_cont_grid = lv_obj_create(ui->Main_1);
    lv_obj_set_size(ui->Main_1_cont_grid, 740, 350);
    lv_obj_align(ui->Main_1_cont_grid, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(ui->Main_1_cont_grid, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->Main_1_cont_grid, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(ui->Main_1_cont_grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->Main_1_cont_grid, LV_OBJ_FLAG_SCROLLABLE);

    static int32_t col_dsc[] = {230, 230, 230, LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[] = {165, 165, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(ui->Main_1_cont_grid, col_dsc, row_dsc);

    create_tile(ui->Main_1_cont_grid, &ui->Main_1_tile_1, &ui->Main_1_img_1, &ui->Main_1_lbl_1,
                "Q:/gui/icon_01_rtmon_RGB565A8_100x100.bin", "实时监控", 0, 0);
    create_tile(ui->Main_1_cont_grid, &ui->Main_1_tile_2, &ui->Main_1_img_2, &ui->Main_1_lbl_2,
                "Q:/gui/icon_02_fault_RGB565A8_100x100.bin", "故障监测", 1, 0);
    create_tile(ui->Main_1_cont_grid, &ui->Main_1_tile_3, &ui->Main_1_img_3, &ui->Main_1_lbl_3,
                "Q:/gui/icon_03_analysis_RGB565A8_100x100.bin", "数据分析", 2, 0);
    create_tile(ui->Main_1_cont_grid, &ui->Main_1_tile_4, &ui->Main_1_img_4, &ui->Main_1_lbl_4,
                "Q:/gui/icon_04_history_RGB565A8_100x100.bin", "历史记录", 0, 1);
    create_tile(ui->Main_1_cont_grid, &ui->Main_1_tile_5, &ui->Main_1_img_5, &ui->Main_1_lbl_5,
                "Q:/gui/icon_05_log_RGB565A8_100x100.bin", "日志查看", 1, 1);
    create_tile(ui->Main_1_cont_grid, &ui->Main_1_tile_6, &ui->Main_1_img_6, &ui->Main_1_lbl_6,
                "Q:/gui/icon_06_alarm_RGB565A8_100x100.bin", "报警设置", 2, 1);

    ui->Main_1_footer = lv_obj_create(ui->Main_1);
    lv_obj_set_size(ui->Main_1_footer, 200, 40);
    lv_obj_align(ui->Main_1_footer, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(ui->Main_1_footer, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->Main_1_footer, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(ui->Main_1_footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui->Main_1_footer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(ui->Main_1_footer, 15, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(ui->Main_1_footer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->Main_1_footer, LV_OBJ_FLAG_SCROLLABLE);

    ui->Main_1_dot_1 = lv_obj_create(ui->Main_1_footer);
    lv_obj_set_size(ui->Main_1_dot_1, 30, 10);
    lv_obj_set_style_bg_color(ui->Main_1_dot_1, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_set_style_radius(ui->Main_1_dot_1, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->Main_1_dot_1, 0, LV_PART_MAIN);

    ui->Main_1_dot_2 = lv_obj_create(ui->Main_1_footer);
    lv_obj_set_size(ui->Main_1_dot_2, 10, 10);
    lv_obj_set_style_bg_color(ui->Main_1_dot_2, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->Main_1_dot_2, 50, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->Main_1_dot_2, 5, LV_PART_MAIN);
    lv_obj_add_flag(ui->Main_1_dot_2, LV_OBJ_FLAG_CLICKABLE);

    ui->Main_1_dot_3 = lv_obj_create(ui->Main_1_footer);
    lv_obj_set_size(ui->Main_1_dot_3, 10, 10);
    lv_obj_set_style_bg_color(ui->Main_1_dot_3, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->Main_1_dot_3, 50, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->Main_1_dot_3, 5, LV_PART_MAIN);
    lv_obj_add_flag(ui->Main_1_dot_3, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_update_layout(ui->Main_1);
    events_init_Main_1(ui);
}
