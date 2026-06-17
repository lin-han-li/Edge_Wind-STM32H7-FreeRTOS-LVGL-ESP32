#include "ad_acq_buffers.h"

#define AXI_SRAM_SECTION __attribute__((section(".axi_sram")))
#define DMA_ALIGN32 __attribute__((aligned(32)))

/* 4096 点双缓冲：放 AXI SRAM，避免 DTCM(128KB) 溢出导致启动卡死/HardFault */
float ADSA_B[4][AD_ACQ_POINTS] AXI_SRAM_SECTION DMA_ALIGN32;
float ADSA_B2[4][AD_ACQ_POINTS] AXI_SRAM_SECTION DMA_ALIGN32;
float ADSA_AUX4[AD_AUX4_COUNT] = {72.5f, 66.5f, 53.0f, 59.0f};
float ADSA_AUX4_2[AD_AUX4_COUNT] = {72.5f, 66.5f, 53.0f, 59.0f};
float ADS131A04_Buf[4] = {0};

volatile uint16_t AD7606_DebugRaw[8] = {0};
volatile float AD7606_DebugVolts[8] = {0};
volatile float AD7606_Aux4Values[AD_AUX4_COUNT] = {72.5f, 66.5f, 53.0f, 59.0f};
volatile uint8_t AD7606_Aux4ValidMask = 0U;
volatile uint8_t AD7606_DebugBusy = 0;
volatile uint8_t AD7606_DebugDb7 = 0;

uint8_t ADSA_AUX4_valid_mask = 0U;
uint8_t ADSA_AUX4_2_valid_mask = 0U;

int ADS131A04_flag = 0;
int ADS131A04_flag2 = 2;
int number = 0;
int number2 = 0;
