#include "gui_assets_sync.h"
#include "fatfs.h"
#include "gui_resource_map.h"
#include "qspi_w25q256.h"
#include "draw/lv_image_dsc.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bsp_driver_sd.h"
#include "sdmmc.h"
#include "diskio.h"

#include "FreeRTOS.h"
#include "task.h"

typedef struct {
    gui_res_id_t id;
    const char * subdir;
    const char * name;
} asset_entry_t;

static const asset_entry_t k_gui_assets[] = {
    {GUI_RES_ICON_01, "gui", "icon_01_rtmon_RGB565A8_100x100.bin"},
    {GUI_RES_ICON_02, "gui", "icon_02_fault_RGB565A8_100x100.bin"},
    {GUI_RES_ICON_03, "gui", "icon_03_analysis_RGB565A8_100x100.bin"},
    {GUI_RES_ICON_04, "gui", "icon_04_history_RGB565A8_100x100.bin"},
    {GUI_RES_ICON_05, "gui", "icon_05_log_RGB565A8_100x100.bin"},
    {GUI_RES_ICON_06, "gui", "icon_06_alarm_RGB565A8_100x100.bin"},
    {GUI_RES_ICON_07, "gui", "icon_07_param_RGB565A8_100x100.bin"},
    {GUI_RES_ICON_08, "gui", "icon_08_net_RGB565A8_100x100.bin"},
    {GUI_RES_ICON_09, "gui", "icon_09_server_RGB565A8_100x100.bin"},
    {GUI_RES_ICON_10, "gui", "icon_10_diag_RGB565A8_100x100.bin"},
    {GUI_RES_ICON_11, "gui", "icon_11_device_RGB565A8_100x100.bin"},
    {GUI_RES_ICON_12, "gui", "icon_12_user_RGB565A8_100x100.bin"},
    {GUI_RES_ICON_13, "gui", "icon_13_fwup_RGB565A8_100x100.bin"},
    {GUI_RES_ICON_14, "gui", "icon_14_about_RGB565A8_100x100.bin"},
    {GUI_RES_FONT_12, "fonts", "SourceHanSerifSC_Regular_12.bin"},
    {GUI_RES_FONT_14, "fonts", "SourceHanSerifSC_Regular_14.bin"},
    {GUI_RES_FONT_16, "fonts", "SourceHanSerifSC_Regular_16.bin"},
    {GUI_RES_FONT_20, "fonts", "SourceHanSerifSC_Regular_20.bin"},
    {GUI_RES_FONT_30, "fonts", "SourceHanSerifSC_Regular_30.bin"},
    {GUI_RES_PINYIN_DICT, "pinyin", "pinyin_dict.bin"},
};

static bool gui_res_is_font(gui_res_id_t id)
{
    return (id == GUI_RES_FONT_12 ||
            id == GUI_RES_FONT_14 ||
            id == GUI_RES_FONT_16 ||
            id == GUI_RES_FONT_20 ||
            id == GUI_RES_FONT_30);
}

static bool gui_res_is_optional(gui_res_id_t id)
{
    return (id == GUI_RES_PINYIN_DICT);
}

static FRESULT file_size(const char * path, FSIZE_t * size_out)
{
    FILINFO info;
    FRESULT res = f_stat(path, &info);
    if (res != FR_OK) {
        return res;
    }
    if (size_out) {
        *size_out = info.fsize;
    }
    return FR_OK;
}

static void log_asset_size(const char * tag, const char * path, FSIZE_t size)
{
    printf("[QSPI_FS] %s size: %s = %lu\r\n", tag, path, (unsigned long)size);
}

static volatile uint8_t g_qspi_assets_ready = 0;
static uint32_t g_qspi_res_sizes[GUI_RES_COUNT];

/* SD 同步进度标志（供 UI 配置界面检测，避免在 QSPI 同步期间访问 SD 造成竞争） */
volatile uint8_t g_qspi_sd_sync_in_progress = 0;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t checksum;
    uint32_t sizes[GUI_RES_COUNT];
} gui_res_header_t;

static uint32_t res_header_checksum(const gui_res_header_t * hdr)
{
    uint32_t sum = hdr->version ^ hdr->count;
    for (uint32_t i = 0; i < hdr->count && i < GUI_RES_COUNT; ++i) {
        sum = (sum << 5) - sum + hdr->sizes[i];
    }
    return sum;
}

static bool qspi_res_header_read(gui_res_header_t * hdr)
{
    if (!hdr) {
        return false;
    }
    (void)QSPI_W25Qxx_ExitMemoryMapped();
    if (QSPI_W25Qxx_ReadBuffer_Slow((uint8_t *)hdr, RES_MAGIC_OFFSET, sizeof(*hdr)) != QSPI_W25Qxx_OK) {
        printf("[QSPI_FS] header read fail @0x%08lX\r\n", (unsigned long)RES_MAGIC_OFFSET);
        return false;
    }
    printf("[QSPI_FS] header read @0x%08lX: magic=0x%08lX ver=0x%08lX count=%lu\r\n",
           (unsigned long)RES_MAGIC_OFFSET,
           (unsigned long)hdr->magic,
           (unsigned long)hdr->version,
           (unsigned long)hdr->count);
    return true;
}

static bool qspi_res_header_valid(const gui_res_header_t * hdr)
{
    if (!hdr) {
        return false;
    }
    if (hdr->magic != RES_MAGIC_VALUE || hdr->version != RES_MAGIC_VERSION) {
        return false;
    }
    if (hdr->count != GUI_RES_COUNT) {
        return false;
    }
    if (hdr->checksum != res_header_checksum(hdr)) {
        return false;
    }
    for (uint32_t i = 0; i < GUI_RES_COUNT; ++i) {
        if (gui_res_is_optional((gui_res_id_t)i)) {
            continue;
        }
        if (hdr->sizes[i] == 0) {
            return false;
        }
        if (hdr->sizes[i] > gui_res_max_size((gui_res_id_t)i)) {
            return false;
        }
    }
    if (hdr->sizes[GUI_RES_PINYIN_DICT] > gui_res_max_size(GUI_RES_PINYIN_DICT)) {
        return false;
    }
    return true;
}

static bool qspi_res_header_write(const gui_res_header_t * hdr)
{
    if (!hdr) {
        return false;
    }
    (void)QSPI_W25Qxx_ExitMemoryMapped();
    if (QSPI_W25Qxx_SectorErase(RES_MAGIC_OFFSET) != QSPI_W25Qxx_OK) {
        return false;
    }
    if (QSPI_W25Qxx_WriteBuffer_Slow((uint8_t *)hdr, RES_MAGIC_OFFSET, sizeof(*hdr)) != QSPI_W25Qxx_OK) {
        return false;
    }
    return true;
}

bool GUI_Assets_QSPIReady(void)
{
    return g_qspi_assets_ready != 0;
}

bool GUI_Assets_GetResSize(gui_res_id_t id, uint32_t * size_out)
{
    if (!size_out || id >= GUI_RES_COUNT) {
        return false;
    }
    if (!g_qspi_assets_ready) {
        return false;
    }
    *size_out = g_qspi_res_sizes[id];
    return (*size_out > 0);
}

static bool qspi_assets_complete(void)
{
    gui_res_header_t hdr;
    if (!qspi_res_header_read(&hdr)) {
        return false;
    }
    if (!qspi_res_header_valid(&hdr)) {
        return false;
    }
    for (uint32_t i = 0; i < GUI_RES_COUNT; ++i) {
        g_qspi_res_sizes[i] = hdr.sizes[i];
    }

    /* 验证字体资源区是否完整（基于 header 的 sizes） */
    for (gui_res_id_t id = GUI_RES_FONT_12; id <= GUI_RES_FONT_30; ++id) {
        if (hdr.sizes[id] == 0 || hdr.sizes[id] > gui_res_max_size(id)) {
            printf("[QSPI_FS] font res invalid: id=%u size=%lu\r\n",
                   (unsigned)id, (unsigned long)hdr.sizes[id]);
            return false;
        }
    }

    if (hdr.sizes[GUI_RES_PINYIN_DICT] == 0) {
        printf("[QSPI_FS] pinyin dict missing (optional)\r\n");
    } else if (hdr.sizes[GUI_RES_PINYIN_DICT] > gui_res_max_size(GUI_RES_PINYIN_DICT)) {
        printf("[QSPI_FS] pinyin dict too large: size=%lu max=%lu\r\n",
               (unsigned long)hdr.sizes[GUI_RES_PINYIN_DICT],
               (unsigned long)gui_res_max_size(GUI_RES_PINYIN_DICT));
        return false;
    }

    /*
     * 验证图标头部有效性（最佳努力）：
     * 某些情况下（QSPI 状态切换/上电瞬态）间接读可能失败，但 memory-mapped 仍可正常工作。
     * 为避免“必须插 SD 才能启动”，这里的图标头校验不再作为 QSPI ready 的硬条件。
     * 真正使用图标时，gui_assets.c 还会再次基于 memory-mapped 进行格式校验。
     */
    static uint8_t val_log_once = 0;
    for (gui_res_id_t id = GUI_RES_ICON_01; id <= GUI_RES_ICON_14; ++id) {
        if (hdr.sizes[id] < sizeof(lv_image_header_t)) {
            printf("[QSPI_FS] icon size too small: id=%u size=%lu\r\n",
                   (unsigned)id, (unsigned long)hdr.sizes[id]);
            /* 图标尺寸异常仍视为不完整 */
            return false;
        }

        lv_image_header_t icon_header;
        if (QSPI_W25Qxx_ReadBuffer_Slow((uint8_t *)&icon_header,
                                        gui_res_offset(id),
                                        sizeof(icon_header)) != QSPI_W25Qxx_OK) {
            printf("[QSPI_FS] icon header read fail (non-fatal): id=%u\r\n", (unsigned)id);
            break;
        }

        /* 兼容 legacy bin image：magic 字段保存的是 cf */
        uint8_t orig_magic = icon_header.magic;
        if (icon_header.magic != LV_IMAGE_HEADER_MAGIC) {
            icon_header.cf = (uint8_t)icon_header.magic;
            icon_header.magic = LV_IMAGE_HEADER_MAGIC;
        }

        if (val_log_once == 0 && id == GUI_RES_ICON_01) {
            printf("[QSPI_FS] icon01 header @0x%08lX: orig_magic=%u cf=%u w=%u h=%u stride=%u\r\n",
                   (unsigned long)gui_res_offset(id),
                   (unsigned)orig_magic, (unsigned)icon_header.cf,
                   (unsigned)icon_header.w, (unsigned)icon_header.h,
                   (unsigned)icon_header.stride);
            val_log_once = 1;
        }

        if (icon_header.w == 0 || icon_header.h == 0 || icon_header.w > 512 || icon_header.h > 512) {
            printf("[QSPI_FS] icon header invalid size: id=%u w=%u h=%u\r\n",
                   (unsigned)id, (unsigned)icon_header.w, (unsigned)icon_header.h);
            /* 图标头内容异常仍视为不完整 */
            return false;
        }
        if (icon_header.stride == 0) {
            printf("[QSPI_FS] icon header invalid stride: id=%u\r\n", (unsigned)id);
            return false;
        }
        if ((uint32_t)icon_header.stride * icon_header.h + sizeof(lv_image_header_t) > hdr.sizes[id]) {
            printf("[QSPI_FS] icon header size overflow: id=%u stride=%u h=%u size=%lu\r\n",
                   (unsigned)id, (unsigned)icon_header.stride, (unsigned)icon_header.h,
                   (unsigned long)hdr.sizes[id]);
            return false;
        }
    }
    return true;
}

static FRESULT qspi_write_from_sd(const char * src, uint32_t dst_offset, uint32_t max_size, uint32_t * written_out)
{
    static uint8_t buf[4096];
    FIL fsrc;
    UINT br = 0;
    uint32_t total = 0;
    FRESULT res = FR_OK;

    memset(&fsrc, 0, sizeof(fsrc));

    res = f_open(&fsrc, src, FA_READ);
    if (res != FR_OK) {
        printf("[QSPI_FS] open src fail: %s, res=%d\r\n", src, (int)res);
        return res;
    }

    for (uint32_t erase_addr = dst_offset;
         erase_addr < (dst_offset + max_size);
         erase_addr += QSPI_RES_SLOT_SIZE) {
        int8_t erase_ret = QSPI_W25Qxx_BlockErase_64K(erase_addr);
        if (erase_ret != QSPI_W25Qxx_OK) {
            printf("[QSPI_FS] erase 64K fail @0x%08lX, ret=%d\r\n", 
                   (unsigned long)erase_addr, (int)erase_ret);
            (void)f_close(&fsrc);
            return FR_DISK_ERR;
        }
    }

    do {
        res = f_read(&fsrc, buf, sizeof(buf), &br);
        if (res != FR_OK) {
            printf("[QSPI_FS] read fail: %s, res=%d, off=%lu\r\n",
                   src, (int)res, (unsigned long)total);
            break;
        }
        if (br == 0U) {
            break;
        }
        if ((total + br) > max_size) {
            printf("[QSPI_FS] write overflow: %s, off=%lu size=%u max=%lu\r\n",
                   src, (unsigned long)total, (unsigned)br, (unsigned long)max_size);
            res = FR_INT_ERR;
            break;
        }
        if (QSPI_W25Qxx_WriteBuffer_Slow(buf, dst_offset + total, br) != QSPI_W25Qxx_OK) {
            printf("[QSPI_FS] write qspi fail: off=0x%08lX len=%u\r\n",
                   (unsigned long)(dst_offset + total), (unsigned)br);
            res = FR_DISK_ERR;
            break;
        }
        total += br;
    } while (br > 0U);

    (void)f_close(&fsrc);
    if (written_out) {
        *written_out = total;
    }
    return res;
}

FRESULT GUI_Assets_SyncFromSD(void)
{
    FILINFO info;
    FRESULT res = FR_OK;

    g_qspi_sd_sync_in_progress = 1; /* 标记 SD 同步开始 */
    g_qspi_assets_ready = 0;
    memset(g_qspi_res_sizes, 0, sizeof(g_qspi_res_sizes));

    if (QSPI_W25Qxx_Init() != QSPI_W25Qxx_OK) {
        printf("[QSPI_FS] QSPI init failed\r\n");
        g_qspi_sd_sync_in_progress = 0;
        return FR_NOT_READY;
    }

    /* QSPI FatFs 分区仍保留（可选配置/日志用途） */
    res = QSPIFS_MountOrMkfs();
    if (res != FR_OK) {
        printf("[QSPI_FS] QSPI mount/mkfs -> %d\r\n", (int)res);
        g_qspi_sd_sync_in_progress = 0;
        return res;
    }

    /* 运行时强制同步开关：在 QSPI FatFs 中放置 1:/force_sync.flag */
    const bool qspi_force_sync = (f_stat("1:/force_sync.flag", &info) == FR_OK);
    if (qspi_force_sync) {
        printf("[QSPI_FS] force_sync.flag found in QSPI, will sync from SD\r\n");
    }

    /* 检查 QSPI 资源是否完整，如果完整则无需 SD 卡 */
    const bool qspi_ok = qspi_assets_complete();

#if (EW_QSPI_SYNC_MODE == 2)
    if (qspi_ok) {
        g_qspi_assets_ready = 1;
        printf("[QSPI_FS] QSPI assets complete, SD sync disabled by EW_QSPI_SYNC_MODE=2\r\n");
        g_qspi_sd_sync_in_progress = 0;
        return FR_OK;
    } else {
        printf("[QSPI_FS] QSPI assets incomplete, SD sync disabled by EW_QSPI_SYNC_MODE=2\r\n");
        g_qspi_sd_sync_in_progress = 0;
        return FR_NOT_READY;
    }
#endif

#if (EW_QSPI_SYNC_MODE == 0)
    if (qspi_ok && !qspi_force_sync) {
        g_qspi_assets_ready = 1;
        /* 不在这里进入 memory-mapped，让字体通过 FatFs 加载，图标访问时再按需进入 */
        printf("[QSPI_FS] QSPI assets complete, skip SD card access\r\n");
        g_qspi_sd_sync_in_progress = 0;
        return FR_OK;
    }
#endif

#if (EW_QSPI_SYNC_MODE == 1)
    if (qspi_ok) {
        printf("[QSPI_FS] QSPI assets complete, but will try SD sync (EW_QSPI_SYNC_MODE=1)\r\n");
    }
#endif

    if (!qspi_ok) {
        printf("[QSPI_FS] QSPI assets incomplete or missing, need SD card sync...\r\n");
    }

    /* QSPI 不完整，需要从 SD 卡同步
     * 对齐 PN_TI_LVGL_SD：让 disk_initialize() 内部完成 BSP_SD_Init，避免外部重复 init 导致状态机紊乱。
     */
    for (int attempt = 1; attempt <= 3; ++attempt) {
        HAL_Delay(200);

        DSTATUS st_init = disk_initialize(0);
        DSTATUS st_stat = disk_status(0);
        uint32_t hal_err = HAL_SD_GetError(&hsd1);
        printf("[QSPI_FS] SD init attempt=%d, disk_init=0x%02X, disk_status=0x%02X, HAL_SD_GetError=0x%08lX\r\n",
               attempt, (unsigned)st_init, (unsigned)st_stat, (unsigned long)hal_err);

        /* 等待卡进入 TRANSFER 状态（短超时） */
        uint32_t t0 = HAL_GetTick();
        while ((HAL_GetTick() - t0) < 300U) {
            if (BSP_SD_GetCardState() == SD_TRANSFER_OK) {
                break;
            }
            HAL_Delay(10);
        }
        printf("[QSPI_FS] SD state=%u (0=OK,1=BUSY)\r\n", (unsigned)BSP_SD_GetCardState());

        res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
        printf("[QSPI_FS] SD mount %s -> %d\r\n", SDPath, (int)res);
        if (res == FR_OK) {
            break;
        }
    }

    if (res != FR_OK) {
        printf("[QSPI_FS] SD card not available, cannot sync (res=%d)\r\n", (int)res);
        /* 若 QSPI 已经完整，允许"无卡继续运行"（主要用于 EW_QSPI_SYNC_MODE=1 的场景） */
        if (qspi_ok) {
            g_qspi_assets_ready = 1;
            g_qspi_sd_sync_in_progress = 0;
            return FR_OK;
        }
        g_qspi_sd_sync_in_progress = 0;
        return res;
    }

    const bool force_update = (f_stat("0:/gui/update.flag", &info) == FR_OK);
    printf("[QSPI_FS] sync check: force_update=%d\r\n", (int)force_update);

    gui_res_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = RES_MAGIC_VALUE;
    hdr.version = RES_MAGIC_VERSION;
    hdr.count = GUI_RES_COUNT;

    uint32_t missing = 0, copied = 0, copy_fail = 0;

    /* ========== 第一阶段：预扫描并记录所有资源大小 ========== */
    FSIZE_t src_sizes[GUI_RES_COUNT];
    memset(src_sizes, 0, sizeof(src_sizes));

    for (size_t i = 0; i < (sizeof(k_gui_assets) / sizeof(k_gui_assets[0])); ++i) {
        char src[96];
        const gui_res_id_t id = k_gui_assets[i].id;
        const uint32_t max_size = gui_res_max_size(id);
        const bool is_font = gui_res_is_font(id);
        const bool is_optional = gui_res_is_optional(id);

        (void)snprintf(src, sizeof(src), "0:/%s/%s", k_gui_assets[i].subdir, k_gui_assets[i].name);

        if (file_size(src, &src_sizes[id]) != FR_OK) {
            printf("[QSPI_FS] src missing: %s\r\n", src);
            if (!is_optional) {
                missing++;
            }
            continue;
        }
        if (src_sizes[id] < 16) {
            log_asset_size("src too small", src, src_sizes[id]);
            src_sizes[id] = 0;
            if (!is_optional) {
                missing++;
            }
            continue;
        }
        if ((uint32_t)src_sizes[id] > max_size) {
            printf("[QSPI_FS] src too large: %s size=%lu max=%lu\r\n",
                   src, (unsigned long)src_sizes[id], (unsigned long)max_size);
            src_sizes[id] = 0;
            if (!is_optional) {
                copy_fail++;
            }
            continue;
        }
    }

    /* ========== 第二阶段：重新初始化 QSPI，直写图标到资源区 ========== */
    /* FatFs 操作完成后，重新初始化 QSPI 以确保状态正确 */
    printf("[QSPI_FS] reinit QSPI for direct write...\r\n");
    
    /* 先卸载 QSPI FatFs 驱动，避免 diskio 干扰直接操作 */
    (void)f_mount(NULL, "1:/", 0);
    
    /* 确保退出 memory-mapped 模式并复位 QSPI 外设 */
    (void)QSPI_W25Qxx_ExitMemoryMapped();
    HAL_Delay(10);  /* 等待 Flash 稳定 */
    
    if (QSPI_W25Qxx_Init() != QSPI_W25Qxx_OK) {
        printf("[QSPI_FS] QSPI reinit failed\r\n");
        g_qspi_sd_sync_in_progress = 0;
        return FR_NOT_READY;
    }
    printf("[QSPI_FS] QSPI reinit OK, starting resource write...\r\n");

    for (size_t i = 0; i < (sizeof(k_gui_assets) / sizeof(k_gui_assets[0])); ++i) {
        char src[96];
        uint32_t written = 0;
        const gui_res_id_t id = k_gui_assets[i].id;
        const uint32_t max_size = gui_res_max_size(id);
        if (src_sizes[id] == 0) {
            continue;  /* 源文件无效，已在第一阶段标记 */
        }

        (void)snprintf(src, sizeof(src), "0:/%s/%s", k_gui_assets[i].subdir, k_gui_assets[i].name);

        /* 资源直写 QSPI 资源区（用于 memory-mapped 访问） */
        uint32_t flash_offset = gui_res_offset(id);
        printf("[QSPI_FS] res write: id=%u src=%s off=0x%08lX size=%lu\r\n",
               (unsigned)id, k_gui_assets[i].name,
               (unsigned long)flash_offset, (unsigned long)src_sizes[id]);

        res = qspi_write_from_sd(src, flash_offset, max_size, &written);
        if (res != FR_OK) {
            printf("[QSPI_FS] qspi write fail: %s, res=%d\r\n", src, (int)res);
            if (!gui_res_is_optional(id)) {
                copy_fail++;
            }
            continue;
        }

        /* 验证写入后的数据头（仅对图标做格式验证；字体不做头校验） */
        if (!gui_res_is_font(id)) {
            uint8_t verify_buf[16];
            if (QSPI_W25Qxx_ReadBuffer_Slow(verify_buf, flash_offset, 16) == QSPI_W25Qxx_OK) {
                printf("[QSPI_FS] verify: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                       verify_buf[0], verify_buf[1], verify_buf[2], verify_buf[3],
                       verify_buf[4], verify_buf[5], verify_buf[6], verify_buf[7]);
            }
        }

        hdr.sizes[id] = written;
        g_qspi_res_sizes[id] = written;
        copied++;
    }

    if (force_update) {
        (void)f_unlink("0:/gui/update.flag");
    }
    if (qspi_force_sync) {
        (void)f_unlink("1:/force_sync.flag");
    }

    if (missing == 0 && copy_fail == 0) {
        hdr.checksum = res_header_checksum(&hdr);
        if (qspi_res_header_write(&hdr)) {
            g_qspi_assets_ready = 1;
        }
    }

    /* 重新挂载 QSPI FatFs（保留给其他配置/日志用途） */
    res = f_mount(&QSPIFatFS, "1:/", 1);
    if (res != FR_OK) {
        printf("[QSPI_FS] remount 1:/ fail: res=%d\r\n", (int)res);
    } else {
        printf("[QSPI_FS] remount 1:/ ok\r\n");
    }

    printf("[QSPI_FS] sync done: copied=%lu missing=%lu copy_fail=%lu\r\n",
           (unsigned long)copied, (unsigned long)missing, (unsigned long)copy_fail);
    g_qspi_sd_sync_in_progress = 0; /* 标记同步结束 */
    return FR_OK;
}
