#ifndef SD_TIME_H
#define SD_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool SD_Time_GetDate(char *buf, size_t len);
bool SD_Time_GetTime(char *buf, size_t len);
bool SD_Time_GetTimestamp(char *buf, size_t len);
bool SD_Time_GetDatePath(char *buf, size_t len, const char *base_dir);
bool SD_Time_GetMonthTag(char *buf, size_t len);
uint32_t SD_Time_GetUnix(void);

#endif /* SD_TIME_H */
