#ifndef SD_WAVEFORM_H
#define SD_WAVEFORM_H

#include <stdbool.h>
#include <stdint.h>

#define SD_WAVE_MAGIC 0x57415645u /* "WAVE" */

typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t timestamp;
	uint32_t channel;
	uint32_t sample_rate;
	uint32_t count;
} WaveFileHeader_t;

typedef struct {
	uint32_t channel;
	uint32_t sample_rate;
	uint32_t timestamp;
} SD_WaveMeta_t;

bool SD_Wave_SaveBin(const char *name, const float *data, uint32_t len);
bool SD_Wave_SaveBinEx(const char *name, const float *data, uint32_t len, const SD_WaveMeta_t *meta);
bool SD_Wave_LoadBin(const char *name, float *data, uint32_t *len);
bool SD_Wave_SaveCSV(const char *name, const float *data, uint32_t len);
bool SD_Wave_AutoSave(uint8_t channel, const float *data, uint32_t len, bool csv);

#endif /* SD_WAVEFORM_H */
