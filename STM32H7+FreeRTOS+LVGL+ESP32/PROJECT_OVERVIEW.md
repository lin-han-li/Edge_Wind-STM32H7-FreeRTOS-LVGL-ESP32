# 项目介绍：STM32H750XBH6 + ESP8266 + FreeRTOS + LVGL 9.4.0

本仓库是一套可直接落地使用的嵌入式固件工程，目标是把“高速采集 + 本地触控界面 + 外部资源管理 + WiFi 联网与数据上报”整合到 STM32H750XBH6（Cortex-M7）平台上。

工程在 STM32H7 常见难点上做了较完整的工程化处理：Cache/DMA 一致性、超大缓冲的内存放置、启动阶段的资源完整性校验与“无 SD 启动”、以及高波特率串口透传下的容错与自恢复。

---

## 1) 硬件平台（按工程当前实现）

- MCU：STM32H750XBH6
- 显示：800x480 RGB LCD（LTDC），RGB565
- 触摸：I2C 电容触摸（工程使用 `touch_800x480` 驱动）
- 外部 QSPI Flash：W25Q256（32MB）
  - memory-mapped 基址：`0x90000000`
- 外部 SDRAM：FMC（用于双缓冲显存与大块网络 payload 缓冲）
- SD 卡：SDMMC + FatFs（用于首次资源下发与部分参数持久化）
- WiFi：ESP8266（USART2，AT 初始化 + TCP 透传）
- 采样 ADC：AD7606（当前为 GPIO 模拟串行读取）

---

## 2) 工程结构与关键入口

工程底层外设由 STM32CubeMX 生成，应用与业务代码主要集中在 `MDK-ARM/HARDWORK`。

关键入口：

- `Core/Src/main.c`
  - HAL/时钟/外设初始化
  - 启动 TIM2 采样节拍
  - 启动 FreeRTOS 调度器
- `Core/Src/freertos.c`
  - 创建并运行系统任务
  - 使用 `vApplicationTickHook()` 驱动 LVGL tick

---

## 3) FreeRTOS 任务划分与并发模型

任务定义在 `Core/Src/freertos.c`：

- `LVGL_Task`
  - 初始化 LVGL、显示 port、输入 port
  - 周期执行 UI 刷新与 `lv_task_handler()`
  - 使用全局互斥锁 `mutex_id` 序列化 LVGL 操作
- `Main_Task`
  - QSPI FatFs 分区挂载/必要时格式化
  - 从 SD 同步 GUI 资源到 QSPI（若资源不完整或强制同步）
  - 同步期间可挂起 ESP 任务，降低 SD/QSPI/FatFs 竞争
- `ESP8266_Task`
  - 提供 UI 驱动的联网流程（WiFi/TCP/注册/开始上报）
  - 允许上报时执行波形+FFT 更新与发送
- `LED_Task`
  - 心跳灯

设计要点：

- LVGL 的时间基由 FreeRTOS tick hook 提供：`lv_tick_inc(1)`。
- 资源同步期间会置位全局“同步中”标志，用于避免 UI/网络侧同时访问 SD 造成竞态。

---

## 4) 显示链路（LTDC 双缓冲 + Cache 一致性 + 防撕裂）

LVGL 显示适配在：`lvgl-9.4.0/examples/porting/lv_port_disp.c`

- 使用整屏 FULL 渲染 + 双缓冲。
- 显存放在外部 SDRAM。
- flush 回调里执行 `SCB_CleanDCache()`，确保 LTDC 从 SDRAM 读到的是最新画面（STM32H7 必要步骤）。
- LTDC line-event 回调里 reload 配置，把显存切换放到合适时机以降低撕裂。

---

## 5) 触摸链路（Scan 驱动 + LVGL Pointer）

LVGL 输入适配在：`lvgl-9.4.0/examples/porting/lv_port_indev.c`

- 在 read_cb 中调用 `Touch_Scan()` 更新触摸状态。
- 通过宏配置 scroll/long-press 阈值，改善滑动与点击体验。

---

## 6) 采样（AD7606）与采样节拍

采样由 TIM2 中断驱动：

- 定时器配置：`Core/Src/tim.c`
- ISR 入口：`Core/Src/main.c` -> `HAL_TIM_PeriodElapsedCallback()`

当前实现路径：

- TIM2 周期中断触发一次采样节拍：启动转换、等待 BUSY、读取 AD7606 原始值并换算。
- 写入 4 通道的 ping-pong 双缓冲：`4 x AD_ACQ_POINTS`。
- `AD_ACQ_POINTS` 默认 4096：`Core/Inc/ad_acq_buffers.h`。

内存放置：

- 4096 点双缓冲放入 `.axi_sram` 段：`Core/Src/ad_acq_buffers.c`。
- scatter 文件将 `.axi_sram` 映射到 AXI SRAM：`MDK-ARM/STM32H750XBH6.sct`（`0x24000000` 区域）。

---

## 7) FFT（CMSIS-DSP）

FFT 计算放在任务上下文执行（而不是 ISR），避免中断过长。

实现位置：

- `MDK-ARM/HARDWORK/ESP8266/esp8266.c`
  - `ESP_Update_Data_And_FFT()`：识别哪块缓冲新就绪 → 拷贝波形 → `arm_rfft_fast_f32()` + 幅值计算 → 填充 `fft_data[]`
  - 通过 `osDelay(0)` 分段让出 CPU，降低 UI 卡顿概率

默认关系：

- 波形点数：4096
- FFT 点数：2048（N/2）

---

## 8) ESP8266 联网与数据上报

串口与 DMA：

- `Core/Src/usart.c`
  - USART2：2,000,000 波特率，DMA circular RX + DMA TX（ESP8266 通信）
  - USART1：printf 日志输出（高波特率）

ESP 驱动实现：

- `MDK-ARM/HARDWORK/ESP8266/esp8266.c`

关键能力：

- AT 指令完成 WiFi 连接、TCP 建链后进入透传模式。
- DMA 流式接收 + 滑动窗口关键字匹配，解决分包导致关键字被切断的问题。
- 为降低“坏帧/解析失败/重连抖动”，在序列化阶段做了数值钳位与格式约束。
- 上报由 UI 驱动开关：固件不强制开机自动联网，用户可通过“设备连接”流程控制 WiFi/TCP/注册/开始上报。

---

## 9) QSPI 资源系统（首次 SD 同步，之后可无卡启动）

工程把 GUI 图标/字体/拼音词典等资源写入 W25Q256 的固定资源区，并通过 QSPI memory-mapped 方式读取。

资源分区与映射：

- `MDK-ARM/HARDWORK/GUI-Guider_Runtime/gui_resource_map.h`
  - 32MB 划分为：16MB 资源区 + 16MB FatFs 区（可选）
  - 资源完整性 header 放在资源区最后 4KB

同步与完整性校验：

- `MDK-ARM/HARDWORK/GUI-Guider_Runtime/gui_assets_sync.c`
  - 通过 header（magic/version/sizes/checksum）判断 QSPI 资源是否完整
  - 完整则跳过 SD，支持“无 SD 启动”
  - 支持强制同步标记：
    - `0:/gui/update.flag`（SD 侧）
    - `1:/force_sync.flag`（QSPI FatFs 侧）

---

## 10) 编译 / 下载 / 首次运行

工具链：

- Keil MDK-ARM（工程：`MDK-ARM/STM32H750XBH6.uvprojx`）

典型流程：

1. 编译并下载固件。
2. 准备 SD 卡（FAT32），把 `tools/sd_payload/` 的目录结构拷到 SD 根目录。
3. 插卡上电，首次启动会把资源同步到 QSPI。
4. 同步完成后，可拔掉 SD，设备仍可正常启动与运行 UI。

---

## 11) 仓库注意事项（凭据/配置）

- `MDK-ARM/HARDWORK/ESP8266/esp8266_config.h` 可能包含 WiFi 账号密码与服务器地址。
  - 建议把它视为本地私有配置。
  - 不要把真实凭据提交到公开仓库。

---

## 12) 新手快速定位（先看这些文件）

- 启动、时钟、外设：`Core/Src/main.c`
- 任务与系统编排：`Core/Src/freertos.c`
- 采样节拍：`Core/Src/tim.c`
- 采样双缓冲：`Core/Src/ad_acq_buffers.c`
- 上报与 FFT：`MDK-ARM/HARDWORK/ESP8266/esp8266.c`
- QSPI 驱动：`MDK-ARM/HARDWORK/W25Q256/qspi_w25q256.c`
- 资源同步与校验：`MDK-ARM/HARDWORK/GUI-Guider_Runtime/gui_assets_sync.c`
- LVGL 显示/触摸 port：`lvgl-9.4.0/examples/porting/lv_port_disp.c`、`lvgl-9.4.0/examples/porting/lv_port_indev.c`
