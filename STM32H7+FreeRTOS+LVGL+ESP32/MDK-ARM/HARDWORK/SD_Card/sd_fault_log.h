#ifndef SD_FAULT_LOG_H
#define SD_FAULT_LOG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint32_t timestamp;
	uint8_t level;
	uint8_t code;
	char message[128];
} FaultEntry_t;

bool SD_Fault_Log(uint8_t level, uint8_t code, const char *msg);
bool SD_Fault_GetRecent(FaultEntry_t *entries, uint32_t max, uint32_t *count);
bool SD_Fault_GetByDate(const char *date, FaultEntry_t *entries, uint32_t max, uint32_t *count);

#endif /* SD_FAULT_LOG_H */
