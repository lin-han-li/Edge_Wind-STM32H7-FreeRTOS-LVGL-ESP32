#include "SD.h"

#include "main.h"
#include "fatfs.h"

#include <string.h>

extern int printf(const char *format, ...);



void SD_card_Init(void)
{
	(void)SD_Init();
}













static bool sd_has_suffix(const char *name, const char *suffix)
{
	if (!suffix || suffix[0] == '\0') {
		return true;
	}
	if (!name) {
		return false;
	}
	size_t name_len = strlen(name);
	size_t suf_len = strlen(suffix);
	if (name_len < suf_len) {
		return false;
	}
	return (strcmp(name + name_len - suf_len, suffix) == 0);
}

static bool sd_is_root_prefix(const char *path, const char *pos)
{
	if (!path || !pos) {
		return false;
	}
	if (pos == path) {
		return true;
	}
	if ((pos - path) >= 2 && path[1] == ':' && (pos == path + 2)) {
		return true;
	}
	if ((pos - path) >= 3 && path[1] == ':' && (pos == path + 3) && path[2] == '/') {
		return true;
	}
	return false;
}

FRESULT SD_Init(void)
{
	printf("[SD] Initializing SD card...\r\n");
	
	FRESULT res = f_mount(&SDFatFS, (TCHAR const *)SDPath, 1);
	if (res != FR_OK) {
		printf("[SD] mount %s -> %d\r\n", SDPath, (int)res);
		return res;
	}
	printf("[SD] mount OK\r\n");
	
	/* 快速检查 SD 卡是否可访问，避免阻塞 */
	printf("[SD] Checking card access...\r\n");
	FILINFO fno;
	res = f_stat("0:/", &fno);
	printf("[SD] f_stat root -> %d\r\n", (int)res);
	if (res != FR_OK) {
		printf("[SD] card not ready: %d\r\n", (int)res);
		f_mount(NULL, SDPath, 0); /* 卸载避免后续误用 */
		return res;
	}
	
	printf("[SD] Creating directories...\r\n");
	(void)SD_MkdirRecursive("0:/config");
	(void)SD_MkdirRecursive("0:/data");
	(void)SD_MkdirRecursive("0:/logs");
	(void)SD_MkdirRecursive("0:/backup");
	printf("[SD] Init complete\r\n");
	return FR_OK;
}

FRESULT SD_MkdirRecursive(const char *path)
{
	if (!path || path[0] == '\0') {
		return FR_INVALID_NAME;
	}

	char tmp[256];
	size_t len = strlen(path);
	if (len >= sizeof(tmp)) {
		return FR_INVALID_NAME;
	}
	memcpy(tmp, path, len + 1);

	for (char *p = tmp; *p != '\0'; ++p) {
		if (*p == '/' || *p == '\\') {
			if (sd_is_root_prefix(tmp, p)) {
				continue;
			}
			char saved = *p;
			*p = '\0';
			FRESULT res = f_mkdir(tmp);
			if (res != FR_OK && res != FR_EXIST) {
				return res;
			}
			*p = saved;
		}
	}

	FRESULT res = f_mkdir(tmp);
	if (res != FR_OK && res != FR_EXIST) {
		return res;
	}
	return FR_OK;
}

bool SD_FileExists(const char *path)
{
	FILINFO info;
	return (path && f_stat(path, &info) == FR_OK);
}

bool SD_GetFileSize(const char *path, FSIZE_t *size_out)
{
	FILINFO info;
	if (!path || !size_out) {
		return false;
	}
	if (f_stat(path, &info) != FR_OK) {
		return false;
	}
	*size_out = info.fsize;
	return true;
}

FRESULT SD_ListDir(const char *path, SD_DirEntryCallback cb, void *user)
{
	if (!path || !cb) {
		return FR_INVALID_NAME;
	}
	DIR dir;
	FILINFO info;
	FRESULT res = f_opendir(&dir, path);
	if (res != FR_OK) {
		return res;
	}
	for (;;) {
		res = f_readdir(&dir, &info);
		if (res != FR_OK) {
			break;
		}
		if (info.fname[0] == '\0') {
			break;
		}
		cb(path, &info, user);
	}
	(void)f_closedir(&dir);
	return res;
}

FRESULT SD_DeleteOldFiles(const char *dir, const char *suffix, uint32_t max_files)
{
	if (!dir || max_files == 0) {
		return FR_INVALID_NAME;
	}

	uint32_t count = 0;
	DIR dj;
	FILINFO fno;
	FRESULT res = f_opendir(&dj, dir);
	if (res != FR_OK) {
		return res;
	}

	for (;;) {
		res = f_readdir(&dj, &fno);
		if (res != FR_OK) {
			break;
		}
		if (fno.fname[0] == '\0') {
			break;
		}
		if (fno.fattrib & AM_DIR) {
			continue;
		}
		if (!sd_has_suffix(fno.fname, suffix)) {
			continue;
		}
		count++;
	}
	(void)f_closedir(&dj);

	while (res == FR_OK && count > max_files) {
		DIR dj2;
		FILINFO fno2;
		res = f_opendir(&dj2, dir);
		if (res != FR_OK) {
			return res;
		}
		uint32_t oldest_stamp = 0xFFFFFFFFu;
		char oldest_name[128] = {0};
		for (;;) {
			res = f_readdir(&dj2, &fno2);
			if (res != FR_OK) {
				break;
			}
			if (fno2.fname[0] == '\0') {
				break;
			}
			if (fno2.fattrib & AM_DIR) {
				continue;
			}
			if (!sd_has_suffix(fno2.fname, suffix)) {
				continue;
			}
			uint32_t stamp = ((uint32_t)fno2.fdate << 16) | fno2.ftime;
			if (stamp <= oldest_stamp) {
				oldest_stamp = stamp;
				strncpy(oldest_name, fno2.fname, sizeof(oldest_name) - 1);
			}
		}
		(void)f_closedir(&dj2);
		if (res != FR_OK) {
			return res;
		}
		if (oldest_name[0] == '\0') {
			break;
		}
		char full[256];
		if (snprintf(full, sizeof(full), "%s/%s", dir, oldest_name) <= 0) {
			return FR_INVALID_NAME;
		}
		res = f_unlink(full);
		if (res != FR_OK) {
			return res;
		}
		count--;
	}

	return res;
}

void file_write_float(TCHAR* filename,float* data,int length){
	FIL file;
	FRESULT res = f_open(&file,filename,FA_OPEN_ALWAYS|FA_WRITE|FA_READ);
	if(res == FR_OK){
		UINT bw=0;
		for(uint16_t i = 0;i<length;i++){
			char text[40];
			sprintf(text,"%f\n",data[i]);
			//f_printf(&file,"data=%f\n",data[i]);
			f_puts(text,&file);
		}
		printf("write OK\r\n");
	}else{
		printf("open file fail:%d\r\n",res);
	}
	f_close(&file);
}

void file_read_float(TCHAR* filename,float* data,int length){
	FIL file;
	FRESULT res = f_open(&file,filename,FA_OPEN_ALWAYS|FA_WRITE|FA_READ);
	if(res == FR_OK){
		UINT bw=0;
		char text[40];
		for(uint16_t i = 0;i<length;i++){
			f_gets(text,40,&file);
			sscanf(text,"%f\n",&data[i]);
		}
//		for(int i=0;i<length;i++){
//			printf("%f\r\n",data[i]);
//		}
		printf("read OK\r\n");
	}else{
		printf("open file fail:%d\r\n",res);
	}
	f_close(&file);
}



//    file_write_float("s11o.txt",adc,4096);
//    
//    file_read_float("s11o.txt",bbb,4096);






