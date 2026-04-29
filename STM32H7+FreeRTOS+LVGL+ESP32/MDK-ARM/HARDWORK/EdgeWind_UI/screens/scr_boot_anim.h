/**
 * @file scr_boot_anim.h
 * @brief CRT 风格开机动画界面
 * 
 * 动画流程：
 * 1. CRT 横线展开效果
 * 2. 四角标记淡入
 * 3. 终端自检日志滚动
 * 4. Logo + Glitch 闪烁特效
 * 5. 进度条填充
 * 6. 放大淡出离场
 */

#ifndef SCR_BOOT_ANIM_H
#define SCR_BOOT_ANIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdbool.h>

/*******************************************************************************
 * 常量定义
 ******************************************************************************/

#define BOOT_ANIM_DURATION      4600    /* 总动画时长 ms */
#define BOOT_LOG_LINES          7       /* 日志行数 - 对齐HTML */

/* 颜色定义 - 对齐HTML */
#define BOOT_COLOR_BG       lv_color_hex(0x050505)  /* 深黑背景 (HTML: #050505) */
#define BOOT_COLOR_GREEN    lv_color_hex(0x00FF41)  /* 终端绿 (HTML: #00ff41) */
#define BOOT_COLOR_BLUE     lv_color_hex(0x00D2FF)  /* 霓虹蓝 (HTML: #00D2FF) */
#define BOOT_COLOR_CYAN     lv_color_hex(0x00FFF2)  /* 霓虹青 (HTML: #00fff2) */
#define BOOT_COLOR_RED      lv_color_hex(0xFF2E4D)  /* 警告红 (HTML: #FF2E4D) */
#define BOOT_COLOR_WHITE    lv_color_hex(0xFFFFFF)  /* 纯白 */
#define BOOT_COLOR_GRAY     lv_color_hex(0x333333)  /* 角落灰 */
#define BOOT_COLOR_HEX_BG   lv_color_hex(0x1A1A1A)  /* HEX背景字色 (HTML: #1a1a1a) */

/*******************************************************************************
 * 数据结构
 ******************************************************************************/

typedef struct {
    lv_obj_t *screen;
    
    /* CRT 效果容器 */
    lv_obj_t *crt_container;
    lv_obj_t *crt_line;             /* CRT 水平亮线 */
    
    /* 四角标记 */
    lv_obj_t *corner_tl;
    lv_obj_t *corner_tr;
    lv_obj_t *corner_bl;
    lv_obj_t *corner_br;
    
    /* 背景HEX数据流 */
    lv_obj_t *hex_bg_label;
    
    /* 终端日志 */
    lv_obj_t *terminal_label;
    uint8_t   log_index;
    lv_timer_t *log_timer;
    
    /* Logo 区域 */
    lv_obj_t *logo_container;
    lv_obj_t *logo_icon_line1;      /* 风车上叶片 */
    lv_obj_t *logo_icon_line2;      /* 风车下叶片 */
    lv_obj_t *logo_icon_line3;      /* 心电图波形 */
    lv_obj_t *logo_text;            /* "EDGEWIND" */
    lv_obj_t *logo_text_red;        /* 红色偏移层 */
    lv_obj_t *logo_text_blue;       /* 蓝色偏移层 */
    lv_obj_t *logo_sub;             /* "SYSTEM V2.0" */
    lv_obj_t *progress_bar;
    
    /* 完整网格背景 (40px) */
    lv_obj_t *grid_lines_h[20];     /* 横向网格线 800/40=20 */
    lv_obj_t *grid_lines_v[12];     /* 纵向网格线 480/40=12 */
    
    /* CRT 扫描线 */
    lv_obj_t *crt_scanline;
    lv_timer_t *scanline_timer;
    uint16_t   scanline_y;
    
    /* Logo 图标旋转容器 */
    lv_obj_t *icon_rotate_container;
    
    /* 激光扫描特效 */
    lv_obj_t *scan_overlay;
    
    /* 进入系统按钮 */
    lv_obj_t *enter_btn;
    lv_obj_t *enter_btn_label;
    
    /* Glitch 特效 */
    lv_timer_t *glitch_timer;
    uint8_t    glitch_count;
    
    /* 状态 */
    bool      finished;
    uint8_t   phase;                /* 当前阶段 0-6 */
    uint32_t  start_tick;
} ew_boot_anim_t;

extern ew_boot_anim_t ew_boot_anim;

/*******************************************************************************
 * 函数声明
 ******************************************************************************/

/**
 * @brief 创建开机动画界面
 */
void ew_boot_anim_create(void);

/**
 * @brief 检查动画是否完成
 * @return true 动画已完成
 */
bool ew_boot_anim_is_finished(void);

#ifdef __cplusplus
}
#endif

#endif /* SCR_BOOT_ANIM_H */
