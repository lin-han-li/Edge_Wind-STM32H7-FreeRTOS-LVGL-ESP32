/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
#include "fatfs.h"

uint8_t retSD;    /* Return value for SD */
char SDPath[4];   /* SD logical drive path */
FATFS SDFatFS;    /* File system object for SD logical drive */
FIL SDFile;       /* File object for SD */

/* USER CODE BEGIN Variables */
uint8_t retQSPI;  /* Return value for QSPI */
char QSPIPath[4]; /* QSPI logical drive path */
FATFS QSPIFatFS;  /* File system object for QSPI logical drive */
FIL QSPIFile;     /* File object for QSPI */

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the SD driver ###########################*/
  retSD = FATFS_LinkDriver(&SD_Driver, SDPath);

  /* USER CODE BEGIN Init */
  /* 仅链接驱动（不依赖 QSPI 外设已初始化）。真正 mount/mkfs 请在 QSPI 初始化完成后调用 QSPIFS_MountOrMkfs() */
  retQSPI = FATFS_LinkDriverEx(&QSPI_Driver, QSPIPath, 1);
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  /* 对齐 CubeMX 默认/PN_TI_LVGL_SD：不依赖 RTC（避免 hrtc 未声明导致编译失败）。
   * 若你后续需要文件时间戳，再在 fatfs.c 顶部引入 rtc.h 并恢复这里的实现。
   */
  return 0;
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */

/* fatfs.c 可能不会默认包含 <stdio.h>，这里在 USER CODE 区域内前置声明，避免 C99 下隐式声明报错 */
extern int printf(const char * format, ...);

static FRESULT qspi_create_marker(const char * path)
{
  FIL fil;
  FRESULT res = f_open(&fil, path, FA_WRITE | FA_CREATE_ALWAYS);
  if (res == FR_OK) {
    (void)f_sync(&fil);
    (void)f_close(&fil);
  }
  return res;
}

FRESULT QSPIFS_MountOrMkfs(void)
{
  FRESULT res = f_mount(&QSPIFatFS, (TCHAR const *)QSPIPath, 1);
  printf("[QSPI_FS] mount %s -> %d\r\n", QSPIPath, (int)res);
  if (res == FR_OK) {
    return FR_OK;
  }

  if (res != FR_NO_FILESYSTEM) {
    return res;
  }

  printf("[QSPI_FS] no filesystem, mkfs...\r\n");
  static uint8_t mkfs_work[4096];
  /* 注意：本工程 FatFs 版本的 f_mkfs 使用旧签名：
   *   FRESULT f_mkfs(const TCHAR* path, BYTE opt, DWORD au, void* work, UINT len)
   */
  /* 32MB(65536*512B) 介质簇数不足以满足 FAT32 最小簇数要求，强制 FAT32 会导致 FR_MKFS_ABORTED。
   * 这里使用 FAT16，并使用 FM_SFD（Super-Floppy）避免创建 MBR 分区表，适合裸 Flash。
   */
  const BYTE opt = (BYTE)(FM_FAT | FM_SFD);
  const DWORD au = 0; /* 0: 由 FatFs 自动选择 */

  res = f_mkfs((TCHAR const *)QSPIPath, opt, au, mkfs_work, sizeof(mkfs_work));
  printf("[QSPI_FS] mkfs -> %d\r\n", (int)res);
  if (res != FR_OK) {
    return res;
  }

  res = f_mount(&QSPIFatFS, (TCHAR const *)QSPIPath, 1);
  printf("[QSPI_FS] remount -> %d\r\n", (int)res);
  if (res != FR_OK) {
    return res;
  }

  (void)qspi_create_marker("1:/qspi_fmt.ok");
  return FR_OK;
}

/* USER CODE END Application */
