#include "sd_waveform.h"

#include "SD.h"
#include "sd_time.h"

#include "ff.h"

#include <stdio.h>
#include <string.h>

static bool sd_make_parent_dir(const char *path)
{
	if (!path) {
		return false;
	}
	char tmp[256];
	size_t len = strlen(path);
	if (len >= sizeof(tmp)) {
		return false;
	}
	memcpy(tmp, path, len + 1);
	char *slash = strrchr(tmp, '/');
	if (!slash) {
		return true;
	}
	if (slash == tmp) {
		return true;
	}
	*slash = '\0';
	return (SD_MkdirRecursive(tmp) == FR_OK);
}

bool SD_Wave_SaveBin(const char *name, const float *data, uint32_t len)
{
	return SD_Wave_SaveBinEx(name, data, len, NULL);
}

bool SD_Wave_SaveBinEx(const char *name, const float *data, uint32_t len, const SD_WaveMeta_t *meta)
{
	if (!name || !data || len == 0) {
		return false;
	}
	if (SD_Init() != FR_OK) {
		return false;
	}
	if (!sd_make_parent_dir(name)) {
		return false;
	}

	WaveFileHeader_t hdr = {0};
	hdr.magic = SD_WAVE_MAGIC;
	hdr.version = 1;
	hdr.timestamp = SD_Time_GetUnix();
	hdr.channel = meta ? meta->channel : 0;
	hdr.sample_rate = meta ? meta->sample_rate : 0;
	hdr.count = len;

	FIL fil;
	FRESULT res = f_open(&fil, name, FA_CREATE_ALWAYS | FA_WRITE);
	if (res != FR_OK) {
		return false;
	}
	UINT bw = 0;
	res = f_write(&fil, &hdr, sizeof(hdr), &bw);
	if (res != FR_OK || bw != sizeof(hdr)) {
		(void)f_close(&fil);
		return false;
	}
	res = f_write(&fil, data, sizeof(float) * len, &bw);
	(void)f_sync(&fil);
	(void)f_close(&fil);
	return (res == FR_OK && bw == sizeof(float) * len);
}

bool SD_Wave_LoadBin(const char *name, float *data, uint32_t *len)
{
	if (!name || !data || !len) {
		return false;
	}
	if (SD_Init() != FR_OK) {
		return false;
	}
	FIL fil;
	FRESULT res = f_open(&fil, name, FA_READ);
	if (res != FR_OK) {
		return false;
	}
	WaveFileHeader_t hdr = {0};
	UINT br = 0;
	res = f_read(&fil, &hdr, sizeof(hdr), &br);
	if (res != FR_OK || br != sizeof(hdr) || hdr.magic != SD_WAVE_MAGIC) {
		(void)f_close(&fil);
		return false;
	}
	uint32_t count = hdr.count;
	if (*len < count) {
		count = *len;
	}
	res = f_read(&fil, data, sizeof(float) * count, &br);
	(void)f_close(&fil);
	if (res != FR_OK) {
		return false;
	}
	*len = count;
	return true;
}

bool SD_Wave_SaveCSV(const char *name, const float *data, uint32_t len)
{
	if (!name || !data || len == 0) {
		return false;
	}
	if (SD_Init() != FR_OK) {
		return false;
	}
	if (!sd_make_parent_dir(name)) {
		return false;
	}
	FIL fil;
	FRESULT res = f_open(&fil, name, FA_CREATE_ALWAYS | FA_WRITE);
	if (res != FR_OK) {
		return false;
	}
	for (uint32_t i = 0; i < len; ++i) {
		char line[48];
		int n = snprintf(line, sizeof(line), "%lu,%.6f\r\n", (unsigned long)i, (double)data[i]);
		if (n <= 0) {
			continue;
		}
		UINT bw = 0;
		res = f_write(&fil, line, (UINT)n, &bw);
		if (res != FR_OK) {
			(void)f_close(&fil);
			return false;
		}
	}
	(void)f_sync(&fil);
	(void)f_close(&fil);
	return true;
}

bool SD_Wave_AutoSave(uint8_t channel, const float *data, uint32_t len, bool csv)
{
	if (!data || len == 0) {
		return false;
	}
	char date_path[64];
	if (!SD_Time_GetDatePath(date_path, sizeof(date_path), "0:/data")) {
		return false;
	}
	if (SD_MkdirRecursive(date_path) != FR_OK) {
		return false;
	}

	char ts[32];
	if (!SD_Time_GetTimestamp(ts, sizeof(ts))) {
		return false;
	}
	char file[128];
	const char *ext = csv ? "csv" : "bin";
	if (snprintf(file, sizeof(file), "%s/wave_ch%u_%s.%s",
	             date_path, (unsigned)channel, ts, ext) <= 0) {
		return false;
	}
	if (csv) {
		return SD_Wave_SaveCSV(file, data, len);
	}

	SD_WaveMeta_t meta;
	meta.channel = channel;
	meta.sample_rate = 0;
	meta.timestamp = SD_Time_GetUnix();
	return SD_Wave_SaveBinEx(file, data, len, &meta);
}
