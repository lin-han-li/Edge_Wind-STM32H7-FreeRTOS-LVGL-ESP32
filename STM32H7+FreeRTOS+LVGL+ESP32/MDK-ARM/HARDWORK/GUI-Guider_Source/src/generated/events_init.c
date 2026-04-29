/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "events_init.h"
#include <stdio.h>
#include "lvgl.h"

#if LV_USE_GUIDER_SIMULATOR && LV_USE_FREEMASTER
#include "freemaster_client.h"
#endif


static void Main_1_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_GESTURE:
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        switch(dir) {
        case LV_DIR_LEFT:
        {
            lv_indev_wait_release(lv_indev_active());
            ui_load_scr_animation(&guider_ui, &guider_ui.Main_2, guider_ui.Main_2_del, &guider_ui.Main_1_del, setup_scr_Main_2, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, true, true);
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

static void Main_1_img_1_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

static void Main_1_img_2_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

static void Main_1_img_3_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

static void Main_1_img_4_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

static void Main_1_img_5_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

static void Main_1_img_6_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

void events_init_Main_1 (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->Main_1, Main_1_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_1_img_1, Main_1_img_1_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_1_img_2, Main_1_img_2_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_1_img_3, Main_1_img_3_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_1_img_4, Main_1_img_4_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_1_img_5, Main_1_img_5_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_1_img_6, Main_1_img_6_event_handler, LV_EVENT_ALL, ui);
}

static void Main_2_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_GESTURE:
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        switch(dir) {
        case LV_DIR_LEFT:
        {
            lv_indev_wait_release(lv_indev_active());
            ui_load_scr_animation(&guider_ui, &guider_ui.Main_3, guider_ui.Main_3_del, &guider_ui.Main_2_del, setup_scr_Main_3, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, true, true);
            break;
        }
        case LV_DIR_RIGHT:
        {
            lv_indev_wait_release(lv_indev_active());
            ui_load_scr_animation(&guider_ui, &guider_ui.Main_1, guider_ui.Main_1_del, &guider_ui.Main_2_del, setup_scr_Main_1, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, true, true);
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

static void Main_2_img_6_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

static void Main_2_img_5_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

static void Main_2_img_4_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

static void Main_2_img_3_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

static void Main_2_img_2_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

static void Main_2_img_1_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

void events_init_Main_2 (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->Main_2, Main_2_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_2_img_6, Main_2_img_6_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_2_img_5, Main_2_img_5_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_2_img_4, Main_2_img_4_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_2_img_3, Main_2_img_3_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_2_img_2, Main_2_img_2_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_2_img_1, Main_2_img_1_event_handler, LV_EVENT_ALL, ui);
}

static void Main_3_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_GESTURE:
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        switch(dir) {
        case LV_DIR_RIGHT:
        {
            lv_indev_wait_release(lv_indev_active());
            ui_load_scr_animation(&guider_ui, &guider_ui.Main_2, guider_ui.Main_2_del, &guider_ui.Main_3_del, setup_scr_Main_2, LV_SCR_LOAD_ANIM_FADE_ON, 200, 20, true, true);
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

static void Main_3_img_2_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

static void Main_3_img_1_event_handler (lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_PRESS_LOST:
    {
        break;
    }
    default:
        break;
    }
}

void events_init_Main_3 (lv_ui *ui)
{
    lv_obj_add_event_cb(ui->Main_3, Main_3_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_3_img_2, Main_3_img_2_event_handler, LV_EVENT_ALL, ui);
    lv_obj_add_event_cb(ui->Main_3_img_1, Main_3_img_1_event_handler, LV_EVENT_ALL, ui);
}


void events_init(lv_ui *ui)
{

}
