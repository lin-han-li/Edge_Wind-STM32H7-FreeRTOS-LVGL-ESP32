/**
 * @file lv_binfont_loader.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_font_fmt_txt_private.h"
#include "../lvgl.h"
#include "../misc/lv_fs_private.h"
#include "../misc/lv_types.h"
#include "../stdlib/lv_string.h"
#include "../stdlib/lv_mem.h"
#include "lv_binfont_loader.h"

#ifndef EW_BINFONT_MMAP_ZERO_COPY
#define EW_BINFONT_MMAP_ZERO_COPY 1
#endif

#ifndef EW_BINFONT_MMAP_CACHE_SIZE
#define EW_BINFONT_MMAP_CACHE_SIZE (8U * 1024U)
#endif

#define BINFONT_MMAP_MAGIC 0x4D4D4150UL /* 'MMAP' */

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    lv_fs_file_t * fp;
    int8_t bit_pos;
    uint8_t byte_value;
} bit_iterator_t;

typedef struct font_header_bin {
    uint32_t version;
    uint16_t tables_count;
    uint16_t font_size;
    uint16_t ascent;
    int16_t descent;
    uint16_t typo_ascent;
    int16_t typo_descent;
    uint16_t typo_line_gap;
    int16_t min_y;
    int16_t max_y;
    uint16_t default_advance_width;
    uint16_t kerning_scale;
    uint8_t index_to_loc_format;
    uint8_t glyph_id_format;
    uint8_t advance_width_format;
    uint8_t bits_per_pixel;
    uint8_t xy_bits;
    uint8_t wh_bits;
    uint8_t advance_width_bits;
    uint8_t compression_id;
    uint8_t subpixels_mode;
    uint8_t padding;
    int16_t underline_position;
    uint16_t underline_thickness;
} font_header_bin_t;

typedef struct cmap_table_bin {
    uint32_t data_offset;
    uint32_t range_start;
    uint16_t range_length;
    uint16_t glyph_id_start;
    uint16_t data_entries_count;
    uint8_t format_type;
    uint8_t padding;
} cmap_table_bin_t;

#if EW_BINFONT_MMAP_ZERO_COPY
typedef struct {
    uint32_t magic;
    const uint8_t * base;
    uint32_t size;
    uint32_t glyf_start;
    uint32_t glyf_length;
    uint32_t loca_count;
    uint32_t nbits;
    uint8_t nbits_rem;
    uint32_t * glyph_offset;
    uint32_t cache_gid;
    uint8_t * cache_buf;
    uint32_t cache_size;
    uint32_t cache_len;
} binfont_mmap_ctx_t;
#endif

/**********************
 *  STATIC PROTOTYPES
 **********************/
static bit_iterator_t init_bit_iterator(lv_fs_file_t * fp);
static bool lvgl_load_font(lv_fs_file_t * fp, lv_font_t * font);
int32_t load_kern(lv_fs_file_t * fp, lv_font_fmt_txt_dsc_t * font_dsc, uint8_t format, uint32_t start);

static int read_bits_signed(bit_iterator_t * it, int n_bits, lv_fs_res_t * res);
static unsigned int read_bits(bit_iterator_t * it, int n_bits, lv_fs_res_t * res);

static lv_font_t * binfont_font_create_cb(const lv_font_info_t * info, const void * src);
static void binfont_font_delete_cb(lv_font_t * font);
static void * binfont_font_dup_src_cb(const void * src);
static void binfont_font_free_src_cb(void * src);

/**********************
 *      MACROS
 **********************/

/**********************
 *  GLOBAL VARIABLES
 **********************/

const lv_font_class_t lv_binfont_font_class = {
    .create_cb = binfont_font_create_cb,
    .delete_cb = binfont_font_delete_cb,
    .dup_src_cb = binfont_font_dup_src_cb,
    .free_src_cb = binfont_font_free_src_cb,
};

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_font_t * lv_binfont_create(const char * path)
{
    LV_ASSERT_NULL(path);

    lv_fs_file_t file;
    lv_fs_res_t fs_res = lv_fs_open(&file, path, LV_FS_MODE_RD);
    if(fs_res != LV_FS_RES_OK) return NULL;

    lv_font_t * font = lv_malloc_zeroed(sizeof(lv_font_t));
    LV_ASSERT_MALLOC(font);

    if(!lvgl_load_font(&file, font)) {
        LV_LOG_WARN("Error loading font file: %s", path);
        /*
        * When `lvgl_load_font` fails it can leak some pointers.
        * All non-null pointers can be assumed as allocated and
        * `lv_binfont_destroy` should free them correctly.
        */
        lv_binfont_destroy(font);
        font = NULL;
    }

    lv_fs_close(&file);

    return font;
}

#if LV_USE_FS_MEMFS
lv_font_t * lv_binfont_create_from_buffer(void * buffer, uint32_t size)
{
    lv_fs_path_ex_t mempath;

    lv_fs_make_path_from_buffer(&mempath, LV_FS_MEMFS_LETTER, buffer, size, "bin");
    return lv_binfont_create((const char *)&mempath);
}
#endif

#if EW_BINFONT_MMAP_ZERO_COPY
lv_font_t * lv_binfont_create_mmap(const void * buffer, uint32_t size)
{
#if LV_USE_FS_MEMFS
    lv_fs_path_ex_t mempath;
    lv_fs_make_path_from_buffer(&mempath, LV_FS_MEMFS_LETTER, buffer, size, "bin");
    return lv_binfont_create((const char *)&mempath);
#else
    LV_UNUSED(buffer);
    LV_UNUSED(size);
    return NULL;
#endif
}
#endif

void lv_binfont_destroy(lv_font_t * font)
{
    if(font == NULL) return;

    const lv_font_fmt_txt_dsc_t * dsc = font->dsc;
    if(dsc == NULL) return;

    bool is_mmap = false;
#if EW_BINFONT_MMAP_ZERO_COPY
    binfont_mmap_ctx_t * mmap_ctx = (binfont_mmap_ctx_t *)font->user_data;
    if(mmap_ctx && mmap_ctx->magic == BINFONT_MMAP_MAGIC) {
        is_mmap = true;
        if(mmap_ctx->glyph_offset) lv_free(mmap_ctx->glyph_offset);
        if(mmap_ctx->cache_buf) lv_free(mmap_ctx->cache_buf);
        lv_free(mmap_ctx);
        font->user_data = NULL;
    }
#endif

    if(dsc->kern_classes == 0) {
        const lv_font_fmt_txt_kern_pair_t * kern_dsc = dsc->kern_dsc;
        if(NULL != kern_dsc) {
            lv_free((void *)kern_dsc->glyph_ids);
            lv_free((void *)kern_dsc->values);
            lv_free((void *)kern_dsc);
        }
    }
    else {
        const lv_font_fmt_txt_kern_classes_t * kern_dsc = dsc->kern_dsc;
        if(NULL != kern_dsc) {
            lv_free((void *)kern_dsc->class_pair_values);
            lv_free((void *)kern_dsc->left_class_mapping);
            lv_free((void *)kern_dsc->right_class_mapping);
            lv_free((void *)kern_dsc);
        }
    }

    const lv_font_fmt_txt_cmap_t * cmaps = dsc->cmaps;
    if(NULL != cmaps) {
        for(int i = 0; i < dsc->cmap_num; ++i) {
            lv_free((void *)cmaps[i].glyph_id_ofs_list);
            lv_free((void *)cmaps[i].unicode_list);
        }
        lv_free((void *)cmaps);
    }

    if(!is_mmap) {
        lv_free((void *)dsc->glyph_bitmap);
    }
    lv_free((void *)dsc->glyph_dsc);
    lv_free((void *)dsc);
    lv_free(font);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static bit_iterator_t init_bit_iterator(lv_fs_file_t * fp)
{
    bit_iterator_t it;
    it.fp = fp;
    it.bit_pos = -1;
    it.byte_value = 0;
    return it;
}

static unsigned int read_bits(bit_iterator_t * it, int n_bits, lv_fs_res_t * res)
{
    unsigned int value = 0;
    while(n_bits--) {
        it->byte_value = it->byte_value << 1;
        it->bit_pos--;

        if(it->bit_pos < 0) {
            it->bit_pos = 7;
            *res = lv_fs_read(it->fp, &(it->byte_value), 1, NULL);
            if(*res != LV_FS_RES_OK) {
                return 0;
            }
        }
        int8_t bit = (it->byte_value & 0x80) ? 1 : 0;

        value |= (bit << n_bits);
    }
    *res = LV_FS_RES_OK;
    return value;
}

#if EW_BINFONT_MMAP_ZERO_COPY
typedef struct {
    const uint8_t * ptr;
    int bit_pos;
    uint8_t byte_value;
} bit_iterator_mem_t;

static bit_iterator_mem_t init_bit_iterator_mem(const uint8_t * ptr)
{
    bit_iterator_mem_t it;
    it.ptr = ptr;
    it.bit_pos = -1;
    it.byte_value = 0;
    return it;
}

static unsigned int read_bits_mem(bit_iterator_mem_t * it, int n_bits)
{
    unsigned int value = 0;
    while(n_bits--) {
        if(it->bit_pos < 0) {
            it->byte_value = *(it->ptr++);
            it->bit_pos = 7;
        }
        int8_t bit = (it->byte_value & 0x80) ? 1 : 0;
        it->byte_value <<= 1;
        it->bit_pos--;
        value |= (bit << n_bits);
    }
    return value;
}
#endif

static int read_bits_signed(bit_iterator_t * it, int n_bits, lv_fs_res_t * res)
{
    unsigned int value = read_bits(it, n_bits, res);
    if(value & (1 << (n_bits - 1))) {
        value |= ~0u << n_bits;
    }
    return value;
}

static int read_label(lv_fs_file_t * fp, int start, const char * label)
{
    lv_fs_seek(fp, start, LV_FS_SEEK_SET);

    uint32_t length;
    char buf[4];

    if(lv_fs_read(fp, &length, 4, NULL) != LV_FS_RES_OK
       || lv_fs_read(fp, buf, 4, NULL) != LV_FS_RES_OK
       || lv_memcmp(label, buf, 4) != 0) {
        LV_LOG_WARN("Error reading '%s' label.", label);
        return -1;
    }

    return length;
}

static bool load_cmaps_tables(lv_fs_file_t * fp, lv_font_fmt_txt_dsc_t * font_dsc,
                              uint32_t cmaps_start, cmap_table_bin_t * cmap_table)
{
    if(lv_fs_read(fp, cmap_table, font_dsc->cmap_num * sizeof(cmap_table_bin_t), NULL) != LV_FS_RES_OK) {
        return false;
    }

    for(unsigned int i = 0; i < font_dsc->cmap_num; ++i) {
        lv_fs_res_t res = lv_fs_seek(fp, cmaps_start + cmap_table[i].data_offset, LV_FS_SEEK_SET);
        if(res != LV_FS_RES_OK) {
            return false;
        }

        lv_font_fmt_txt_cmap_t * cmap = (lv_font_fmt_txt_cmap_t *) & (font_dsc->cmaps[i]);

        cmap->range_start = cmap_table[i].range_start;
        cmap->range_length = cmap_table[i].range_length;
        cmap->glyph_id_start = cmap_table[i].glyph_id_start;
        cmap->type = cmap_table[i].format_type;

        switch(cmap_table[i].format_type) {
            case LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL: {
                    uint32_t ids_size = (uint32_t)(sizeof(uint8_t) * cmap_table[i].data_entries_count);
                    uint8_t * glyph_id_ofs_list = lv_malloc(ids_size);

                    cmap->glyph_id_ofs_list = glyph_id_ofs_list;

                    if(lv_fs_read(fp, glyph_id_ofs_list, ids_size, NULL) != LV_FS_RES_OK) {
                        return false;
                    }

                    cmap->list_length = cmap->range_length;
                    break;
                }
            case LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY:
                break;
            case LV_FONT_FMT_TXT_CMAP_SPARSE_FULL:
            case LV_FONT_FMT_TXT_CMAP_SPARSE_TINY: {
                    uint32_t list_size = sizeof(uint16_t) * cmap_table[i].data_entries_count;
                    uint16_t * unicode_list = (uint16_t *)lv_malloc(list_size);

                    cmap->unicode_list = unicode_list;
                    cmap->list_length = cmap_table[i].data_entries_count;

                    if(lv_fs_read(fp, unicode_list, list_size, NULL) != LV_FS_RES_OK) {
                        return false;
                    }

                    if(cmap_table[i].format_type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL) {
                        uint16_t * buf = lv_malloc(sizeof(uint16_t) * cmap->list_length);

                        cmap->glyph_id_ofs_list = buf;

                        if(lv_fs_read(fp, buf, sizeof(uint16_t) * cmap->list_length, NULL) != LV_FS_RES_OK) {
                            return false;
                        }
                    }
                    break;
                }
            default:
                LV_LOG_WARN("Unknown cmaps format type %d.", cmap_table[i].format_type);
                return false;
        }
    }
    return true;
}

static int32_t load_cmaps(lv_fs_file_t * fp, lv_font_fmt_txt_dsc_t * font_dsc, uint32_t cmaps_start)
{
    int32_t cmaps_length = read_label(fp, cmaps_start, "cmap");
    if(cmaps_length < 0) {
        return -1;
    }

    uint32_t cmaps_subtables_count;
    if(lv_fs_read(fp, &cmaps_subtables_count, sizeof(uint32_t), NULL) != LV_FS_RES_OK) {
        return -1;
    }

    lv_font_fmt_txt_cmap_t * cmaps =
        lv_malloc(cmaps_subtables_count * sizeof(lv_font_fmt_txt_cmap_t));

    lv_memset(cmaps, 0, cmaps_subtables_count * sizeof(lv_font_fmt_txt_cmap_t));

    font_dsc->cmaps = cmaps;
    font_dsc->cmap_num = cmaps_subtables_count;

    cmap_table_bin_t * cmaps_tables = lv_malloc(sizeof(cmap_table_bin_t) * font_dsc->cmap_num);

    bool success = load_cmaps_tables(fp, font_dsc, cmaps_start, cmaps_tables);

    lv_free(cmaps_tables);

    return success ? cmaps_length : -1;
}

static int32_t load_glyph(lv_fs_file_t * fp, lv_font_fmt_txt_dsc_t * font_dsc,
                          uint32_t start, uint32_t * glyph_offset, uint32_t loca_count, font_header_bin_t * header
#if EW_BINFONT_MMAP_ZERO_COPY
                          , binfont_mmap_ctx_t * mmap_ctx
#endif
                          )
{
    int32_t glyph_length = read_label(fp, start, "glyf");
    if(glyph_length < 0) {
        return -1;
    }

    lv_font_fmt_txt_glyph_dsc_t * glyph_dsc = (lv_font_fmt_txt_glyph_dsc_t *)
                                              lv_malloc(loca_count * sizeof(lv_font_fmt_txt_glyph_dsc_t));

    lv_memset(glyph_dsc, 0, loca_count * sizeof(lv_font_fmt_txt_glyph_dsc_t));

    font_dsc->glyph_dsc = glyph_dsc;

    int cur_bmp_size = 0;

    for(unsigned int i = 0; i < loca_count; ++i) {
        lv_font_fmt_txt_glyph_dsc_t * gdsc = &glyph_dsc[i];

        lv_fs_res_t res = lv_fs_seek(fp, start + glyph_offset[i], LV_FS_SEEK_SET);
        if(res != LV_FS_RES_OK) {
            return -1;
        }

        bit_iterator_t bit_it = init_bit_iterator(fp);

        if(header->advance_width_bits == 0) {
            gdsc->adv_w = header->default_advance_width;
        }
        else {
            gdsc->adv_w = read_bits(&bit_it, header->advance_width_bits, &res);
            if(res != LV_FS_RES_OK) {
                return -1;
            }
        }

        if(header->advance_width_format == 0) {
            gdsc->adv_w *= 16;
        }

        gdsc->ofs_x = read_bits_signed(&bit_it, header->xy_bits, &res);
        if(res != LV_FS_RES_OK) {
            return -1;
        }

        gdsc->ofs_y = read_bits_signed(&bit_it, header->xy_bits, &res);
        if(res != LV_FS_RES_OK) {
            return -1;
        }

        gdsc->box_w = read_bits(&bit_it, header->wh_bits, &res);
        if(res != LV_FS_RES_OK) {
            return -1;
        }

        gdsc->box_h = read_bits(&bit_it, header->wh_bits, &res);
        if(res != LV_FS_RES_OK) {
            return -1;
        }

        int nbits = header->advance_width_bits + 2 * header->xy_bits + 2 * header->wh_bits;
        int next_offset = (i < loca_count - 1) ? glyph_offset[i + 1] : (uint32_t)glyph_length;
        int bmp_size = next_offset - glyph_offset[i] - nbits / 8;

        if(i == 0) {
            gdsc->adv_w = 0;
            gdsc->box_w = 0;
            gdsc->box_h = 0;
            gdsc->ofs_x = 0;
            gdsc->ofs_y = 0;
        }

        gdsc->bitmap_index = cur_bmp_size;
        if(gdsc->box_w * gdsc->box_h != 0) {
            cur_bmp_size += bmp_size;
        }
    }

#if EW_BINFONT_MMAP_ZERO_COPY
    if(mmap_ctx) {
        font_dsc->glyph_bitmap = NULL;
        return glyph_length;
    }
#endif

    uint8_t * glyph_bmp = (uint8_t *)lv_malloc(sizeof(uint8_t) * cur_bmp_size);
    LV_ASSERT_MALLOC(glyph_bmp);

    font_dsc->glyph_bitmap = glyph_bmp;

    cur_bmp_size = 0;

    for(unsigned int i = 1; i < loca_count; ++i) {
        lv_fs_res_t res = lv_fs_seek(fp, start + glyph_offset[i], LV_FS_SEEK_SET);
        if(res != LV_FS_RES_OK) {
            return -1;
        }
        bit_iterator_t bit_it = init_bit_iterator(fp);

        int nbits = header->advance_width_bits + 2 * header->xy_bits + 2 * header->wh_bits;

        read_bits(&bit_it, nbits, &res);
        if(res != LV_FS_RES_OK) {
            return -1;
        }

        if(glyph_dsc[i].box_w * glyph_dsc[i].box_h == 0) {
            continue;
        }

        int next_offset = (i < loca_count - 1) ? glyph_offset[i + 1] : (uint32_t)glyph_length;
        int bmp_size = next_offset - glyph_offset[i] - nbits / 8;

        if(nbits % 8 == 0) {  /*Fast path*/
            if(lv_fs_read(fp, &glyph_bmp[cur_bmp_size], bmp_size, NULL) != LV_FS_RES_OK) {
                return -1;
            }
        }
        else {
            for(int k = 0; k < bmp_size - 1; ++k) {
                glyph_bmp[cur_bmp_size + k] = read_bits(&bit_it, 8, &res);
                if(res != LV_FS_RES_OK) {
                    return -1;
                }
            }
            glyph_bmp[cur_bmp_size + bmp_size - 1] = read_bits(&bit_it, 8 - nbits % 8, &res);
            if(res != LV_FS_RES_OK) {
                return -1;
            }

            /*The last fragment should be on the MSB but read_bits() will place it to the LSB*/
            glyph_bmp[cur_bmp_size + bmp_size - 1] = glyph_bmp[cur_bmp_size + bmp_size - 1] << (nbits % 8);

        }

        cur_bmp_size += bmp_size;
    }
    return glyph_length;
}

#if EW_BINFONT_MMAP_ZERO_COPY
static void binfont_mmap_ensure_cache(binfont_mmap_ctx_t * ctx, uint32_t need_size)
{
    if(need_size == 0) return;
    if(ctx->cache_buf == NULL || ctx->cache_size < need_size) {
        if(ctx->cache_buf) {
            lv_free(ctx->cache_buf);
        }
        uint32_t alloc_size = (need_size > EW_BINFONT_MMAP_CACHE_SIZE) ? need_size : EW_BINFONT_MMAP_CACHE_SIZE;
        ctx->cache_buf = (uint8_t *)lv_malloc(alloc_size);
        ctx->cache_size = ctx->cache_buf ? alloc_size : 0;
        ctx->cache_gid = UINT32_MAX;
        ctx->cache_len = 0;
    }
}

static void binfont_mmap_copy_bitmap(binfont_mmap_ctx_t * ctx, uint32_t gid, uint32_t bmp_size)
{
    if(!ctx || !ctx->cache_buf || bmp_size == 0) return;

    const uint8_t * glyph_ptr = ctx->base + ctx->glyf_start + ctx->glyph_offset[gid];

    if(ctx->nbits_rem == 0) {
        const uint8_t * src = glyph_ptr + (ctx->nbits / 8U);
        lv_memcpy(ctx->cache_buf, src, bmp_size);
        return;
    }

    bit_iterator_mem_t bit_it = init_bit_iterator_mem(glyph_ptr);
    (void)read_bits_mem(&bit_it, (int)ctx->nbits);

    if(bmp_size == 1) {
        ctx->cache_buf[0] = (uint8_t)(read_bits_mem(&bit_it, 8 - ctx->nbits_rem) << ctx->nbits_rem);
        return;
    }

    for(uint32_t k = 0; k < bmp_size - 1; ++k) {
        ctx->cache_buf[k] = (uint8_t)read_bits_mem(&bit_it, 8);
    }

    ctx->cache_buf[bmp_size - 1] =
        (uint8_t)(read_bits_mem(&bit_it, 8 - ctx->nbits_rem) << ctx->nbits_rem);
}

static const void * binfont_mmap_get_glyph_bitmap(lv_font_glyph_dsc_t * g_dsc, lv_draw_buf_t * draw_buf)
{
    const lv_font_t * font = g_dsc->resolved_font;
    lv_font_fmt_txt_dsc_t * fdsc = (lv_font_fmt_txt_dsc_t *)font->dsc;
    binfont_mmap_ctx_t * ctx = (binfont_mmap_ctx_t *)font->user_data;

    if(!ctx || ctx->magic != BINFONT_MMAP_MAGIC) {
        return lv_font_get_bitmap_fmt_txt(g_dsc, draw_buf);
    }

    uint32_t gid = g_dsc->gid.index;
    if(!gid || gid >= ctx->loca_count) return NULL;

    const lv_font_fmt_txt_glyph_dsc_t * gdsc = &fdsc->glyph_dsc[gid];
    int32_t gsize = (int32_t)gdsc->box_w * gdsc->box_h;
    if(gsize == 0) return NULL;

    uint32_t next_offset = (gid < ctx->loca_count - 1) ? ctx->glyph_offset[gid + 1] : ctx->glyf_length;
    uint32_t bmp_size = next_offset - ctx->glyph_offset[gid] - (ctx->nbits / 8U);
    if(bmp_size == 0) return NULL;

    if(ctx->cache_gid != gid || ctx->cache_len != bmp_size) {
        binfont_mmap_ensure_cache(ctx, bmp_size);
        if(ctx->cache_buf == NULL) return NULL;
        binfont_mmap_copy_bitmap(ctx, gid, bmp_size);
        ctx->cache_gid = gid;
        ctx->cache_len = bmp_size;
    }

    if(g_dsc->req_raw_bitmap) {
        return ctx->cache_buf;
    }

    const uint8_t * orig_bitmap = fdsc->glyph_bitmap;
    uint32_t orig_index = ((lv_font_fmt_txt_glyph_dsc_t *)gdsc)->bitmap_index;
    fdsc->glyph_bitmap = ctx->cache_buf;
    ((lv_font_fmt_txt_glyph_dsc_t *)gdsc)->bitmap_index = 0;

    const void * out = lv_font_get_bitmap_fmt_txt(g_dsc, draw_buf);

    ((lv_font_fmt_txt_glyph_dsc_t *)gdsc)->bitmap_index = orig_index;
    fdsc->glyph_bitmap = orig_bitmap;
    return out;
}
#endif

/*
 * Loads a `lv_font_t` from a binary file, given a `lv_fs_file_t`.
 *
 * Memory allocations on `lvgl_load_font` should be immediately zeroed and
 * the pointer should be set on the `lv_font_t` data before any possible return.
 *
 * When something fails, it returns `false` and the memory on the `lv_font_t`
 * still needs to be freed using `lv_binfont_destroy`.
 *
 * `lv_binfont_destroy` will assume that all non-null pointers are allocated and
 * should be freed.
 */
static bool lvgl_load_font(lv_fs_file_t * fp, lv_font_t * font)
{
    lv_font_fmt_txt_dsc_t * font_dsc = (lv_font_fmt_txt_dsc_t *)
                                       lv_malloc(sizeof(lv_font_fmt_txt_dsc_t));

    lv_memset(font_dsc, 0, sizeof(lv_font_fmt_txt_dsc_t));

    font->dsc = font_dsc;

    /*header*/
    int32_t header_length = read_label(fp, 0, "head");
    if(header_length < 0) {
        return false;
    }

    font_header_bin_t font_header;
    if(lv_fs_read(fp, &font_header, sizeof(font_header_bin_t), NULL) != LV_FS_RES_OK) {
        return false;
    }

    font->base_line = -font_header.descent;
    font->line_height = font_header.ascent - font_header.descent;
    font->get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt;
    font->get_glyph_bitmap = lv_font_get_bitmap_fmt_txt;
    font->subpx = font_header.subpixels_mode;
    font->underline_position = (int8_t) font_header.underline_position;
    font->underline_thickness = (int8_t) font_header.underline_thickness;

    font_dsc->bpp = font_header.bits_per_pixel;
    font_dsc->kern_scale = font_header.kerning_scale;
    font_dsc->bitmap_format = font_header.compression_id;

    /*cmaps*/
    uint32_t cmaps_start = header_length;
    int32_t cmaps_length = load_cmaps(fp, font_dsc, cmaps_start);
    if(cmaps_length < 0) {
        return false;
    }

    /*loca*/
    uint32_t loca_start = cmaps_start + cmaps_length;
    int32_t loca_length = read_label(fp, loca_start, "loca");
    if(loca_length < 0) {
        return false;
    }

    uint32_t loca_count;
    if(lv_fs_read(fp, &loca_count, sizeof(uint32_t), NULL) != LV_FS_RES_OK) {
        return false;
    }

    bool failed = false;
    uint32_t * glyph_offset = lv_malloc(sizeof(uint32_t) * (loca_count + 1));

    if(font_header.index_to_loc_format == 0) {
        for(unsigned int i = 0; i < loca_count; ++i) {
            uint16_t offset;
            if(lv_fs_read(fp, &offset, sizeof(uint16_t), NULL) != LV_FS_RES_OK) {
                failed = true;
                break;
            }
            glyph_offset[i] = offset;
        }
    }
    else if(font_header.index_to_loc_format == 1) {
        if(lv_fs_read(fp, glyph_offset, loca_count * sizeof(uint32_t), NULL) != LV_FS_RES_OK) {
            failed = true;
        }
    }
    else {
        LV_LOG_WARN("Unknown index_to_loc_format: %d.", font_header.index_to_loc_format);
        failed = true;
    }

    if(failed) {
        lv_free(glyph_offset);
        return false;
    }

    /*glyph*/
    uint32_t glyph_start = loca_start + loca_length;
    #if EW_BINFONT_MMAP_ZERO_COPY
    binfont_mmap_ctx_t * mmap_ctx = NULL;
    if(fp && fp->drv && fp->drv->cache_size == LV_FS_CACHE_FROM_BUFFER && fp->cache && fp->cache->buffer) {
        mmap_ctx = lv_malloc_zeroed(sizeof(binfont_mmap_ctx_t));
        if(mmap_ctx) {
            mmap_ctx->magic = BINFONT_MMAP_MAGIC;
            mmap_ctx->base = (const uint8_t *)fp->cache->buffer;
            mmap_ctx->size = fp->cache->end;
            mmap_ctx->cache_gid = UINT32_MAX;
            mmap_ctx->cache_buf = NULL;
            mmap_ctx->cache_size = 0;
            mmap_ctx->cache_len = 0;
            mmap_ctx->nbits = (uint32_t)(font_header.advance_width_bits +
                                         2U * font_header.xy_bits +
                                         2U * font_header.wh_bits);
            mmap_ctx->nbits_rem = (uint8_t)(mmap_ctx->nbits % 8U);
        }
    }
    #endif

    int32_t glyph_length = load_glyph(
                               fp, font_dsc, glyph_start, glyph_offset, loca_count, &font_header
    #if EW_BINFONT_MMAP_ZERO_COPY
                               , mmap_ctx
    #endif
                               );
    if(glyph_length < 0) {
        #if EW_BINFONT_MMAP_ZERO_COPY
        if(mmap_ctx) {
            if(mmap_ctx->cache_buf) lv_free(mmap_ctx->cache_buf);
            lv_free(mmap_ctx);
        }
        #endif
        lv_free(glyph_offset);
        return false;
    }

    #if EW_BINFONT_MMAP_ZERO_COPY
    if(mmap_ctx) {
        mmap_ctx->glyf_start = glyph_start;
        mmap_ctx->glyf_length = (uint32_t)glyph_length;
        mmap_ctx->loca_count = loca_count;
        mmap_ctx->glyph_offset = glyph_offset;
        font->user_data = mmap_ctx;
        font->get_glyph_bitmap = binfont_mmap_get_glyph_bitmap;
    }
    else {
        lv_free(glyph_offset);
    }
    #else
    lv_free(glyph_offset);
    #endif

    /*kerning*/
    if(font_header.tables_count < 4) {
        font_dsc->kern_dsc = NULL;
        font_dsc->kern_classes = 0;
        font_dsc->kern_scale = 0;
        return true;
    }

    uint32_t kern_start = glyph_start + glyph_length;

    int32_t kern_length = load_kern(fp, font_dsc, font_header.glyph_id_format, kern_start);

    return kern_length >= 0;
}

int32_t load_kern(lv_fs_file_t * fp, lv_font_fmt_txt_dsc_t * font_dsc, uint8_t format, uint32_t start)
{
    int32_t kern_length = read_label(fp, start, "kern");
    if(kern_length < 0) {
        return -1;
    }

    uint8_t kern_format_type;
    int32_t padding;
    if(lv_fs_read(fp, &kern_format_type, sizeof(uint8_t), NULL) != LV_FS_RES_OK ||
       lv_fs_read(fp, &padding, 3 * sizeof(uint8_t), NULL) != LV_FS_RES_OK) {
        return -1;
    }

    if(0 == kern_format_type) { /*sorted pairs*/
        lv_font_fmt_txt_kern_pair_t * kern_pair = lv_malloc(sizeof(lv_font_fmt_txt_kern_pair_t));

        lv_memset(kern_pair, 0, sizeof(lv_font_fmt_txt_kern_pair_t));

        font_dsc->kern_dsc = kern_pair;
        font_dsc->kern_classes = 0;

        uint32_t glyph_entries;
        if(lv_fs_read(fp, &glyph_entries, sizeof(uint32_t), NULL) != LV_FS_RES_OK) {
            return -1;
        }

        int ids_size;
        if(format == 0) {
            ids_size = sizeof(int8_t) * 2 * glyph_entries;
        }
        else {
            ids_size = sizeof(int16_t) * 2 * glyph_entries;
        }

        uint8_t * glyph_ids = lv_malloc(ids_size);
        int8_t * values = lv_malloc(glyph_entries);

        kern_pair->glyph_ids_size = format;
        kern_pair->pair_cnt = glyph_entries;
        kern_pair->glyph_ids = glyph_ids;
        kern_pair->values = values;

        if(lv_fs_read(fp, glyph_ids, ids_size, NULL) != LV_FS_RES_OK) {
            return -1;
        }

        if(lv_fs_read(fp, values, glyph_entries, NULL) != LV_FS_RES_OK) {
            return -1;
        }
    }
    else if(3 == kern_format_type) { /*array M*N of classes*/

        lv_font_fmt_txt_kern_classes_t * kern_classes = lv_malloc(sizeof(lv_font_fmt_txt_kern_classes_t));

        lv_memset(kern_classes, 0, sizeof(lv_font_fmt_txt_kern_classes_t));

        font_dsc->kern_dsc = kern_classes;
        font_dsc->kern_classes = 1;

        uint16_t kern_class_mapping_length;
        uint8_t kern_table_rows;
        uint8_t kern_table_cols;

        if(lv_fs_read(fp, &kern_class_mapping_length, sizeof(uint16_t), NULL) != LV_FS_RES_OK ||
           lv_fs_read(fp, &kern_table_rows, sizeof(uint8_t), NULL) != LV_FS_RES_OK ||
           lv_fs_read(fp, &kern_table_cols, sizeof(uint8_t), NULL) != LV_FS_RES_OK) {
            return -1;
        }

        int kern_values_length = sizeof(int8_t) * kern_table_rows * kern_table_cols;

        uint8_t * kern_left = lv_malloc(kern_class_mapping_length);
        uint8_t * kern_right = lv_malloc(kern_class_mapping_length);
        int8_t * kern_values = lv_malloc(kern_values_length);

        kern_classes->left_class_mapping  = kern_left;
        kern_classes->right_class_mapping = kern_right;
        kern_classes->left_class_cnt = kern_table_rows;
        kern_classes->right_class_cnt = kern_table_cols;
        kern_classes->class_pair_values = kern_values;

        if(lv_fs_read(fp, kern_left, kern_class_mapping_length, NULL) != LV_FS_RES_OK ||
           lv_fs_read(fp, kern_right, kern_class_mapping_length, NULL) != LV_FS_RES_OK ||
           lv_fs_read(fp, kern_values, kern_values_length, NULL) != LV_FS_RES_OK) {
            return -1;
        }
    }
    else {
        LV_LOG_WARN("Unknown kern_format_type: %d", kern_format_type);
        return -1;
    }

    return kern_length;
}

static lv_font_t * binfont_font_create_cb(const lv_font_info_t * info, const void * src)
{
    const lv_binfont_font_src_t * font_src = src;

    if(info->size == font_src->font_size) {
        if(font_src->path) {
            return lv_binfont_create(font_src->path);
        }
#if LV_USE_FS_MEMFS
        return lv_binfont_create_from_buffer((void *)font_src->buffer, font_src->buffer_size);
#else
        LV_LOG_WARN("LV_USE_FS_MEMFS not enabled");
        return NULL;
#endif
    }

    return NULL;
}

static void binfont_font_delete_cb(lv_font_t * font)
{
    lv_binfont_destroy(font);
}

static void * binfont_font_dup_src_cb(const void * src)
{
    const lv_binfont_font_src_t * font_src = src;

    lv_binfont_font_src_t * new_src = lv_malloc_zeroed(sizeof(lv_binfont_font_src_t));
    LV_ASSERT_MALLOC(new_src);
    *new_src = *font_src;

    if(font_src->path) {
        new_src->path = lv_strdup(font_src->path);
    }

    return new_src;
}

static void binfont_font_free_src_cb(void * src)
{
    lv_binfont_font_src_t * font_src = src;
    if(font_src->path) {
        lv_free((char *)font_src->path);
        font_src->path = NULL;
    }

    lv_free(font_src);
}
