#ifndef SD_CONFIG_H
#define SD_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define SD_CONFIG_PATH "0:/config/system.json"
#define SD_CONFIG_BAK_PATH "0:/backup/config_bak.json"

typedef struct {
	char wifi_ssid[64];
	char wifi_password[64];
	char server_ip[32];
	uint16_t server_port;
	char node_id[64];
	char node_location[64];
} SystemConfig_t;

void SD_Config_SetDefaults(SystemConfig_t *cfg);
bool SD_Config_Load(SystemConfig_t *cfg);
bool SD_Config_Save(const SystemConfig_t *cfg);
bool SD_Config_SetWiFi(const char *ssid, const char *password);
bool SD_Config_SetServer(const char *ip, uint16_t port);
bool SD_Config_SetNode(const char *id, const char *location);

#endif /* SD_CONFIG_H */
