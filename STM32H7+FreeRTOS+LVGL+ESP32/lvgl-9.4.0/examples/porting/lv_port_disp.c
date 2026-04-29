/**
 * @file lv_port_disp.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 * INCLUDES
 *********************/
#include "lv_port_disp.h"
#include <stdbool.h>
#include "lcd_rgb.h"
#include "led.h"
#include "usart.h"
#include "stm32h7xx_hal.h" /* 必须包含此头文件以使用 SCB_CleanDCache */

/*********************
 * DEFINES
 *********************/
/* 显存计算宏，沿用你原项目的定义 */
/* 请确保 BytesPerPixel_0 在 lcd_rgb.h 或此处已定义，通常 RGB565 为 2 */
#ifndef BytesPerPixel_0
#define BytesPerPixel_0 2
#endif

#define LVGL_MemoryAdd	( LCD_MemoryAdd + LCD_Width*LCD_Height*BytesPerPixel_0 )

/**********************
 * TYPEDEFS
 **********************/

/**********************
 * STATIC PROTOTYPES
 **********************/
static void disp_init(void);
static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map);

/**********************
 * STATIC VARIABLES
 **********************/

/**********************
 * MACROS
 **********************/

/**********************
 * GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /*------------------------------------
     * Create a display and set a flush_cb
     * -----------------------------------*/
    /* 创建显示对象，传入宽高 */
    lv_display_t * disp = lv_display_create(LCD_Width, LCD_Height);
    
    /* 设置刷新回调函数 */
    lv_display_set_flush_cb(disp, disp_flush);

    /* 设置显示的颜色格式为 RGB565（必须在设置缓冲区之前） */
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    /*------------------------------------
     * Buffer Configuration (FULL mode with double buffering)
     * -----------------------------------*/
    /* * 改用 FULL 模式：每次渲染整个屏幕
     * 这对于图片渲染更可靠
     */
    
    /* 计算两个显存区的地址 */
    void * buf_1 = (void *)(LVGL_MemoryAdd);
    void * buf_2 = (void *)(LVGL_MemoryAdd + LCD_Width * LCD_Height * BytesPerPixel_0);
    
    /* 设置缓冲区：Buffer大小为一整屏的字节数 */
    uint32_t buf_size = LCD_Width * LCD_Height * BytesPerPixel_0;
    
    lv_display_set_buffers(disp, buf_1, buf_2, buf_size, LV_DISPLAY_RENDER_MODE_FULL);
    
    /* 如果你需要 LVGL 自动处理旋转或特定格式，可以在这里设置 */
    /* lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0); */
}

/**********************
 * STATIC FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
static void disp_init(void)
{
    /* 你的底层初始化代码，如果已经在 main 中调用过 LCD_Init，这里可以留空 */
    /* LCD_Init(); */
}

volatile bool disp_flush_enabled = true;

/* Enable updating the screen (the flushing process) when disp_flush() is called by LVGL */
void disp_enable_update(void)
{
    disp_flush_enabled = true;
}

/* Disable updating the screen (the flushing process) when disp_flush() is called by LVGL */
void disp_disable_update(void)
{
    disp_flush_enabled = false;
}

/**
  * @brief  Line Event callback.
  * @note   这是 STM32 HAL 库的 LTDC 行中断回调，用于在垂直消隐期更新显存地址，防止画面撕裂
  */
void HAL_LTDC_LineEvenCallback(LTDC_HandleTypeDef *hltdc)
{   
	// 重新载入参数，新显存地址生效，此时显示才会更新
	// 每次进入中断才会更新显示，这样能有效避免撕裂现象	
	__HAL_LTDC_RELOAD_CONFIG(hltdc);						
	HAL_LTDC_ProgramLineEvent(hltdc, 0);
    // LED1_Toggle; // 调试用
}

/*Flush the content of the internal buffer the specific area on the display.*/
static void disp_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    if(disp_flush_enabled) {
        /* * ！！！核心修复！！！
         * STM32H7 具有 D-Cache，CPU 写入的数据可能还停留在 Cache 中，
         * 而 LTDC 是直接从 SDRAM 读取数据的。
         * 如果不清理 Cache，LTDC 可能会读到旧数据（导致屏幕内容缺失或花屏）。
         * 这里强制将 D-Cache 中的内容写入到 RAM 中。
         */
        SCB_CleanDCache();

        /* * 切换 LTDC 的显存读取地址
         * px_map 就是当前 LVGL 绘制好的 buffer 地址 (buf_1 或 buf_2)
         */
		LTDC_Layer1->CFBAR = (uint32_t)px_map;
    }

    /*IMPORTANT!!!
     *Inform the graphics library that you are ready with the flushing*/
    lv_display_flush_ready(disp);
}

#else /*Enable this file at the top*/

/*This dummy typedef exists purely to silence -Wpedantic.*/
typedef int keep_pedantic_happy;
#endif
