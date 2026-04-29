#ifndef _SPI_AD7606_H
#define _SPI_AD7606_H

#include "main.h"
#include "stm32h7xx_hal.h"

/* ==========================================
 * AD7606 电压换算与量程配置
 * ========================================== */

/* AD7606 内部基准电压通常为 2.5V */
#ifndef AD7606_VREF_VOLTS
#define AD7606_VREF_VOLTS (2.5f)
#endif

/*
 * 模拟前端增益（Vin_adc = Vin_in * AD7606_FRONTEND_GAIN）
 * - 大多数情况下为 1.0 (无外部运放衰减)
 * - 如果你的板子前面有电阻分压（例如 1/2 分压），则填 0.5
 */
#ifndef AD7606_FRONTEND_GAIN
#define AD7606_FRONTEND_GAIN (1.0f)
#endif

/* 默认过采样模式（0=无过采样，1=2x，2=4x，3=8x，4=16x，5=32x，6=64x） */
#ifndef AD7606_OS_MODE
#define AD7606_OS_MODE (3u)
#endif

/* * 获取满量程电压范围
 * AD7606 的 Range 引脚决定输入范围是 ±5V 还是 ±10V。
 * 当设置为 ±10V 范围时，相当于内部基准的 4 倍 (4 * 2.5V = 10V)。
 * 此函数用于将 ADC 的原始值 (Raw Code) 换算为实际电压。
 */
static inline float AD7606_GetFullScaleVolts(void)
{
    /* 假设硬件 Range 引脚拉高，固定为 ±10V 量程 */
    return (4.0f * AD7606_VREF_VOLTS);
}

/* ==========================================
 * GPIO 高速控制宏定义 (直接操作寄存器)
 * 注意：STM32 的 BSRR (Bit Set/Reset Register) 用于原子操作 GPIO。
 * - 低 16 位 (0-15) 置 1：引脚输出 高电平 (Set)
 * - 高 16 位 (16-31) 置 1：引脚输出 低电平 (Reset)
 * 使用寄存器操作比 HAL_GPIO_WritePin 快得多，适合软件模拟时序。
 * ========================================== */

/* CS (Chip Select) 片选信号 */
#define AD7606_CS_H        AD7606_CS_GPIO_Port->BSRR = AD7606_CS_Pin          /* CS 置高 (取消选中) */
#define AD7606_CS_L        AD7606_CS_GPIO_Port->BSRR = AD7606_CS_Pin << 16    /* CS 置低 (选中芯片) */

/* RESET 复位信号 (AD7606 为高电平复位) */
#define AD7606_REST_H      AD7606_REST_GPIO_Port->BSRR = AD7606_REST_Pin      /* 复位 置高 (开始复位) */
#define AD7606_REST_L      AD7606_REST_GPIO_Port->BSRR = AD7606_REST_Pin << 16 /* 复位 置低 (结束复位) */

/* CONVST (Conversion Start) 转换启动信号 (上升沿触发) */
#define AD7606_CONVEST_A_H AD7606_CONVEST_A_GPIO_Port->BSRR = AD7606_CONVEST_A_Pin /* A组 启动 */
#define AD7606_CONVEST_A_L AD7606_CONVEST_A_GPIO_Port->BSRR = AD7606_CONVEST_A_Pin << 16
#define AD7606_CONVEST_B_H AD7606_CONVEST_B_GPIO_Port->BSRR = AD7606_CONVEST_B_Pin /* B组 启动 */
#define AD7606_CONVEST_B_L AD7606_CONVEST_B_GPIO_Port->BSRR = AD7606_CONVEST_B_Pin << 16

/* Oversampling (过采样) 模式选择引脚 OS [2:0] */
#define AD7606_OS0_H       AD7606_OS0_GPIO_Port->BSRR = AD7606_OS0_Pin
#define AD7606_OS0_L       AD7606_OS0_GPIO_Port->BSRR = AD7606_OS0_Pin << 16
#define AD7606_OS1_H       AD7606_OS1_GPIO_Port->BSRR = AD7606_OS1_Pin
#define AD7606_OS1_L       AD7606_OS1_GPIO_Port->BSRR = AD7606_OS1_Pin << 16
#define AD7606_OS2_H       AD7606_OS2_GPIO_Port->BSRR = AD7606_OS2_Pin
#define AD7606_OS2_L       AD7606_OS2_GPIO_Port->BSRR = AD7606_OS2_Pin << 16

/* SCLK (Serial Clock) 串行时钟信号 (软件模拟 SPI 时钟) */
#define AD7606_SCLK_H      AD7606_SCLK_GPIO_Port->BSRR = AD7606_SCLK_Pin
#define AD7606_SCLK_L      AD7606_SCLK_GPIO_Port->BSRR = AD7606_SCLK_Pin << 16

/* ==========================================
 * GPIO 输入读取宏定义
 * ========================================== */

/* * 读取数据引脚 DB7 (DOUTA)
 * 在串行模式下，DB7 用作数据输出线 (MISO)。
 * 读取 IDR (Input Data Register) 寄存器判断高低电平。
 */
#define READ_AD7606_DB7       ((AD7606_DB7_GPIO_Port->IDR & AD7606_DB7_Pin) || 0)

/* * 读取 BUSY 状态引脚
 * BUSY 为高电平时表示正在转换，低电平时表示转换完成，可以读取数据。
 */
#define READ_AD7606_BUSY      ((AD7606_BUSY_GPIO_Port->IDR & AD7606_BUSY_Pin) || 0)


/* ==========================================
 * 占位符注释 (原文件保留的分类标签)
 * ========================================== */
/* SPI总线的SCK、MOSI、MISO 在 bsp_spi_bus.c中配置  */
/* CSN片选 */
/* RESET */
/* RANGE */
/* CONVST */    
/* BUSY */
/* 片选 */
/* 设置量程 */
/* 复位引脚 */
/* 起始信号 */

/* ==========================================
 * 函数声明
 * ========================================== */

/* 初始化 AD7606 (GPIO配置, 复位, 默认状态) */
void AD7606_Init(void);

/* 配置 GPIO 引脚模式 (通常在 Init 中调用或由 CubeMX 生成) */
void AD7606_ConfigGPIO(void);

/* 执行硬件复位时序 (Reset High -> Low) */
void AD7606_Reset(void);    

/* 产生 CONVST 上升沿，启动 ADC 转换 */
void AD7606_StartConv(void);

/* 配置硬件 SPI (如果打算使用硬件 SPI 外设而不是软件模拟) */
void AD7606_CfgSpiHard(void);

/* 读取 ADC 数据 (核心读取函数)
 * DB_data: 存储读取结果的数组指针 (u16数组) 
 */
void AD7606_read_data(uint16_t * DB_data); 

/* 扫描函数 (包含 检测BUSY -> 读取 -> 启动下一次转换 的完整流程) */
void AD7606_Scan(uint16_t * DB_data);

/* 设置过采样倍率
 * _ucMode: 0=无过采样, 1=2x, 2=4x ... 6=64x 
 */
void AD7606_SetOS(uint8_t _ucMode);

/* 读取8通道原始数据（串行模式，DOUTA） */
void AD7606_ReadRaw8(uint16_t raw[8]);

/* 运行期统计 */
void AD7606_GetStats(uint32_t *frames, uint32_t *miss);

/* ==========================================
 * 原始码值 -> 电压换算（单位：V）
 * ========================================== */

/* AD7606 16bit 两补码原始值 -> 有符号值 */
int16_t AD7606_RawToS16(uint16_t raw);

/* AD7606 原始值 -> 实际输入电压（单位：V） */
double AD7606_RawToVolts(uint16_t raw);

/* float 版本（减少双精度开销） */
float AD7606_RawToVoltsF(uint16_t raw);

/* 统计计数器（由采样调度层更新） */
extern volatile uint32_t g_ad7606_frames;
extern volatile uint32_t g_ad7606_miss;

/* ==========================================
 * 硬件连线与配置说明
 * ========================================== */
/*
连线定义（以 `Core\\Inc\\main.h` 为准，STM32 <-> AD7606）：

// 控制线（输出）
RESET        : PB7  -> RESET
CS           : PB6  -> CS
CONVST_A     : PB3  -> CONVST A
CONVST_B     : PB9  -> CONVST B
OS0          : PB8  -> OS0
OS1          : PB4  -> OS1
OS2          : PE3  -> OS2
SCLK(软件SPI) : PE5  -> SCLK

// 状态/数据（输入）
BUSY         : PC3  -> BUSY
DOUTA(DB7)   : PA0  -> DOUTA（串行数据输出，作为 MISO 读取）

// 硬件固定配置
1) 串行模式：SER = 1（接 3.3V），DB15 = 0（接 GND）
2) 量程固定：硬件已固定为 ±10V（RANGE 未由 MCU 控制/已移除相关引脚）

// 说明
- 本工程当前未使用：STBY、FRSTDATA、RD、DOUTB(DB8) 等引脚（若你的硬件接了也不影响本驱动）。
*/

#endif
