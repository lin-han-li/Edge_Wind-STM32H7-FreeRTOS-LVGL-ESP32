# EdgeWind 三端交接 - 监测端 STM32/ESP32

更新日期：2026-06-20

本文是监测端自己的交接文档，用于和 AI 训练端、DAC8568 HIL 播放端、Web/云端对齐。当前正式回退基线仍是 v68 三输入单模型固件；当前正在部署和上板验收的是 v72 aux4 四输入单模型 release candidate，不得直接宣布替代 v68。当前不再按 v6.3 双模型 guard/router 方案推进。

## 当前回退点与本端状态

- v68 稳定回退提交：`dbe0a75`
- v68 稳定回退标签：`snapshot-before-next-update-20260616-212612`
- v69 RC 接入前回退提交：`2c9c7d0`
- v69 RC 接入前回退标签：`snapshot-before-v69-rc-network-20260617-133815`
- v69 约 1s 上传稳定标签：`stable-1s-upload-v69-20260619-025934`
- v70_r2 RC 接入前回退标签：`snapshot-before-v70-r2-network-20260619-171630`
- v70_r2 RC 已部署标签：`v70-r2-rc-monitor-deployed-20260619-174208`
- v72 接入前回退标签：`snapshot-before-v72-network-20260620-180615`
- 当前分支：`codex/v69-aux4-integration`
- 规则：v72 只能作为上板验收线。只有 golden vector、normal 5 分钟和七类 HIL 回放验收通过后，才允许把它作为 v68 的正式替代线。

## 三端目录索引

从监测端视角，当前三端目录如下。后续让各端互看文档时，优先从这些目录开始，不要临时猜路径。

### 本端：监测端 STM32/ESP32

- 仓库根目录：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32`
- STM32 主工程：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\STM32H7+FreeRTOS+LVGL+ESP32`
- Keil 工程：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\STM32H7+FreeRTOS+LVGL+ESP32\MDK-ARM\STM32H750XBH6.uvprojx`
- ESP32 协处理器工程：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\esp32_spi_coprocessor`
- Web 工程：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System`
- 本端交接入口：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\docs\three_side_handoff_monitor_stm32.md`

### 另一端：AI 训练端

- 根目录：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training`
- 三项目对齐文档：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\three_project_alignment_current.md`
- 当前状态文档：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\CURRENT_STATUS.md`
- v68 回退交接文档：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\v68_wind_sensor_public_fused_single_stm32_handoff.md`
- v72 最新部署包：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_deploy_packages\dataset_v72_wind_e00e01_separated_single7_final`
- v72 golden vectors：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_test_vectors\dataset_v72_wind_e00e01_separated_single7_final`
- v72 回放波形包：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\data_v72_wind_e00e01_separated_public_single\playback_hil\dataset_v72_e00e01sep_test_sd_g000000\wave`
- v70_r2 回退部署包：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_deploy_packages\dataset_v70_r2_wind_realfield_e01sep_single7_20260619_023056_rc`

### 另一端：DAC8568 HIL 播放端

- 根目录：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0`
- Keil 工程：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\MDK-ARM\STM32H750XBH6.uvprojx`
- 播放端交接：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\NEXT_AI_PROJECT_HANDOFF.md`
- 三项目上下文：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\EDGEWIND_THREE_PROJECTS_CONTEXT.md`
- aux4 播放交接：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\V69_AUX4_PLAYBACK_HANDOFF.md`
- SD 卡波形目录：`0:/wave/`

### 另一端：Web/云端

- 本地 Web 工程：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System`
- 本地 Web 文档目录：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System\docs`
- 本地 Web 入口文档：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System\README.md`
- 阿里云部署文档：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System\docs\阿里云部署实现详解.md`
- 云端已知代码目录：`/var/www/edge_wind/Edge_Wind_System`
- 云端 systemd 服务：`edge_wind.service`

## 已读取的其他端文档

- AI 训练端三项目对齐：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\three_project_alignment_current.md`
- AI 训练端 v72 README：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_deploy_packages\dataset_v72_wind_e00e01_separated_single7_final\README.md`
- AI 训练端 v72 manifest：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_deploy_packages\dataset_v72_wind_e00e01_separated_single7_final\manifest.json`
- AI 训练端 v72 监测端部署说明：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_deploy_packages\dataset_v72_wind_e00e01_separated_single7_final\docs\MONITOR_STM32_V72_DEPLOYMENT.md`
- AI 训练端 v72 播放端说明：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_deploy_packages\dataset_v72_wind_e00e01_separated_single7_final\docs\PLAYBACK_STM32_V72_A4B_NOTE.md`
- DAC8568 HIL 播放端：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\NEXT_AI_PROJECT_HANDOFF.md`
- DAC8568 三项目上下文：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\EDGEWIND_THREE_PROJECTS_CONTEXT.md`
- DAC8568 aux4 播放交接：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\V69_AUX4_PLAYBACK_HANDOFF.md`
- Web 上位机：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System\README.md`
- 云端部署：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System\docs\阿里云部署实现详解.md`
- ESP32 协处理器：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\esp32_spi_coprocessor\README.md`
- STM32 固件说明：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\STM32H7+FreeRTOS+LVGL+ESP32\PROJECT_OVERVIEW.md`

## 2026-06-20 三端快照

### AI 训练端

- v68 仍是正式稳定回退线：`dataset_v68_wind_sensor_public_fused_single_v6`。
- 最新部署候选：`dataset_v72_wind_e00e01_separated_single7_final`。
- v72 状态：PC gate passed，board acceptance required。
- v72 不能直接替代 v68，必须完成板端 golden vector、normal 5 分钟和七类 HIL 回放验收。
- X-CUBE-AI STM32 输入顺序：`X_aux[1,4]`、`X_dwt[1,104]`、`X_feat[1,116]`、`X_spec[1,512,4]`。监测端必须按生成报告绑定，不按 Python/TFLite 显示顺序猜测。

v72 PC gate 指标：

| split | accuracy | E00 row | E00->E01 | E00->E04 |
| --- | ---: | --- | ---: | ---: |
| test | 0.999371 | `[2496, 0, 4, 0, 0, 0, 0]` | 0 | 0 |
| hil_holdout | 0.999257 | `[2493, 0, 6, 1, 0, 0, 0]` | 0 | 0 |
| normal_only_playback_holdout | 1.000000 | `[5000, 0, 0, 0, 0, 0, 0]` | 0 | 0 |

### DAC8568 HIL 播放端

- A/B/C/D 继续读取严格 4 通道 D8CW `.bin`，不扩展为 8 通道。
- E/F/G/H 使用低速 sidecar：`0:/wave/aux4_schedule.a4b`。
- `aux4_schedule.json` 只作为人工检查镜像，不是播放端稳定运行输入。
- 一个 aux4 item 对应 `16384` 个 DAC 采样点，也对应监测端 `4096 @ 25.6 kHz` 的一个 160 ms AI 窗口。
- 已知小风险：播放端注入 E/F/G/H 帧会占用极少量 A/B/C/D sample slot；按播放端评估，该风险不阻塞 v72 aux4 bring-up，若 HIL 异常再回到播放端优化更新方式。

### 监测端

- 当前固件线：v72 RC 正在接入，仍按 RC 验收线管理。
- v72 接入方式：不使用 CubeMX 重新生成，不改 `.ioc`，直接替换 AI 端交付的 `network*` 和 `preprocess_c` 文件，避免时钟源被 CubeMX 改坏。
- 已保留 v69 约 1s 上传稳定标签，通信节奏回退点为 `stable-1s-upload-v69-20260619-025934`。
- Web/ESP32 8 通道展示已经按“前四路波形/频谱 + 后四路 aux 状态趋势”处理；ch4-ch7 不上传 4096 点波形，不生成频谱。
- 本轮 v72 记录：
  - pre-deploy clock check：`Clock baseline check OK`
  - pre-deploy tag：`snapshot-before-v72-network-20260620-180615`
  - rebuild：`STM32H7+FreeRTOS+LVGL+ESP32\MDK-ARM\STM32H750XBH6\STM32H750XBH6.build_log.htm`，`0 Error(s), 0 Warning(s)`，`Program Size: Code=652812 RO-data=524128 RW-data=328496 ZI-data=142544`
  - flash：`STM32H7+FreeRTOS+LVGL+ESP32\MDK-ARM\flash_v72_20260620_181442.log`，`Erase Done`、`Programming Done`、`Verify OK`、`Application running ...`
  - post-flash clock check：`Clock baseline check OK`
  - COM7 quick read：可见 `model=dataset_v72_wind_e00e01_separated_single7_final`、`aux_valid=0x0F`、`ppermil[7]`、`feat=40ms infer=267ms total=307ms`
  - SPI full upload quick read：`wave=4096`、`fft=2048`、HTTP `200`，多帧 `full http done` 约 `0.63..0.83s`，单帧观察到 `1.744s`

## AI 模型契约

正式稳定回退线仍以 AI 训练端 v68 handoff 为准：

- 模型族：`dataset_v68_wind_sensor_public_fused_single_v6`
- 网络形态：单个 7 类 `network`
- 输入：`X_dwt[104]`、`X_feat[116]`、`X_spec[512,4]`
- 输出：`probabilities[7]`
- 类别：`E00 normal`、`E01 ac_coupling`、`E02 insulation`、`E03 cap_aging`、`E04 igbt_fault`、`E05 bus_ground`、`E06 pwm_abnormal`
- 不包含：raw-lite、E00 guard、router、第二阶段模型、硬规则 masking

训练端报告中的验证范围是 val/test/hil_holdout split，不代表真实风场长期准确率。

当前上板验收线以 AI 训练端 v72 部署包和本端 `network_generate_report.txt` 为准。

## v72 aux4 RC 接入状态

v72 当前按 release candidate 接入监测端固件。它尚未完成板端 golden vector、normal 5 分钟和七类 HIL 回放验收，因此不能直接声明正式替代 v68/v6；若验收退化，保持 v68 回退线或回到 v70_r2 已部署标签。

当前监测端实现状态：

- 已接入 AD7606 ch4-ch7 的 aux4 采集、窗口均值、0.5V..4.5V 解码和 valid mask。
- 已新增 `ADSA_AUX4` / `ADSA_AUX4_2` 与 `ADSA_AUX4_valid_mask` / `ADSA_AUX4_2_valid_mask`，跟随 `ADSA_B` / `ADSA_B2` 双缓冲发布。
- `EdgeWind_AI_RunOnAnalogWindow()` 已改成 `analog_v + aux4 + result` 兼容接口。
- `AI_NETWORK_IN_NUM == 3` 时走 v68 三输入路径；`AI_NETWORK_IN_NUM == 4` 时走 aux4 四输入路径。
- v72 四输入绑定顺序固定为 `input[0]=X_aux`、`input[1]=X_dwt`、`input[2]=X_feat`、`input[3]=X_spec`。
- ESP32 SPI summary/full payload 暂不强制扩展 aux4 模型字段；Web 已支持 4/8 通道展示兼容。

v72 模型信息：

- 模型族：`dataset_v72_wind_e00e01_separated_single7_final`
- 部署包：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_deploy_packages\dataset_v72_wind_e00e01_separated_single7_final`
- 网络形态：单个 7 类 `network`
- TFLite SHA256：`4f24089664b26488518bc3fa8554a51491c1a75118e0288131abd8e195f19356`
- 输出：`probabilities[7]`
- X-CUBE-AI 输入顺序：`X_aux[4]`、`X_dwt[104]`、`X_feat[116]`、`X_spec[512,4]`
- X-CUBE-AI 输出：`nl_24`，`f32[1,7]`
- MACC：`6,225,536`
- weights：`273,020 B`
- activations：`295,920 B`
- compression：`none`

`X_aux[4]` 物理顺序：

- `T_igbt_C`
- `T_dc_cap_C`
- `RH_cabinet_pct`
- `wind_load_pct`

禁止恢复：

- raw-lite 输入
- E00 guard
- router
- 第二模型
- 硬规则 masking

监测端接入 v72 的门禁：

1. Keil rebuild 必须保持 `0 Error(s), 0 Warning(s)`，且 `python tools\check_stm32_clock_baseline.py` 通过。
2. v72 golden vectors 必须全部 top1 与 PC 一致。
3. v72 `normal.bin` 必须连续 5 分钟不反复误报 E01/E04。
4. 七类 HIL 回放记录 top1、confidence、ppermil 和耗时；若退化，立即保持 v68，不继续扩展协议。
5. v72 验收通过前，不把它写成正式替代 v68 的结论。

## 已接入的模型文件

STM32 工程中由 v72 部署包替换：

- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network.c`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network.h`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_config.h`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_data.c`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_data.h`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_data_params.c`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_data_params.h`
- `STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_generate_report.txt`
- `STM32H7+FreeRTOS+LVGL+ESP32\Core\Src\edgewind_ai_preprocess_params.c`
- `STM32H7+FreeRTOS+LVGL+ESP32\Core\Inc\edgewind_ai_preprocess_params.h`

## 单位约定

三端必须保持同一套单位链：

- AD7606/监测接口采样值先作为低压模拟量 `analog_V`。
- AI 特征提取前使用 `train_mV = analog_V * 1000`。
- Web/云端展示工程量时才换算到物理母线值，例如 `physical_bus_V = analog_V * 100 = train_mV * 0.1`。
- 禁止把 AI 输入重标定为物理 `±500V` 或 `±500000mV`。

aux4 解码约定：

- AD7606 ch4-ch7 作为低速 aux4，每个 AI 窗口发布一组值。
- aux4 解码固定使用 `AD7606_RawToVoltsF()` 的外部低压模拟量，不再二次补偿前端分压。
- 0.5V..4.5V 映射范围固定为 `20..125 C`、`18..115 C`、`8..98 %RH`、`8..110 %`。
- 窗口均值低于 0.25V 或高于 4.75V 时，该通道使用默认均值 `72.5, 66.5, 53.0, 59.0` 并清除 valid mask 对应 bit。

## 监测端输出给通信链路的字段

AI 推理输出：

- `fault_code`
- `confidence`
- `probabilities[7]`
- `feature_ms`
- `inference_ms`
- `total_ms`
- 串口 bring-up 日志额外输出 `model_version`、`aux4[4]`、`aux_valid`

ESP32 SPI summary payload 当前保留兼容字段：

- `frame_id`
- `timestamp_ms`
- `downsample_step`
- `upload_points`
- `fault_code[8]`
- `report_mode`
- `status_code`
- `channel_count`
- 前四路摘要：`waveform_count`、`fft_count`、`value_scaled`、`current_value_scaled`
- 后四路 aux4：仅当前值、有效标志、单位和范围；不分配 waveform/fft 缓冲

full upload 由 begin/chunk/end 组成，用于上传前四路波形和频谱。上传任务只发送最新快照，避免云端或 ESP32 忙时阻塞本地采样和推理。

## Web/云端对齐

Web 端当前职责：

- Flask + Socket.IO + SQLite。
- 页面：overview、monitor、history、faults、settings、snapshots。
- 支持 4 通道旧 payload 和 8 通道新 payload 并存。
- 实时监测页采用“主4路波形/频谱 + 后4路 aux 状态趋势”的分层显示。
- 四通道同窗只叠加 ch0-ch3；ch4-ch7 不进入波形和频谱同窗。
- DeepSeek 只作为 Web 端异步辅助诊断，不参与 STM32 实时闭环控制。

云端部署基线：

- 代码目录：`/var/www/edge_wind/Edge_Wind_System`
- systemd 服务：`edge_wind.service`
- Gunicorn：eventlet，单 worker
- Nginx 反代到 `127.0.0.1:5001`
- 已知 DeepSeek/eventlet DNS 问题需要 `EVENTLET_NO_GREENDNS=yes`

## 编译与烧录基线

工具链：

- Keil：`D:\Keil_v542`
- 编译器：ARM Compiler 6.23
- 工程：`STM32H7+FreeRTOS+LVGL+ESP32\MDK-ARM\STM32H750XBH6.uvprojx`

推荐下载设置：

- CMSIS-DAP
- Port：SW
- Connect：under Reset
- Reset：HW RESET
- SWD clock：1 MHz，失败时降到 500 kHz
- Flash algorithm：`STM32H7x_2048.FLM`
- 勾选 `Reset after Connect`

不要再使用普通 `Normal + SYSRESETREQ + 10MHz` 作为默认下载方式。

串口观察约定：

- STM32 日志：COM7，`921600`
- ESP32 日志：CH340 对应端口，常见为 COM8，`115200`
- 若实际 USB 端口变化，先按设备描述和日志内容确认，不要只凭端口号判断。

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
- FMC/QSPI/SDMMC：240 MHz
- LTDC：50 MHz
- USART1：120 MHz
- SPI123：160 MHz

禁止 CubeMX 把 HSE/LSE 改成 bypass、disable、HSI 或 CSI 派生时钟。若发现 `.ioc`、`SystemClock_Config()`、`quadspi.c`、`sdmmc.c`、`fmc.c`、`rtc.c` 发生非预期改动，不编译、不烧录，先回退时钟相关文件。

## 下一步对齐项

1. 完成本轮 v72 Keil rebuild、下载和 post-flash 串口确认。
2. 运行 v72 golden vectors；记录 top1 是否与 PC 一致。
3. 使用 v72 回放包运行 `normal.bin` 连续 5 分钟，观察 Web/串口是否反复误报 E01/E04。
4. 跑七类 HIL 回放，逐类记录 top1、confidence、ppermil、feature/infer/total ms 和 Web full frame 状态。
5. 若 v72 normal 或七类回放退化，回退到 `v70-r2-rc-monitor-deployed-20260619-174208`、`stable-1s-upload-v69-20260619-025934` 或 v68 稳定标签，不继续扩大协议改动。
