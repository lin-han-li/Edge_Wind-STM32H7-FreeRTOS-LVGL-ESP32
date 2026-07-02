/*
 * Copyright 2026 NXP
 * EdgeWind About System Screen (关于系统 - 纯展示) - LVGL 9.4.0
 *
 * Pure-display screen: firmware version, build time, device ID, serial number.
 */

#include "lvgl.h"
#include "gui_guider.h"
#include "events_init.h"
#include "../../gui_assets.h"
#include "../../../EdgeComm/edge_comm.h"
#include "main.h"          /* HAL_GetUIDw0/1/2 for hardware serial number */
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

/* 固件版本：应用固件版本号（与 AI 模型版本相互独立）。发版时在此处递增。 */
#ifndef EDGEWIND_FW_VERSION
#define EDGEWIND_FW_VERSION "V1.0.0"
#endif

#define ABOUT_W 800
#define ABOUT_H 480
#define ABOUT_HEADER_H 60

/* 浅色主题：与 Aurora 主界面一致（天蓝 → 薰衣草渐变，白卡片）*/
#define AB_COL_BG_TOP   0xE8F4FD   /* 顶部浅天蓝 */
#define AB_COL_BG_BOT   0xEEECFD   /* 底部浅薰衣草 */
#define AB_COL_PANEL    0xFFFFFF   /* 白色标题栏 */
#define AB_COL_TITLE    0x0369A1   /* 深海蓝标题 */
#define AB_COL_ACCENT   0x0EA5E9   /* 蓝色强调条 */
#define AB_COL_CARD     0xFFFFFF   /* 白色卡片 */
#define AB_COL_ROW_KEY  0x64748B   /* 次要文字（标签）*/
#define AB_COL_ROW_VAL  0x1E293B   /* 主文字（值）*/
#define AB_COL_DIVIDER  0xDDE6F0   /* 分隔线 */
#define AB_COL_BACK     0x0EA5E9   /* 返回按钮蓝 */

/* 单行“标签 : 值”布局，返回值 label 存放 value 文本对象 */
static lv_obj_t *about_create_row(lv_obj_t *parent, int32_t y, const char *key, const char *value)
{
    lv_obj_t *lbl_key = lv_label_create(parent);
    lv_label_set_text(lbl_key, key);
    lv_obj_set_style_text_color(lbl_key, lv_color_hex(AB_COL_ROW_KEY), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_key, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_set_pos(lbl_key, 32, y);

    lv_obj_t *lbl_val = lv_label_create(parent);
    lv_label_set_text(lbl_val, value);
    lv_obj_set_style_text_color(lbl_val, lv_color_hex(AB_COL_ROW_VAL), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_val, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_set_pos(lbl_val, 240, y);
    return lbl_val;
}

void setup_scr_About(lv_ui *ui)
{
    /* 屏幕根对象：浅色天蓝 → 薰衣草渐变 */
    ui->About = lv_obj_create(NULL);
    lv_obj_remove_style_all(ui->About);
    lv_obj_set_size(ui->About, ABOUT_W, ABOUT_H);
    lv_obj_set_scrollbar_mode(ui->About, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->About, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui->About, lv_color_hex(AB_COL_BG_TOP), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(ui->About, lv_color_hex(AB_COL_BG_BOT), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(ui->About, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->About, LV_OPA_COVER, LV_PART_MAIN);

    /* 顶部标题栏：白色 */
    lv_obj_t *header = lv_obj_create(ui->About);
    lv_obj_remove_style_all(header);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, ABOUT_W, ABOUT_HEADER_H);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(header, lv_color_hex(AB_COL_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *accent = lv_obj_create(header);
    lv_obj_remove_style_all(accent);
    lv_obj_set_pos(accent, 0, 0);
    lv_obj_set_size(accent, 6, ABOUT_HEADER_H);
    lv_obj_set_style_bg_color(accent, lv_color_hex(AB_COL_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, LV_PART_MAIN);

    ui->About_lbl_title = lv_label_create(header);
    lv_label_set_text(ui->About_lbl_title, "关于系统");
    lv_obj_set_style_text_color(ui->About_lbl_title, lv_color_hex(AB_COL_TITLE), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->About_lbl_title, gui_assets_get_font_30(), LV_PART_MAIN);
    lv_obj_align(ui->About_lbl_title, LV_ALIGN_LEFT_MID, 34, 0);

    /* 信息卡片：白色实心卡片 + 细边框 */
    lv_obj_t *card = lv_obj_create(ui->About);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, 60, 108);
    lv_obj_set_size(card, 680, 300);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 16, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(AB_COL_DIVIDER), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(AB_COL_CARD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(card, 60, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(card, 4, LV_PART_MAIN);

    /* 固件版本 */
    ui->About_lbl_fw_value = about_create_row(card, 32, "固件版本", EDGEWIND_FW_VERSION);

    /* 编译时间：编译器内建宏 */
    ui->About_lbl_build_value = about_create_row(card, 96, "编译时间", __DATE__ " " __TIME__);

    /* 设备 ID：来自系统配置 node_id */
    {
        const char *node = ESP_UI_NodeId();
        ui->About_lbl_node_value = about_create_row(card, 160, "设备 ID", node ? node : "--");
    }

    /* 序列号 (SN)：STM32 96 位唯一 ID（硬件序列号）*/
    {
        static char sn[40];
        snprintf(sn, sizeof(sn), "%08lX%08lX%08lX",
                 (unsigned long)HAL_GetUIDw2(),
                 (unsigned long)HAL_GetUIDw1(),
                 (unsigned long)HAL_GetUIDw0());
        ui->About_lbl_sn_value = about_create_row(card, 224, "序列号 (SN)", sn);
    }

    /* 返回按钮 */
    ui->About_btn_back = lv_button_create(ui->About);
    lv_obj_set_pos(ui->About_btn_back, 648, 418);
    lv_obj_set_size(ui->About_btn_back, 128, 44);
    lv_obj_set_style_bg_color(ui->About_btn_back, lv_color_hex(AB_COL_BACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->About_btn_back, 230, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->About_btn_back, 22, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->About_btn_back, 0, LV_PART_MAIN);

    ui->About_lbl_back = lv_label_create(ui->About_btn_back);
    lv_label_set_text(ui->About_lbl_back, "返回");
    lv_obj_set_style_text_color(ui->About_lbl_back, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->About_lbl_back, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_center(ui->About_lbl_back);

    lv_obj_update_layout(ui->About);
    events_init_About(ui);
}
