/**
 * @file scr_boot_anim.c
 * @brief CRT 风格开机动画实现
 * 
 * 动画流程（约 4.6 秒）：
 * Phase 1: CRT 横线展开 (0-500ms)
 * Phase 2: 四角标记淡入 (500-700ms)
 * Phase 3: 终端日志滚动 (600-2200ms)
 * Phase 4: Logo + Glitch 闪烁 (2200-2800ms)
 * Phase 5: 进度条填充 (2800-3600ms)
 * Phase 6: 放大淡出离场 (3600-4600ms)
 */

#include "scr_boot_anim.h"
#include "../edgewind_theme.h"
#include "../edgewind_ui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*******************************************************************************
 * 全局变量
 ******************************************************************************/

ew_boot_anim_t ew_boot_anim = {0};

/*******************************************************************************
 * 内部常量
 ******************************************************************************/

/* 屏幕尺寸 */
#define SCREEN_W    EW_SCREEN_WIDTH
#define SCREEN_H    EW_SCREEN_HEIGHT

/* 动画阶段时间 (ms) - 对齐HTML时序 */
#define PHASE1_CRT_DURATION      600     /* CRT 展开时长 - HTML: 600ms */
#define PHASE2_CORNER_DELAY      600     /* 四角淡入延迟 */
#define PHASE2_CORNER_DURATION   200     /* 四角淡入时长 */
#define PHASE3_LOG_START         600     /* 日志开始 - CRT完成后 */
#define PHASE3_LOG_INTERVAL      200     /* 每行日志间隔 - HTML: 100-300ms随机 */
#define PHASE4_GLITCH_INTERVAL   80      /* Glitch 闪烁间隔 - 更快 */
#define PHASE4_GLITCH_COUNT      5       /* Glitch 次数 - HTML: 约400ms */
#define PHASE5_PROGRESS_DURATION 1500    /* 进度条时长 - HTML: 1.5秒 */
#define PHASE6_ZOOMOUT_DURATION  600     /* 放大淡出时长 */

/* 终端日志内容 - 对齐HTML格式 [时间] 消息 状态 */
static const char *boot_logs[BOOT_LOG_LINES] = {
    "[0.002] CORTEX-M7 CORE RESET        OK",
    "[0.015] INIT AXI/SRAM BUS           OK",
    "[0.040] LOAD KERNEL @0x08000000     DONE",
    "[0.120] CHECK SENSORS (ADS131)      READY",
    "[0.350] WIFI UPLINK (ESP8266)       LINKED",
    "[0.800] MOUNTING FILESYSTEM         MOUNTED",
    "[1.200] STARTING GUI ENGINE         ..."
};

/* HEX背景字符缓冲区 */
static char hex_bg_buffer[1200];

/* 日志缓冲区 */
static char log_buffer[512];

/*******************************************************************************
 * 内部函数声明
 ******************************************************************************/

static void create_crt_effect(void);
static void create_hex_background(void);
static void create_corners(void);
static void create_grid_lines(void);
static void create_crt_scanline(void);
static void create_terminal(void);
static void create_logo_area(void);
static void create_scan_overlay(void);
static void generate_hex_string(void);
static void scan_overlay_anim_cb(void *var, int32_t v);

static void crt_height_anim_cb(void *var, int32_t v);
static void corner_opa_anim_cb(void *var, int32_t v);
static void scanline_timer_cb(lv_timer_t *timer);
static void log_timer_cb(lv_timer_t *timer);
static void log_timer_start_cb(lv_anim_t *a);
static void start_logo_phase(void);
static void timer_resume_ready_cb(lv_anim_t *a);
static void glitch_timer_cb(lv_timer_t *timer);
static void progress_anim_cb(void *var, int32_t v);
static void start_zoomout_phase(void);
static void zoomout_start_cb(lv_anim_t *a);
static void enter_btn_click_cb(lv_event_t *e);

/*******************************************************************************
 * 公共函数实现
 ******************************************************************************/

void ew_boot_anim_create(void)
{
    /* 创建屏幕 */
    ew_boot_anim.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ew_boot_anim.screen, BOOT_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ew_boot_anim.screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ew_boot_anim.screen, LV_OBJ_FLAG_SCROLLABLE);
    
    /* 初始化状态 */
    ew_boot_anim.finished = false;
    ew_boot_anim.phase = 0;
    ew_boot_anim.log_index = 0;
    ew_boot_anim.glitch_count = 0;
    ew_boot_anim.start_tick = lv_tick_get();
    memset(log_buffer, 0, sizeof(log_buffer));
    
    /* Phase 1: CRT 展开效果 */
    create_crt_effect();
    
    /* 背景HEX数据流（极淡，CRT完成后淡入） */
    create_hex_background();
    
    /* Phase 2: 四角标记（初始隐藏） */
    create_corners();
    
    /* 完整40px网格背景（初始隐藏） - 简化版CRT纹理 */
    create_grid_lines();
    
    /* CRT 扫描线动画（初始隐藏） */
    create_crt_scanline();
    
    /* Phase 3: 终端日志区域（初始隐藏） */
    create_terminal();
    
    /* 激光扫描特效（Logo显示时触发） */
    create_scan_overlay();
    
    /* Phase 4-6: Logo 区域（初始隐藏） */
    create_logo_area();
}

bool ew_boot_anim_is_finished(void)
{
    return ew_boot_anim.finished;
}

/*******************************************************************************
 * Phase 1: CRT 展开效果
 ******************************************************************************/

static void create_crt_effect(void)
{
    /* 创建 CRT 容器 - 初始为一条细线 */
    ew_boot_anim.crt_container = lv_obj_create(ew_boot_anim.screen);
    lv_obj_set_size(ew_boot_anim.crt_container, SCREEN_W, 2);
    lv_obj_align(ew_boot_anim.crt_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(ew_boot_anim.crt_container, BOOT_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(ew_boot_anim.crt_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ew_boot_anim.crt_container, 0, 0);
    lv_obj_set_style_radius(ew_boot_anim.crt_container, 0, 0);
    lv_obj_clear_flag(ew_boot_anim.crt_container, LV_OBJ_FLAG_SCROLLABLE);
    
    /* CRT 亮线效果 */
    ew_boot_anim.crt_line = lv_obj_create(ew_boot_anim.crt_container);
    lv_obj_set_size(ew_boot_anim.crt_line, SCREEN_W, 2);
    lv_obj_align(ew_boot_anim.crt_line, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(ew_boot_anim.crt_line, BOOT_COLOR_WHITE, 0);
    lv_obj_set_style_bg_opa(ew_boot_anim.crt_line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ew_boot_anim.crt_line, 0, 0);
    lv_obj_set_style_shadow_width(ew_boot_anim.crt_line, 30, 0);
    lv_obj_set_style_shadow_color(ew_boot_anim.crt_line, BOOT_COLOR_WHITE, 0);
    lv_obj_set_style_shadow_opa(ew_boot_anim.crt_line, LV_OPA_80, 0);
    lv_obj_clear_flag(ew_boot_anim.crt_line, LV_OBJ_FLAG_SCROLLABLE);
    
    /* CRT 高度展开动画 */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ew_boot_anim.crt_container);
    lv_anim_set_values(&a, 2, SCREEN_H);
    lv_anim_set_time(&a, PHASE1_CRT_DURATION);
    lv_anim_set_delay(&a, 100);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, crt_height_anim_cb);
    lv_anim_start(&a);
    
    /* CRT 亮线淡出动画 */
    lv_anim_t line_anim;
    lv_anim_init(&line_anim);
    lv_anim_set_var(&line_anim, ew_boot_anim.crt_line);
    lv_anim_set_values(&line_anim, 255, 0);
    lv_anim_set_time(&line_anim, 300);
    lv_anim_set_delay(&line_anim, 200);
    lv_anim_set_path_cb(&line_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&line_anim, corner_opa_anim_cb);
    lv_anim_start(&line_anim);
}

static void crt_height_anim_cb(void *var, int32_t v)
{
    lv_obj_t *obj = (lv_obj_t *)var;
    lv_obj_set_height(obj, v);
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
}

/*******************************************************************************
 * 背景HEX数据流 - 对齐HTML hex-bg效果
 ******************************************************************************/

static void generate_hex_string(void)
{
    /* 生成随机HEX字符串填充背景 */
    for (int i = 0; i < 400; i++) {
        sprintf(hex_bg_buffer + i * 3, "%02X ", rand() % 256);
    }
    hex_bg_buffer[1199] = '\0';
}

static void create_hex_background(void)
{
    /* 生成HEX字符串 */
    generate_hex_string();
    
    /* 创建HEX背景标签 */
    ew_boot_anim.hex_bg_label = lv_label_create(ew_boot_anim.screen);
    lv_label_set_text(ew_boot_anim.hex_bg_label, hex_bg_buffer);
    lv_obj_set_size(ew_boot_anim.hex_bg_label, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(ew_boot_anim.hex_bg_label, 0, 0);
    lv_obj_set_style_text_font(ew_boot_anim.hex_bg_label, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(ew_boot_anim.hex_bg_label, BOOT_COLOR_HEX_BG, 0);
    lv_obj_set_style_text_line_space(ew_boot_anim.hex_bg_label, 2, 0);
    lv_obj_set_style_opa(ew_boot_anim.hex_bg_label, LV_OPA_TRANSP, 0);  /* 初始隐藏 */
    lv_label_set_long_mode(ew_boot_anim.hex_bg_label, LV_LABEL_LONG_WRAP);
    
    /* CRT完成后淡入到15%透明度 - 对齐HTML */
    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, ew_boot_anim.hex_bg_label);
    lv_anim_set_values(&fade, 0, 38);  /* 15% of 255 ≈ 38 */
    lv_anim_set_time(&fade, 1000);
    lv_anim_set_delay(&fade, PHASE1_CRT_DURATION);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fade, corner_opa_anim_cb);
    lv_anim_start(&fade);
}

/*******************************************************************************
 * Phase 2: 四角标记
 ******************************************************************************/

static lv_obj_t *create_corner_line(lv_obj_t *parent, bool horizontal, int32_t len)
{
    lv_obj_t *line = lv_obj_create(parent);
    if (horizontal) {
        lv_obj_set_size(line, len, 2);
    } else {
        lv_obj_set_size(line, 2, len);
    }
    lv_obj_set_style_bg_color(line, BOOT_COLOR_GRAY, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_70, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

static void create_corners(void)
{
    const int32_t corner_len = 20;
    const int32_t margin = 20;
    
    /* 左上角 */
    ew_boot_anim.corner_tl = lv_obj_create(ew_boot_anim.screen);
    lv_obj_set_size(ew_boot_anim.corner_tl, corner_len, corner_len);
    lv_obj_set_pos(ew_boot_anim.corner_tl, margin, margin);
    lv_obj_set_style_bg_opa(ew_boot_anim.corner_tl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ew_boot_anim.corner_tl, 0, 0);
    lv_obj_clear_flag(ew_boot_anim.corner_tl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(ew_boot_anim.corner_tl, LV_OPA_TRANSP, 0);
    lv_obj_t *tl_h = create_corner_line(ew_boot_anim.corner_tl, true, corner_len);
    lv_obj_align(tl_h, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *tl_v = create_corner_line(ew_boot_anim.corner_tl, false, corner_len);
    lv_obj_align(tl_v, LV_ALIGN_TOP_LEFT, 0, 0);
    
    /* 右上角 */
    ew_boot_anim.corner_tr = lv_obj_create(ew_boot_anim.screen);
    lv_obj_set_size(ew_boot_anim.corner_tr, corner_len, corner_len);
    lv_obj_set_pos(ew_boot_anim.corner_tr, SCREEN_W - margin - corner_len, margin);
    lv_obj_set_style_bg_opa(ew_boot_anim.corner_tr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ew_boot_anim.corner_tr, 0, 0);
    lv_obj_clear_flag(ew_boot_anim.corner_tr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(ew_boot_anim.corner_tr, LV_OPA_TRANSP, 0);
    lv_obj_t *tr_h = create_corner_line(ew_boot_anim.corner_tr, true, corner_len);
    lv_obj_align(tr_h, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *tr_v = create_corner_line(ew_boot_anim.corner_tr, false, corner_len);
    lv_obj_align(tr_v, LV_ALIGN_TOP_RIGHT, 0, 0);
    
    /* 左下角 */
    ew_boot_anim.corner_bl = lv_obj_create(ew_boot_anim.screen);
    lv_obj_set_size(ew_boot_anim.corner_bl, corner_len, corner_len);
    lv_obj_set_pos(ew_boot_anim.corner_bl, margin, SCREEN_H - margin - corner_len);
    lv_obj_set_style_bg_opa(ew_boot_anim.corner_bl, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ew_boot_anim.corner_bl, 0, 0);
    lv_obj_clear_flag(ew_boot_anim.corner_bl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(ew_boot_anim.corner_bl, LV_OPA_TRANSP, 0);
    lv_obj_t *bl_h = create_corner_line(ew_boot_anim.corner_bl, true, corner_len);
    lv_obj_align(bl_h, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *bl_v = create_corner_line(ew_boot_anim.corner_bl, false, corner_len);
    lv_obj_align(bl_v, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    
    /* 右下角 */
    ew_boot_anim.corner_br = lv_obj_create(ew_boot_anim.screen);
    lv_obj_set_size(ew_boot_anim.corner_br, corner_len, corner_len);
    lv_obj_set_pos(ew_boot_anim.corner_br, SCREEN_W - margin - corner_len, SCREEN_H - margin - corner_len);
    lv_obj_set_style_bg_opa(ew_boot_anim.corner_br, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ew_boot_anim.corner_br, 0, 0);
    lv_obj_clear_flag(ew_boot_anim.corner_br, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(ew_boot_anim.corner_br, LV_OPA_TRANSP, 0);
    lv_obj_t *br_h = create_corner_line(ew_boot_anim.corner_br, true, corner_len);
    lv_obj_align(br_h, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t *br_v = create_corner_line(ew_boot_anim.corner_br, false, corner_len);
    lv_obj_align(br_v, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    
    /* 四角淡入动画 */
    lv_obj_t *corners[] = {ew_boot_anim.corner_tl, ew_boot_anim.corner_tr, 
                           ew_boot_anim.corner_bl, ew_boot_anim.corner_br};
    for (int i = 0; i < 4; i++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, corners[i]);
        lv_anim_set_values(&a, 0, 255);
        lv_anim_set_time(&a, PHASE2_CORNER_DURATION);
        lv_anim_set_delay(&a, PHASE2_CORNER_DELAY);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, corner_opa_anim_cb);
        lv_anim_start(&a);
    }
}

static void corner_opa_anim_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

/*******************************************************************************
 * 装饰网格线
 ******************************************************************************/

static void create_grid_lines(void)
{
    /* 完整40px网格背景 - HTML效果 */
    /* 横向网格线 - 800 / 40 = 20 条 */
    for (int i = 0; i < 20; i++) {
        ew_boot_anim.grid_lines_h[i] = lv_obj_create(ew_boot_anim.screen);
        lv_obj_set_size(ew_boot_anim.grid_lines_h[i], SCREEN_W, 1);
        lv_obj_set_pos(ew_boot_anim.grid_lines_h[i], 0, i * 40);
        lv_obj_set_style_bg_color(ew_boot_anim.grid_lines_h[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(ew_boot_anim.grid_lines_h[i], 8, 0); /* 3% 透明度 */
        lv_obj_set_style_border_width(ew_boot_anim.grid_lines_h[i], 0, 0);
        lv_obj_set_style_radius(ew_boot_anim.grid_lines_h[i], 0, 0);
        lv_obj_set_style_opa(ew_boot_anim.grid_lines_h[i], LV_OPA_TRANSP, 0); /* 初始隐藏 */
        lv_obj_clear_flag(ew_boot_anim.grid_lines_h[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    
    /* 纵向网格线 - 480 / 40 = 12 条 */
    for (int i = 0; i < 12; i++) {
        ew_boot_anim.grid_lines_v[i] = lv_obj_create(ew_boot_anim.screen);
        lv_obj_set_size(ew_boot_anim.grid_lines_v[i], 1, SCREEN_H);
        lv_obj_set_pos(ew_boot_anim.grid_lines_v[i], i * 40, 0);
        lv_obj_set_style_bg_color(ew_boot_anim.grid_lines_v[i], lv_color_white(), 0);
        lv_obj_set_style_bg_opa(ew_boot_anim.grid_lines_v[i], 8, 0); /* 3% 透明度 */
        lv_obj_set_style_border_width(ew_boot_anim.grid_lines_v[i], 0, 0);
        lv_obj_set_style_radius(ew_boot_anim.grid_lines_v[i], 0, 0);
        lv_obj_set_style_opa(ew_boot_anim.grid_lines_v[i], LV_OPA_TRANSP, 0); /* 初始隐藏 */
        lv_obj_clear_flag(ew_boot_anim.grid_lines_v[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    
    /* 网格淡入动画 - 与四角同步，32条线同时淡入 */
    for (int i = 0; i < 20; i++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, ew_boot_anim.grid_lines_h[i]);
        lv_anim_set_values(&a, 0, 255);
        lv_anim_set_time(&a, 800);
        lv_anim_set_delay(&a, PHASE2_CORNER_DELAY + i * 10); /* 微小的延迟错开，增加层次感 */
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, corner_opa_anim_cb);
        lv_anim_start(&a);
    }
    
    for (int i = 0; i < 12; i++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, ew_boot_anim.grid_lines_v[i]);
        lv_anim_set_values(&a, 0, 255);
        lv_anim_set_time(&a, 800);
        lv_anim_set_delay(&a, PHASE2_CORNER_DELAY + i * 10);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, corner_opa_anim_cb);
        lv_anim_start(&a);
    }
}

/*******************************************************************************
 * CRT 扫描线动画
 ******************************************************************************/

static void create_crt_scanline(void)
{
    /* 初始化扫描线Y坐标 */
    ew_boot_anim.scanline_y = 0;
    
    /* 创建扫描线 - 2px高的白色亮线 */
    ew_boot_anim.crt_scanline = lv_obj_create(ew_boot_anim.screen);
    lv_obj_set_size(ew_boot_anim.crt_scanline, SCREEN_W, 2);
    lv_obj_set_pos(ew_boot_anim.crt_scanline, 0, 0);
    lv_obj_set_style_bg_color(ew_boot_anim.crt_scanline, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(ew_boot_anim.crt_scanline, 25, 0); /* 10% 透明度 */
    lv_obj_set_style_border_width(ew_boot_anim.crt_scanline, 0, 0);
    lv_obj_set_style_radius(ew_boot_anim.crt_scanline, 0, 0);
    lv_obj_set_style_opa(ew_boot_anim.crt_scanline, LV_OPA_TRANSP, 0); /* 初始隐藏 */
    lv_obj_clear_flag(ew_boot_anim.crt_scanline, LV_OBJ_FLAG_SCROLLABLE);
    
    /* 淡入扫描线 */
    lv_anim_t fadein;
    lv_anim_init(&fadein);
    lv_anim_set_var(&fadein, ew_boot_anim.crt_scanline);
    lv_anim_set_values(&fadein, 0, 255);
    lv_anim_set_time(&fadein, 200);
    lv_anim_set_delay(&fadein, PHASE2_CORNER_DELAY + 100);
    lv_anim_set_path_cb(&fadein, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fadein, corner_opa_anim_cb);
    lv_anim_start(&fadein);
    
    /* 创建定时器控制扫描线移动 - 每5ms移动3px，800ms循环一次 */
    ew_boot_anim.scanline_timer = lv_timer_create(scanline_timer_cb, 5, NULL);
    lv_timer_set_repeat_count(ew_boot_anim.scanline_timer, 160); /* 160次 * 5ms = 800ms */
    lv_timer_pause(ew_boot_anim.scanline_timer);
    
    /* 延迟启动扫描线定时器 */
    lv_anim_t delay_start;
    lv_anim_init(&delay_start);
    lv_anim_set_var(&delay_start, ew_boot_anim.scanline_timer);
    lv_anim_set_values(&delay_start, 0, 1);
    lv_anim_set_time(&delay_start, 1);
    lv_anim_set_delay(&delay_start, PHASE2_CORNER_DELAY + 300);
    lv_anim_set_ready_cb(&delay_start, timer_resume_ready_cb);
    lv_anim_start(&delay_start);
}

static void scanline_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    
    /* 移动扫描线 */
    lv_obj_set_pos(ew_boot_anim.crt_scanline, 0, ew_boot_anim.scanline_y);
    ew_boot_anim.scanline_y += 3;
    
    if (ew_boot_anim.scanline_y >= SCREEN_H) {
        /* 扫描线到底后淡出并停止 */
        ew_boot_anim.scanline_y = 0;
        lv_anim_t fadeout;
        lv_anim_init(&fadeout);
        lv_anim_set_var(&fadeout, ew_boot_anim.crt_scanline);
        lv_anim_set_values(&fadeout, 255, 0);
        lv_anim_set_time(&fadeout, 200);
        lv_anim_set_exec_cb(&fadeout, corner_opa_anim_cb);
        lv_anim_start(&fadeout);
    }
}

/*******************************************************************************
 * Phase 3: 终端日志
 ******************************************************************************/

static void create_terminal(void)
{
    ew_boot_anim.terminal_label = lv_label_create(ew_boot_anim.screen);
    lv_label_set_text(ew_boot_anim.terminal_label, "");
    lv_obj_set_style_text_font(ew_boot_anim.terminal_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ew_boot_anim.terminal_label, BOOT_COLOR_GREEN, 0);
    lv_obj_set_style_text_line_space(ew_boot_anim.terminal_label, 4, 0);
    lv_obj_set_pos(ew_boot_anim.terminal_label, 40, SCREEN_H - 180);
    lv_obj_set_style_opa(ew_boot_anim.terminal_label, LV_OPA_TRANSP, 0);
    
    /* 文字发光效果 */
    lv_obj_set_style_shadow_width(ew_boot_anim.terminal_label, 5, 0);
    lv_obj_set_style_shadow_color(ew_boot_anim.terminal_label, BOOT_COLOR_GREEN, 0);
    lv_obj_set_style_shadow_opa(ew_boot_anim.terminal_label, LV_OPA_50, 0);
    
    /* 启动日志定时器 */
    ew_boot_anim.log_timer = lv_timer_create(log_timer_cb, PHASE3_LOG_INTERVAL, NULL);
    lv_timer_set_repeat_count(ew_boot_anim.log_timer, BOOT_LOG_LINES + 1);
    /* 延迟启动 */
    lv_timer_pause(ew_boot_anim.log_timer);
    
    /* 延迟启动日志显示 */
    lv_anim_t delay_anim;
    lv_anim_init(&delay_anim);
    lv_anim_set_var(&delay_anim, ew_boot_anim.terminal_label);
    lv_anim_set_values(&delay_anim, 0, 255);
    lv_anim_set_time(&delay_anim, 100);
    lv_anim_set_delay(&delay_anim, PHASE3_LOG_START);
    lv_anim_set_exec_cb(&delay_anim, corner_opa_anim_cb);
    lv_anim_set_ready_cb(&delay_anim, log_timer_start_cb);
    lv_anim_start(&delay_anim);
}

/* 日志定时器恢复回调 */
static void log_timer_start_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    lv_timer_resume(ew_boot_anim.log_timer);
}

/* 通用：动画完成后恢复定时器 */
static void timer_resume_ready_cb(lv_anim_t *a)
{
    if (!a) return;
    lv_timer_t *timer = (lv_timer_t *)a->var;
    if (timer) {
        lv_timer_resume(timer);
    }
}

static void log_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    
    if (ew_boot_anim.log_index < BOOT_LOG_LINES) {
        /* 追加新的日志行 */
        if (ew_boot_anim.log_index == 0) {
            strcpy(log_buffer, boot_logs[0]);
        } else {
            strcat(log_buffer, "\n");
            strcat(log_buffer, boot_logs[ew_boot_anim.log_index]);
        }
        lv_label_set_text(ew_boot_anim.terminal_label, log_buffer);
        ew_boot_anim.log_index++;
    } else {
        /* 日志显示完毕，启动 Logo 阶段 */
        start_logo_phase();
    }
}

/*******************************************************************************
 * 激光扫描特效 - 对齐HTML scan-overlay效果
 ******************************************************************************/

static void create_scan_overlay(void)
{
    /* 创建扫描光带 - 初始在Logo左侧外 */
    ew_boot_anim.scan_overlay = lv_obj_create(ew_boot_anim.screen);
    lv_obj_set_size(ew_boot_anim.scan_overlay, 30, 150);  /* 光带宽度30px，高度覆盖Logo */
    lv_obj_set_pos(ew_boot_anim.scan_overlay, -50, (SCREEN_H - 150) / 2);  /* 初始在屏幕左侧外 */
    
    /* 霓虹青色光带 - 对齐HTML #00fff2 */
    lv_obj_set_style_bg_color(ew_boot_anim.scan_overlay, BOOT_COLOR_CYAN, 0);
    lv_obj_set_style_bg_opa(ew_boot_anim.scan_overlay, LV_OPA_60, 0);
    lv_obj_set_style_bg_grad_color(ew_boot_anim.scan_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_grad_dir(ew_boot_anim.scan_overlay, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(ew_boot_anim.scan_overlay, 0, 0);
    lv_obj_set_style_radius(ew_boot_anim.scan_overlay, 2, 0);
    
    /* 发光效果 */
    lv_obj_set_style_shadow_width(ew_boot_anim.scan_overlay, 40, 0);
    lv_obj_set_style_shadow_color(ew_boot_anim.scan_overlay, BOOT_COLOR_CYAN, 0);
    lv_obj_set_style_shadow_opa(ew_boot_anim.scan_overlay, LV_OPA_80, 0);
    
    lv_obj_set_style_opa(ew_boot_anim.scan_overlay, LV_OPA_TRANSP, 0);  /* 初始隐藏 */
    lv_obj_clear_flag(ew_boot_anim.scan_overlay, LV_OBJ_FLAG_SCROLLABLE);
}

static void scan_overlay_anim_cb(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
}

/*******************************************************************************
 * Phase 4: Logo + Glitch 特效
 ******************************************************************************/

static void create_logo_area(void)
{
    /* Logo 容器 */
    ew_boot_anim.logo_container = lv_obj_create(ew_boot_anim.screen);
    lv_obj_set_size(ew_boot_anim.logo_container, 300, 200);
    lv_obj_align(ew_boot_anim.logo_container, LV_ALIGN_CENTER, 0, -50); /* 上移50px为按钮留空间 */
    /* 完全清除所有可见样式 */
    lv_obj_set_style_bg_opa(ew_boot_anim.logo_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ew_boot_anim.logo_container, 0, 0);
    lv_obj_set_style_border_opa(ew_boot_anim.logo_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_width(ew_boot_anim.logo_container, 0, 0);
    lv_obj_set_style_shadow_width(ew_boot_anim.logo_container, 0, 0);
    lv_obj_clear_flag(ew_boot_anim.logo_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(ew_boot_anim.logo_container, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(ew_boot_anim.logo_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ew_boot_anim.logo_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(ew_boot_anim.logo_container, 8, 0);
    lv_obj_set_style_pad_all(ew_boot_anim.logo_container, 0, 0);
    
    /* 设置缩放中心点 */
    lv_obj_set_style_transform_pivot_x(ew_boot_anim.logo_container, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(ew_boot_anim.logo_container, lv_pct(50), 0);
    
    /* 风力图标 - 用 lv_line 绘制 SVG 路径 (放大1.5倍) */
    /* 创建旋转容器用于动画 */
    ew_boot_anim.icon_rotate_container = lv_obj_create(ew_boot_anim.logo_container);
    lv_obj_set_size(ew_boot_anim.icon_rotate_container, 150, 110); /* 放大: 100x80 -> 150x110 */
    /* 完全清除所有可见样式 - 修复边框显示问题 */
    lv_obj_set_style_bg_opa(ew_boot_anim.icon_rotate_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ew_boot_anim.icon_rotate_container, 0, 0);
    lv_obj_set_style_border_opa(ew_boot_anim.icon_rotate_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_width(ew_boot_anim.icon_rotate_container, 0, 0);
    lv_obj_set_style_shadow_width(ew_boot_anim.icon_rotate_container, 0, 0);
    lv_obj_set_style_pad_all(ew_boot_anim.icon_rotate_container, 0, 0);
    lv_obj_clear_flag(ew_boot_anim.icon_rotate_container, LV_OBJ_FLAG_SCROLLABLE);
    /* 设置旋转中心点为容器中心 */
    lv_obj_set_style_transform_pivot_x(ew_boot_anim.icon_rotate_container, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(ew_boot_anim.icon_rotate_container, lv_pct(50), 0);
    
    lv_obj_t *icon_container = ew_boot_anim.icon_rotate_container;
    
    /* SVG: 风车上叶片 (坐标放大1.5倍) */
    static lv_point_precise_t blade1[] = {
        {36, 55}, {18, 24}, {54, 48}, {36, 55}
    };
    ew_boot_anim.logo_icon_line1 = lv_line_create(icon_container);
    lv_line_set_points(ew_boot_anim.logo_icon_line1, blade1, 4);
    lv_obj_set_style_line_width(ew_boot_anim.logo_icon_line1, 7, 0);  /* 放大: 5 -> 7 */
    lv_obj_set_style_line_color(ew_boot_anim.logo_icon_line1, BOOT_COLOR_BLUE, 0);
    lv_obj_set_style_line_rounded(ew_boot_anim.logo_icon_line1, true, 0);
    lv_obj_set_style_shadow_width(ew_boot_anim.logo_icon_line1, 15, 0);  /* 放大: 8 -> 15 */
    lv_obj_set_style_shadow_color(ew_boot_anim.logo_icon_line1, BOOT_COLOR_BLUE, 0);
    lv_obj_align(ew_boot_anim.logo_icon_line1, LV_ALIGN_CENTER, 0, 0);
    
    /* SVG: 风车下叶片 (坐标放大1.5倍) */
    static lv_point_precise_t blade2[] = {
        {36, 55}, {24, 86}, {60, 67}, {36, 55}
    };
    ew_boot_anim.logo_icon_line2 = lv_line_create(icon_container);
    lv_line_set_points(ew_boot_anim.logo_icon_line2, blade2, 4);
    lv_obj_set_style_line_width(ew_boot_anim.logo_icon_line2, 7, 0);  /* 放大: 5 -> 7 */
    lv_obj_set_style_line_color(ew_boot_anim.logo_icon_line2, BOOT_COLOR_BLUE, 0);
    lv_obj_set_style_line_rounded(ew_boot_anim.logo_icon_line2, true, 0);
    lv_obj_set_style_shadow_width(ew_boot_anim.logo_icon_line2, 15, 0);  /* 放大: 8 -> 15 */
    lv_obj_set_style_shadow_color(ew_boot_anim.logo_icon_line2, BOOT_COLOR_BLUE, 0);
    lv_obj_align(ew_boot_anim.logo_icon_line2, LV_ALIGN_CENTER, 0, 0);
    
    /* SVG: 心电图波形 (坐标放大1.5倍) */
    static lv_point_precise_t wave[] = {
        {36, 55}, {66, 55}, {78, 24}, {90, 86}, {102, 55}, {114, 55}
    };
    ew_boot_anim.logo_icon_line3 = lv_line_create(icon_container);
    lv_line_set_points(ew_boot_anim.logo_icon_line3, wave, 6);
    lv_obj_set_style_line_width(ew_boot_anim.logo_icon_line3, 7, 0);  /* 放大: 5 -> 7 */
    lv_obj_set_style_line_color(ew_boot_anim.logo_icon_line3, BOOT_COLOR_BLUE, 0);
    lv_obj_set_style_line_rounded(ew_boot_anim.logo_icon_line3, true, 0);
    lv_obj_set_style_shadow_width(ew_boot_anim.logo_icon_line3, 15, 0);  /* 放大: 8 -> 15 */
    lv_obj_set_style_shadow_color(ew_boot_anim.logo_icon_line3, BOOT_COLOR_BLUE, 0);
    lv_obj_align(ew_boot_anim.logo_icon_line3, LV_ALIGN_CENTER, 0, 0);
    
    /* EDGEWIND 主标题容器 - 用于叠加Glitch层 */
    lv_obj_t *text_container = lv_obj_create(ew_boot_anim.logo_container);
    lv_obj_set_size(text_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    /* 完全清除所有可见样式 - 文字容器 */
    lv_obj_set_style_bg_opa(text_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(text_container, 0, 0);
    lv_obj_set_style_border_opa(text_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_width(text_container, 0, 0);
    lv_obj_set_style_shadow_width(text_container, 0, 0);
    lv_obj_set_style_pad_all(text_container, 0, 0);
    lv_obj_clear_flag(text_container, LV_OBJ_FLAG_SCROLLABLE);
    
    /* 红色偏移层（底层） - 放大字号 */
    ew_boot_anim.logo_text_red = lv_label_create(text_container);
    lv_label_set_text(ew_boot_anim.logo_text_red, "EDGEWIND");
    lv_obj_set_style_text_font(ew_boot_anim.logo_text_red, &lv_font_montserrat_40, 0); /* 放大: 32 -> 40 */
    lv_obj_set_style_text_color(ew_boot_anim.logo_text_red, BOOT_COLOR_RED, 0);
    lv_obj_set_style_text_letter_space(ew_boot_anim.logo_text_red, 4, 0); /* 放大: 2 -> 4 */
    lv_obj_set_style_opa(ew_boot_anim.logo_text_red, LV_OPA_TRANSP, 0); /* 初始隐藏 */
    lv_obj_set_pos(ew_boot_anim.logo_text_red, 0, 0);
    
    /* 蓝色偏移层（中层） - 放大字号 */
    ew_boot_anim.logo_text_blue = lv_label_create(text_container);
    lv_label_set_text(ew_boot_anim.logo_text_blue, "EDGEWIND");
    lv_obj_set_style_text_font(ew_boot_anim.logo_text_blue, &lv_font_montserrat_40, 0); /* 放大: 32 -> 40 */
    lv_obj_set_style_text_color(ew_boot_anim.logo_text_blue, BOOT_COLOR_BLUE, 0);
    lv_obj_set_style_text_letter_space(ew_boot_anim.logo_text_blue, 4, 0); /* 放大: 2 -> 4 */
    lv_obj_set_style_opa(ew_boot_anim.logo_text_blue, LV_OPA_TRANSP, 0); /* 初始隐藏 */
    lv_obj_set_pos(ew_boot_anim.logo_text_blue, 0, 0);
    
    /* 白色主文字（顶层） - 放大字号 + 增强霓虹灯阴影 */
    ew_boot_anim.logo_text = lv_label_create(text_container);
    lv_label_set_text(ew_boot_anim.logo_text, "EDGEWIND");
    lv_obj_set_style_text_font(ew_boot_anim.logo_text, &lv_font_montserrat_40, 0); /* 放大: 32 -> 40 */
    lv_obj_set_style_text_color(ew_boot_anim.logo_text, BOOT_COLOR_WHITE, 0);
    lv_obj_set_style_text_letter_space(ew_boot_anim.logo_text, 4, 0); /* 放大: 2 -> 4 */
    lv_obj_set_pos(ew_boot_anim.logo_text, 0, 0);
    /* 强烈的蓝色霓虹发光阴影 - 增强 */
    lv_obj_set_style_shadow_width(ew_boot_anim.logo_text, 35, 0); /* 放大: 25 -> 35 */
    lv_obj_set_style_shadow_color(ew_boot_anim.logo_text, BOOT_COLOR_BLUE, 0);
    lv_obj_set_style_shadow_opa(ew_boot_anim.logo_text, LV_OPA_90, 0); /* 增强: 80 -> 90 */
    lv_obj_set_style_shadow_offset_x(ew_boot_anim.logo_text, 0, 0);
    lv_obj_set_style_shadow_offset_y(ew_boot_anim.logo_text, 0, 0);
    lv_obj_set_style_shadow_spread(ew_boot_anim.logo_text, 5, 0); /* 放大: 3 -> 5 */
    
    /* SYSTEM V2.0 副标题 - 对齐HTML */
    ew_boot_anim.logo_sub = lv_label_create(ew_boot_anim.logo_container);
    lv_label_set_text(ew_boot_anim.logo_sub, "SYSTEM V2.0");
    lv_obj_set_style_text_font(ew_boot_anim.logo_sub, &lv_font_montserrat_14, 0); /* 放大: 12 -> 14 */
    lv_obj_set_style_text_color(ew_boot_anim.logo_sub, BOOT_COLOR_BLUE, 0);
    lv_obj_set_style_text_letter_space(ew_boot_anim.logo_sub, 8, 0); /* 放大: 6 -> 8 */
    /* 副标题也添加发光 - 增强 */
    lv_obj_set_style_shadow_width(ew_boot_anim.logo_sub, 18, 0); /* 放大: 12 -> 18 */
    lv_obj_set_style_shadow_color(ew_boot_anim.logo_sub, BOOT_COLOR_BLUE, 0);
    lv_obj_set_style_shadow_opa(ew_boot_anim.logo_sub, LV_OPA_60, 0);
    
    /* 进度条 - 独立于logo_container，直接挂在screen上 */
    ew_boot_anim.progress_bar = lv_bar_create(ew_boot_anim.screen);
    lv_obj_set_size(ew_boot_anim.progress_bar, 200, 4);
    lv_obj_align(ew_boot_anim.progress_bar, LV_ALIGN_CENTER, 0, 50); /* Logo下方50px */
    lv_bar_set_range(ew_boot_anim.progress_bar, 0, 100);
    lv_bar_set_value(ew_boot_anim.progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ew_boot_anim.progress_bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ew_boot_anim.progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(ew_boot_anim.progress_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ew_boot_anim.progress_bar, BOOT_COLOR_BLUE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ew_boot_anim.progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(ew_boot_anim.progress_bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(ew_boot_anim.progress_bar, 8, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_color(ew_boot_anim.progress_bar, BOOT_COLOR_BLUE, LV_PART_INDICATOR);
    /* 初始隐藏进度条，等 Glitch 结束后再显示 */
    lv_obj_set_style_opa(ew_boot_anim.progress_bar, LV_OPA_TRANSP, 0);
    /* 确保进度条在最顶层显示 */
    lv_obj_move_foreground(ew_boot_anim.progress_bar);
    
    /* 进入系统按钮 - 初始隐藏 */
    ew_boot_anim.enter_btn = lv_btn_create(ew_boot_anim.screen);
    lv_obj_set_size(ew_boot_anim.enter_btn, 180, 50);
    lv_obj_align(ew_boot_anim.enter_btn, LV_ALIGN_BOTTOM_MID, 0, -60);
    /* 科技风格样式 */
    lv_obj_set_style_bg_color(ew_boot_anim.enter_btn, lv_color_hex(0x1E1E24), 0);
    lv_obj_set_style_bg_opa(ew_boot_anim.enter_btn, LV_OPA_90, 0);
    lv_obj_set_style_border_width(ew_boot_anim.enter_btn, 2, 0);
    lv_obj_set_style_border_color(ew_boot_anim.enter_btn, BOOT_COLOR_BLUE, 0);
    lv_obj_set_style_radius(ew_boot_anim.enter_btn, 8, 0);
    lv_obj_set_style_shadow_width(ew_boot_anim.enter_btn, 20, 0);
    lv_obj_set_style_shadow_color(ew_boot_anim.enter_btn, BOOT_COLOR_BLUE, 0);
    lv_obj_set_style_shadow_opa(ew_boot_anim.enter_btn, LV_OPA_60, 0);
    /* 按下效果 */
    lv_obj_set_style_bg_color(ew_boot_anim.enter_btn, BOOT_COLOR_BLUE, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(ew_boot_anim.enter_btn, LV_OPA_30, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale(ew_boot_anim.enter_btn, 240, LV_STATE_PRESSED); /* 95% */
    /* 按钮文字 */
    ew_boot_anim.enter_btn_label = lv_label_create(ew_boot_anim.enter_btn);
    lv_label_set_text(ew_boot_anim.enter_btn_label, "进 入 系 统");
    lv_obj_center(ew_boot_anim.enter_btn_label);
    /* 中文必须使用思源宋体字库，否则会显示方块 */
    lv_obj_set_style_text_font(ew_boot_anim.enter_btn_label, EW_FONT_CN_NORMAL, 0);
    lv_obj_set_style_text_color(ew_boot_anim.enter_btn_label, BOOT_COLOR_WHITE, 0);
    /* 初始隐藏 */
    lv_obj_set_style_opa(ew_boot_anim.enter_btn, LV_OPA_TRANSP, 0);
    /* 确保按钮在最顶层显示 */
    lv_obj_move_foreground(ew_boot_anim.enter_btn);
}

static void start_logo_phase(void)
{
    /* 隐藏终端日志 - 对齐HTML: 500ms淡出 */
    lv_anim_t hide_term;
    lv_anim_init(&hide_term);
    lv_anim_set_var(&hide_term, ew_boot_anim.terminal_label);
    lv_anim_set_values(&hide_term, 255, 0);
    lv_anim_set_time(&hide_term, 500);
    lv_anim_set_exec_cb(&hide_term, corner_opa_anim_cb);
    lv_anim_start(&hide_term);
    
    /* 显示 Logo 容器 - 对齐HTML */
    lv_anim_t show_logo;
    lv_anim_init(&show_logo);
    lv_anim_set_var(&show_logo, ew_boot_anim.logo_container);
    lv_anim_set_values(&show_logo, 0, 255);
    lv_anim_set_time(&show_logo, 100);
    lv_anim_set_delay(&show_logo, 200);
    lv_anim_set_exec_cb(&show_logo, corner_opa_anim_cb);
    lv_anim_start(&show_logo);
    
    /* 激光扫描特效 - 对齐HTML scan-overlay效果 */
    /* 1. 淡入扫描光带 */
    lv_anim_t scan_fadein;
    lv_anim_init(&scan_fadein);
    lv_anim_set_var(&scan_fadein, ew_boot_anim.scan_overlay);
    lv_anim_set_values(&scan_fadein, 0, 255);
    lv_anim_set_time(&scan_fadein, 100);
    lv_anim_set_delay(&scan_fadein, 100);
    lv_anim_set_exec_cb(&scan_fadein, corner_opa_anim_cb);
    lv_anim_start(&scan_fadein);
    
    /* 2. 扫描光带从左到右移动 - 对齐HTML: 1.5秒 */
    lv_anim_t scan_move;
    lv_anim_init(&scan_move);
    lv_anim_set_var(&scan_move, ew_boot_anim.scan_overlay);
    lv_anim_set_values(&scan_move, -50, SCREEN_W + 50);  /* 从屏幕左外到右外 */
    lv_anim_set_time(&scan_move, 1500);
    lv_anim_set_delay(&scan_move, 100);
    lv_anim_set_path_cb(&scan_move, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&scan_move, scan_overlay_anim_cb);
    lv_anim_start(&scan_move);
    
    /* 3. 扫描光带淡出 */
    lv_anim_t scan_fadeout;
    lv_anim_init(&scan_fadeout);
    lv_anim_set_var(&scan_fadeout, ew_boot_anim.scan_overlay);
    lv_anim_set_values(&scan_fadeout, 255, 0);
    lv_anim_set_time(&scan_fadeout, 200);
    lv_anim_set_delay(&scan_fadeout, 1500);  /* 移动结束后淡出 */
    lv_anim_set_exec_cb(&scan_fadeout, corner_opa_anim_cb);
    lv_anim_start(&scan_fadeout);
    
    /* Glitch 闪烁 - 对齐HTML: 激光扫描后开始，约400ms */
    ew_boot_anim.glitch_count = 0;
    ew_boot_anim.glitch_timer = lv_timer_create(glitch_timer_cb, PHASE4_GLITCH_INTERVAL, NULL);
    lv_timer_set_repeat_count(ew_boot_anim.glitch_timer, PHASE4_GLITCH_COUNT + 1);
    lv_timer_pause(ew_boot_anim.glitch_timer);
    
    /* 延迟启动 Glitch 定时器 - 对齐HTML: 激光扫描后 */
    lv_anim_t delay_glitch;
    lv_anim_init(&delay_glitch);
    lv_anim_set_var(&delay_glitch, ew_boot_anim.glitch_timer);
    lv_anim_set_values(&delay_glitch, 0, 1);
    lv_anim_set_time(&delay_glitch, 1);
    lv_anim_set_delay(&delay_glitch, 800);  /* SVG路径绘制后 */
    lv_anim_set_ready_cb(&delay_glitch, timer_resume_ready_cb);
    lv_anim_start(&delay_glitch);
}

static void glitch_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    
    if (ew_boot_anim.glitch_count < PHASE4_GLITCH_COUNT) {
        /* Glitch 特效：快速切换红蓝层的位置和透明度 */
        /* 先隐藏所有层 */
        lv_obj_set_style_opa(ew_boot_anim.logo_text_red, LV_OPA_TRANSP, 0);
        lv_obj_set_style_opa(ew_boot_anim.logo_text_blue, LV_OPA_TRANSP, 0);
        
        /* 根据帧数随机显示不同层，模拟撕裂感 */
        uint8_t pattern = ew_boot_anim.glitch_count % 4;
        
        if (pattern == 0) {
            /* 红色层向左偏移 */
            lv_obj_set_pos(ew_boot_anim.logo_text_red, -3, (ew_boot_anim.glitch_count % 3) - 1);
            lv_obj_set_style_opa(ew_boot_anim.logo_text_red, LV_OPA_70, 0);
        } else if (pattern == 1) {
            /* 蓝色层向右偏移 */
            lv_obj_set_pos(ew_boot_anim.logo_text_blue, 3, -(ew_boot_anim.glitch_count % 3 - 1));
            lv_obj_set_style_opa(ew_boot_anim.logo_text_blue, LV_OPA_70, 0);
        } else if (pattern == 2) {
            /* 两层同时显示，增强撕裂感 */
            lv_obj_set_pos(ew_boot_anim.logo_text_red, -2, 0);
            lv_obj_set_pos(ew_boot_anim.logo_text_blue, 2, 1);
            lv_obj_set_style_opa(ew_boot_anim.logo_text_red, LV_OPA_60, 0);
            lv_obj_set_style_opa(ew_boot_anim.logo_text_blue, LV_OPA_60, 0);
        }
        /* pattern == 3 时所有层透明，只显示白色主文字 */
        
        ew_boot_anim.glitch_count++;
    } else {
        /* Glitch 结束，隐藏所有偏移层 */
        lv_obj_set_style_opa(ew_boot_anim.logo_text_red, LV_OPA_TRANSP, 0);
        lv_obj_set_style_opa(ew_boot_anim.logo_text_blue, LV_OPA_TRANSP, 0);

        /* 防止重复触发：删除定时器并仅启动一次进度条 */
        if (ew_boot_anim.glitch_timer) {
            lv_timer_del(ew_boot_anim.glitch_timer);
            ew_boot_anim.glitch_timer = NULL;
        }
        if (ew_boot_anim.phase < 5) {
            ew_boot_anim.phase = 5;

            /* 显示进度条 */
            lv_obj_set_style_opa(ew_boot_anim.progress_bar, LV_OPA_COVER, 0);

            /* 进度条填充动画 */
            lv_anim_t progress_anim;
            lv_anim_init(&progress_anim);
            lv_anim_set_var(&progress_anim, ew_boot_anim.progress_bar);
            lv_anim_set_values(&progress_anim, 0, 100);
            lv_anim_set_time(&progress_anim, PHASE5_PROGRESS_DURATION);
            lv_anim_set_path_cb(&progress_anim, lv_anim_path_ease_in_out);
            lv_anim_set_exec_cb(&progress_anim, progress_anim_cb);
            lv_anim_set_ready_cb(&progress_anim, zoomout_start_cb);
            lv_anim_start(&progress_anim);
        }
    }
}

static void progress_anim_cb(void *var, int32_t v)
{
    lv_bar_set_value((lv_obj_t *)var, v, LV_ANIM_OFF);
}

/*******************************************************************************
 * Phase 6: 放大淡出
 ******************************************************************************/

/* 放大淡出阶段启动回调 */
static void zoomout_start_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    start_zoomout_phase();
}

/* 按钮点击事件回调 */
static void enter_btn_click_cb(lv_event_t *e)
{
    /* 防止重复触发：立即禁用按钮 */
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_add_state(btn, LV_STATE_DISABLED);
    lv_obj_remove_event_cb(btn, enter_btn_click_cb); /* 移除事件回调 */
    
    ew_boot_anim.finished = true;
    edgewind_ui_on_enter_system();
}

static void start_zoomout_phase(void)
{
    /* 不再执行淡出动画，改为显示"进入系统"按钮 */

    /* ⚠️需求：加载完开机动画但还没有弹出“进入系统”按钮时，若需要断电重连则先触发自动连接上报 */
    edgewind_ui_on_before_enter_button();
    
    /* 隐藏进度条 */
    lv_anim_t hide_progress;
    lv_anim_init(&hide_progress);
    lv_anim_set_var(&hide_progress, ew_boot_anim.progress_bar);
    lv_anim_set_values(&hide_progress, 255, 0);
    lv_anim_set_time(&hide_progress, 300);
    lv_anim_set_exec_cb(&hide_progress, corner_opa_anim_cb);
    lv_anim_start(&hide_progress);
    
    /* 显示进入系统按钮 - 淡入动画 */
    lv_anim_t show_btn;
    lv_anim_init(&show_btn);
    lv_anim_set_var(&show_btn, ew_boot_anim.enter_btn);
    lv_anim_set_values(&show_btn, 0, 255);
    lv_anim_set_time(&show_btn, 500);
    lv_anim_set_delay(&show_btn, 200);
    lv_anim_set_path_cb(&show_btn, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&show_btn, corner_opa_anim_cb);
    lv_anim_start(&show_btn);
    
    /* 绑定按钮点击事件 */
    lv_obj_add_event_cb(ew_boot_anim.enter_btn, enter_btn_click_cb, LV_EVENT_CLICKED, NULL);
}

