#ifndef SD_H
#define SD_H

#include "main.h"
#include "usart.h"
#include "stdio.h"
#include <stdbool.h>
#include <stdint.h>
#include "ff.h"

typedef void (*SD_DirEntryCallback)(const char *path, const FILINFO *info, void *user);

FRESULT SD_Init(void);
FRESULT SD_MkdirRecursive(const char *path);
bool SD_FileExists(const char *path);
bool SD_GetFileSize(const char *path, FSIZE_t *size_out);
FRESULT SD_ListDir(const char *path, SD_DirEntryCallback cb, void *user);
FRESULT SD_DeleteOldFiles(const char *dir, const char *suffix, uint32_t max_files);

void file_write_float(TCHAR* filename,float* data,int length);
void file_read_float(TCHAR* filename,float* data,int length);

#endif
