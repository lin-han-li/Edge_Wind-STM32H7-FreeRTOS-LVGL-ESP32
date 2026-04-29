# STM32H750XBH6 + ESP8266 + FreeRTOS + LVGL 9.4.0

基于 STM32H750XBH6 的嵌入式系统，集成 ESP8266 WiFi 通信、FreeRTOS 实时操作系统和 LVGL 9.4.0 图形界面。

## 硬件平台

- **MCU**: STM32H750XBH6 (Cortex-M7 @ 480MHz, 128KB Flash, 1MB RAM)
- **外部 Flash**: W25Q256 (32MB QSPI Flash)
- **外部 RAM**: SDRAM (FMC 接口)
- **显示**: 800x480 RGB LCD (LTDC 接口)
- **触摸**: GT911/FT5206 电容触摸 (I2C 接口)
- **通信**: ESP8266 WiFi 模块 (UART 接口)
- **存储**: SD 卡 (SDMMC 接口)

## 软件架构

### 核心组件

- **RTOS**: FreeRTOS (任务调度、互斥锁、事件管理)
- **GUI**: LVGL 9.4.0 (轻量级图形库)
- **文件系统**: FatFs (SD 卡 + QSPI Flash 双挂载)
- **WiFi**: ESP8266 AT 固件 (TCP/IP 通信)

### 目录结构

```
├── Core/                   # STM32 HAL 核心代码
│   ├── Inc/               # 头文件
│   └── Src/               # 源文件 (main.c, freertos.c, etc.)
├── Drivers/               # STM32 HAL 驱动库
├── FATFS/                 # FatFs 文件系统
│   ├── App/              # FatFs 应用层
│   └── Target/           # 磁盘 I/O 驱动 (SD + QSPI)
├── lvgl-9.4.0/           # LVGL 图形库
├── Middlewares/          # FreeRTOS + CMSIS-RTOS2
├── MDK-ARM/              # Keil 工程文件
│   └── HARDWORK/         # 应用层代码
│       ├── EdgeWind_UI/          # 启动动画
│       ├── GUI-Guider_Runtime/   # GUI Guider 生成的界面
│       ├── GUI-Guider_Source/    # GUI Guider 源工程
│       ├── LCD/                  # LCD 驱动
│       ├── Touch/                # 触摸驱动
│       ├── W25Q256/              # QSPI Flash 驱动
│       └── ESP8266/              # ESP8266 驱动
└── tools/                # 工具和资源
    └── sd_payload/       # SD 卡资源文件 (字体、图标)
```

## 功能特性

### ✅ 已实现

1. **GUI 资源管理**
   - 字体和图标从 W25Q256 外部 Flash 加载
   - SD 卡首次启动时自动同步资源到 QSPI
   - 支持 binfont 压缩字体格式
   - 图标使用 RGB565A8 格式

2. **文件系统**
   - SD 卡挂载到 `0:/`
   - QSPI Flash 挂载到 `1:/`
   - FatFs 双盘符支持
   - 资源完整性校验

3. **WiFi 通信**
   - ESP8266 AT 指令控制
   - TCP 透传模式
   - 4 通道数据上报
   - 服务器下发命令接收

4. **UI 界面**
   - 3 页主界面，每页 6 个图标
   - 中文字体显示 (20pt + 60pt)
   - 手势滑动切换页面
   - 屏幕转场保护防止卡死

5. **电源管理**
   - 无 SD 卡时快速启动 (6ms)
   - 资源完全依赖外部 Flash
   - 内部 Flash 仅存代码 (节省 ~800KB)

### 系统配置

#### LVGL 配置 (`lvgl-9.4.0/lv_conf.h`)

```c
#define LV_USE_FS_FATFS              1      // 启用 FatFs 文件系统
#define LV_FS_FATFS_LETTER           'Q'    // 驱动字母 Q: 映射到 1: (QSPI)
#define LV_FS_FATFS_PATH             "1:"   // FatFs 路径映射
#define LV_USE_FONT_COMPRESSED       1      // 启用字体压缩支持
#define LV_FONT_SOURCE_HAN_SANS_SC_16_CJK  1  // 中文字体兜底
```

#### FreeRTOS 任务配置

| 任务名称 | 栈大小 | 优先级 | 功能 |
|---------|--------|--------|------|
| Main_Task | 16KB | Normal | 资源同步、GUI 初始化 |
| LVGL940 | 16KB | High | LVGL 渲染 (5ms tick) |
| ESP8266_Task | 8KB | Normal | WiFi 数据收发 |

#### 内存分配

- **堆大小**: 2MB (lvgl heap)
- **LVGL 缓冲区**: 双缓冲 (800x480 RGB565)
- **FatFs 缓冲区**: 512 字节扇区缓冲

## 快速开始

### 1. 环境准备

- **IDE**: Keil MDK-ARM v5.42+
- **编译器**: ARM Compiler 6.23
- **调试器**: J-Link / ST-Link
- **烧录工具**: STM32CubeProgrammer (可选)

### 2. 首次烧录

1. 使用 Keil 打开 `MDK-ARM/STM32H750XBH6.uvprojx`
2. 编译项目 (Rebuild All)
3. 下载程序到 MCU

### 3. 资源准备

**重要**: 首次启动需要 SD 卡同步资源到 QSPI Flash

1. 准备 SD 卡（FAT32 格式）
2. 复制 `tools/sd_payload/` 目录结构到 SD 卡根目录：
   ```
   SD卡根目录/
   ├── fonts/
   │   ├── SourceHanSerifSC_Regular_20.bin
   │   └── SourceHanSerifSC_Regular_60.bin
   └── gui/
       ├── icon_01_rtmon_RGB565A8_100x100.bin
       ├── icon_02_fault_RGB565A8_100x100.bin
       └── ... (共14个图标)
   ```
3. 插入 SD 卡，启动 MCU
4. 等待资源同步完成（约 9 秒，日志显示 `sync done`）
5. 同步完成后可拔出 SD 卡，后续启动无需 SD 卡

### 4. 强制更新资源

如果需要更新 QSPI 中的资源：

1. 在 SD 卡根目录创建 `gui/update.flag` 空文件
2. 重启 MCU，系统会重新同步所有资源
3. 同步完成后 `update.flag` 会被自动删除

## 调试说明

### 串口日志

- **USART1**: 系统日志输出 (115200-8-N-1)
- **USART2**: ESP8266 通信

### 关键日志

```
[QSPI_FS] QSPI assets complete, skip SD card access  ← 资源完整，跳过 SD
[GUI_ASSETS] binfont 20 load=0xXXXXXXXX              ← 字体加载成功
[GUI_ASSETS] icon ok: Q:/gui/icon_XX...              ← 图标加载成功
[ESP] 系统就绪（4通道），开始上报数据             ← WiFi 连接成功
```

### 常见问题

**Q: 字体显示为方框？**  
A: 检查 `LV_USE_FONT_COMPRESSED = 1` 是否启用

**Q: 拔掉 SD 卡后启动卡死？**  
A: 确保首次同步已完成，QSPI 中存在 `1:/gui/.ok` 标记文件

**Q: 图标不显示？**  
A: 检查 QSPI 资源是否完整，查看日志 `[GUI_ASSETS] icon ok` 信息

**Q: 内存不足？**  
A: 检查 FreeRTOS 堆大小配置，确保 LVGL 缓冲区分配成功

## 性能指标

- **启动时间** (有 SD 卡首次): ~9 秒 (含资源同步)
- **启动时间** (无 SD 卡): ~1 秒
- **GUI 帧率**: 30 FPS (LVGL 渲染)
- **WiFi 上报频率**: 500ms/次 (4 通道数据)
- **Flash 占用**: 
  - Code: ~533KB
  - RO-data: ~20KB (已移除内置资源)
  - RW-data: ~54KB
  - ZI-data: ~210KB

## 贡献指南

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交修改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 提交 Pull Request

## 许可证

请根据项目实际情况添加许可证信息。

## 联系方式

如有问题请提交 Issue 或联系项目维护者。

---

**注意**: 本项目完全依赖外部 W25Q256 Flash 存储 GUI 资源，请确保 QSPI 连接正常且资源已正确同步。
