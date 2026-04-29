/**
 * @file lv_port_indev.c
 *
 */

/*Copy this file as "lv_port_indev.c" and set this value to "1" to enable content*/
#if 1

/*********************
 * INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "touch_800x480.h" /* 引入你的触摸屏驱动头文件 */

/*********************
 * DEFINES
 *********************/
#ifndef EW_TOUCH_SCROLL_LIMIT
/* 滑动判定距离（像素）：越小越“灵敏”，越大越不易误触发滑动 */
#define EW_TOUCH_SCROLL_LIMIT 10
#endif

#ifndef EW_TOUCH_SCROLL_THROW
/* 滑动松手后的惯性衰减（百分比）：越大停止越快，越小惯性越强 */
#define EW_TOUCH_SCROLL_THROW 25
#endif

#ifndef EW_TOUCH_LONG_PRESS_MS
/* 长按判定时间（ms）：适当调小可让按键反馈更快（不影响普通点击的抬起触发） */
#define EW_TOUCH_LONG_PRESS_MS 350
#endif

#ifndef EW_TOUCH_LONG_PRESS_REPEAT_MS
#define EW_TOUCH_LONG_PRESS_REPEAT_MS 120
#endif

/**********************
 * TYPEDEFS
 **********************/

/**********************
 * STATIC PROTOTYPES
 **********************/

static void touchpad_init(void);
static void touchpad_read(lv_indev_t * indev, lv_indev_data_t * data);
static bool touchpad_is_pressed(void);
static void touchpad_get_xy(int32_t * x, int32_t * y);

/**********************
 * STATIC VARIABLES
 **********************/
lv_indev_t * indev_touchpad;

/**********************
 * MACROS
 **********************/

/**********************
 * GLOBAL FUNCTIONS
 **********************/

void lv_port_indev_init(void)
{
    /*------------------
     * Touchpad
     * -----------------*/

    /* 初始化触摸屏硬件 */
    touchpad_init();

    /* 注册触摸输入设备 */
    indev_touchpad = lv_indev_create();
    lv_indev_set_type(indev_touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_touchpad, touchpad_read);

    /* ！！！新增代码：设置触摸防抖/滑动阈值 ！！！ */
    /* 根据主界面“滑动不灵敏/点击迟钝”的体验，降低滑动门槛并加快惯性衰减 */
    lv_indev_set_scroll_limit(indev_touchpad, EW_TOUCH_SCROLL_LIMIT);
    lv_indev_set_scroll_throw(indev_touchpad, EW_TOUCH_SCROLL_THROW);
    lv_indev_set_long_press_time(indev_touchpad, EW_TOUCH_LONG_PRESS_MS);
    lv_indev_set_long_press_repeat_time(indev_touchpad, EW_TOUCH_LONG_PRESS_REPEAT_MS);
}

/**********************
 * STATIC FUNCTIONS
 **********************/

/*------------------
 * Touchpad
 * -----------------*/

/* 初始化你的触摸屏 */
static void touchpad_init(void)
{
    /* 调用底层驱动的初始化函数 */
    Touch_Init(); 
}

/* LVGL 库会定期调用此函数来读取触摸状态 */
static void touchpad_read(lv_indev_t * indev_drv, lv_indev_data_t * data)
{
    /* ！！！关键修改！！！ */
    /* 必须在此处调用底层的扫描函数，否则 touchInfo 不会更新 */
    Touch_Scan(); 
    /* */

    static int32_t last_x = 0;
    static int32_t last_y = 0;

    /* 保存按下时的坐标和状态 */
    if(touchpad_is_pressed()) {
        touchpad_get_xy(&last_x, &last_y);
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /* 设置最后的坐标 */
    data->point.x = last_x;
    data->point.y = last_y;
}

/* 返回 true 如果触摸屏被按下 */
static bool touchpad_is_pressed(void)
{
    /* 使用驱动中的全局变量 touchInfo 判断按下状态 */
    /* 注意：Touch_Scan() 会更新 touchInfo.flag */
    if(touchInfo.flag == 1) 
    {
        return true;
    } 
    else 
    {
        return false;
    }
}

/* 如果触摸屏被按下，获取 X 和 Y 坐标 */
static void touchpad_get_xy(int32_t * x, int32_t * y)
{
    /* 将硬件读取到的 uint16_t 坐标赋值给 LVGL 的 int32_t 坐标 */
    (*x) = (int32_t)touchInfo.x[0];
    (*y) = (int32_t)touchInfo.y[0];
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
