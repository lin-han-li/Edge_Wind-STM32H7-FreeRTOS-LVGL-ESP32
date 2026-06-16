# EdgeWind 三端交接 - 监测端 STM32/ESP32

更新日期：2026-06-16

本文是监测端自己的交接文档，用于和 AI 训练端、Web/云端对齐。当前有效基线是 v68 单模型固件，不再按 v6.3 双模型 guard/router 方案推进。

## 三端目录索引

从监测端视角，当前三端目录如下。后续让各端互看文档时，优先从这些目录开始，不要临时猜路径。

### 本端：监测端 STM32/ESP32

- 仓库根目录：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32`
- STM32 主工程：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\STM32H7+FreeRTOS+LVGL+ESP32`
- Keil 工程：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\STM32H7+FreeRTOS+LVGL+ESP32\MDK-ARM\STM32H750XBH6.uvprojx`
- ESP32 协处理器工程：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\esp32_spi_coprocessor`
- 本端交接文档目录：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\docs`
- 本端当前交接入口：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\docs\three_side_handoff_monitor_stm32.md`

### 另一端：AI 训练端

- 根目录：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training`
- 文档目录：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs`
- 当前 v68 交接文档：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\v68_wind_sensor_public_fused_single_stm32_handoff.md`
- 当前 v68 STM32 部署包：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_deploy_packages\dataset_v68_wind_sensor_public_fused_single_v6_single7_20260616_031448`
- 当前 v68 板端测试向量：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_test_vectors\dataset_v68_wind_sensor_public_fused_single_v6_single7_20260616_031448`

### 另一端：Web/云端

- 本地 Web 工程：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System`
- 本地 Web 文档目录：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System\docs`
- 本地 Web 入口文档：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System\README.md`
- 阿里云部署文档：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System\docs\阿里云部署实现详解.md`
- 阿里云 SSH 材料目录：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\ALiYunFuWuQi`
- 云端已知代码目录：`/var/www/edge_wind/Edge_Wind_System`
- 云端 systemd 服务：`edge_wind.service`

## 本端文档维护规则

- 本文件只维护监测端视角的三端交接事实，不写宣传性描述。
- AI 模型契约以 AI 训练端最新 handoff 为准；当前是 v68 单模型。
- Web/云端能力以本地 `Edge_Wind_System` 与云端部署文档为准；DeepSeek 只写 Web 异步辅助诊断。
- 若模型输入、类别、单位、ESP32 payload、Web 上报 JSON 任一项变化，必须同步更新本文件。
- 若 CubeMX、Keil 下载配置、时钟源、Flash algorithm 变化，必须同步更新本文件。

## 已读取的其他端文档

- AI 训练端：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\v68_wind_sensor_public_fused_single_stm32_handoff.md`
- AI 训练端历史移植说明：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\stm32_porting.md`
- Web 上位机：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System\README.md`
- 云端部署：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System\docs\阿里云部署实现详解.md`
- 阿里云 SSH 接入：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\ALiYunFuWuQi\README.md`
- ESP32 协处理器：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\esp32_spi_coprocessor\README.md`
- STM32 固件说明：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\STM32H7+FreeRTOS+LVGL+ESP32\PROJECT_OVERVIEW.md`

## 当前监测端状态

- 主工程：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\STM32H7+FreeRTOS+LVGL+ESP32`
- Keil 工程：`STM32H7+FreeRTOS+LVGL+ESP32\MDK-ARM\STM32H750XBH6.uvprojx`
- MCU：`STM32H750XBH6`
- 本地任务链路：
  - `DSP_Algorithm_Task` 高优先级消费采样窗口，执行 FFT、特征提取和 AI 推理。
  - `Upload_Task` 低优先级上传最新处理快照；ESP32/云端忙时允许丢弃旧上传快照，DSP 不被阻塞。
- 当前已烧录固件：v68 AI 模型版本；2026-06-16 已完成 Keil 编译下载验证。清理项目目录后不保留临时 build/flash log，后续需要时重新生成。

## AI 模型契约

当前以 AI 训练端 v68 handoff 为准：

- 模型族：`dataset_v68_wind_sensor_public_fused_single_v6`
- 部署包：`dataset_v68_wind_sensor_public_fused_single_v6_single7_20260616_031448`
- 网络形态：单个 7 类 `network`
- 输入：
  - `X_dwt[104]`
  - `X_feat[116]`
  - `X_spec[512,4]`
- 输出：`probabilities[7]`
- 类别：
  - `E00 normal`
  - `E01 ac_coupling`
  - `E02 insulation`
  - `E03 cap_aging`
  - `E04 igbt_fault`
  - `E05 bus_ground`
  - `E06 pwm_abnormal`
- 不包含：
  - raw-lite 输入
  - E00 guard
  - router
  - 第二阶段模型
  - 硬规则 masking

训练端报告中的验证范围是 val/test/hil_holdout split，不代表真实风场长期准确率。

## 已接入的模型文件

STM32 工程中已替换：

- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network.c`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network.h`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_config.h`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_data.c`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_data.h`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_data_params.c`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_data_params.h`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_generate_report.txt`
- `STM32H7+FreeRTOS+LVGL+ESP32\Core\Src\edgewind_ai_preprocess_params.c`

当前 X-CUBE-AI 摘要：

- model hash：`0x268ca3fc9c57c9f78c3e8269cc0b14ea`
- MACC：`6,216,960`
- weights：`238,972 B`
- activations：`295,904 B`
- activation 外部 SDRAM 起始地址：`0xC0600000`
- upload snapshot 起始地址：`0xC0680000`

## 单位约定

三端必须保持同一套单位链：

- AD7606/监测接口采样值先作为低压模拟量 `analog_V`。
- AI 特征提取前使用 `train_mV = analog_V * 1000`。
- Web/云端展示工程量时才换算到物理母线值，例如 `physical_bus_V = analog_V * 100 = train_mV * 0.1`。
- 禁止把 AI 输入重标定为物理 `±500V` 或 `±500000mV`。

## 监测端输出给通信链路的字段

AI 推理输出：

- `fault_code`
- `confidence`
- `probabilities[7]`
- `feature_ms`
- `inference_ms`
- `total_ms`

ESP32 SPI summary payload 当前保留：

- `frame_id`
- `timestamp_ms`
- `downsample_step`
- `upload_points`
- `fault_code[8]`
- `report_mode`
- `status_code`
- `channel_count`
- 4 通道摘要：`waveform_count`、`fft_count`、`value_scaled`、`current_value_scaled`

full upload 由 begin/chunk/end 组成，用于上传波形和频谱。上传任务只发送最新快照，避免云端或 ESP32 忙时阻塞本地采样和推理。

## 与 ESP32 端对齐

ESP32 协处理器目标：

- STM32 <-> ESP32 主链路为 SPI。
- `UART0 + EN + GPIO0` 保留维护和烧录。
- 云端接口保持兼容：
  - `POST /api/register`
  - `POST /api/node/heartbeat`
- ESP32 负责 Wi-Fi、HTTP、重试、服务器命令解析。
- STM32 负责采样、FFT、AI 推理、UI、summary/full frame 生成。

当前 STM32 侧 SPI payload 中已有 status、summary、full begin/chunk/end、event、tx accepted/result、nack 等结构。ESP32 侧后续若调整 JSON 字段，需要先保持 `fault_code` 和 4 通道摘要兼容。

## 与 Web/云端对齐

Web 端目录：

- `Edge_Wind_System`

Web 端当前职责：

- Flask + Socket.IO + SQLite。
- 页面：overview、monitor、history、faults、settings、snapshots。
- 设备侧接口不带 CSRF，主要包括 register、upload、heartbeat。
- 故障快照、工单、历史曲线和 dashboard 从服务端数据库/内存态节点状态生成。
- DeepSeek 只作为 Web 端异步辅助诊断，不参与 STM32 实时闭环控制。

云端部署基线：

- 代码目录：`/var/www/edge_wind/Edge_Wind_System`
- systemd 服务：`edge_wind.service`
- Gunicorn：eventlet，单 worker
- Nginx 反代到 `127.0.0.1:5001`
- 监控推送默认：`8Hz / 1024 waveform / 512 spectrum`
- 已知 DeepSeek/eventlet DNS 问题需要 `EVENTLET_NO_GREENDNS=yes`

## 编译与烧录基线

工具链：

- Keil：`D:\Keil_v542`
- 编译器：ARM Compiler 6.23
- 工程：`STM32H750XBH6.uvprojx`

已验证结果：

```text
"STM32H750XBH6\STM32H750XBH6.axf" - 0 Error(s), 0 Warning(s).
Erase Done.
Programming Done.
Verify OK.
Application running ...
```

推荐下载设置：

- CMSIS-DAP
- Port：SW
- Connect：under Reset
- Reset：HW RESET
- SWD clock：1 MHz，失败时降到 500 kHz
- Flash algorithm：`STM32H7x_2048.FLM`

不要再使用普通 `Normal + SYSRESETREQ + 10MHz` 作为默认下载方式。

## 时钟和 CubeMX 防改基线

模型更新或 CubeMX 生成前后必须运行：

```powershell
python tools\check_stm32_clock_baseline.py
```

当前必须保持：

- HSE：外部晶振 25 MHz，`RCC_HSE_ON`
- LSE：外部 32.768 kHz，`RCC_LSE_ON`
- RTC：`RCC_RTCCLKSOURCE_LSE`
- PLL source：HSE
- SYSCLK：480 MHz
- HCLK/AXI：240 MHz
- APB1/APB2/APB3/APB4：120 MHz
- QSPI/FMC/SDMMC：240 MHz
- LTDC：50 MHz

如果 CubeMX 改坏 HSE/LSE/PLL，不继续编译或烧录。

## 当前需要其他端注意

- AI 训练端：后续新模型如果仍沿用 v68 三输入契约，可只交付 `network_output` 与 `preprocess_c`；如果输入数量、类别、单位或后处理变化，必须先更新 handoff 文档。
- AI 训练端：`docs\stm32_porting.md` 中 v6.3 双模型 guard/router 内容是历史计划，当前监测端以 v68 单模型为准。
- ESP32 端：summary payload 已带 `fault_code`，full frame 用于波形/频谱；后续若要上传 `probabilities[7]` 或 AI 耗时，需要扩展 SPI 协议和 Web 接口。
- Web/云端：DeepSeek 分析只基于工单、快照、历史和知识图谱做辅助解释，不是实时控制闭环。
- Web/云端：单 worker/eventlet 和 `EVENTLET_NO_GREENDNS=yes` 是当前稳定运行约束。

## 下一步对齐项

1. 监测端跑 v68 golden vectors 或 HIL 回放，记录板端 top1、confidence 和耗时。
2. 决定是否把 `probabilities[7]` 和 AI 耗时字段纳入 ESP32/Web 上报 JSON。
3. 更新 ESP32 协处理器 README，把当前 STM32 summary/full payload 字段补进去。
4. 更新 Web API 文档，明确设备上报 JSON 的必填字段和可选字段。
5. 若后续重新启用 guard/router，必须重新定义三端契约，不能直接覆盖 v68 单模型接口。
