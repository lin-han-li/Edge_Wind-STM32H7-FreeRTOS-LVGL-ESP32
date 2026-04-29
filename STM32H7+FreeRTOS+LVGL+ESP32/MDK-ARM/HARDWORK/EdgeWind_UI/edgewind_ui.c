/**
 * @file edgewind_ui.c
 * @brief EdgeWind UI 主入口实现
 * 
 * 仅保留开机动画与进入按钮入口
 */

#include "edgewind_ui.h"
#include "screens/scr_boot_anim.h"

/*******************************************************************************
 * 内部变量
 ******************************************************************************/

static bool ui_initialized = false;

/*******************************************************************************
 * 弱回调：进入系统按钮点击入口
 ******************************************************************************/
#if defined(__GNUC__) || defined(__clang__)
#define EW_WEAK __attribute__((weak))
#else
#define EW_WEAK __weak
#endif

EW_WEAK void edgewind_ui_on_enter_system(void)
{
}

/* 弱回调：开机动画结束、进入系统按钮显示前 */
EW_WEAK void edgewind_ui_on_before_enter_button(void)
{
}

/*******************************************************************************
 * 公共函数实现
 ******************************************************************************/

void edgewind_ui_init(void)
{
    if (ui_initialized) return;
    
    /* 创建并加载开机动画界面 */
    ew_boot_anim_create();
    lv_scr_load(ew_boot_anim.screen);

    ui_initialized = true;
}

void edgewind_ui_refresh(void)
{
    /* 开机动画完全由 LVGL 定时器驱动，这里保持为空实现即可 */
}

bool edgewind_ui_boot_finished(void)
{
    return ew_boot_anim_is_finished();
}
