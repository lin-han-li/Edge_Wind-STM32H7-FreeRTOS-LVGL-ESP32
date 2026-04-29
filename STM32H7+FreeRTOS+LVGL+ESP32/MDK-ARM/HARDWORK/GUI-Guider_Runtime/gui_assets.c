#include "gui_assets.h"
#include "gui_assets_sync.h"
#include "gui_resource_map.h"
#include "qspi_w25q256.h"
#include "lv_mem.h"
#include "font/lv_font_fmt_txt.h"
#include "font/lv_binfont_loader.h"
#include "draw/lv_image_dsc.h"

/* 避免依赖额外 include 路径：直接声明 printf */
extern int printf(const char * format, ...);

static bool assets_initialized = false;
static const lv_font_t * font_cn_12 = NULL;
static const lv_font_t * font_cn_14 = NULL;
static const lv_font_t * font_cn_16 = NULL;
static const lv_font_t * font_cn_20 = NULL;
static const lv_font_t * font_cn_30 = NULL;
static const lv_font_t * fallback_font = NULL;

/* 内置中文字体兜底（LVGL 内置的 Source Han Sans，需在 lv_conf.h 启用） */
#if LV_FONT_SOURCE_HAN_SANS_SC_16_CJK
    extern const lv_font_t lv_font_source_han_sans_sc_16_cjk;
    #define FALLBACK_CN_FONT (&lv_font_source_han_sans_sc_16_cjk)
#elif LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
    extern const lv_font_t lv_font_source_han_sans_sc_14_cjk;
    #define FALLBACK_CN_FONT (&lv_font_source_han_sans_sc_14_cjk)
#else
    #define FALLBACK_CN_FONT LV_FONT_DEFAULT
#endif

/* 资源映射：全部来自 W25Q256 的固定地址 */
#define GUI_ICON_COUNT 14U

static const gui_res_id_t k_icon_ids[GUI_ICON_COUNT] = {
    GUI_RES_ICON_01, GUI_RES_ICON_02, GUI_RES_ICON_03, GUI_RES_ICON_04,
    GUI_RES_ICON_05, GUI_RES_ICON_06, GUI_RES_ICON_07, GUI_RES_ICON_08,
    GUI_RES_ICON_09, GUI_RES_ICON_10, GUI_RES_ICON_11, GUI_RES_ICON_12,
    GUI_RES_ICON_13, GUI_RES_ICON_14
};

static lv_image_dsc_t g_icon_desc[GUI_ICON_COUNT];
static bool g_icon_desc_ready = false;

static void log_lv_mem(const char * tag)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    printf("[GUI_ASSETS] mem %s: free=%lu big=%lu frag=%u%% used=%u%%\r\n",
           tag,
           (unsigned long)mon.free_size,
           (unsigned long)mon.free_biggest_size,
           (unsigned)mon.frag_pct,
           (unsigned)mon.used_pct);
}

static bool qspi_res_size_ok(gui_res_id_t id, uint32_t * size_out)
{
    uint32_t size = 0;
    if (!GUI_Assets_GetResSize(id, &size)) {
        printf("[GUI_ASSETS] res size fail: id=%u\r\n", (unsigned)id);
        return false;
    }
    if (size < 16U) {
        printf("[GUI_ASSETS] res too small: id=%u size=%lu\r\n",
               (unsigned)id, (unsigned long)size);
        return false;
    }
    if (size_out) {
        *size_out = size;
    }
    return true;
}

static bool font_has_glyph(const lv_font_t * font, uint32_t unicode, const char * name)
{
    if (!font) {
        printf("[GUI_ASSETS] font_has_glyph: font is NULL\r\n");
        return false;
    }
    lv_font_glyph_dsc_t dsc;
    lv_memzero(&dsc, sizeof(dsc));
    bool found = lv_font_get_glyph_dsc(font, &dsc, unicode, 0);
    printf("[GUI_ASSETS] %s glyph U+%04lX: found=%d box=%ux%u adv=%u fmt=%u\r\n",
           name, (unsigned long)unicode, (int)found,
           (unsigned)dsc.box_w, (unsigned)dsc.box_h,
           (unsigned)dsc.adv_w, (unsigned)dsc.format);
    if (!found) {
        return false;
    }
    return (dsc.box_w > 0U && dsc.box_h > 0U && dsc.adv_w > 0U);
}

static bool init_icon_desc(void)
{
    if (g_icon_desc_ready) {
        return true;
    }

    for (uint32_t i = 0; i < GUI_ICON_COUNT; ++i) {
        uint32_t size = 0;
        gui_res_id_t id = k_icon_ids[i];
        if (!qspi_res_size_ok(id, &size)) {
            return false;
        }

        const uint8_t * base = gui_res_mmap_ptr(id);

        /* 调试：打印内存映射原始字节 */
        if (i == 0) {
            printf("[GUI_ASSETS] icon0 mmap @%p: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                   (void *)base,
                   base[0], base[1], base[2], base[3],
                   base[4], base[5], base[6], base[7],
                   base[8], base[9], base[10], base[11]);
        }

        lv_image_header_t header = *(const lv_image_header_t *)base;

        /* 与 lv_bin_decoder 的兼容逻辑保持一致：支持 legacy bin image（无 magic 字段） */
        if (header.magic != LV_IMAGE_HEADER_MAGIC) {
            /* legacy：原 magic 字段位置存放的是 cf */
            header.cf = (uint8_t)header.magic;
            header.magic = LV_IMAGE_HEADER_MAGIC;
        }

        if (header.cf == LV_COLOR_FORMAT_UNKNOWN) {
            printf("[GUI_ASSETS] icon header cf invalid: id=%u\r\n", (unsigned)id);
            return false;
        }

        g_icon_desc[i].header = header;
        g_icon_desc[i].data_size = size - sizeof(lv_image_header_t);
        g_icon_desc[i].data = base + sizeof(lv_image_header_t);
        g_icon_desc[i].reserved = NULL;
        g_icon_desc[i].reserved_2 = NULL;
    }

    g_icon_desc_ready = true;
    return true;
}

static void apply_icon_src(lv_obj_t * img, uint32_t icon_index)
{
    static uint8_t log_quota = 16;

    if (!img) {
        return;
    }
    if (!lv_obj_is_valid(img)) {
        return;
    }

    if (!GUI_Assets_QSPIReady()) {
        printf("[GUI_ASSETS] ERROR: QSPI not ready, icon cannot load\r\n");
        return;
    }

    if (icon_index >= GUI_ICON_COUNT) {
        printf("[GUI_ASSETS] ERROR: icon index out of range: %lu\r\n", (unsigned long)icon_index);
        return;
    }

    if (!init_icon_desc()) {
        printf("[GUI_ASSETS] ERROR: icon desc init failed\r\n");
        return;
    }

    lv_image_set_src(img, &g_icon_desc[icon_index]);
    if (log_quota > 0) {
        const lv_image_header_t * header = &g_icon_desc[icon_index].header;
        printf("[GUI_ASSETS] icon ok: idx=%lu (%ux%u cf=%u stride=%u)\r\n",
               (unsigned long)icon_index,
               (unsigned)header->w,
               (unsigned)header->h,
               (unsigned)header->cf,
               (unsigned)header->stride);
        log_quota--;
    }
}

void gui_assets_set_icon(lv_obj_t * img, uint32_t icon_index)
{
    apply_icon_src(img, icon_index);
}

void gui_assets_init(void)
{
    if (assets_initialized) {
        return;
    }

    assets_initialized = true;
    fallback_font = LV_FONT_DEFAULT;

    (void)QSPI_W25Qxx_EnterMemoryMapped();

    log_lv_mem("before fonts");

    uint32_t sz20 = 0;
    if (qspi_res_size_ok(GUI_RES_FONT_20, &sz20)) {
        font_cn_20 = lv_binfont_create_mmap(gui_res_mmap_ptr(GUI_RES_FONT_20), sz20);
        if (font_cn_20) {
            printf("[GUI_ASSETS] binfont 20 mmap=%p size=%lu line_h=%d base=%d\r\n",
                   (void *)font_cn_20, (unsigned long)sz20,
                   (int)font_cn_20->line_height, (int)font_cn_20->base_line);
        } else {
            printf("[GUI_ASSETS] binfont 20 mmap create failed, size=%lu\r\n", (unsigned long)sz20);
        }
    }
    if (!font_cn_20 || !font_has_glyph(font_cn_20, 0x5B9E /* 实 */, "binfont20")) {
        font_cn_20 = FALLBACK_CN_FONT;
        printf("[GUI_ASSETS] binfont 20 invalid, fallback to builtin font\r\n");
    } else {
        const lv_font_fmt_txt_dsc_t * dsc20 = (const lv_font_fmt_txt_dsc_t *)font_cn_20->dsc;
        if (dsc20) {
            printf("[GUI_ASSETS] font20 dsc: bpp=%u bitmap=%p cmap_num=%u fmt=%u\r\n",
                   (unsigned)dsc20->bpp, (void *)dsc20->glyph_bitmap, 
                   (unsigned)dsc20->cmap_num, (unsigned)dsc20->bitmap_format);
        }
    }

    if (GUI_Assets_QSPIReady()) {
        uint32_t sz12 = 0;
        if (qspi_res_size_ok(GUI_RES_FONT_12, &sz12)) {
            font_cn_12 = lv_binfont_create_mmap(gui_res_mmap_ptr(GUI_RES_FONT_12), sz12);
            if (!font_cn_12 || !font_has_glyph(font_cn_12, 0x7BA1 /* 管 */, "binfont12")) {
                font_cn_12 = NULL;
                printf("[GUI_ASSETS] binfont 12 not available, keep using 16/20\r\n");
            } else {
                printf("[GUI_ASSETS] binfont 12 mmap=%p\r\n", (void *)font_cn_12);
            }
        }

        uint32_t sz14 = 0;
        if (qspi_res_size_ok(GUI_RES_FONT_14, &sz14)) {
            font_cn_14 = lv_binfont_create_mmap(gui_res_mmap_ptr(GUI_RES_FONT_14), sz14);
            if (!font_cn_14 || !font_has_glyph(font_cn_14, 0x7BA1 /* 管 */, "binfont14")) {
                font_cn_14 = NULL;
                printf("[GUI_ASSETS] binfont 14 not available, keep using 16/20\r\n");
            } else {
                printf("[GUI_ASSETS] binfont 14 mmap=%p\r\n", (void *)font_cn_14);
            }
        }

        uint32_t sz16 = 0;
        if (qspi_res_size_ok(GUI_RES_FONT_16, &sz16)) {
            font_cn_16 = lv_binfont_create_mmap(gui_res_mmap_ptr(GUI_RES_FONT_16), sz16);
            if (!font_cn_16 || !font_has_glyph(font_cn_16, 0x7BA1 /* 管 */, "binfont16")) {
                font_cn_16 = NULL;
                printf("[GUI_ASSETS] binfont 16 not available, keep using 20\r\n");
            } else {
                printf("[GUI_ASSETS] binfont 16 mmap=%p\r\n", (void *)font_cn_16);
            }
        }
    }

    if (GUI_Assets_QSPIReady()) {
        uint32_t sz30 = 0;
        if (qspi_res_size_ok(GUI_RES_FONT_30, &sz30)) {
            font_cn_30 = lv_binfont_create_mmap(gui_res_mmap_ptr(GUI_RES_FONT_30), sz30);
            if (!font_cn_30 || !font_has_glyph(font_cn_30, 0x7F51 /* 置 */, "binfont30")) {
                font_cn_30 = NULL;
                printf("[GUI_ASSETS] binfont 30 not available, keep using 20\r\n");
            } else {
                printf("[GUI_ASSETS] binfont 30 mmap=%p\r\n", (void *)font_cn_30);
            }
        }
    }

    /* D-Cache 失效：确保从 QSPI 读取的字体位图数据对 CPU 可见（丢弃缓存，强制从内存读） */
    #if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache();
    printf("[GUI_ASSETS] D-Cache invalidated for font bitmaps\r\n");
    #endif

    /* 字体加载完成后，进入 memory-mapped 模式以供图标直接访问 */
    (void)QSPI_W25Qxx_EnterMemoryMapped();
    printf("[GUI_ASSETS] Entered memory-mapped mode for icon access\r\n");

    log_lv_mem("after fonts");
}

const lv_font_t * gui_assets_get_font_16(void)
{
    return font_cn_16 ? font_cn_16 : gui_assets_get_font_20();
}

const lv_font_t * gui_assets_get_font_12(void)
{
    return font_cn_12 ? font_cn_12 : gui_assets_get_font_16();
}

const lv_font_t * gui_assets_get_font_14(void)
{
    return font_cn_14 ? font_cn_14 : gui_assets_get_font_16();
}

const lv_font_t * gui_assets_get_font_20(void)
{
    return font_cn_20 ? font_cn_20 : LV_FONT_DEFAULT;
}

const lv_font_t * gui_assets_get_font_30(void)
{
    return font_cn_30 ? font_cn_30 : gui_assets_get_font_20();
}

void gui_assets_patch_images(lv_ui * ui)
{
    if (!ui) {
        return;
    }

    apply_icon_src(ui->Main_1_img_1, 0);
    apply_icon_src(ui->Main_1_img_2, 1);
    apply_icon_src(ui->Main_1_img_3, 2);
    apply_icon_src(ui->Main_1_img_4, 3);
    apply_icon_src(ui->Main_1_img_5, 4);
    apply_icon_src(ui->Main_1_img_6, 5);

    apply_icon_src(ui->Main_2_img_1, 6);
    apply_icon_src(ui->Main_2_img_2, 7);
    apply_icon_src(ui->Main_2_img_3, 8);
    apply_icon_src(ui->Main_2_img_4, 9);
    apply_icon_src(ui->Main_2_img_5, 10);
    apply_icon_src(ui->Main_2_img_6, 11);

    apply_icon_src(ui->Main_3_img_1, 12);
    apply_icon_src(ui->Main_3_img_2, 13);
}

/* 已移除内置图标兜底，完全依赖 W25Q256 外部 Flash 以节省内部 Flash 空间 */
