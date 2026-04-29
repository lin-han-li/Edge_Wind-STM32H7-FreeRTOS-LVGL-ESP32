#ifndef GUI_ASSETS_H
#define GUI_ASSETS_H

#include <stdint.h>
#include "lvgl.h"
#include "src/generated/gui_guider.h"

void gui_assets_init(void);
const lv_font_t * gui_assets_get_font_12(void);
const lv_font_t * gui_assets_get_font_14(void);
const lv_font_t * gui_assets_get_font_16(void);
const lv_font_t * gui_assets_get_font_20(void);
const lv_font_t * gui_assets_get_font_30(void);
void gui_assets_set_icon(lv_obj_t * img, uint32_t icon_index);
void gui_assets_patch_images(lv_ui * ui);

#endif /* GUI_ASSETS_H */
