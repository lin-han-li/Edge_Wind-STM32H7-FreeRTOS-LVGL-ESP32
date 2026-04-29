#ifndef GUI_ASSETS_SYNC_H
#define GUI_ASSETS_SYNC_H

#include "ff.h"
#include <stdbool.h>
#include "gui_resource_map.h"

#define EW_QSPI_SYNC_MODE 0
/* ================= QSPI<->SD 同步策略（编译期可选） =================
 *
 * EW_QSPI_SYNC_MODE:
 *  - 0: AUTO（默认）—— QSPI 资源完整则跳过 SD；不完整才尝试 SD 同步
 *  - 1: ALWAYS（用于“更新 bin 文件”）—— 只要 SD 可用就执行同步，即使 QSPI 已完整
 *  - 2: NEVER —— 永不访问 SD（QSPI 不完整则返回 FR_NOT_READY）
 *
对应文件               
 * 额外运行时开关（不改宏也能触发一次同步）：
 *  - 在 QSPI FatFs 分区放置文件: 1:/force_sync.flag
 *    启动时将强制执行一次 SD->QSPI 同步，同步结束后自动删除该文件。
 */
#ifndef EW_QSPI_SYNC_MODE
#define EW_QSPI_SYNC_MODE 0
#endif

FRESULT GUI_Assets_SyncFromSD(void);
bool GUI_Assets_QSPIReady(void);
bool GUI_Assets_GetResSize(gui_res_id_t id, uint32_t * size_out);

/* SD 同步进度标志（供 UI 配置界面检测，避免在 QSPI 同步期间访问 SD） */
extern volatile uint8_t g_qspi_sd_sync_in_progress;

#endif /* GUI_ASSETS_SYNC_H */
