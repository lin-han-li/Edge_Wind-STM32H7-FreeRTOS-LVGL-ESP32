#ifndef GUI_IME_PINYIN_H
#define GUI_IME_PINYIN_H

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

bool gui_ime_pinyin_attach(lv_obj_t * kb);
bool gui_ime_pinyin_dict_ready(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* GUI_IME_PINYIN_H */
