#ifndef SD_DISKIO_USER_H
#define SD_DISKIO_USER_H

#include "ff_gen_drv.h"

/* 自定义 SD DiskIO 驱动（放在 SD_Card 下，避免 CubeMX 覆盖） */
extern const Diskio_drvTypeDef SD_User_Driver;

#endif /* SD_DISKIO_USER_H */
