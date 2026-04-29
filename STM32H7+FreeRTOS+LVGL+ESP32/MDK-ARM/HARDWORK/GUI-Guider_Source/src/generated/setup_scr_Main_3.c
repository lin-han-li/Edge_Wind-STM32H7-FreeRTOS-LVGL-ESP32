/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "lvgl.h"
#include <stdio.h>
#include "gui_guider.h"
#include "events_init.h"
#include "widgets_init.h"
#include "custom.h"



void setup_scr_Main_3(lv_ui *ui)
{
    //Write codes Main_3
    ui->Main_3 = lv_obj_create(NULL);
    lv_obj_set_size(ui->Main_3, 800, 480);
    lv_obj_set_scrollbar_mode(ui->Main_3, LV_SCROLLBAR_MODE_OFF);

    //Write style for Main_3, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->Main_3, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->Main_3, lv_color_hex(0x65adff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->Main_3, LV_GRAD_DIR_HOR, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(ui->Main_3, lv_color_hex(0x2195f6), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(ui->Main_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(ui->Main_3, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes Main_3_img_2
    ui->Main_3_img_2 = lv_image_create(ui->Main_3);
    lv_obj_set_pos(ui->Main_3_img_2, 423, 180);
    lv_obj_set_size(ui->Main_3_img_2, 100, 100);
    lv_obj_add_flag(ui->Main_3_img_2, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(ui->Main_3_img_2, &_icon_14_about_RGB565A8_100x100);
    lv_image_set_pivot(ui->Main_3_img_2, 50,50);
    lv_image_set_rotation(ui->Main_3_img_2, 0);

    //Write style for Main_3_img_2, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_image_recolor_opa(ui->Main_3_img_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(ui->Main_3_img_2, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes Main_3_img_1
    ui->Main_3_img_1 = lv_image_create(ui->Main_3);
    lv_obj_set_pos(ui->Main_3_img_1, 251, 180);
    lv_obj_set_size(ui->Main_3_img_1, 100, 100);
    lv_obj_add_flag(ui->Main_3_img_1, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(ui->Main_3_img_1, &_icon_13_fwup_RGB565A8_100x100);
    lv_image_set_pivot(ui->Main_3_img_1, 50,50);
    lv_image_set_rotation(ui->Main_3_img_1, 0);

    //Write style for Main_3_img_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_image_recolor_opa(ui->Main_3_img_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(ui->Main_3_img_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes Main_3_label_1
    ui->Main_3_label_1 = lv_label_create(ui->Main_3);
    lv_obj_set_pos(ui->Main_3_label_1, 251, 281);
    lv_obj_set_size(ui->Main_3_label_1, 100, 30);
    lv_label_set_text(ui->Main_3_label_1, "固件升级");
    lv_label_set_long_mode(ui->Main_3_label_1, LV_LABEL_LONG_WRAP);

    //Write style for Main_3_label_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->Main_3_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->Main_3_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->Main_3_label_1, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->Main_3_label_1, &lv_font_SourceHanSerifSC_Regular_20, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->Main_3_label_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->Main_3_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->Main_3_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->Main_3_label_1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->Main_3_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->Main_3_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->Main_3_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->Main_3_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->Main_3_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->Main_3_label_1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes Main_3_label_2
    ui->Main_3_label_2 = lv_label_create(ui->Main_3);
    lv_obj_set_pos(ui->Main_3_label_2, 423, 281);
    lv_obj_set_size(ui->Main_3_label_2, 100, 30);
    lv_label_set_text(ui->Main_3_label_2, "关于系统");
    lv_label_set_long_mode(ui->Main_3_label_2, LV_LABEL_LONG_WRAP);

    //Write style for Main_3_label_2, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->Main_3_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->Main_3_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->Main_3_label_2, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->Main_3_label_2, &lv_font_SourceHanSerifSC_Regular_20, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->Main_3_label_2, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->Main_3_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->Main_3_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->Main_3_label_2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->Main_3_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->Main_3_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->Main_3_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->Main_3_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->Main_3_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->Main_3_label_2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes Main_3_line_1
    ui->Main_3_line_1 = lv_line_create(ui->Main_3);
    lv_obj_set_pos(ui->Main_3_line_1, 190, 460);
    lv_obj_set_size(ui->Main_3_line_1, 400, 6);
    static lv_point_precise_t Main_3_line_1[] = {{300, 0},{400, 0}};
    lv_line_set_points(ui->Main_3_line_1, Main_3_line_1, 2);

    //Write style for Main_3_line_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_line_width(ui->Main_3_line_1, 6, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(ui->Main_3_line_1, lv_color_hex(0x3dfb00), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(ui->Main_3_line_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(ui->Main_3_line_1, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes Main_3_label_3
    ui->Main_3_label_3 = lv_label_create(ui->Main_3);
    lv_obj_set_pos(ui->Main_3_label_3, 200, 1);
    lv_obj_set_size(ui->Main_3_label_3, 380, 80);
    lv_label_set_text(ui->Main_3_label_3, "主界面三");
    lv_label_set_long_mode(ui->Main_3_label_3, LV_LABEL_LONG_WRAP);

    //Write style for Main_3_label_3, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->Main_3_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->Main_3_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->Main_3_label_3, lv_color_hex(0x2F35DA), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->Main_3_label_3, &lv_font_SourceHanSerifSC_Regular_60, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->Main_3_label_3, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->Main_3_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->Main_3_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->Main_3_label_3, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->Main_3_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->Main_3_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->Main_3_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->Main_3_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->Main_3_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->Main_3_label_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //The custom code of Main_3.


    //Update current screen layout.
    lv_obj_update_layout(ui->Main_3);

    //Init events for screen.
    events_init_Main_3(ui);
}
