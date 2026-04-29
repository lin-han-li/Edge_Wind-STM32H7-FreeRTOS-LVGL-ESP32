#include "ad_acq_buffers.h"

#define AXI_SRAM_SECTION __attribute__((section(".axi_sram")))
#define DMA_ALIGN32 __attribute__((aligned(32)))

/* 4096 点双缓冲：放 AXI SRAM，避免 DTCM(128KB) 溢出导致启动卡死/HardFault */
float ADSA_B[4][AD_ACQ_POINTS] AXI_SRAM_SECTION DMA_ALIGN32;
float ADSA_B2[4][AD_ACQ_POINTS] AXI_SRAM_SECTION DMA_ALIGN32;
float ADS131A04_Buf[4] = {0};

int ADS131A04_flag = 0;
int ADS131A04_flag2 = 2;
int number = 0;
int number2 = 0;

