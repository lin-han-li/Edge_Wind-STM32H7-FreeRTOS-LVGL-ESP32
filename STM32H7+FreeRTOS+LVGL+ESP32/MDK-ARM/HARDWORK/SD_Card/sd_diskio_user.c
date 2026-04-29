#include "sd_diskio_user.h"

#include "sd_diskio.h" /* 复用 bsp_driver_sd.h 等底层定义 */
#include "bsp_driver_sd.h"
#include "sdmmc.h"

#include "cmsis_os2.h"

#include <string.h>

/* 与 FatFs 保持一致 */
#ifndef SD_DEFAULT_BLOCK_SIZE
#define SD_DEFAULT_BLOCK_SIZE 512
#endif

/* 小读操作的“就绪等待”超时，避免 f_mount/f_stat 卡死 */
#ifndef SD_READY_TIMEOUT_MS
#define SD_READY_TIMEOUT_MS 1000U
#endif

/* 大读写的超时 */
#ifndef SD_RW_TIMEOUT_MS
#define SD_RW_TIMEOUT_MS 30000U
#endif

/* Disk status */
static volatile DSTATUS s_stat = STA_NOINIT;
/* 幂等性保护：避免短时间内反复 BSP_SD_Init */
static volatile uint8_t s_sd_inited = 0;
static uint32_t s_last_init_fail_tick = 0;

/* scratch buffer：解决 FatFs 可能传入非 4 字节对齐 buffer 的问题 */
__attribute__((aligned(32))) static uint8_t s_scratch[SD_DEFAULT_BLOCK_SIZE];

static inline uint32_t _align_down_32(uint32_t x) { return x & ~31u; }
static inline uint32_t _align_up_32(uint32_t x) { return (x + 31u) & ~31u; }

static void dcache_clean_any(const void *addr, uint32_t len)
{
#if defined(SCB_CleanDCache_by_Addr)
    uint32_t a = _align_down_32((uint32_t)addr);
    uint32_t end = _align_up_32(((uint32_t)addr) + len);
    SCB_CleanDCache_by_Addr((uint32_t *)a, (int32_t)(end - a));
#else
    (void)addr; (void)len;
#endif
}

static void dcache_invalidate_any(void *addr, uint32_t len)
{
#if defined(SCB_InvalidateDCache_by_Addr)
    uint32_t a = _align_down_32((uint32_t)addr);
    uint32_t end = _align_up_32(((uint32_t)addr) + len);
    SCB_InvalidateDCache_by_Addr((uint32_t *)a, (int32_t)(end - a));
#else
    (void)addr; (void)len;
#endif
}

static int sd_wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = osKernelGetTickCount();
    while ((osKernelGetTickCount() - t0) < timeout_ms) {
        if (BSP_SD_GetCardState() == SD_TRANSFER_OK) {
            return 0;
        }
        osDelay(1);
    }
    return -1;
}

static DSTATUS SDU_CheckStatus(BYTE lun)
{
    (void)lun;
    s_stat = STA_NOINIT;
    uint8_t state = BSP_SD_GetCardState();
    if (state == SD_TRANSFER_OK || state == SD_TRANSFER_BUSY) {
        s_stat &= ~STA_NOINIT;
    }
    return s_stat;
}

static DSTATUS SDU_initialize(BYTE lun)
{
    (void)lun;
    s_stat = STA_NOINIT;

    /* 若未检测到卡，直接返回 */
    if (BSP_SD_IsDetected() != SD_PRESENT) {
        s_sd_inited = 0;
        return s_stat;
    }

    /* 若最近初始化失败过，退避 1 秒，避免紧密重试卡死 UI */
    if (s_last_init_fail_tick && ((osKernelGetTickCount() - s_last_init_fail_tick) < 1000U)) {
        return s_stat;
    }

    /* 快速检查卡状态：若已在 TRANSFER 状态，标记已初始化并跳过 BSP_SD_Init */
    HAL_SD_CardStateTypeDef card_state = HAL_SD_GetCardState(&hsd1);
    if (s_sd_inited && (card_state == HAL_SD_CARD_TRANSFER || card_state == HAL_SD_CARD_SENDING))
    {
        s_stat = SDU_CheckStatus(lun);
        return s_stat;
    }

    /* 需要初始化：调用 BSP_SD_Init */
    uint8_t init_ret = BSP_SD_Init();
    if (init_ret != MSD_OK)
    {
        s_last_init_fail_tick = osKernelGetTickCount();
        s_sd_inited = 0;
        return s_stat;
    }

    s_sd_inited = 1;
    s_last_init_fail_tick = 0;

    /* 等待进入可传输状态（短超时，失败就快速返回） */
    (void)sd_wait_ready(SD_READY_TIMEOUT_MS);
    s_stat = SDU_CheckStatus(lun);
    return s_stat;
}

static DSTATUS SDU_status(BYTE lun)
{
    return SDU_CheckStatus(lun);
}

static DRESULT SDU_read(BYTE lun, BYTE *buff, DWORD sector, UINT count)
{
    (void)lun;
    if (!buff || count == 0) {
        return RES_PARERR;
    }
    if (SDU_initialize(0) & STA_NOINIT) {
        return RES_NOTRDY;
    }
    if (sd_wait_ready(SD_READY_TIMEOUT_MS) < 0) {
        return RES_NOTRDY;
    }

    /* 处理非 4 字节对齐的情况：逐扇区读取到 scratch 再 memcpy */
    if (((uint32_t)buff & 0x3u) != 0u) {
        for (UINT i = 0; i < count; ++i) {
            if (BSP_SD_ReadBlocks((uint32_t *)s_scratch, (uint32_t)(sector + i), 1, SD_RW_TIMEOUT_MS) != MSD_OK) {
                return RES_ERROR;
            }
            if (sd_wait_ready(SD_READY_TIMEOUT_MS) < 0) {
                return RES_ERROR;
            }
            dcache_invalidate_any(s_scratch, SD_DEFAULT_BLOCK_SIZE);
            memcpy(buff + i * SD_DEFAULT_BLOCK_SIZE, s_scratch, SD_DEFAULT_BLOCK_SIZE);
        }
        return RES_OK;
    }

    /* 4 字节对齐：直接读到目标缓冲区（失败则重试一次并触发重新 init） */
    for (int attempt = 1; attempt <= 2; ++attempt) {
        if (BSP_SD_ReadBlocks((uint32_t *)buff, (uint32_t)sector, count, SD_RW_TIMEOUT_MS) == MSD_OK) {
            break;
        }
        /* 读失败：标记需重新初始化后重试 */
        s_sd_inited = 0;
        (void)BSP_SD_Init();
        if (attempt == 2) {
            return RES_ERROR;
        }
        osDelay(5);
    }
    if (sd_wait_ready(SD_READY_TIMEOUT_MS) < 0) {
        s_sd_inited = 0;
        return RES_ERROR;
    }
    dcache_invalidate_any(buff, (uint32_t)count * SD_DEFAULT_BLOCK_SIZE);
    return RES_OK;
}

#if _USE_WRITE == 1
static DRESULT SDU_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
{
    (void)lun;
    if (!buff || count == 0) {
        return RES_PARERR;
    }
    if (SDU_initialize(0) & STA_NOINIT) {
        return RES_NOTRDY;
    }
    if (sd_wait_ready(SD_READY_TIMEOUT_MS) < 0) {
        return RES_NOTRDY;
    }

    if (((uint32_t)buff & 0x3u) != 0u) {
        for (UINT i = 0; i < count; ++i) {
            memcpy(s_scratch, buff + i * SD_DEFAULT_BLOCK_SIZE, SD_DEFAULT_BLOCK_SIZE);
            dcache_clean_any(s_scratch, SD_DEFAULT_BLOCK_SIZE);
            if (BSP_SD_WriteBlocks((uint32_t *)s_scratch, (uint32_t)(sector + i), 1, SD_RW_TIMEOUT_MS) != MSD_OK) {
                return RES_ERROR;
            }
            if (sd_wait_ready(SD_READY_TIMEOUT_MS) < 0) {
                return RES_ERROR;
            }
        }
        return RES_OK;
    }

    dcache_clean_any(buff, (uint32_t)count * SD_DEFAULT_BLOCK_SIZE);
    for (int attempt = 1; attempt <= 2; ++attempt) {
        if (BSP_SD_WriteBlocks((uint32_t *)buff, (uint32_t)sector, count, SD_RW_TIMEOUT_MS) == MSD_OK) {
            break;
        }
        /* 写失败：标记需重新初始化后重试 */
        s_sd_inited = 0;
        (void)BSP_SD_Init();
        if (attempt == 2) {
            return RES_ERROR;
        }
        osDelay(5);
    }
    if (sd_wait_ready(SD_READY_TIMEOUT_MS) < 0) {
        s_sd_inited = 0;
        return RES_ERROR;
    }
    return RES_OK;
}
#endif

#if _USE_IOCTL == 1
static DRESULT SDU_ioctl(BYTE lun, BYTE cmd, void *buff)
{
    (void)lun;
    if (s_stat & STA_NOINIT) {
        return RES_NOTRDY;
    }
    BSP_SD_CardInfo CardInfo;
    switch (cmd) {
    case CTRL_SYNC:
        /* 确保 SD 卡处于可传输状态，避免后续读写遇到 BUSY */
        if (sd_wait_ready(SD_READY_TIMEOUT_MS) < 0) {
            s_sd_inited = 0;
            return RES_ERROR;
        }
        return RES_OK;
    case GET_SECTOR_COUNT:
        if (!buff) return RES_PARERR;
        BSP_SD_GetCardInfo(&CardInfo);
        *(DWORD *)buff = (DWORD)CardInfo.LogBlockNbr;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (!buff) return RES_PARERR;
        BSP_SD_GetCardInfo(&CardInfo);
        *(WORD *)buff = (WORD)CardInfo.LogBlockSize;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (!buff) return RES_PARERR;
        BSP_SD_GetCardInfo(&CardInfo);
        *(DWORD *)buff = (DWORD)(CardInfo.LogBlockSize / SD_DEFAULT_BLOCK_SIZE);
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
#endif

const Diskio_drvTypeDef SD_User_Driver = {
    SDU_initialize,
    SDU_status,
    SDU_read,
#if _USE_WRITE == 1
    SDU_write,
#endif
#if _USE_IOCTL == 1
    SDU_ioctl,
#endif
};

