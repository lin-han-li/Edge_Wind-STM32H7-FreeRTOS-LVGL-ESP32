#include "sd_fault_log.h"

#include "SD.h"
#include "sd_time.h"

#include "ff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_fault_log_last_error[64] = "OK";

static void sd_fault_set_error(const char *stage, int code)
{
	if (!stage) {
		stage = "unknown";
	}
	(void)snprintf(s_fault_log_last_error, sizeof(s_fault_log_last_error),
	               "%s:%d", stage, code);
}

const char *SD_Fault_LastError(void)
{
	return s_fault_log_last_error;
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

static bool sd_json_get_uint(const char *json, const char *key, uint32_t *out)
{
	if (!json || !key || !out) {
		return false;
	}
	char pattern[32];
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

static bool sd_json_get_string(const char *json, const char *key, char *out, size_t out_len)
{
	if (!json || !key || !out || out_len == 0) {
		return false;
	}
	char pattern[32];
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

static bool sd_append_line(const char *path, const char *line)
{
	if (!path || !line) {
		sd_fault_set_error("append_arg", -1);
		return false;
	}
	FIL fil;
	FRESULT res = f_open(&fil, path, FA_OPEN_ALWAYS | FA_WRITE);
	if (res != FR_OK) {
		sd_fault_set_error("open", (int)res);
		return false;
	}
	(void)f_lseek(&fil, f_size(&fil));
	UINT bw = 0;
	res = f_write(&fil, line, (UINT)strlen(line), &bw);
	(void)f_sync(&fil);
	(void)f_close(&fil);
	if (res != FR_OK) {
		sd_fault_set_error("write", (int)res);
	} else if (bw != (UINT)strlen(line)) {
		sd_fault_set_error("short_write", (int)bw);
	}
	return (res == FR_OK && bw == (UINT)strlen(line));
}

static bool sd_parse_fault_line(const char *line, FaultEntry_t *entry)
{
	if (!line || !entry) {
		return false;
	}
	memset(entry, 0, sizeof(*entry));
	uint32_t ts = 0;
	uint32_t level = 0;
	uint32_t code = 0;
	(void)sd_json_get_uint(line, "ts", &ts);
	(void)sd_json_get_uint(line, "level", &level);
	(void)sd_json_get_uint(line, "code", &code);
	entry->timestamp = ts;
	entry->level = (uint8_t)level;
	entry->code = (uint8_t)code;
	(void)sd_json_get_string(line, "msg", entry->message, sizeof(entry->message));
	return true;
}

static void sd_swap_fault(FaultEntry_t *a, FaultEntry_t *b)
{
    FaultEntry_t tmp = *a;
    *a = *b;
    *b = tmp;
}

static void sd_reverse_faults(FaultEntry_t *arr, uint32_t start, uint32_t end)
{
    while (start < end) {
        sd_swap_fault(&arr[start], &arr[end]);
        start++;
        end--;
    }
}

bool SD_Fault_Log(uint8_t level, uint8_t code, const char *msg)
{
	sd_fault_set_error("OK", 0);
	FRESULT res = SD_Init();
	if (res != FR_OK) {
		sd_fault_set_error("init", (int)res);
		return false;
	}
	return SD_Fault_LogMounted(level, code, msg);
}

bool SD_Fault_LogMounted(uint8_t level, uint8_t code, const char *msg)
{
	sd_fault_set_error("OK", 0);
	FRESULT res;
	char date[16];
	char month[16];
	if (!SD_Time_GetDate(date, sizeof(date))) {
		(void)snprintf(date, sizeof(date), "2000-01-01");
		sd_fault_set_error("date_fallback", -1);
	}
	if (!SD_Time_GetMonthTag(month, sizeof(month))) {
		(void)snprintf(month, sizeof(month), "2000-01");
		sd_fault_set_error("month_fallback", -1);
	}
	char date_dir[64];
	if (snprintf(date_dir, sizeof(date_dir), "0:/data/%s", date) <= 0) {
		sd_fault_set_error("date_dir", -1);
		return false;
	}
	res = SD_MkdirRecursive(date_dir);
	if (res != FR_OK) {
		sd_fault_set_error("mkdir_date", (int)res);
		return false;
	}

	char daily_path[96];
	char month_path[96];
	if (snprintf(daily_path, sizeof(daily_path), "%s/fault.log", date_dir) <= 0) {
		sd_fault_set_error("daily_path", -1);
		return false;
	}
	if (snprintf(month_path, sizeof(month_path), "0:/logs/event_%s.log", month) <= 0) {
		sd_fault_set_error("month_path", -1);
		return false;
	}
	(void)SD_MkdirRecursive("0:/logs");

	char msg_buf[128];
	sd_sanitize_str(msg_buf, sizeof(msg_buf), msg);
	uint32_t ts = SD_Time_GetUnix();
	char line[256];
	int n = snprintf(line, sizeof(line),
	                 "{\"ts\":%lu,\"level\":%u,\"code\":%u,\"msg\":\"%s\"}\r\n",
	                 (unsigned long)ts, (unsigned)level, (unsigned)code, msg_buf);
	if (n <= 0 || (size_t)n >= sizeof(line)) {
		sd_fault_set_error("line", n);
		return false;
	}

	if (!sd_append_line(daily_path, line)) {
		return false;
	}
	(void)sd_append_line(month_path, line);
	sd_fault_set_error("OK", 0);
	return true;
}

bool SD_Fault_GetByDate(const char *date, FaultEntry_t *entries, uint32_t max, uint32_t *count)
{
	if (!date || !entries || max == 0 || !count) {
		return false;
	}
	*count = 0;
	if (SD_Init() != FR_OK) {
		return false;
	}
	return SD_Fault_GetByDateMounted(date, entries, max, count);
}

bool SD_Fault_GetByDateMounted(const char *date, FaultEntry_t *entries, uint32_t max, uint32_t *count)
{
	if (!date || !entries || max == 0 || !count) {
		return false;
	}
	*count = 0;
	char path[96];
	if (snprintf(path, sizeof(path), "0:/data/%s/fault.log", date) <= 0) {
		return false;
	}
	FIL fil;
	FRESULT res = f_open(&fil, path, FA_READ);
	if (res != FR_OK) {
		if (res == FR_NO_FILE || res == FR_NO_PATH) {
			return true;
		}
		return false;
	}
	char line[256];
	while (f_gets(line, sizeof(line), &fil)) {
		if (*count >= max) {
			break;
		}
		(void)sd_parse_fault_line(line, &entries[*count]);
		(*count)++;
	}
	(void)f_close(&fil);
	return true;
}

bool SD_Fault_GetRecent(FaultEntry_t *entries, uint32_t max, uint32_t *count)
{
	if (!entries || max == 0 || !count) {
		return false;
	}
	*count = 0;
	if (SD_Init() != FR_OK) {
		return false;
	}
	return SD_Fault_GetRecentMounted(entries, max, count);
}

bool SD_Fault_GetRecentMounted(FaultEntry_t *entries, uint32_t max, uint32_t *count)
{
	if (!entries || max == 0 || !count) {
		return false;
	}
	*count = 0;
	char month[16];
	if (!SD_Time_GetMonthTag(month, sizeof(month))) {
		(void)snprintf(month, sizeof(month), "2000-01");
	}
	char path[96];
	if (snprintf(path, sizeof(path), "0:/logs/event_%s.log", month) <= 0) {
		return false;
	}
	FIL fil;
	FRESULT res = f_open(&fil, path, FA_READ);
	if (res != FR_OK) {
		if (res == FR_NO_FILE || res == FR_NO_PATH) {
			return true;
		}
		return false;
	}
	char line[256];
	uint32_t idx = 0;
	uint32_t total = 0;
	while (f_gets(line, sizeof(line), &fil)) {
		(void)sd_parse_fault_line(line, &entries[idx % max]);
		idx++;
		total++;
	}
	(void)f_close(&fil);
	if (total == 0) {
		return true;
	}
	if (total <= max) {
		*count = total;
		return true;
	}
	uint32_t start = idx % max;
    if (start != 0) {
        sd_reverse_faults(entries, 0, start - 1);
        sd_reverse_faults(entries, start, max - 1);
        sd_reverse_faults(entries, 0, max - 1);
    }
	*count = max;
	return true;
}
