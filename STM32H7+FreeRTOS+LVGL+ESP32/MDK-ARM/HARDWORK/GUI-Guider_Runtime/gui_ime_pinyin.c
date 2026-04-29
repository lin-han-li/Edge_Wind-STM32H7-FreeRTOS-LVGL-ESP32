#include "gui_ime_pinyin.h"
#include "gui_assets.h"
#include "gui_assets_sync.h"
#include "gui_resource_map.h"
#include "ff.h"
#include <stdio.h>

#if LV_USE_IME_PINYIN

/* 拼音字典二进制格式（全部为小端）：
 * [Header]
 *  - uint32 magic = 'PYDB'
 *  - uint16 version = 1
 *  - uint16 reserved
 *  - uint32 entry_count
 *  - uint32 table_offset   (pinyin_dict_entry_t 数组偏移)
 *  - uint32 strings_offset (字符串区起始偏移，可用于校验)
 * [Table]
 *  - entry_count 个条目，每条:
 *    uint32 py_offset     (指向拼音字符串，'\0' 结尾)
 *    uint32 py_mb_offset  (指向候选词 UTF-8 字符串，'\0' 结尾)
 * 字典需按拼音首字母分组排序（a..z 连续），与 LVGL 搜索逻辑一致。
 */
#define PY_DICT_MAGIC   0x50594442UL /* 'PYDB' */
#define PY_DICT_VERSION 0x0001U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t entry_count;
    uint32_t table_offset;
    uint32_t strings_offset;
} pinyin_dict_header_t;

typedef struct {
    uint32_t py_offset;
    uint32_t py_mb_offset;
} pinyin_dict_entry_t;

/* lv_pinyin_dict_t 的成员是 const 指针，运行时不可直接赋值。
 * 使用同布局的可写结构体构造后，再 cast 为 lv_pinyin_dict_t。 */
typedef struct {
    const char * py;
    const char * py_mb;
} pinyin_dict_mut_t;

static lv_obj_t * s_ime = NULL;
static lv_pinyin_dict_t * s_dict = NULL;
static uint32_t s_dict_count = 0;
static uint8_t * s_dict_blob = NULL;
static uint32_t s_dict_blob_size = 0;
static lv_timer_t * s_dict_retry_timer = NULL;

static bool pinyin_dict_init_from_base(const uint8_t * base, uint32_t dict_size)
{
    if (s_dict) {
        return true;
    }
    if (!base || dict_size < sizeof(pinyin_dict_header_t)) {
        return false;
    }

    pinyin_dict_header_t hdr;
    lv_memcpy(&hdr, base, sizeof(hdr));

    if (hdr.magic != PY_DICT_MAGIC || hdr.version != PY_DICT_VERSION) {
        return false;
    }
    if (hdr.entry_count == 0) {
        return false;
    }
    if (hdr.table_offset >= dict_size || hdr.strings_offset >= dict_size) {
        return false;
    }
    if ((uint64_t)hdr.table_offset + (uint64_t)hdr.entry_count * sizeof(pinyin_dict_entry_t) > dict_size) {
        return false;
    }

    pinyin_dict_mut_t * dict = lv_malloc(sizeof(pinyin_dict_mut_t) * (hdr.entry_count + 1));
    if (!dict) {
        return false;
    }

    const uint8_t * table_base = base + hdr.table_offset;
    for (uint32_t i = 0; i < hdr.entry_count; ++i) {
        pinyin_dict_entry_t ent;
        lv_memcpy(&ent, table_base + i * sizeof(ent), sizeof(ent));

        if (ent.py_offset >= dict_size || ent.py_mb_offset >= dict_size) {
            lv_free(dict);
            return false;
        }
        dict[i].py = (const char *)(base + ent.py_offset);
        dict[i].py_mb = (const char *)(base + ent.py_mb_offset);
    }

    dict[hdr.entry_count].py = NULL;
    dict[hdr.entry_count].py_mb = NULL;

    s_dict = (lv_pinyin_dict_t *)dict;
    s_dict_count = hdr.entry_count;
    return true;
}

static bool pinyin_dict_load_from_qspi(void)
{
    if (s_dict) {
        return true;
    }
    if (!GUI_Assets_QSPIReady()) {
        return false;
    }

    uint32_t dict_size = 0;
    if (!GUI_Assets_GetResSize(GUI_RES_PINYIN_DICT, &dict_size)) {
        return false;
    }
    if (dict_size < sizeof(pinyin_dict_header_t)) {
        return false;
    }

    const uint8_t * base = gui_res_mmap_ptr(GUI_RES_PINYIN_DICT);
    bool ok = pinyin_dict_init_from_base(base, dict_size);
    if (ok) {
        printf("[IME_PY] QSPI dict loaded: %lu bytes\r\n", (unsigned long)dict_size);
    }
    return ok;
}

static bool pinyin_dict_load_from_sd(void)
{
    if (s_dict) {
        return true;
    }
    if (s_dict_blob) {
        return pinyin_dict_init_from_base(s_dict_blob, s_dict_blob_size);
    }

    const char * path = "0:/pinyin/pinyin_dict.bin";
    FIL fil;
    FRESULT res = f_open(&fil, path, FA_READ);
    if (res != FR_OK) {
        printf("[IME_PY] SD dict open fail: %s res=%d\r\n", path, (int)res);
        return false;
    }

    FSIZE_t fsize = f_size(&fil);
    if (fsize < sizeof(pinyin_dict_header_t)) {
        f_close(&fil);
        printf("[IME_PY] SD dict too small: %lu\r\n", (unsigned long)fsize);
        return false;
    }

    uint8_t * buf = lv_malloc((size_t)fsize);
    if (!buf) {
        f_close(&fil);
        printf("[IME_PY] SD dict malloc fail: %lu\r\n", (unsigned long)fsize);
        return false;
    }

    UINT br = 0;
    res = f_read(&fil, buf, (UINT)fsize, &br);
    f_close(&fil);
    if (res != FR_OK || br != (UINT)fsize) {
        printf("[IME_PY] SD dict read fail: res=%d br=%u\r\n", (int)res, (unsigned)br);
        lv_free(buf);
        return false;
    }

    s_dict_blob = buf;
    s_dict_blob_size = (uint32_t)fsize;
    if (!pinyin_dict_init_from_base(s_dict_blob, s_dict_blob_size)) {
        lv_free(s_dict_blob);
        s_dict_blob = NULL;
        s_dict_blob_size = 0;
        return false;
    }
    printf("[IME_PY] SD dict loaded: %lu bytes\r\n", (unsigned long)fsize);
    return true;
}

static void pinyin_dict_retry_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    if (s_dict) {
        if (s_ime) {
            lv_ime_pinyin_set_dict(s_ime, s_dict);
        }
        if (s_dict_retry_timer) {
            lv_timer_del(s_dict_retry_timer);
            s_dict_retry_timer = NULL;
        }
        return;
    }

    if (!pinyin_dict_load_from_qspi()) {
        (void)pinyin_dict_load_from_sd();
    }

    if (s_dict && s_ime) {
        lv_ime_pinyin_set_dict(s_ime, s_dict);
        if (s_dict_retry_timer) {
            lv_timer_del(s_dict_retry_timer);
            s_dict_retry_timer = NULL;
        }
    }
}

bool gui_ime_pinyin_attach(lv_obj_t * kb)
{
    if (kb == NULL) {
        return false;
    }

    if (s_ime == NULL) {
        s_ime = lv_ime_pinyin_create(lv_layer_top());
    }

    if (s_dict == NULL) {
        if (!pinyin_dict_load_from_qspi()) {
            (void)pinyin_dict_load_from_sd();
        }
    }

    if (s_dict) {
        lv_ime_pinyin_set_dict(s_ime, s_dict);
    } else if (s_dict_retry_timer == NULL) {
        s_dict_retry_timer = lv_timer_create(pinyin_dict_retry_cb, 500, NULL);
    }

    lv_ime_pinyin_set_keyboard(s_ime, kb);
    lv_ime_pinyin_set_mode(s_ime, LV_IME_PINYIN_MODE_K26);

    const lv_font_t * font = gui_assets_get_font_20();
    if (font) {
        lv_obj_set_style_text_font(s_ime, font, 0);
    }

    return (s_dict != NULL && s_dict_count > 0);
}

bool gui_ime_pinyin_dict_ready(void)
{
    return (s_dict != NULL && s_dict_count > 0);
}

#else

bool gui_ime_pinyin_attach(lv_obj_t * kb)
{
    LV_UNUSED(kb);
    return false;
}

bool gui_ime_pinyin_dict_ready(void)
{
    return false;
}

#endif /* LV_USE_IME_PINYIN */
