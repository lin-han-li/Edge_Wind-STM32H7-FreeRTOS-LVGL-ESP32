/**
 * @file lv_port_indev.c
 */

/*Copy this file as "lv_port_indev.c" and set this value to "1" to enable content*/
#if 1

/*********************
 * INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "touch_800x480.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/*********************
 * DEFINES
 *********************/
#ifndef EW_TOUCH_SCROLL_LIMIT
#define EW_TOUCH_SCROLL_LIMIT 10
#endif

#ifndef EW_TOUCH_SCROLL_THROW
#define EW_TOUCH_SCROLL_THROW 25
#endif

#ifndef EW_TOUCH_LONG_PRESS_MS
#define EW_TOUCH_LONG_PRESS_MS 350
#endif

#ifndef EW_TOUCH_LONG_PRESS_REPEAT_MS
#define EW_TOUCH_LONG_PRESS_REPEAT_MS 120
#endif

#ifndef EW_TOUCH_PRESS_CONFIRM_SAMPLES
#define EW_TOUCH_PRESS_CONFIRM_SAMPLES 2
#endif

#ifndef EW_TOUCH_RELEASE_CONFIRM_SAMPLES
#define EW_TOUCH_RELEASE_CONFIRM_SAMPLES 2
#endif

#ifndef EW_TOUCH_JUMP_LIMIT_PX
#define EW_TOUCH_JUMP_LIMIT_PX 160
#endif

#ifndef EW_TOUCH_DIAG_LOG_MS
#define EW_TOUCH_DIAG_LOG_MS 5000U
#endif

/**********************
 * STATIC PROTOTYPES
 **********************/
static void touchpad_init(void);
static void touchpad_read(lv_indev_t * indev_drv, lv_indev_data_t * data);
static bool touchpad_is_pressed(void);
static void touchpad_get_xy(int32_t * x, int32_t * y);
static uint32_t touchpad_abs_diff(int32_t a, int32_t b);
static void touchpad_log_filter_stats(void);

/**********************
 * STATIC VARIABLES
 **********************/
lv_indev_t * indev_touchpad;
static uint32_t touch_filter_unstable;
static uint32_t touch_filter_jump_reject;
static uint32_t touch_filter_accepted_press;
static uint32_t touch_filter_released;

/**********************
 * GLOBAL FUNCTIONS
 **********************/
void lv_port_indev_init(void)
{
    touchpad_init();

    indev_touchpad = lv_indev_create();
    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);

    lv_indev_set_scroll_limit(indev_touchpad, EW_TOUCH_SCROLL_LIMIT);
    lv_indev_set_scroll_throw(indev_touchpad, EW_TOUCH_SCROLL_THROW);
    lv_indev_set_long_press_time(indev_touchpad, EW_TOUCH_LONG_PRESS_MS);
    lv_indev_set_long_press_repeat_time(indev_touchpad, EW_TOUCH_LONG_PRESS_REPEAT_MS);
}

/**********************
 * STATIC FUNCTIONS
 **********************/
static void touchpad_init(void)
{
    Touch_Init();
}

static void touchpad_read(lv_indev_t * indev_drv, lv_indev_data_t * data)
{
    static int32_t last_x = 0;
    static int32_t last_y = 0;
    static int32_t candidate_x = 0;
    static int32_t candidate_y = 0;
    static uint8_t stable_pressed = 0;
    static uint8_t press_seen = 0;
    static uint8_t release_seen = 0;
    static uint8_t candidate_valid = 0;
    static uint8_t jump_seen = 0;
    int32_t raw_x;
    int32_t raw_y;
    bool raw_pressed;
    uint32_t dx;
    uint32_t dy;

    (void)indev_drv;

    Touch_Scan();
    raw_pressed = touchpad_is_pressed();
    raw_x = last_x;
    raw_y = last_y;

    if(raw_pressed) {
        touchpad_get_xy(&raw_x, &raw_y);
        release_seen = 0;

        if(stable_pressed) {
            dx = touchpad_abs_diff(raw_x, last_x);
            dy = touchpad_abs_diff(raw_y, last_y);

            if(((dx > EW_TOUCH_JUMP_LIMIT_PX) || (dy > EW_TOUCH_JUMP_LIMIT_PX)) && (jump_seen == 0)) {
                jump_seen = 1;
                touch_filter_jump_reject++;
            }
            else {
                last_x = raw_x;
                last_y = raw_y;
                jump_seen = 0;
            }
        }
        else {
            if(candidate_valid == 0) {
                candidate_x = raw_x;
                candidate_y = raw_y;
                candidate_valid = 1;
                press_seen = 1;
                touch_filter_unstable++;
            }
            else {
                dx = touchpad_abs_diff(raw_x, candidate_x);
                dy = touchpad_abs_diff(raw_y, candidate_y);

                if((dx > EW_TOUCH_JUMP_LIMIT_PX) || (dy > EW_TOUCH_JUMP_LIMIT_PX)) {
                    candidate_x = raw_x;
                    candidate_y = raw_y;
                    press_seen = 1;
                    touch_filter_jump_reject++;
                }
                else {
                    press_seen++;
                    candidate_x = raw_x;
                    candidate_y = raw_y;
                    if(press_seen >= EW_TOUCH_PRESS_CONFIRM_SAMPLES) {
                        stable_pressed = 1;
                        candidate_valid = 0;
                        press_seen = 0;
                        last_x = raw_x;
                        last_y = raw_y;
                        touch_filter_accepted_press++;
                    }
                }
            }
        }
    }
    else {
        candidate_valid = 0;
        press_seen = 0;
        jump_seen = 0;

        if(stable_pressed) {
            release_seen++;
            if(release_seen >= EW_TOUCH_RELEASE_CONFIRM_SAMPLES) {
                stable_pressed = 0;
                release_seen = 0;
                touch_filter_released++;
            }
        }
        else {
            release_seen = 0;
        }
    }

    data->state = stable_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = last_x;
    data->point.y = last_y;

    touchpad_log_filter_stats();
}

static bool touchpad_is_pressed(void)
{
    return (touchInfo.flag == 1);
}

static void touchpad_get_xy(int32_t * x, int32_t * y)
{
    (*x) = (int32_t)touchInfo.x[0];
    (*y) = (int32_t)touchInfo.y[0];
}

static uint32_t touchpad_abs_diff(int32_t a, int32_t b)
{
    return (a >= b) ? (uint32_t)(a - b) : (uint32_t)(b - a);
}

static void touchpad_log_filter_stats(void)
{
#if EW_TOUCH_DIAG_LOG_MS > 0
    static uint32_t last_log_ms = 0;
    static uint32_t last_sum = 0;
    TouchDiagStats raw_stats;
    uint32_t now;
    uint32_t sum;

    now = lv_tick_get();
    sum = touch_filter_unstable + touch_filter_jump_reject +
          touch_filter_accepted_press + touch_filter_released;

    if((sum != last_sum) && ((uint32_t)(now - last_log_ms) >= EW_TOUCH_DIAG_LOG_MS)) {
        Touch_GetDiagStats(&raw_stats);
        printf("[TOUCH] raw=%lu ok=%lu fail=%lu nr=%lu bad=%lu oob=%lu clr=%lu filt_unstable=%lu jump=%lu press=%lu rel=%lu\r\n",
               (unsigned long)raw_stats.raw_reads,
               (unsigned long)raw_stats.accepted_raw,
               (unsigned long)raw_stats.read_fail,
               (unsigned long)raw_stats.not_ready,
               (unsigned long)raw_stats.bad_count,
               (unsigned long)raw_stats.out_of_bounds,
               (unsigned long)raw_stats.clear_fail,
               (unsigned long)touch_filter_unstable,
               (unsigned long)touch_filter_jump_reject,
               (unsigned long)touch_filter_accepted_press,
               (unsigned long)touch_filter_released);
        last_sum = sum;
        last_log_ms = now;
    }
#endif
}

#else /*Enable this file at the top*/

typedef int keep_pedantic_happy;
#endif