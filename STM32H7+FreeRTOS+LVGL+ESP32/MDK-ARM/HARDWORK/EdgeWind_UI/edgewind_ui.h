/**
 * @file edgewind_ui.h
 * @brief EdgeWind UI 主入口
 * 
 * 简化版：仅包含开机动画
 */

#ifndef EDGEWIND_UI_H
#define EDGEWIND_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/*******************************************************************************
 * 函数声明
 ******************************************************************************/

/**
 * @brief 初始化 EdgeWind UI
 * @note  在 lv_init() 之后、进入主循环之前调用
 */
void edgewind_ui_init(void);

/**
 * @brief UI 周期刷新（在 LVGL 任务中调用）
 * @note  开机动画阶段可为空实现
 */
void edgewind_ui_refresh(void);

/**
 * @brief 检查开机动画是否完成
 * @return true 动画已完成
 */
bool edgewind_ui_boot_finished(void);

/**
 * @brief “进入系统”按钮点击入口（弱符号）
 * @note  在业务代码中实现同名函数即可接管。
 */
void edgewind_ui_on_enter_system(void);

/**
 * @brief 开机动画结束、"进入系统"按钮显示前回调（弱符号）
 * @note  典型用途：断电重连/自动上报预启动（此时还未进入系统 UI）。
 */
void edgewind_ui_on_before_enter_button(void);

#ifdef __cplusplus
}
#endif

#endif /* EDGEWIND_UI_H */
