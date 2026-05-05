/*
 * Copyright 2026 NXP
 * EdgeWind Realtime Monitor Screen - LVGL 9.4.0
 */

#include "lvgl.h"
#include "gui_guider.h"
#include "events_init.h"
#include "../../gui_assets.h"
#include "../../../EdgeComm/edge_comm.h"
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

static lv_timer_t *s_rtmon_timer = NULL;
static lv_obj_t *s_wave_chart = NULL;
static lv_obj_t *s_lbl_wave_title = NULL;
static lv_obj_t *s_lbl_wave_meta = NULL;
static lv_obj_t *s_mode_btns[2];
static lv_obj_t *s_mode_labels[2];
static lv_obj_t *s_switch_cards[4];
static lv_obj_t *s_switch_labels[4];
static lv_chart_series_t *s_wave_series = NULL;
static uint32_t s_active_ch = 0U;
static bool s_rtmon_manual_view = false;
static uint32_t s_wave_window_start = 0U;
static int32_t s_wave_axis_offset = 0;
static int32_t s_drag_x_accum_px = 0;
static int32_t s_drag_y_accum_px = 0;

typedef enum {
    RTMON_VIEW_WAVE = 0,
    RTMON_VIEW_FFT = 1,
} rtmon_view_t;

static rtmon_view_t s_view_mode = RTMON_VIEW_WAVE;

#define RTMON_W 800
#define RTMON_H 480
#define RTMON_HEADER_H 60
#define RTMON_CH_COUNT 4U
#define RTMON_WAVE_SOURCE_POINTS 1024U
#define RTMON_FFT_SOURCE_POINTS 2048U
#define RTMON_CHART_POINTS 256U
#define RTMON_REFRESH_MS 1000U
#define RTMON_WAVE_AXIS_SCALE 100.0f

#define RT_COL_BLUE    0x2563EB
#define RT_COL_CYAN    0x0891B2
#define RT_COL_GREEN   0x059669
#define RT_COL_AMBER   0xD97706
#define RT_COL_RED     0xE11D48
#define RT_COL_PURPLE  0x7C3AED
#define RT_COL_HEADER  0x0F172A
#define RT_COL_SLATE   0x334155
#define RT_COL_MUTED   0x64748B
#define RT_COL_GRID    0xCBD5E1
#define RT_COL_CARD_BG 0xF8FAFC
#define RT_COL_SCREEN  0xF0F9FF
#define RT_COL_TOPBAR  0xD7F1FF
#define RT_COL_TITLE   0x0F4C81
#define RT_COL_CHART_BG     0x203A4F
#define RT_COL_CHART_GRID   0x6B8BA8
#define RT_COL_CHART_BORDER 0x6EA9D6

static int32_t s_wave_y[RTMON_CHART_POINTS];

static const uint32_t s_ch_colors[RTMON_CH_COUNT] = {
    RT_COL_BLUE, RT_COL_GREEN, RT_COL_AMBER, RT_COL_PURPLE
};

static const uint32_t s_ch_plot_colors[RTMON_CH_COUNT] = {
    0x60A5FA, 0x34D399, 0xFBBF24, 0xC084FC
};

static const uint32_t s_ch_dark_colors[RTMON_CH_COUNT] = {
    0x1D4ED8, 0x047857, 0xB45309, 0x6D28D9
};

static const uint32_t s_ch_soft_colors[RTMON_CH_COUNT] = {
    0xEFF6FF, 0xECFDF5, 0xFFFBEB, 0xF5F3FF
};

static const float s_ch_wave_axis_min[RTMON_CH_COUNT] = {
    -10.0f, -10.0f, -10.0f, -10.0f
};

static const float s_ch_wave_axis_max[RTMON_CH_COUNT] = {
    10.0f, 10.0f, 10.0f, 10.0f
};

static void rtmon_update_wave_chart(void);
static void rtmon_update_switch_cards(void);
static void rtmon_update_mode_buttons(void);
static void rtmon_reset_view(void);
static void rtmon_chart_event_cb(lv_event_t *e);
static void rtmon_mode_event_cb(lv_event_t *e);
static void rtmon_switch_event_cb(lv_event_t *e);

static void rtmon_set_panel_style(lv_obj_t *obj, uint32_t bg, lv_opa_t opa)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(RT_COL_GRID), LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, 185, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 16, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *rtmon_create_panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                                    const char *title, lv_obj_t **body_label)
{
    lv_obj_t *panel = lv_obj_create(parent);
    rtmon_set_panel_style(panel, 0xFFFFFF, 225);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);

    lv_obj_t *lbl_title = lv_label_create(panel);
    lv_label_set_text(lbl_title, title ? title : "");
    lv_obj_set_style_text_font(lbl_title, gui_assets_get_font_16(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(RT_COL_TITLE), LV_PART_MAIN);
    lv_obj_set_pos(lbl_title, 14, 9);

    if (body_label) {
        lv_obj_t *lbl_body = lv_label_create(panel);
        lv_label_set_text(lbl_body, "--");
        lv_obj_set_width(lbl_body, w - 28);
        lv_label_set_long_mode(lbl_body, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(lbl_body, gui_assets_get_font_16(), LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_body, lv_color_hex(RT_COL_SLATE), LV_PART_MAIN);
        lv_obj_set_pos(lbl_body, 14, 36);
        *body_label = lbl_body;
    }
    return panel;
}

static lv_obj_t *rtmon_create_status_dot(lv_obj_t *parent, const char *text, uint32_t color)
{
    lv_obj_t *item = lv_obj_create(parent);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(item, 5, 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = lv_obj_create(item);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, 255, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(item);
    lv_label_set_text(lbl, text ? text : "");
    lv_obj_set_style_text_font(lbl, gui_assets_get_font_16(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(RT_COL_SLATE), LV_PART_MAIN);
    return item;
}

static lv_obj_t *rtmon_create_mode_button(lv_obj_t *parent, rtmon_view_t mode, const char *text)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 86, 32);
    lv_obj_set_pos(btn, 230 + (int32_t)mode * 96, 14);
    lv_obj_set_style_radius(btn, 16, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, rtmon_mode_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)mode);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text ? text : "");
    lv_obj_set_style_text_font(lbl, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_center(lbl);

    s_mode_btns[(uint32_t)mode] = btn;
    s_mode_labels[(uint32_t)mode] = lbl;
    return btn;
}

static lv_obj_t *rtmon_create_channel_row(lv_obj_t *parent, uint32_t index)
{
    lv_obj_t *row = lv_obj_create(parent);
    int32_t col = (int32_t)(index & 1U);
    int32_t line = (int32_t)(index >> 1U);

    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 96, 80);
    lv_obj_set_pos(row, 12 + col * 104, 42 + line * 92);
    lv_obj_set_style_bg_color(row, lv_color_hex(s_ch_soft_colors[index]), 0);
    lv_obj_set_style_bg_opa(row, 238, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(s_ch_dark_colors[index]), 0);
    lv_obj_set_style_border_opa(row, 120, 0);
    lv_obj_set_style_pad_all(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, rtmon_switch_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)index);

    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 36, 4);
    lv_obj_set_pos(dot, 8, 8);
    lv_obj_set_style_radius(dot, 2, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(s_ch_dark_colors[index]), 0);
    lv_obj_set_style_bg_opa(dot, 255, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, "CH --");
    lv_obj_set_width(lbl, 84);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(lbl, gui_assets_get_font_12(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(RT_COL_SLATE), 0);
    lv_obj_set_pos(lbl, 8, 22);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lbl, rtmon_switch_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)index);

    s_switch_cards[index] = row;
    s_switch_labels[index] = lbl;
    return lbl;
}

static void rtmon_style_wave_chart(lv_obj_t *chart)
{
    lv_obj_remove_style_all(chart);
    lv_obj_set_style_bg_color(chart, lv_color_hex(RT_COL_CHART_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chart, 246, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(chart, lv_color_hex(RT_COL_CHART_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(chart, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart, 8, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, lv_color_hex(RT_COL_CHART_GRID), LV_PART_MAIN);
    lv_obj_set_style_line_opa(chart, 118, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_CLICKABLE);
}

static void rtmon_switch_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    uint32_t ch = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (ch >= RTMON_CH_COUNT) {
        return;
    }
    s_active_ch = ch;
    rtmon_reset_view();
    rtmon_update_switch_cards();
    rtmon_update_wave_chart();
}

static void rtmon_mode_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    uint32_t mode = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (mode > (uint32_t)RTMON_VIEW_FFT) {
        return;
    }
    s_view_mode = (rtmon_view_t)mode;
    rtmon_reset_view();
    rtmon_update_mode_buttons();
    rtmon_update_wave_chart();
}

static const char *rtmon_bool_text(bool ok)
{
    return ok ? "ON" : "OFF";
}

static float rtmon_safe_float(float value)
{
    if (value != value || value > 1000000000.0f || value < -1000000000.0f) {
        return 0.0f;
    }
    return value;
}

static int32_t rtmon_clamp_i32(int32_t value, int32_t min_value, int32_t max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int32_t rtmon_to_axis_i32(float value)
{
    float scaled = rtmon_safe_float(value) * RTMON_WAVE_AXIS_SCALE;
    return (int32_t)((scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f));
}

static void rtmon_wave_axis_range(uint32_t ch, float *axis_min, float *axis_max)
{
    if (ch >= RTMON_CH_COUNT) {
        ch = 0U;
    }

    if (axis_min) {
        *axis_min = s_ch_wave_axis_min[ch];
    }
    if (axis_max) {
        *axis_max = s_ch_wave_axis_max[ch];
    }
}

static void rtmon_format_fixed_2(char *buf, size_t len, float value)
{
    if (!buf || len == 0U) {
        return;
    }
    int negative = (value < 0.0f) ? 1 : 0;
    float abs_value = negative ? -value : value;
    int32_t scaled = (int32_t)(abs_value * 100.0f + 0.5f);
    int32_t whole = scaled / 100;
    int32_t frac = scaled % 100;
    snprintf(buf, len, "%s%ld.%02ld",
             negative ? "-" : "",
             (long)whole,
             (long)frac);
}

static uint32_t rtmon_source_wave_points(void)
{
    return ((uint32_t)WAVEFORM_POINTS < RTMON_WAVE_SOURCE_POINTS) ?
           (uint32_t)WAVEFORM_POINTS : RTMON_WAVE_SOURCE_POINTS;
}

static uint32_t rtmon_source_fft_points(void)
{
    return ((uint32_t)FFT_POINTS < RTMON_FFT_SOURCE_POINTS) ?
           (uint32_t)FFT_POINTS : RTMON_FFT_SOURCE_POINTS;
}

static uint32_t rtmon_visible_points(uint32_t source_count)
{
    if (source_count == 0U) {
        return 1U;
    }
    return (source_count < RTMON_CHART_POINTS) ? source_count : RTMON_CHART_POINTS;
}

static uint32_t rtmon_window_max(uint32_t source_count)
{
    uint32_t visible = rtmon_visible_points(source_count);
    return (source_count > visible) ? (source_count - visible) : 0U;
}

static void rtmon_clamp_current_window(uint32_t source_count)
{
    uint32_t max_start = rtmon_window_max(source_count);
    if (s_wave_window_start > max_start) {
        s_wave_window_start = max_start;
    }
}

static void rtmon_reset_view(void)
{
    s_rtmon_manual_view = false;
    s_wave_window_start = 0U;
    s_wave_axis_offset = 0;
    s_drag_x_accum_px = 0;
    s_drag_y_accum_px = 0;
}

static void rtmon_chart_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_DOUBLE_CLICKED) {
        rtmon_reset_view();
        rtmon_update_wave_chart();
        return;
    }

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_drag_x_accum_px = 0;
        s_drag_y_accum_px = 0;
        return;
    }

    if (code != LV_EVENT_PRESSING) {
        return;
    }

    lv_indev_t *indev = lv_indev_active();
    if (!indev || !s_wave_chart || !lv_obj_is_valid(s_wave_chart)) {
        return;
    }

    lv_point_t vect;
    lv_indev_get_vect(indev, &vect);
    if (vect.x == 0 && vect.y == 0) {
        return;
    }

    uint32_t source_count = (s_view_mode == RTMON_VIEW_FFT) ?
                            rtmon_source_fft_points() : rtmon_source_wave_points();
    if (source_count == 0U) {
        source_count = 1U;
    }
    uint32_t visible = rtmon_visible_points(source_count);
    int32_t chart_w = lv_obj_get_width(s_wave_chart);
    int32_t chart_h = lv_obj_get_height(s_wave_chart);
    if (chart_w <= 0) {
        chart_w = 1;
    }
    if (chart_h <= 0) {
        chart_h = 1;
    }

    s_rtmon_manual_view = true;

    /* Finger left means later samples; finger right means earlier samples. */
    s_drag_x_accum_px += -vect.x;
    int32_t step_points = (s_drag_x_accum_px * (int32_t)visible) / chart_w;
    if (step_points != 0) {
        int32_t next_start = (int32_t)s_wave_window_start + step_points;
        int32_t max_start = (int32_t)rtmon_window_max(source_count);
        next_start = rtmon_clamp_i32(next_start, 0, max_start);
        s_wave_window_start = (uint32_t)next_start;
        s_drag_x_accum_px -= (step_points * chart_w) / (int32_t)visible;
    }

    if (s_view_mode == RTMON_VIEW_WAVE) {
        float axis_min = 0.0f;
        float axis_max = 1.0f;
        rtmon_wave_axis_range(s_active_ch, &axis_min, &axis_max);
        int32_t axis_span = rtmon_to_axis_i32(axis_max) - rtmon_to_axis_i32(axis_min);
        if (axis_span <= 0) {
            axis_span = 1;
        }

        s_drag_y_accum_px += vect.y;
        int32_t step_axis = (s_drag_y_accum_px * axis_span) / chart_h;
        if (step_axis != 0) {
            s_wave_axis_offset += step_axis;
            s_wave_axis_offset = rtmon_clamp_i32(s_wave_axis_offset, -axis_span, axis_span);
            s_drag_y_accum_px -= (step_axis * chart_h) / axis_span;
        }
    } else {
        s_drag_y_accum_px = 0;
    }

    rtmon_update_wave_chart();
}

static void rtmon_wave_stats(uint32_t ch, float *min_out, float *max_out, float *mean_out)
{
    uint32_t count = rtmon_source_wave_points();
    if (count == 0U) {
        count = 1U;
    }
    float mn = rtmon_safe_float(node_channels[ch].waveform[0]);
    float mx = mn;
    double sum = 0.0;

    for (uint32_t i = 0; i < count; i++) {
        float v = rtmon_safe_float(node_channels[ch].waveform[i]);
        if (v < mn) {
            mn = v;
        }
        if (v > mx) {
            mx = v;
        }
        sum += (double)v;
    }

    if (min_out) {
        *min_out = mn;
    }
    if (max_out) {
        *max_out = mx;
    }
    if (mean_out) {
        *mean_out = rtmon_safe_float((float)(sum / (double)count));
    }
}

static void rtmon_fft_stats(uint32_t ch, float *max_out, uint32_t *max_index_out, float *mean_out)
{
    uint32_t count = rtmon_source_fft_points();
    if (count == 0U) {
        count = 1U;
    }

    float mx = rtmon_safe_float(node_channels[ch].fft_data[0]);
    uint32_t peak_index = 0U;
    double sum = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        float v = rtmon_safe_float(node_channels[ch].fft_data[i]);
        if (v < 0.0f) {
            v = 0.0f;
        }
        if (v > mx) {
            mx = v;
            peak_index = i;
        }
        sum += (double)v;
    }

    if (max_out) {
        *max_out = mx;
    }
    if (max_index_out) {
        *max_index_out = peak_index;
    }
    if (mean_out) {
        *mean_out = rtmon_safe_float((float)(sum / (double)count));
    }
}

static void rtmon_update_switch_cards(void)
{
    for (uint32_t ch = 0; ch < RTMON_CH_COUNT; ch++) {
        if (!s_switch_cards[ch] || !lv_obj_is_valid(s_switch_cards[ch])) {
            continue;
        }
        bool active = (ch == s_active_ch);
        lv_obj_set_style_bg_color(s_switch_cards[ch],
                                  lv_color_hex(active ? s_ch_dark_colors[ch] : s_ch_soft_colors[ch]), 0);
        lv_obj_set_style_bg_opa(s_switch_cards[ch], active ? 248 : 238, 0);
        lv_obj_set_style_border_color(s_switch_cards[ch], lv_color_hex(s_ch_dark_colors[ch]), 0);
        lv_obj_set_style_border_opa(s_switch_cards[ch], active ? 255 : 130, 0);
        lv_obj_set_style_border_width(s_switch_cards[ch], active ? 2 : 1, 0);
        if (s_switch_labels[ch] && lv_obj_is_valid(s_switch_labels[ch])) {
            lv_obj_set_style_text_color(s_switch_labels[ch],
                                        lv_color_hex(active ? 0xFFFFFF : RT_COL_SLATE), 0);
        }
    }
}

static void rtmon_update_mode_buttons(void)
{
    for (uint32_t i = 0; i < 2U; i++) {
        if (!s_mode_btns[i] || !lv_obj_is_valid(s_mode_btns[i])) {
            continue;
        }

        bool active = (i == (uint32_t)s_view_mode);
        uint32_t active_color = (s_view_mode == RTMON_VIEW_FFT) ? RT_COL_PURPLE : s_ch_dark_colors[s_active_ch];
        lv_obj_set_style_bg_color(s_mode_btns[i],
                                  lv_color_hex(active ? active_color : 0xFFFFFF),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_mode_btns[i], active ? 245 : 235, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_mode_btns[i],
                                      lv_color_hex(active ? active_color : RT_COL_CHART_BORDER),
                                      LV_PART_MAIN);
        lv_obj_set_style_border_opa(s_mode_btns[i], active ? 255 : 180, LV_PART_MAIN);
        if (s_mode_labels[i] && lv_obj_is_valid(s_mode_labels[i])) {
            lv_obj_set_style_text_color(s_mode_labels[i],
                                        lv_color_hex(active ? 0xFFFFFF : RT_COL_SLATE),
                                        LV_PART_MAIN);
        }
    }
}

static void rtmon_update_wave_chart(void)
{
    if (!s_wave_chart || !lv_obj_is_valid(s_wave_chart) || !s_wave_series) {
        return;
    }

    uint32_t ch = s_active_ch;
    if (ch >= RTMON_CH_COUNT) {
        ch = 0U;
        s_active_ch = 0U;
    }

    float mn = 0.0f, mx = 0.0f, mean = 0.0f;
    uint32_t source_count = 1U;
    uint32_t visible = 1U;
    uint32_t window_start = 0U;
    uint32_t window_end = 0U;
    uint32_t peak_index = 0U;
    const bool fft_mode = (s_view_mode == RTMON_VIEW_FFT);

    if (fft_mode) {
        rtmon_fft_stats(ch, &mx, &peak_index, &mean);
        source_count = rtmon_source_fft_points();
        if (source_count == 0U) {
            source_count = 1U;
        }
        rtmon_clamp_current_window(source_count);
        visible = rtmon_visible_points(source_count);
        window_start = s_wave_window_start;
        window_end = window_start + visible - 1U;
        float scale = (mx < 0.000001f) ? 1.0f : mx;
        for (uint32_t i = 0; i < RTMON_CHART_POINTS; i++) {
            uint32_t src_i = window_start + ((i < visible) ? i : (visible - 1U));
            if (src_i >= source_count) {
                src_i = source_count - 1U;
            }
            float raw = rtmon_safe_float(node_channels[ch].fft_data[src_i]);
            if (raw < 0.0f) {
                raw = 0.0f;
            }
            int32_t y = (int32_t)((raw * 100.0f) / scale);
            s_wave_y[i] = rtmon_clamp_i32(y, 0, 100);
        }
        lv_chart_set_axis_range(s_wave_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
        lv_chart_set_div_line_count(s_wave_chart, 5, 8);
        lv_chart_set_series_color(s_wave_chart, s_wave_series, lv_color_hex(0xC084FC));
    } else {
        rtmon_wave_stats(ch, &mn, &mx, &mean);
        float axis_min = 0.0f;
        float axis_max = 1.0f;
        rtmon_wave_axis_range(ch, &axis_min, &axis_max);

        source_count = rtmon_source_wave_points();
        if (source_count == 0U) {
            source_count = 1U;
        }
        rtmon_clamp_current_window(source_count);
        visible = rtmon_visible_points(source_count);
        window_start = s_wave_window_start;
        window_end = window_start + visible - 1U;

        for (uint32_t i = 0; i < RTMON_CHART_POINTS; i++) {
            uint32_t src_i = window_start + ((i < visible) ? i : (visible - 1U));
            if (src_i >= source_count) {
                src_i = source_count - 1U;
            }
            float raw = rtmon_safe_float(node_channels[ch].waveform[src_i]);
            s_wave_y[i] = rtmon_to_axis_i32(raw);
        }
        lv_chart_set_axis_range(s_wave_chart, LV_CHART_AXIS_PRIMARY_Y,
                                rtmon_to_axis_i32(axis_min) + s_wave_axis_offset,
                                rtmon_to_axis_i32(axis_max) + s_wave_axis_offset);
        lv_chart_set_div_line_count(s_wave_chart, 5, 8);
        lv_chart_set_series_color(s_wave_chart, s_wave_series, lv_color_hex(s_ch_plot_colors[ch]));
    }
    lv_chart_refresh(s_wave_chart);

    if (s_lbl_wave_title && lv_obj_is_valid(s_lbl_wave_title)) {
        const char *name = (node_channels[ch].label[0] != '\0') ? node_channels[ch].label : "CH";
        lv_label_set_text_fmt(s_lbl_wave_title, "CH%lu %s %s",
                              (unsigned long)(ch + 1U), name, fft_mode ? "FFT" : "波形");
        lv_obj_set_style_text_color(s_lbl_wave_title,
                                    lv_color_hex(fft_mode ? 0xC084FC : s_ch_plot_colors[ch]),
                                    LV_PART_MAIN);
    }

    if (s_lbl_wave_meta && lv_obj_is_valid(s_lbl_wave_meta)) {
        const char *unit = (node_channels[ch].unit[0] != '\0') ? node_channels[ch].unit : "";
        char mean_buf[24];
        char value_buf[24];
        char range_min_buf[24];
        char range_max_buf[24];
        if (fft_mode) {
            rtmon_format_fixed_2(value_buf, sizeof(value_buf), mx);
            lv_label_set_text_fmt(s_lbl_wave_meta, "峰值 %s%s  峰点 #%lu  窗口 %lu-%lu",
                                  value_buf, unit, (unsigned long)peak_index,
                                  (unsigned long)window_start, (unsigned long)window_end);
        } else {
            float axis_min = 0.0f;
            float axis_max = 1.0f;
            rtmon_wave_axis_range(ch, &axis_min, &axis_max);
            float axis_offset = ((float)s_wave_axis_offset) / RTMON_WAVE_AXIS_SCALE;
            rtmon_format_fixed_2(mean_buf, sizeof(mean_buf), mean);
            rtmon_format_fixed_2(value_buf, sizeof(value_buf), mx - mn);
            rtmon_format_fixed_2(range_min_buf, sizeof(range_min_buf), axis_min + axis_offset);
            rtmon_format_fixed_2(range_max_buf, sizeof(range_max_buf), axis_max + axis_offset);
            lv_label_set_text_fmt(s_lbl_wave_meta, "均值 %s%s  峰峰值 %s%s  窗口 %lu-%lu  量程 %s-%s%s",
                                  mean_buf, unit, value_buf, unit,
                                  (unsigned long)window_start, (unsigned long)window_end,
                                  range_min_buf, range_max_buf, unit);
        }
    }

    rtmon_update_mode_buttons();
}

static void rtmon_refresh(lv_ui *ui, bool update_wave)
{
    if (!ui || !ui->RealtimeMonitor || !lv_obj_is_valid(ui->RealtimeMonitor)) {
        return;
    }

    bool wifi_ok = ESP_UI_IsWiFiOk();
    bool tcp_ok = ESP_UI_IsTcpOk();
    bool reg_ok = ESP_UI_IsRegOk();
    bool rep_ok = ESP_UI_IsReporting();
    if (rep_ok) {
        wifi_ok = true;
        tcp_ok = true;
        reg_ok = true;
    }

    if (ui->RealtimeMonitor_lbl_status && lv_obj_is_valid(ui->RealtimeMonitor_lbl_status)) {
        lv_label_set_text_fmt(ui->RealtimeMonitor_lbl_status, "WIFI:%s  TCP:%s  REG:%s  REP:%s",
                              rtmon_bool_text(wifi_ok), rtmon_bool_text(tcp_ok),
                              rtmon_bool_text(reg_ok), rtmon_bool_text(rep_ok));
        lv_obj_set_style_text_color(ui->RealtimeMonitor_lbl_status,
                                    rep_ok ? lv_color_hex(RT_COL_GREEN) : lv_color_hex(RT_COL_MUTED),
                                    LV_PART_MAIN);
    }

    if (ui->RealtimeMonitor_lbl_node && lv_obj_is_valid(ui->RealtimeMonitor_lbl_node)) {
        const char *id = ESP_UI_NodeId();
        lv_label_set_text_fmt(ui->RealtimeMonitor_lbl_node, "NODE:%s", id ? id : "--");
    }

    for (uint32_t i = 0; i < RTMON_CH_COUNT; i++) {
        const char *name = (node_channels[i].label[0] != '\0') ? node_channels[i].label : "CH";
        const char *unit = (node_channels[i].unit[0] != '\0') ? node_channels[i].unit : "";
        float mn = 0.0f, mx = 0.0f, mean = 0.0f;
        char mean_buf[24];
        char pp_buf[24];
        rtmon_wave_stats(i, &mn, &mx, &mean);
        rtmon_format_fixed_2(mean_buf, sizeof(mean_buf), mean);
        rtmon_format_fixed_2(pp_buf, sizeof(pp_buf), mx - mn);

        if (ui->RealtimeMonitor_lbl_ch[i] && lv_obj_is_valid(ui->RealtimeMonitor_lbl_ch[i])) {
            lv_label_set_text_fmt(ui->RealtimeMonitor_lbl_ch[i], "CH%lu\n%s\n%s%s",
                                  (unsigned long)(i + 1U), name, mean_buf, unit);
        }

        if (s_switch_labels[i] && s_switch_labels[i] != ui->RealtimeMonitor_lbl_ch[i] &&
            lv_obj_is_valid(s_switch_labels[i])) {
            lv_label_set_text_fmt(s_switch_labels[i], "CH%lu %s\n%s%s",
                                  (unsigned long)(i + 1U), name, mean_buf, unit);
        }
    }

    ESP_CommParams_t p;
    ESP_CommParams_Get(&p);
    if (ui->RealtimeMonitor_lbl_cloud && lv_obj_is_valid(ui->RealtimeMonitor_lbl_cloud)) {
        lv_label_set_text_fmt(ui->RealtimeMonitor_lbl_cloud,
                              "上报:%s  点数:%lu\n步长:%lu  超时:%lums",
                              ESP_ServerReportFull() ? "FULL" : "SUMMARY",
                              (unsigned long)p.upload_points,
                              (unsigned long)p.wave_step,
                              (unsigned long)p.http_timeout_ms);
    }

    if (ui->RealtimeMonitor_lbl_diag && lv_obj_is_valid(ui->RealtimeMonitor_lbl_diag)) {
        lv_label_set_text(ui->RealtimeMonitor_lbl_diag, rep_ok ? "状态: 上报中\n风险: 正常" : "状态: 待上报\n风险: 观察");
    }

    if (ui->RealtimeMonitor_lbl_event && lv_obj_is_valid(ui->RealtimeMonitor_lbl_event)) {
        lv_label_set_text(ui->RealtimeMonitor_lbl_event, "最近事件: 等待告警/服务器命令");
    }

    rtmon_update_switch_cards();
    if (update_wave) {
        rtmon_update_wave_chart();
    }
}

static void rtmon_timer_cb(lv_timer_t *t)
{
    lv_ui *ui = (lv_ui *)lv_timer_get_user_data(t);
    if (!ui || !ui->RealtimeMonitor || !lv_obj_is_valid(ui->RealtimeMonitor)) {
        s_rtmon_timer = NULL;
        lv_timer_delete(t);
        return;
    }

    if (lv_screen_active() != ui->RealtimeMonitor) {
        s_rtmon_timer = NULL;
        lv_timer_delete(t);
        return;
    }

    rtmon_refresh(ui, true);
}

static void rtmon_screen_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCREEN_LOADED) {
        lv_ui *ui = (lv_ui *)lv_event_get_user_data(e);
        rtmon_refresh(ui, false);
        if (!s_rtmon_timer) {
            s_rtmon_timer = lv_timer_create(rtmon_timer_cb, RTMON_REFRESH_MS, ui);
        }
    } else if (code == LV_EVENT_SCREEN_UNLOADED || code == LV_EVENT_DELETE) {
        if (s_rtmon_timer) {
            lv_timer_delete(s_rtmon_timer);
            s_rtmon_timer = NULL;
        }
        if (code == LV_EVENT_DELETE) {
            s_wave_chart = NULL;
            s_lbl_wave_title = NULL;
            s_lbl_wave_meta = NULL;
            s_wave_series = NULL;
            rtmon_reset_view();
            for (uint32_t i = 0; i < 2U; i++) {
                s_mode_btns[i] = NULL;
                s_mode_labels[i] = NULL;
            }
            for (uint32_t i = 0; i < RTMON_CH_COUNT; i++) {
                s_switch_cards[i] = NULL;
                s_switch_labels[i] = NULL;
            }
        }
    }
}

void setup_scr_RealtimeMonitor(lv_ui *ui)
{
    s_view_mode = RTMON_VIEW_WAVE;
    rtmon_reset_view();

    ui->RealtimeMonitor = lv_obj_create(NULL);
    lv_obj_remove_style_all(ui->RealtimeMonitor);
    lv_obj_set_size(ui->RealtimeMonitor, RTMON_W, RTMON_H);
    lv_obj_set_scrollbar_mode(ui->RealtimeMonitor, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(ui->RealtimeMonitor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui->RealtimeMonitor, lv_color_hex(RT_COL_SCREEN), 0);
    lv_obj_set_style_bg_opa(ui->RealtimeMonitor, 255, 0);

    lv_obj_t *header = lv_obj_create(ui->RealtimeMonitor);
    rtmon_set_panel_style(header, RT_COL_TOPBAR, 255);
    lv_obj_set_size(header, RTMON_W, RTMON_HEADER_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);

    lv_obj_t *bar = lv_obj_create(header);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 4, 18);
    lv_obj_set_style_bg_color(bar, lv_color_hex(RT_COL_CYAN), 0);
    lv_obj_set_style_bg_opa(bar, 255, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 20, 0);

    ui->RealtimeMonitor_lbl_title = lv_label_create(header);
    lv_label_set_text(ui->RealtimeMonitor_lbl_title, "实时监控");
    lv_obj_set_style_text_color(ui->RealtimeMonitor_lbl_title, lv_color_hex(0x0F4C81), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->RealtimeMonitor_lbl_title, gui_assets_get_font_30(), LV_PART_MAIN);
    lv_obj_align(ui->RealtimeMonitor_lbl_title, LV_ALIGN_LEFT_MID, 34, 0);

    (void)rtmon_create_mode_button(header, RTMON_VIEW_WAVE, "波形");
    (void)rtmon_create_mode_button(header, RTMON_VIEW_FFT, "FFT");

    lv_obj_t *status_pill = lv_obj_create(header);
    rtmon_set_panel_style(status_pill, 0xFFFFFF, 190);
    lv_obj_set_style_border_width(status_pill, 0, LV_PART_MAIN);
    lv_obj_set_size(status_pill, 354, 42);
    lv_obj_align(status_pill, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_flex_flow(status_pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_pill, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(status_pill, 8, 0);

    (void)rtmon_create_status_dot(status_pill, "WIFI", RT_COL_GREEN);
    (void)rtmon_create_status_dot(status_pill, "TCP", RT_COL_BLUE);
    (void)rtmon_create_status_dot(status_pill, "REG", RT_COL_AMBER);
    (void)rtmon_create_status_dot(status_pill, "REP", RT_COL_CYAN);

    ui->RealtimeMonitor_lbl_node = lv_label_create(ui->RealtimeMonitor);
    lv_label_set_text(ui->RealtimeMonitor_lbl_node, "NODE:--");
    lv_obj_set_style_text_color(ui->RealtimeMonitor_lbl_node, lv_color_hex(RT_COL_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->RealtimeMonitor_lbl_node, gui_assets_get_font_16(), LV_PART_MAIN);
    lv_obj_set_pos(ui->RealtimeMonitor_lbl_node, 26, 68);

    ui->RealtimeMonitor_lbl_status = lv_label_create(ui->RealtimeMonitor);
    lv_label_set_text(ui->RealtimeMonitor_lbl_status, "WIFI:--  TCP:--  REG:--  REP:--");
    lv_obj_set_style_text_color(ui->RealtimeMonitor_lbl_status, lv_color_hex(RT_COL_MUTED), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui->RealtimeMonitor_lbl_status, gui_assets_get_font_16(), LV_PART_MAIN);
    lv_obj_set_pos(ui->RealtimeMonitor_lbl_status, 496, 68);

    lv_obj_t *wave_panel = lv_obj_create(ui->RealtimeMonitor);
    lv_obj_remove_style_all(wave_panel);
    lv_obj_set_pos(wave_panel, 20, 96);
    lv_obj_set_size(wave_panel, 512, 366);
    lv_obj_set_scrollbar_mode(wave_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(wave_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_wave_chart = lv_chart_create(wave_panel);
    lv_obj_set_pos(s_wave_chart, 0, 8);
    lv_obj_set_size(s_wave_chart, 512, 318);
    rtmon_style_wave_chart(s_wave_chart);
    lv_obj_add_event_cb(s_wave_chart, rtmon_chart_event_cb, LV_EVENT_ALL, NULL);
    lv_chart_set_type(s_wave_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_wave_chart, RTMON_CHART_POINTS);
    lv_chart_set_axis_range(s_wave_chart, LV_CHART_AXIS_PRIMARY_Y, -100, 100);
    lv_chart_set_div_line_count(s_wave_chart, 5, 8);
    for (uint32_t i = 0; i < RTMON_CHART_POINTS; i++) {
        s_wave_y[i] = 0;
    }
    s_wave_series = lv_chart_add_series(s_wave_chart, lv_color_hex(s_ch_plot_colors[0]), LV_CHART_AXIS_PRIMARY_Y);
    if (s_wave_series) {
        lv_chart_set_series_ext_y_array(s_wave_chart, s_wave_series, s_wave_y);
    }

    s_lbl_wave_title = lv_label_create(s_wave_chart);
    lv_label_set_text(s_lbl_wave_title, "CH1");
    lv_obj_set_width(s_lbl_wave_title, 300);
    lv_label_set_long_mode(s_lbl_wave_title, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s_lbl_wave_title, gui_assets_get_font_16(), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_wave_title, lv_color_hex(s_ch_plot_colors[0]), LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_wave_title, 16, 10);

    s_lbl_wave_meta = lv_label_create(wave_panel);
    lv_label_set_text(s_lbl_wave_meta, "均值 --  峰峰值 --  窗口 0-255");
    lv_obj_set_width(s_lbl_wave_meta, 496);
    lv_label_set_long_mode(s_lbl_wave_meta, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(s_lbl_wave_meta, gui_assets_get_font_16(), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_wave_meta, lv_color_hex(RT_COL_TITLE), LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_wave_meta, 16, 330);

    lv_obj_t *table_panel = rtmon_create_panel(ui->RealtimeMonitor, 552, 92, 224, 236, "通道数据", NULL);
    for (uint32_t i = 0; i < RTMON_CH_COUNT; i++) {
        ui->RealtimeMonitor_lbl_ch[i] = rtmon_create_channel_row(table_panel, i);
    }

    (void)rtmon_create_panel(ui->RealtimeMonitor, 552, 334, 224, 76, "边缘-云端", &ui->RealtimeMonitor_lbl_cloud);
    ui->RealtimeMonitor_lbl_event = NULL;
    ui->RealtimeMonitor_lbl_diag = NULL;

    ui->RealtimeMonitor_btn_back = lv_button_create(ui->RealtimeMonitor);
    lv_obj_set_pos(ui->RealtimeMonitor_btn_back, 648, 418);
    lv_obj_set_size(ui->RealtimeMonitor_btn_back, 128, 44);
    lv_obj_set_style_bg_color(ui->RealtimeMonitor_btn_back, lv_color_hex(RT_COL_BLUE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->RealtimeMonitor_btn_back, 230, LV_PART_MAIN);
    lv_obj_set_style_radius(ui->RealtimeMonitor_btn_back, 22, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->RealtimeMonitor_btn_back, 0, LV_PART_MAIN);

    ui->RealtimeMonitor_lbl_back = lv_label_create(ui->RealtimeMonitor_btn_back);
    lv_label_set_text(ui->RealtimeMonitor_lbl_back, "返回");
    lv_obj_set_style_text_font(ui->RealtimeMonitor_lbl_back, gui_assets_get_font_20(), LV_PART_MAIN);
    lv_obj_set_style_text_color(ui->RealtimeMonitor_lbl_back, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(ui->RealtimeMonitor_lbl_back);

    ui->RealtimeMonitor_lbl_health = NULL;

    lv_obj_add_event_cb(ui->RealtimeMonitor, rtmon_screen_event_cb, LV_EVENT_ALL, ui);
    events_init_RealtimeMonitor(ui);
    rtmon_update_mode_buttons();
    rtmon_refresh(ui, false);
}
