/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    qspi_diskio.c
  * @brief   QSPI Disk I/O driver for W25Q256
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "qspi_diskio.h"
#include "qspi_w25q256.h"
#include "../../MDK-ARM/HARDWORK/GUI-Guider_Runtime/gui_resource_map.h"

#include <string.h>
#include <stdio.h>

/* Private defines -----------------------------------------------------------*/
#define QSPI_SECTOR_SIZE          512U
#define QSPI_ERASE_BLOCK_SIZE     4096U
#define QSPI_SECTORS_PER_BLOCK    (QSPI_ERASE_BLOCK_SIZE / QSPI_SECTOR_SIZE)

/* Private variables ---------------------------------------------------------*/
static volatile DSTATUS Stat = STA_NOINIT;
static uint8_t qspi_initialized = 0;
static uint8_t qspi_block_buf[QSPI_ERASE_BLOCK_SIZE];

/* Private function prototypes -----------------------------------------------*/
static int8_t qspi_ensure_ready(void);

/* Diskio driver -------------------------------------------------------------*/
DSTATUS QSPI_initialize(BYTE lun);
DSTATUS QSPI_status(BYTE lun);
DRESULT QSPI_read(BYTE lun, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
DRESULT QSPI_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
DRESULT QSPI_ioctl(BYTE lun, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

const Diskio_drvTypeDef QSPI_Driver =
{
  QSPI_initialize,
  QSPI_status,
  QSPI_read,
#if _USE_WRITE == 1
  QSPI_write,
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  QSPI_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/
static int8_t qspi_ensure_ready(void)
{
  if (qspi_initialized) {
    return QSPI_W25Qxx_OK;
  }

  if (QSPI_W25Qxx_Init() != QSPI_W25Qxx_OK) {
    printf("[QSPI_DISK] QSPI_W25Qxx_Init failed\r\n");
    return W25Qxx_ERROR_INIT;
  }

  qspi_initialized = 1;
  /* 注意：FatFs 的元数据读写要求“读到刚写入的数据”。
   * 在 M7 + DCache 场景下，直接用 QSPI memory-mapped memcpy 读取可能命中旧 cache，
   * 导致 mkdir 后马上 open 报 FR_NO_PATH 等一致性问题。
   * 因此 diskio 层默认不进入 memory-mapped，统一走间接读写以保证一致性。
   */
  return QSPI_W25Qxx_OK;
}

/**
  * @brief  Initializes a Drive
  * @param  lun : not used
  * @retval DSTATUS: Operation status
  */
DSTATUS QSPI_initialize(BYTE lun)
{
  (void)lun;

  if (qspi_ensure_ready() == QSPI_W25Qxx_OK) {
    Stat &= ~STA_NOINIT;
  }
  else {
    Stat = STA_NOINIT;
  }

  return Stat;
}

/**
  * @brief  Gets Disk Status
  * @param  lun : not used
  * @retval DSTATUS: Operation status
  */
DSTATUS QSPI_status(BYTE lun)
{
  (void)lun;
  if (qspi_ensure_ready() == QSPI_W25Qxx_OK) {
    Stat &= ~STA_NOINIT;
  }
  else {
    Stat = STA_NOINIT;
  }
  return Stat;
}

/**
  * @brief  Reads sector(s)
  * @param  lun : not used
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read
  * @retval DRESULT: Operation result
  */
DRESULT QSPI_read(BYTE lun, BYTE *buff, DWORD sector, UINT count)
{
  (void)lun;

  if (count == 0U || buff == NULL) {
    return RES_PARERR;
  }

  if (qspi_ensure_ready() != QSPI_W25Qxx_OK) {
    return RES_NOTRDY;
  }

  const uint32_t addr = QSPI_FATFS_OFFSET + sector * QSPI_SECTOR_SIZE;
  const uint32_t size = count * QSPI_SECTOR_SIZE;

  if ((addr + size) > (QSPI_FATFS_OFFSET + QSPI_FATFS_SIZE)) {
    return RES_PARERR;
  }

  (void)QSPI_W25Qxx_ExitMemoryMapped();
  if (QSPI_W25Qxx_ReadBuffer_Slow(buff, addr, size) != QSPI_W25Qxx_OK) {
    (void)QSPI_W25Qxx_EnterMemoryMapped();
    return RES_ERROR;
  }

  (void)QSPI_W25Qxx_EnterMemoryMapped();
  return RES_OK;
}

#if _USE_WRITE == 1
/**
  * @brief  Writes sector(s)
  * @param  lun : not used
  * @param  *buff: Data buffer to write
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write
  * @retval DRESULT: Operation result
  */
DRESULT QSPI_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
{
  (void)lun;

  if (count == 0U || buff == NULL) {
    return RES_PARERR;
  }

  if (qspi_ensure_ready() != QSPI_W25Qxx_OK) {
    return RES_NOTRDY;
  }

  (void)QSPI_W25Qxx_ExitMemoryMapped();

  uint32_t addr = QSPI_FATFS_OFFSET + sector * QSPI_SECTOR_SIZE;
  const uint32_t total_size = count * QSPI_SECTOR_SIZE;

  if ((addr + total_size) > (QSPI_FATFS_OFFSET + QSPI_FATFS_SIZE)) {
    return RES_PARERR;
  }

  while (count > 0U) {
    const uint32_t block_addr = (addr / QSPI_ERASE_BLOCK_SIZE) * QSPI_ERASE_BLOCK_SIZE;
    const uint32_t offset_in_block = addr - block_addr;
    const uint32_t sectors_in_block = (uint32_t)((QSPI_ERASE_BLOCK_SIZE - offset_in_block) / QSPI_SECTOR_SIZE);
    const uint32_t sectors_to_write = (count < sectors_in_block) ? count : sectors_in_block;
    const uint32_t bytes_to_write = sectors_to_write * QSPI_SECTOR_SIZE;

    if (QSPI_W25Qxx_ReadBuffer_Slow(qspi_block_buf, block_addr, QSPI_ERASE_BLOCK_SIZE) != QSPI_W25Qxx_OK) {
      printf("[QSPI_DISK] read block fail @0x%08lX\r\n", (unsigned long)block_addr);
      (void)QSPI_W25Qxx_EnterMemoryMapped();
      return RES_ERROR;
    }

    memcpy(&qspi_block_buf[offset_in_block], buff, bytes_to_write);

    if (QSPI_W25Qxx_SectorErase(block_addr) != QSPI_W25Qxx_OK) {
      printf("[QSPI_DISK] erase 4K fail @0x%08lX\r\n", (unsigned long)block_addr);
      (void)QSPI_W25Qxx_EnterMemoryMapped();
      return RES_ERROR;
    }

    if (QSPI_W25Qxx_WriteBuffer_Slow(qspi_block_buf, block_addr, QSPI_ERASE_BLOCK_SIZE) != QSPI_W25Qxx_OK) {
      printf("[QSPI_DISK] write 4K fail @0x%08lX\r\n", (unsigned long)block_addr);
      (void)QSPI_W25Qxx_EnterMemoryMapped();
      return RES_ERROR;
    }

    buff += bytes_to_write;
    addr += bytes_to_write;
    count -= (UINT)sectors_to_write;
  }

  (void)QSPI_W25Qxx_EnterMemoryMapped();
  return RES_OK;
}
#endif /* _USE_WRITE == 1 */

#if _USE_IOCTL == 1
/**
  * @brief  IO control
  * @param  lun : not used
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
DRESULT QSPI_ioctl(BYTE lun, BYTE cmd, void *buff)
{
  (void)lun;

  /* FatFs 允许 CTRL_SYNC 的 buff 为 NULL。其它命令再检查 buff。 */
  static uint8_t ioctl_log_cnt = 0;
  if (ioctl_log_cnt < 8) {
    printf("[QSPI_DISK] ioctl cmd=%u buff=%p\r\n", (unsigned)cmd, buff);
    ioctl_log_cnt++;
  }

  switch (cmd) {
    case CTRL_SYNC:
      return RES_OK;

    case GET_SECTOR_COUNT:
      if (buff == NULL) return RES_PARERR;
      *(DWORD *)buff = (DWORD)(QSPI_FATFS_SIZE / QSPI_SECTOR_SIZE);
      if (ioctl_log_cnt < 12) {
        printf("[QSPI_DISK] GET_SECTOR_COUNT=%lu\r\n", (unsigned long)*(DWORD *)buff);
        ioctl_log_cnt++;
      }
      return RES_OK;

    case GET_SECTOR_SIZE:
      if (buff == NULL) return RES_PARERR;
      *(WORD *)buff = (WORD)QSPI_SECTOR_SIZE;
      return RES_OK;

    case GET_BLOCK_SIZE:
      if (buff == NULL) return RES_PARERR;
      *(DWORD *)buff = (DWORD)QSPI_SECTORS_PER_BLOCK;
      if (ioctl_log_cnt < 12) {
        printf("[QSPI_DISK] GET_BLOCK_SIZE=%lu\r\n", (unsigned long)*(DWORD *)buff);
        ioctl_log_cnt++;
      }
      return RES_OK;

    default:
      return RES_PARERR;
  }
}
#endif /* _USE_IOCTL == 1 */
