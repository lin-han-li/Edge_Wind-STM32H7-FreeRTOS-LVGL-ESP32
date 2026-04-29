#include "sd_config.h"

#include "SD.h"

#include "ff.h"

#include "../ESP8266/esp8266_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SD_CONFIG_MAX_JSON 1024

static void sd_copy_str(char *dst, size_t dst_len, const char *src)
{
	if (!dst || dst_len == 0) {
		return;
	}
	if (!src) {
		dst[0] = '\0';
		return;
	}
	strncpy(dst, src, dst_len - 1);
	dst[dst_len - 1] = '\0';
}

static void sd_sanitize_str(char *dst, size_t dst_len, const char *src)
{
	if (!dst || dst_len == 0) {
		return;
	}
	size_t i = 0;
	if (src) {
		for (; src[i] && i < dst_len - 1; ++i) {
			char c = src[i];
			if (c == '"' || c == '\\' || (unsigned char)c < 0x20) {
				dst[i] = '_';
			} else {
				dst[i] = c;
			}
		}
	}
	dst[i] = '\0';
}

static bool sd_json_get_string(const char *json, const char *key, char *out, size_t out_len)
{
	if (!json || !key || !out || out_len == 0) {
		return false;
	}
	char pattern[64];
	if (snprintf(pattern, sizeof(pattern), "\"%s\"", key) <= 0) {
		return false;
	}
	const char *p = strstr(json, pattern);
	if (!p) {
		return false;
	}
	p = strchr(p, ':');
	if (!p) {
		return false;
	}
	p++;
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (*p != '"') {
		return false;
	}
	p++;
	size_t i = 0;
	while (*p && *p != '"' && i < out_len - 1) {
		if (*p == '\\' && p[1]) {
			p++;
		}
		out[i++] = *p++;
	}
	out[i] = '\0';
	return (i > 0);
}

static bool sd_json_get_uint(const char *json, const char *key, uint32_t *out)
{
	if (!json || !key || !out) {
		return false;
	}
	char pattern[64];
	if (snprintf(pattern, sizeof(pattern), "\"%s\"", key) <= 0) {
		return false;
	}
	const char *p = strstr(json, pattern);
	if (!p) {
		return false;
	}
	p = strchr(p, ':');
	if (!p) {
		return false;
	}
	p++;
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (*p < '0' || *p > '9') {
		return false;
	}
	*out = (uint32_t)strtoul(p, NULL, 10);
	return true;
}

void SD_Config_SetDefaults(SystemConfig_t *cfg)
{
	if (!cfg) {
		return;
	}
	memset(cfg, 0, sizeof(*cfg));
#ifdef WIFI_SSID
	sd_copy_str(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), WIFI_SSID);
#else
	sd_copy_str(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), "YOUR_WIFI_SSID");
#endif
#ifdef WIFI_PASSWORD
	sd_copy_str(cfg->wifi_password, sizeof(cfg->wifi_password), WIFI_PASSWORD);
#else
	sd_copy_str(cfg->wifi_password, sizeof(cfg->wifi_password), "YOUR_WIFI_PASSWORD");
#endif
#ifdef SERVER_IP
	sd_copy_str(cfg->server_ip, sizeof(cfg->server_ip), SERVER_IP);
#else
	sd_copy_str(cfg->server_ip, sizeof(cfg->server_ip), "192.168.10.43");
#endif
#ifdef SERVER_PORT
	cfg->server_port = (uint16_t)SERVER_PORT;
#else
	cfg->server_port = 5000;
#endif
#ifdef NODE_ID
	sd_copy_str(cfg->node_id, sizeof(cfg->node_id), NODE_ID);
#else
	sd_copy_str(cfg->node_id, sizeof(cfg->node_id), "STM32_H7_Device");
#endif
#ifdef NODE_LOCATION
	sd_copy_str(cfg->node_location, sizeof(cfg->node_location), NODE_LOCATION);
#else
	sd_copy_str(cfg->node_location, sizeof(cfg->node_location), "Lab_Test");
#endif
}

bool SD_Config_Load(SystemConfig_t *cfg)
{
	extern int printf(const char *format, ...);
	if (!cfg) {
		return false;
	}
	printf("[SD_Config] Setting defaults...\r\n");
	SD_Config_SetDefaults(cfg);

	printf("[SD_Config] Calling SD_Init...\r\n");
	if (SD_Init() != FR_OK) {
		printf("[SD_Config] SD_Init failed\r\n");
		return false;
	}

	printf("[SD_Config] Opening config file...\r\n");
	FIL fil;
	FRESULT res = f_open(&fil, SD_CONFIG_PATH, FA_READ);
	if (res != FR_OK) {
		printf("[SD_Config] Config file not found: %d\r\n", (int)res);
		return false;
	}

	FSIZE_t size = f_size(&fil);
	if (size >= SD_CONFIG_MAX_JSON) {
		(void)f_close(&fil);
		return false;
	}

	char json[SD_CONFIG_MAX_JSON];
	UINT br = 0;
	res = f_read(&fil, json, (UINT)size, &br);
	(void)f_close(&fil);
	if (res != FR_OK || br == 0) {
		return false;
	}
	json[br] = '\0';

	(void)sd_json_get_string(json, "ssid", cfg->wifi_ssid, sizeof(cfg->wifi_ssid));
	(void)sd_json_get_string(json, "password", cfg->wifi_password, sizeof(cfg->wifi_password));
	(void)sd_json_get_string(json, "ip", cfg->server_ip, sizeof(cfg->server_ip));
	uint32_t port = 0;
	if (sd_json_get_uint(json, "port", &port)) {
		cfg->server_port = (uint16_t)port;
	}
	(void)sd_json_get_string(json, "id", cfg->node_id, sizeof(cfg->node_id));
	(void)sd_json_get_string(json, "location", cfg->node_location, sizeof(cfg->node_location));

	return true;
}

static bool sd_write_config_file(const char *path, const char *json)
{
	if (!path || !json) {
		return false;
	}
	FIL fil;
	FRESULT res = f_open(&fil, path, FA_CREATE_ALWAYS | FA_WRITE);
	if (res != FR_OK) {
		return false;
	}
	UINT bw = 0;
	res = f_write(&fil, json, (UINT)strlen(json), &bw);
	(void)f_sync(&fil);
	(void)f_close(&fil);
	return (res == FR_OK && bw == (UINT)strlen(json));
}

bool SD_Config_Save(const SystemConfig_t *cfg)
{
	if (!cfg) {
		return false;
	}
	if (SD_Init() != FR_OK) {
		return false;
	}

	char ssid[sizeof(cfg->wifi_ssid)];
	char pass[sizeof(cfg->wifi_password)];
	char ip[sizeof(cfg->server_ip)];
	char node_id[sizeof(cfg->node_id)];
	char node_loc[sizeof(cfg->node_location)];

	sd_sanitize_str(ssid, sizeof(ssid), cfg->wifi_ssid);
	sd_sanitize_str(pass, sizeof(pass), cfg->wifi_password);
	sd_sanitize_str(ip, sizeof(ip), cfg->server_ip);
	sd_sanitize_str(node_id, sizeof(node_id), cfg->node_id);
	sd_sanitize_str(node_loc, sizeof(node_loc), cfg->node_location);

	char json[SD_CONFIG_MAX_JSON];
	int n = snprintf(json, sizeof(json),
	                 "{\n"
	                 "  \"wifi\": {\"ssid\": \"%s\", \"password\": \"%s\"},\n"
	                 "  \"server\": {\"ip\": \"%s\", \"port\": %u},\n"
	                 "  \"node\": {\"id\": \"%s\", \"location\": \"%s\"}\n"
	                 "}\n",
	                 ssid, pass, ip, (unsigned)cfg->server_port, node_id, node_loc);
	if (n <= 0 || (size_t)n >= sizeof(json)) {
		return false;
	}

	if (!sd_write_config_file(SD_CONFIG_PATH, json)) {
		return false;
	}
	(void)sd_write_config_file(SD_CONFIG_BAK_PATH, json);
	return true;
}

bool SD_Config_SetWiFi(const char *ssid, const char *password)
{
	SystemConfig_t cfg;
	SD_Config_SetDefaults(&cfg);
	(void)SD_Config_Load(&cfg);
	sd_copy_str(cfg.wifi_ssid, sizeof(cfg.wifi_ssid), ssid);
	sd_copy_str(cfg.wifi_password, sizeof(cfg.wifi_password), password);
	return SD_Config_Save(&cfg);
}

bool SD_Config_SetServer(const char *ip, uint16_t port)
{
	SystemConfig_t cfg;
	SD_Config_SetDefaults(&cfg);
	(void)SD_Config_Load(&cfg);
	sd_copy_str(cfg.server_ip, sizeof(cfg.server_ip), ip);
	cfg.server_port = port;
	return SD_Config_Save(&cfg);
}

bool SD_Config_SetNode(const char *id, const char *location)
{
	SystemConfig_t cfg;
	SD_Config_SetDefaults(&cfg);
	(void)SD_Config_Load(&cfg);
	sd_copy_str(cfg.node_id, sizeof(cfg.node_id), id);
	sd_copy_str(cfg.node_location, sizeof(cfg.node_location), location);
	return SD_Config_Save(&cfg);
}
