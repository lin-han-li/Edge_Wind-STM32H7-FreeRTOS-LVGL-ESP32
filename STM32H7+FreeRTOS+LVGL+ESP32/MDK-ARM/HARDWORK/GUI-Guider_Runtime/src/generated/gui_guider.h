/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#ifndef GUI_GUIDER_H
#define GUI_GUIDER_H
#ifdef __cplusplus
extern "C" {
#endif

#include "../../../../../lvgl-9.4.0/lvgl.h"


typedef struct
{
	// === Screen 1 (Main) ===
	lv_obj_t *Main_1;
	bool Main_1_del;
	lv_obj_t *Main_1_header;
	lv_obj_t *Main_1_title;
	lv_obj_t *Main_1_status;
	lv_obj_t *Main_1_cont_grid;
	lv_obj_t *Main_1_tile_1; lv_obj_t *Main_1_img_1; lv_obj_t *Main_1_lbl_1;
	lv_obj_t *Main_1_tile_2; lv_obj_t *Main_1_img_2; lv_obj_t *Main_1_lbl_2;
	lv_obj_t *Main_1_tile_3; lv_obj_t *Main_1_img_3; lv_obj_t *Main_1_lbl_3;
	lv_obj_t *Main_1_tile_4; lv_obj_t *Main_1_img_4; lv_obj_t *Main_1_lbl_4;
	lv_obj_t *Main_1_tile_5; lv_obj_t *Main_1_img_5; lv_obj_t *Main_1_lbl_5;
	lv_obj_t *Main_1_tile_6; lv_obj_t *Main_1_img_6; lv_obj_t *Main_1_lbl_6;
	lv_obj_t *Main_1_footer;
	lv_obj_t *Main_1_dot_1;
	lv_obj_t *Main_1_dot_2;
	lv_obj_t *Main_1_dot_3;

	// RealtimeMonitor Screen Objects
	lv_obj_t *RealtimeMonitor;
	bool RealtimeMonitor_del;
	lv_obj_t *RealtimeMonitor_lbl_title;
	lv_obj_t *RealtimeMonitor_lbl_status;
	lv_obj_t *RealtimeMonitor_lbl_node;
	lv_obj_t *RealtimeMonitor_lbl_ch[4];
	lv_obj_t *RealtimeMonitor_lbl_diag;
	lv_obj_t *RealtimeMonitor_lbl_health;
	lv_obj_t *RealtimeMonitor_lbl_cloud;
	lv_obj_t *RealtimeMonitor_lbl_event;
	lv_obj_t *RealtimeMonitor_btn_back;
	lv_obj_t *RealtimeMonitor_lbl_back;

	// === Screen 2 (Config) ===
	lv_obj_t *Main_2;
	bool Main_2_del;
	lv_obj_t *Main_2_header;
	lv_obj_t *Main_2_title;
	lv_obj_t *Main_2_status;
	lv_obj_t *Main_2_cont_grid;
	lv_obj_t *Main_2_tile_1; lv_obj_t *Main_2_img_1; lv_obj_t *Main_2_lbl_1;
	lv_obj_t *Main_2_tile_2; lv_obj_t *Main_2_img_2; lv_obj_t *Main_2_lbl_2;
	lv_obj_t *Main_2_tile_3; lv_obj_t *Main_2_img_3; lv_obj_t *Main_2_lbl_3;
	lv_obj_t *Main_2_tile_4; lv_obj_t *Main_2_img_4; lv_obj_t *Main_2_lbl_4;
	lv_obj_t *Main_2_tile_5; lv_obj_t *Main_2_img_5; lv_obj_t *Main_2_lbl_5;
	lv_obj_t *Main_2_tile_6; lv_obj_t *Main_2_img_6; lv_obj_t *Main_2_lbl_6;
	lv_obj_t *Main_2_footer;
	lv_obj_t *Main_2_dot_1;
	lv_obj_t *Main_2_dot_2;
	lv_obj_t *Main_2_dot_3;

	// === Screen 3 (System) ===
	lv_obj_t *Main_3;
	bool Main_3_del;
	lv_obj_t *Main_3_header;
	lv_obj_t *Main_3_title;
	lv_obj_t *Main_3_status;
	lv_obj_t *Main_3_cont_grid;
	lv_obj_t *Main_3_tile_1; lv_obj_t *Main_3_img_1; lv_obj_t *Main_3_lbl_1;
	lv_obj_t *Main_3_tile_2; lv_obj_t *Main_3_img_2; lv_obj_t *Main_3_lbl_2;
	lv_obj_t *Main_3_footer;
	lv_obj_t *Main_3_dot_1;
	lv_obj_t *Main_3_dot_2;
	lv_obj_t *Main_3_dot_3;

	// WifiConfig Screen Objects
	lv_obj_t *WifiConfig;
	bool WifiConfig_del;
	lv_obj_t *WifiConfig_cont_panel;
	lv_obj_t *WifiConfig_lbl_title;
	lv_obj_t *WifiConfig_lbl_ssid;
	lv_obj_t *WifiConfig_ta_ssid;
	lv_obj_t *WifiConfig_lbl_pwd;
	lv_obj_t *WifiConfig_ta_pwd;
	lv_obj_t *WifiConfig_btn_back;
	lv_obj_t *WifiConfig_btn_back_label;
	lv_obj_t *WifiConfig_btn_scan;
	lv_obj_t *WifiConfig_btn_scan_label;
	lv_obj_t *WifiConfig_btn_save;
	lv_obj_t *WifiConfig_btn_save_label;
	lv_obj_t *WifiConfig_kb;
	lv_obj_t *WifiConfig_lbl_status;

	// ServerConfig Screen Objects
	lv_obj_t *ServerConfig;
	bool ServerConfig_del;
	lv_obj_t *ServerConfig_cont_panel;
	lv_obj_t *ServerConfig_lbl_title;
	lv_obj_t *ServerConfig_lbl_ip;
	lv_obj_t *ServerConfig_ta_ip;
	lv_obj_t *ServerConfig_lbl_port;
	lv_obj_t *ServerConfig_ta_port;
	lv_obj_t *ServerConfig_lbl_id;
	lv_obj_t *ServerConfig_ta_id;
	lv_obj_t *ServerConfig_lbl_loc;
	lv_obj_t *ServerConfig_ta_loc;
	lv_obj_t *ServerConfig_btn_back;
	lv_obj_t *ServerConfig_btn_back_label;
	lv_obj_t *ServerConfig_btn_save;
	lv_obj_t *ServerConfig_btn_save_label;
	lv_obj_t *ServerConfig_btn_load;
	lv_obj_t *ServerConfig_btn_load_label;
	lv_obj_t *ServerConfig_lbl_status;
	lv_obj_t *ServerConfig_kb;

	// ParamConfig Screen Objects
	lv_obj_t *ParamConfig;
	bool ParamConfig_del;
	lv_obj_t *ParamConfig_cont_panel;
	lv_obj_t *ParamConfig_lbl_title;
	// Header quick presets
	lv_obj_t *ParamConfig_btn_lan;
	lv_obj_t *ParamConfig_lbl_lan;
	lv_obj_t *ParamConfig_btn_wan;
	lv_obj_t *ParamConfig_lbl_wan;
	// Row 1
	lv_obj_t *ParamConfig_lbl_heartbeat;
	lv_obj_t *ParamConfig_ta_heartbeat;
	lv_obj_t *ParamConfig_lbl_sendlimit;
	lv_obj_t *ParamConfig_ta_sendlimit;
	// Row 2
	lv_obj_t *ParamConfig_lbl_httptimeout;
	lv_obj_t *ParamConfig_ta_httptimeout;
	lv_obj_t *ParamConfig_lbl_hardreset;
	lv_obj_t *ParamConfig_ta_hardreset;
	// Extra: Downsample
	lv_obj_t *ParamConfig_lbl_downsample;
	lv_obj_t *ParamConfig_ta_downsample;
	// Extra: Upload points (after downsample)
	lv_obj_t *ParamConfig_lbl_uploadpoints;
	lv_obj_t *ParamConfig_ta_uploadpoints;
	// Extra: Chunked send
	lv_obj_t *ParamConfig_lbl_chunkkb;
	lv_obj_t *ParamConfig_ta_chunkkb;
	lv_obj_t *ParamConfig_lbl_chunkdelay;
	lv_obj_t *ParamConfig_ta_chunkdelay;
	// Quick action: disable chunking
	lv_obj_t *ParamConfig_btn_nochunk;
	lv_obj_t *ParamConfig_lbl_nochunk;
	// Buttons & Status
	lv_obj_t *ParamConfig_btn_back;
	lv_obj_t *ParamConfig_btn_back_label;
	lv_obj_t *ParamConfig_btn_load;
	lv_obj_t *ParamConfig_btn_load_label;
	lv_obj_t *ParamConfig_btn_save;
	lv_obj_t *ParamConfig_btn_save_label;
	lv_obj_t *ParamConfig_lbl_status;
	lv_obj_t *ParamConfig_lbl_tips;
	lv_obj_t *ParamConfig_kb;

	// DeviceConnect Screen Objects
	lv_obj_t *DeviceConnect;
	bool DeviceConnect_del;
	lv_obj_t *DeviceConnect_lbl_title;
	/* 右上角：断电重连开关按钮（是否） */
	lv_obj_t *DeviceConnect_btn_autorec;
	lv_obj_t *DeviceConnect_lbl_autorec;
	lv_obj_t *DeviceConnect_cont_panel;
	lv_obj_t *DeviceConnect_led_wifi;
	lv_obj_t *DeviceConnect_lbl_stat_wifi;
	lv_obj_t *DeviceConnect_btn_wifi;
	lv_obj_t *DeviceConnect_lbl_btn_wifi;
	lv_obj_t *DeviceConnect_led_tcp;
	lv_obj_t *DeviceConnect_lbl_stat_tcp;
	lv_obj_t *DeviceConnect_btn_tcp;
	lv_obj_t *DeviceConnect_lbl_btn_tcp;
	lv_obj_t *DeviceConnect_led_reg;
	lv_obj_t *DeviceConnect_lbl_stat_reg;
	lv_obj_t *DeviceConnect_btn_reg;
	lv_obj_t *DeviceConnect_lbl_btn_reg;
	lv_obj_t *DeviceConnect_led_report;
	lv_obj_t *DeviceConnect_lbl_stat_report;
	lv_obj_t *DeviceConnect_btn_report;
	lv_obj_t *DeviceConnect_lbl_btn_report;
	lv_obj_t *DeviceConnect_btn_back;
	lv_obj_t *DeviceConnect_lbl_back;
	lv_obj_t *DeviceConnect_btn_auto;
	lv_obj_t *DeviceConnect_lbl_auto;
	lv_obj_t *DeviceConnect_ta_console;
}lv_ui;

typedef void (*ui_setup_scr_t)(lv_ui * ui);

void ui_init_style(lv_style_t * style);

void ui_load_scr_animation(lv_ui *ui, lv_obj_t ** new_scr, bool new_scr_del, bool * old_scr_del, ui_setup_scr_t setup_scr,
                           lv_screen_load_anim_t anim_type, uint32_t time, uint32_t delay, bool is_clean, bool auto_del);

void ui_animation(void * var, uint32_t duration, int32_t delay, int32_t start_value, int32_t end_value, lv_anim_path_cb_t path_cb,
                  uint32_t repeat_cnt, uint32_t repeat_delay, uint32_t playback_time, uint32_t playback_delay,
                  lv_anim_exec_xcb_t exec_cb, lv_anim_start_cb_t start_cb, lv_anim_completed_cb_t ready_cb, lv_anim_deleted_cb_t deleted_cb);


void init_scr_del_flag(lv_ui *ui);

void setup_bottom_layer(void);

void setup_ui(lv_ui *ui);

void video_play(lv_ui *ui);

void init_keyboard(lv_ui *ui);

extern lv_ui guider_ui;


void setup_scr_Main_1(lv_ui *ui);
void setup_scr_Main_2(lv_ui *ui);
void setup_scr_Main_3(lv_ui *ui);
void setup_scr_RealtimeMonitor(lv_ui *ui);
void setup_scr_WifiConfig(lv_ui *ui);
void setup_scr_ServerConfig(lv_ui *ui);
void setup_scr_ParamConfig(lv_ui *ui);
void setup_scr_DeviceConnect(lv_ui *ui);



#ifdef __cplusplus
}
#endif
#endif
