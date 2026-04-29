#ifndef __ESP8266_CONFIG_H
#define __ESP8266_CONFIG_H

/*
 * Public-safe STM32 -> ESP32/cloud configuration wrapper.
 *
 * For local hardware builds, create esp8266_config_private.h in this directory
 * with the same macros below. The private file is ignored by Git so WiFi
 * credentials/server secrets are not uploaded to GitHub.
 */

#if defined(__has_include)
#  if __has_include("esp8266_config_private.h")
#    include "esp8266_config_private.h"
#    define ESP8266_CONFIG_PRIVATE_INCLUDED 1
#  endif
#endif

#ifndef ESP8266_CONFIG_PRIVATE_INCLUDED

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

/* Server used by ESP32 HTTP uploader. */
#define SERVER_IP "YOUR_SERVER_HOST_OR_IP"
#define SERVER_PORT 8080

#define NODE_ID "STM32_RTOS_Device"
#define NODE_LOCATION "EdgeWind Lab"

#endif /* ESP8266_CONFIG_PRIVATE_INCLUDED */

#endif /* __ESP8266_CONFIG_H */
