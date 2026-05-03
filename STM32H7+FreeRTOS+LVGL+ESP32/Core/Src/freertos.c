/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * File Name          : freertos.c
 * Description        : Code for freertos applications
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "led.h"
#include "touch_800x480.h"
#include "tim.h"
#include "stm32h7xx_hal.h"
#include "SD.h"
#include "fatfs.h"
#include "tim.h"

#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
// Demo 已移除，使用自定义 EdgeWind UI
#include "EdgeWind_UI/edgewind_ui.h"
#include "edge_comm.h"
#include "ESP32SPI/esp32_spi_debug.h"
#include "qspi_w25q256.h"
#include "GUI-Guider_Runtime/gui_assets_sync.h"
#include <string.h>
#include <stdio.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
void HAL_Delay(uint32_t Delay)
{
  if (Delay == 0U)
  {
    return;
  }

  uint32_t tickstart = HAL_GetTick();

  if ((osKernelGetState() == osKernelRunning) && (__get_IPSR() == 0U))
  {
    while ((HAL_GetTick() - tickstart) < Delay)
    {
      osDelay(1);
    }
    return;
  }

  while ((HAL_GetTick() - tickstart) < Delay)
  {
    /* busy wait before scheduler starts or in ISR */
  }
}
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

#ifndef W25Q256_SELFTEST_ENABLE
/* 默认关闭：避免每次上电都擦写外部 Flash。
 * 需要自检时手动改为 1，或通过编译宏覆盖。
 */
#define W25Q256_SELFTEST_ENABLE 0
#endif

/* 自检等级：
 * 1: 单扇区擦除 + 单页写入 + 读回校验（最快，推荐长期保留）
 * 2: 增加跨页/跨扇区写入读回（更贴近真实存储使用）
 * 3: 预留（可在后续加入 16MB 边界/更大范围测试）
 */
#ifndef W25Q256_SELFTEST_LEVEL
/* 默认自检等级（仅在 W25Q256_SELFTEST_ENABLE=1 时生效） */
#define W25Q256_SELFTEST_LEVEL 2
#endif

/* 内存映射读取校验（可选）：开启后会将 QSPI 切到 memory-mapped 再 memcpy 读回校验。
 * 注意：memory-mapped 与 cache 行为相关，若你担心影响系统其他读写，保持 0 即可。
 */
#ifndef W25Q256_SELFTEST_USE_MEMORY_MAPPED
#define W25Q256_SELFTEST_USE_MEMORY_MAPPED 0
#endif

/* 启动阶段同步资源时，暂时挂起 ESP 任务，避免文件系统/总线竞争 */
#ifndef EW_SUSPEND_ESP_DURING_SYNC
#define EW_SUSPEND_ESP_DURING_SYNC 1
#endif

#ifndef ESP32_SPI_AUTOTEST_ON_BOOT
#define ESP32_SPI_AUTOTEST_ON_BOOT 0
#endif

#define W25Q256_SELFTEST_ADDR   (0x1FF0000u) /* 32MB Flash 末尾附近，避开常用数据区 */
#define W25Q256_SELFTEST_LEN    (256u)       /* 单页测试 */
#define W25Q256_CROSS_LEN       (512u)       /* 跨页/跨扇区测试长度 */

static int8_t W25Q256_SelfTest_Small(void)
{
  uint8_t tx[W25Q256_SELFTEST_LEN];
  uint8_t rx[W25Q256_SELFTEST_LEN];
  uint32_t id;
  int8_t ret;

  ret = QSPI_W25Qxx_Init();
  id  = QSPI_W25Qxx_ReadID();
  printf("[W25Q256] init ret=%d, JEDEC=0x%06lX\r\n", (int)ret, (unsigned long)id);
  if (ret != QSPI_W25Qxx_OK)
  {
    return ret;
  }

  ret = QSPI_W25Qxx_SectorErase(W25Q256_SELFTEST_ADDR);
  printf("[W25Q256] sector erase @0x%08lX ret=%d\r\n", (unsigned long)W25Q256_SELFTEST_ADDR, (int)ret);
  if (ret != QSPI_W25Qxx_OK)
  {
    return ret;
  }

  for (uint32_t i = 0; i < W25Q256_SELFTEST_LEN; i++)
  {
    tx[i] = (uint8_t)(i ^ 0xA5u);
    rx[i] = 0;
  }

  ret = QSPI_W25Qxx_WritePage(tx, W25Q256_SELFTEST_ADDR, (uint16_t)W25Q256_SELFTEST_LEN);
  printf("[W25Q256] write page ret=%d\r\n", (int)ret);
  if (ret != QSPI_W25Qxx_OK)
  {
    return ret;
  }

  ret = QSPI_W25Qxx_ReadBuffer(rx, W25Q256_SELFTEST_ADDR, W25Q256_SELFTEST_LEN);
  printf("[W25Q256] read back ret=%d\r\n", (int)ret);
  if (ret != QSPI_W25Qxx_OK)
  {
    return ret;
  }

  if (memcmp(tx, rx, W25Q256_SELFTEST_LEN) != 0)
  {
    for (uint32_t i = 0; i < W25Q256_SELFTEST_LEN; i++)
    {
      if (tx[i] != rx[i])
      {
        printf("[W25Q256] verify FAIL @+%lu tx=0x%02X rx=0x%02X\r\n",
               (unsigned long)i, (unsigned int)tx[i], (unsigned int)rx[i]);
        break;
      }
    }
    return (int8_t)W25Qxx_ERROR_TRANSMIT;
  }

  printf("[W25Q256] selftest PASS (erase+program+readback)\r\n");
  return QSPI_W25Qxx_OK;
}

static int8_t W25Q256_SelfTest_CrossPageAndSector(void)
{
  const uint32_t base0 = W25Q256_SELFTEST_ADDR;          /* 4KB 扇区对齐 */
  const uint32_t base1 = W25Q256_SELFTEST_ADDR + 0x1000; /* 下一个 4KB 扇区 */
  const uint32_t start = base0 + 0x0F80u;                /* 故意靠近扇区末尾，触发跨扇区 */
  const uint32_t len   = W25Q256_CROSS_LEN;              /* 512B：跨页且跨扇区 */

  uint8_t tx[W25Q256_CROSS_LEN];
  uint8_t rx[W25Q256_CROSS_LEN];

  printf("[W25Q256] cross-sector test: erase 2 sectors, start=0x%08lX len=%lu\r\n",
         (unsigned long)start, (unsigned long)len);

  int8_t ret = QSPI_W25Qxx_SectorErase(base0);
  printf("[W25Q256] erase sector0 @0x%08lX ret=%d\r\n", (unsigned long)base0, (int)ret);
  if (ret != QSPI_W25Qxx_OK) return ret;

  ret = QSPI_W25Qxx_SectorErase(base1);
  printf("[W25Q256] erase sector1 @0x%08lX ret=%d\r\n", (unsigned long)base1, (int)ret);
  if (ret != QSPI_W25Qxx_OK) return ret;

  /* 擦除校验：读少量数据确认为 0xFF */
  {
    uint8_t blank[64];
    ret = QSPI_W25Qxx_ReadBuffer(blank, base0, sizeof(blank));
    if (ret != QSPI_W25Qxx_OK) return ret;
    for (uint32_t i = 0; i < sizeof(blank); i++)
    {
      if (blank[i] != 0xFFu)
      {
        printf("[W25Q256] erase verify FAIL sector0 @+%lu =0x%02X\r\n",
               (unsigned long)i, (unsigned int)blank[i]);
        return (int8_t)W25Qxx_ERROR_Erase;
      }
    }

    ret = QSPI_W25Qxx_ReadBuffer(blank, base1, sizeof(blank));
    if (ret != QSPI_W25Qxx_OK) return ret;
    for (uint32_t i = 0; i < sizeof(blank); i++)
    {
      if (blank[i] != 0xFFu)
      {
        printf("[W25Q256] erase verify FAIL sector1 @+%lu =0x%02X\r\n",
               (unsigned long)i, (unsigned int)blank[i]);
        return (int8_t)W25Qxx_ERROR_Erase;
      }
    }
    printf("[W25Q256] erase verify PASS (both sectors blank)\r\n");
  }

  for (uint32_t i = 0; i < len; i++)
  {
    tx[i] = (uint8_t)((i * 37u) ^ 0x5Au);
    rx[i] = 0;
  }

  ret = QSPI_W25Qxx_WriteBuffer(tx, start, len);
  printf("[W25Q256] write buffer (cross) ret=%d\r\n", (int)ret);
  if (ret != QSPI_W25Qxx_OK) return ret;

  ret = QSPI_W25Qxx_ReadBuffer(rx, start, len);
  printf("[W25Q256] read buffer (cross) ret=%d\r\n", (int)ret);
  if (ret != QSPI_W25Qxx_OK) return ret;

  if (memcmp(tx, rx, len) != 0)
  {
    for (uint32_t i = 0; i < len; i++)
    {
      if (tx[i] != rx[i])
      {
        printf("[W25Q256] cross verify FAIL @+%lu tx=0x%02X rx=0x%02X\r\n",
               (unsigned long)i, (unsigned int)tx[i], (unsigned int)rx[i]);
        break;
      }
    }
    return (int8_t)W25Qxx_ERROR_TRANSMIT;
  }

#if W25Q256_SELFTEST_USE_MEMORY_MAPPED
  ret = QSPI_W25Qxx_MemoryMappedMode();
  printf("[W25Q256] enter memory-mapped ret=%d\r\n", (int)ret);
  if (ret == QSPI_W25Qxx_OK)
  {
    uint8_t mm[W25Q256_CROSS_LEN];
    memcpy(mm, (uint8_t *)0x90000000u + start, len);
    if (memcmp(tx, mm, len) != 0)
    {
      printf("[W25Q256] memory-mapped verify FAIL\r\n");
      return (int8_t)W25Qxx_ERROR_TRANSMIT;
    }
    printf("[W25Q256] memory-mapped verify PASS\r\n");
  }
#endif

  printf("[W25Q256] cross-sector test PASS\r\n");
  return QSPI_W25Qxx_OK;
}

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for LVGL940 */
osThreadId_t LVGL940Handle;
const osThreadAttr_t LVGL940_attributes = {
  .name = "LVGL940",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for LED */
osThreadId_t LEDHandle;
const osThreadAttr_t LED_attributes = {
  .name = "LED",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for Main */
osThreadId_t MainHandle;
const osThreadAttr_t Main_attributes = {
  .name = "Main",
  .stack_size = 8192 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for EdgeComm */
osThreadId_t EdgeCommHandle;
const osThreadAttr_t EdgeComm_attributes = {
  .name = "EdgeComm",
  .stack_size = 5128 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for DSPAlgo */
osThreadId_t DSPAlgoHandle;
const osThreadAttr_t DSPAlgo_attributes = {
  .name = "DSPAlgo",
  .stack_size = 3072 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for Upload */
osThreadId_t UploadHandle;
const osThreadAttr_t Upload_attributes = {
  .name = "Upload",
  .stack_size = 3072 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
extern const osMutexAttr_t Thread_Mutex_attr;
void ESP32_SPI_TestTask(void *argument);

/* USER CODE END FunctionPrototypes */

void LVGL_Task(void *argument);
void LED_Task(void *argument);
void Main_Task(void *argument);
void EdgeComm_Task(void *argument);
void DSP_Algorithm_Task(void *argument);
void Upload_Task(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationTickHook(void);

/* USER CODE BEGIN 3 */
void vApplicationTickHook(void)
{
  /* This function will be called by each tick interrupt if
  configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h. User code can be
  added here, but the tick hook is called from an interrupt context, so
  code must not attempt to block, and only the interrupt safe FreeRTOS API
  functions can be used (those that end in FromISR()). */

  lv_tick_inc(1);
}
/* USER CODE END 3 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  mutex_id = osMutexNew(&Thread_Mutex_attr);
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of LVGL940 */
  LVGL940Handle = osThreadNew(LVGL_Task, NULL, &LVGL940_attributes);

  /* creation of LED */
  LEDHandle = osThreadNew(LED_Task, NULL, &LED_attributes);

  /* creation of Main */
  MainHandle = osThreadNew(Main_Task, NULL, &Main_attributes);

  /* creation of EdgeComm */
  EdgeCommHandle = osThreadNew(EdgeComm_Task, NULL, &EdgeComm_attributes);

  /* creation of DSPAlgo */
  DSPAlgoHandle = osThreadNew(DSP_Algorithm_Task, NULL, &DSPAlgo_attributes);

  /* creation of Upload */
  UploadHandle = osThreadNew(Upload_Task, NULL, &Upload_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
#if ESP32_SPI_AUTOTEST_ON_BOOT
  static const osThreadAttr_t ESP32_SPI_Test_attributes = {
    .name = "ESP32SPITest",
    .stack_size = 4096 * 4,
    .priority = (osPriority_t) osPriorityLow,
  };
  osThreadNew(ESP32_SPI_TestTask, NULL, &ESP32_SPI_Test_attributes);
#endif
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_LVGL_Task */
/**
  * @brief  Function implementing the LVGL940 thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_LVGL_Task */
void LVGL_Task(void *argument)
{
  /* USER CODE BEGIN LVGL_Task */
  uint32_t time;
  UBaseType_t min_hw = (UBaseType_t)0xFFFFFFFFu;
  TickType_t last_log = 0;
  //  // LVGL图形库初始化三件套
  lv_init();            // 初始化LVGL核心库（内存管理、内部变量等）
  lv_port_disp_init();  // 初始化显示驱动接口（配置帧缓冲区、注册刷新回调）
  lv_port_indev_init(); // 初始化输入设备接口（注册触摸屏/编码器驱动）
  
  /* 初始化 EdgeWind 自定义 UI */
  edgewind_ui_init();
  /* Infinite loop */
  for(;;)
  {

    /* === 临界区开始（保护LVGL操作）=== */
    osMutexAcquire(mutex_id, osWaitForever);

    /* EdgeWind UI 数据刷新 */
    edgewind_ui_refresh();
    
    /* LVGL 核心处理 */
    lv_task_handler();

    osMutexRelease(mutex_id);
    /* === 临界区结束 === */

    /* 低频监测 LVGL 线程栈水位（观察是否逼近溢出） */
    TickType_t now = xTaskGetTickCount();
    if ((now - last_log) > pdMS_TO_TICKS(2000)) {
      UBaseType_t hw = uxTaskGetStackHighWaterMark(NULL);
      if (hw < min_hw) {
        min_hw = hw;
        printf("[LVGL] stack HW=%lu words, freeHeap=%lu\r\n",
               (unsigned long)hw,
               (unsigned long)xPortGetFreeHeapSize());
      }
      last_log = now;
    }

    //    /* 周期延时（关键性能参数）*/
    osDelay(LV_DEF_REFR_PERIOD + 1); // 保持屏幕刷新率稳定（典型值30ms≈33FPS）

    // 注意：
    // 1. 延时过短：导致刷新不全（屏幕闪烁）
    // 2. 延时过长：界面响应迟滞
    // 3. LV_DISP_DEF_REFR_PERIOD应与屏显参数匹配（在lv_conf.h中配置）

    // 调试语句（使用时需注意串口输出可能影响实时性）
    // printf("GUI task heartbeat\r\n");
  }
  /* USER CODE END LVGL_Task */
}

/* USER CODE BEGIN Header_LED_Task */
/**
* @brief Function implementing the LED thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_LED_Task */
void LED_Task(void *argument)
{
  /* USER CODE BEGIN LED_Task */
  /* Infinite loop */
  for(;;)
  {
    LED1_Toggle;
    osDelay(500);
  }
  /* USER CODE END LED_Task */
}

/* USER CODE BEGIN Header_Main_Task */
/**
* @brief Function implementing the Main thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Main_Task */
void Main_Task(void *argument)
{
  /* USER CODE BEGIN Main_Task */

#ifdef EW_SUSPEND_ESP_DURING_SYNC
  if (EdgeCommHandle) {
    osThreadSuspend(EdgeCommHandle);
    printf("[QSPI_FS] suspended ESP task during sync\r\n");
  }
#endif

  if (mutex_id) {
    osMutexAcquire(mutex_id, osWaitForever);
    printf("[QSPI_FS] locked LVGL mutex during sync\r\n");
  }

#if W25Q256_SELFTEST_ENABLE
  osDelay(200); /* 让系统先跑起来，避免和启动阶段串口输出打架 */
  (void)W25Q256_SelfTest_Small();
#if (W25Q256_SELFTEST_LEVEL >= 2)
  (void)W25Q256_SelfTest_CrossPageAndSector();
#endif
#endif

  uint32_t t0 = HAL_GetTick();
  FRESULT fr_qspi = QSPIFS_MountOrMkfs();
  uint32_t t1 = HAL_GetTick();
  printf("[QSPI_FS] QSPIFS_MountOrMkfs=%d, dt=%lu ms\r\n", (int)fr_qspi, (unsigned long)(t1 - t0));

  FRESULT fr_sync = GUI_Assets_SyncFromSD();
  uint32_t t2 = HAL_GetTick();
  printf("[QSPI_FS] GUI_Assets_SyncFromSD=%d, dt=%lu ms\r\n", (int)fr_sync, (unsigned long)(t2 - t1));
  printf("[QSPI_FS] Main stack HW=%lu words, freeHeap=%lu bytes\r\n",
         (unsigned long)uxTaskGetStackHighWaterMark(NULL),
         (unsigned long)xPortGetFreeHeapSize());

  if (mutex_id) {
    osMutexRelease(mutex_id);
    printf("[QSPI_FS] unlocked LVGL mutex\r\n");
  }

#ifdef EW_SUSPEND_ESP_DURING_SYNC
  if (EdgeCommHandle) {
    osThreadResume(EdgeCommHandle);
    printf("[QSPI_FS] resumed ESP task\r\n");
  }
#endif

  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Main_Task */
}

/* USER CODE BEGIN Header_EdgeComm_Task */
/**
* @brief Function implementing the EdgeComm thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_EdgeComm_Task */
void EdgeComm_Task(void *argument)
{
  /* USER CODE BEGIN EdgeComm_Task */
  osThreadId_t self = osThreadGetId();
  if (self)
  {
    osThreadSetPriority(self, osPriorityAboveNormal);
  }
  ESP_Console_Init();
  /* 不再开机自启动连接：由 DeviceConnect 界面按钮驱动 */
  ESP_UI_TaskInit();
  if (self)
  {
    osThreadSetPriority(self, osPriorityNormal);
  }
  uint8_t boot_autoreconnect_checked = 0;
  uint32_t boot_autoreconnect_start = HAL_GetTick();
  uint32_t boot_autoreconnect_next = boot_autoreconnect_start + 3000U;
  /* Infinite loop */
  for(;;)
  {
    ESP_UI_TaskPoll();
    ESP_Console_Poll();
    if (!boot_autoreconnect_checked)
    {
      uint32_t now = HAL_GetTick();
      if ((int32_t)(now - boot_autoreconnect_next) >= 0)
      {
        bool auto_reconnect_en = false;
        bool last_reporting = false;
        boot_autoreconnect_next = now + 1000U;
        bool sd_runtime_ok = ESP_Config_LoadRuntimeFromSD();
        if (ESP_AutoReconnect_Read(&auto_reconnect_en, &last_reporting))
        {
          boot_autoreconnect_checked = 1U;
          printf("[ESP_BOOT] autoreconnect cfg: enabled=%u last_reporting=%u\r\n",
                 auto_reconnect_en ? 1U : 0U,
                 last_reporting ? 1U : 0U);
          if (auto_reconnect_en && last_reporting && sd_runtime_ok)
          {
            printf("[ESP_BOOT] queue AUTO connect/report from SD flag\r\n");
            (void)ESP_UI_SendCmd(ESP_UI_CMD_AUTO_CONNECT);
          }
          else if (auto_reconnect_en && last_reporting && !sd_runtime_ok)
          {
            printf("[ESP_BOOT] autoreconnect skipped: SD runtime config invalid\r\n");
          }
        }
        else if ((now - boot_autoreconnect_start) > 30000U)
        {
          boot_autoreconnect_checked = 1U;
          printf("[ESP_BOOT] autoreconnect cfg unavailable, skip boot auto\r\n");
        }
      }
    }
    /* EdgeComm task now handles only UI/console/control.
       DSP and upload run in independent producer/consumer tasks. */
    osDelay(5);
  }
  /* USER CODE END EdgeComm_Task */
}

/* USER CODE BEGIN Header_DSP_Algorithm_Task */
/**
* @brief Function implementing the DSPAlgo thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_DSP_Algorithm_Task */
void DSP_Algorithm_Task(void *argument)
{
  /* USER CODE BEGIN DSP_Algorithm_Task */
  (void)argument;
  /* High-priority local path: consume completed acquisition windows,
     compute FFT/fault features, and publish latest upload snapshots. */
  for (;;)
  {
    ESP_Update_Data_And_FFT();
    osDelay(1);
  }
  /* USER CODE END DSP_Algorithm_Task */
}

/* USER CODE BEGIN Header_Upload_Task */
/**
* @brief Function implementing the Upload thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_Upload_Task */
void Upload_Task(void *argument)
{
  /* USER CODE BEGIN Upload_Task */
  (void)argument;
  /* Low-priority consumer: upload only the latest processed snapshot.
     If ESP32/cloud is busy, DSP keeps running and old upload snapshots are dropped. */
  for (;;)
  {
    if (ESP_UI_IsReporting())
    {
      if (ESP_ServerReportFull())
      {
        ESP_Post_Data();
      }
      else
      {
        ESP_Post_Summary();
      }
    }
    osDelay(2);
  }
  /* USER CODE END Upload_Task */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void ESP32_SPI_TestTask(void *argument)
{
  (void)argument;
  osDelay(10000);
  ESP32_SPI_DebugRunPing();
  osThreadTerminate(osThreadGetId());
  for (;;) {
    osDelay(osWaitForever);
  }
}

/* USER CODE END Application */
