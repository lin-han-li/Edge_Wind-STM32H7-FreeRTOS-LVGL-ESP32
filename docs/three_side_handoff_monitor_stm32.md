# EdgeWind 三端交接 - 监测端 STM32/ESP32

更新日期：2026-06-18

本文是监测端自己的交接文档，用于和 AI 训练端、DAC8568 HIL 播放端、Web/云端对齐。当前正式回退基线仍是 v68 三输入单模型固件；当前监测端已接入并烧录 v69 publicfix RC 四输入单模型，用于板端验收，不得直接宣布替代 v68。当前不再按 v6.3 双模型 guard/router 方案推进。

当前回退点与本端状态：

- v68 稳定回退提交：`dbe0a75`
- v68 稳定回退标签：`snapshot-before-next-update-20260616-212612`
- v69 RC 接入前回退提交：`2c9c7d0`
- v69 RC 接入前回退标签：`snapshot-before-v69-rc-network-20260617-133815`
- 当前本端提交：`f7142b3`
- 当前分支：`codex/v69-aux4-integration`，本地比远端多 3 个提交，尚未推送。
- 规则：v69 publicfix RC 已可上板验证，但只有 normal 5 分钟和七类 HIL 回放验收通过后，才允许把它作为 v68 的正式替代线。

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
- 下一集成线 v69 交接文档：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\v69_wind_sensor_aux4_public_fused_single_handoff.md`
- 当前 v68 STM32 部署包：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_deploy_packages\dataset_v68_wind_sensor_public_fused_single_v6_single7_20260616_031448`
- 当前 v68 板端测试向量：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_test_vectors\dataset_v68_wind_sensor_public_fused_single_v6_single7_20260616_031448`

### 另一端：DAC8568 HIL 播放端

- 根目录：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0`
- Keil 工程：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\MDK-ARM\STM32H750XBH6.uvprojx`
- 播放端交接：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\NEXT_AI_PROJECT_HANDOFF.md`
- 三项目上下文：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\EDGEWIND_THREE_PROJECTS_CONTEXT.md`
- SD 卡波形目录：`0:/wave/`

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
- AI 模型契约以 AI 训练端 handoff 和本端 `network_generate_report.txt` 为准；当前稳定回退线是 v68 单模型，当前上板验收线是 v69 publicfix RC 单模型。
- Web/云端能力以本地 `Edge_Wind_System` 与云端部署文档为准；DeepSeek 只写 Web 异步辅助诊断。
- 若模型输入、类别、单位、ESP32 payload、Web 上报 JSON 任一项变化，必须同步更新本文件。
- 若 CubeMX、Keil 下载配置、时钟源、Flash algorithm 变化，必须同步更新本文件。

## 已读取的其他端文档

- AI 训练端：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\v68_wind_sensor_public_fused_single_stm32_handoff.md`
- AI 训练端 v69 RC：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\v69_wind_sensor_aux4_public_fused_single_handoff.md`
- AI 训练端 v69 A4B/监测回放交接：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\v69_a4b_monitor_playback_handoff.md`
- AI 训练端当前状态：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\CURRENT_STATUS.md`
- AI 训练端三项目对齐：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\three_project_alignment_current.md`
- AI 训练端数据格式：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\data_format.md`
- AI 训练端历史移植说明：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\stm32_porting.md`
- DAC8568 HIL 播放端：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\NEXT_AI_PROJECT_HANDOFF.md`
- DAC8568 三项目上下文：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\EDGEWIND_THREE_PROJECTS_CONTEXT.md`
- DAC8568 v69 aux4 播放交接：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\V69_AUX4_PLAYBACK_HANDOFF.md`
- Web 上位机：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System\README.md`
- 云端部署：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System\docs\阿里云部署实现详解.md`
- 阿里云 SSH 接入：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\ALiYunFuWuQi\README.md`
- ESP32 协处理器：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\esp32_spi_coprocessor\README.md`
- STM32 固件说明：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\STM32H7+FreeRTOS+LVGL+ESP32\PROJECT_OVERVIEW.md`

## 2026-06-18 三端快照

### AI 训练端

- 路径：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training`
- Git 状态：分支 `codex/v69-aux4-integration`，存在未提交改动和 v69 新产物；监测端只读取交接事实，不直接修改 AI 仓库。
- v68 仍是正式稳定回退线：`dataset_v68_wind_sensor_public_fused_single_v6`。
- v69 publicfix RC 已完成 AI 侧全量训练、TFLite 导出、X-CUBE-AI 生成、golden vectors 和回放包准备。
- v69 RC 部署包：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_deploy_packages\dataset_v69_wind_sensor_aux4_public_fused_single_publicfix_single7_20260617_001615_rc`
- v69 RC 回放包：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\data_v69_wind_sensor_aux4_public_fused_single_publicfix\playback_hil\dataset_v69_publicfix_rc_test_sd_g000000\wave`
- v69 RC golden vectors：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_test_vectors\dataset_v69_wind_sensor_aux4_public_fused_single_publicfix_20260617_001615_rc`
- AI 端记录的 v69 RC 风险：PC 评估仍存在少量 `E00->E01`，所以板端 normal 5 分钟验收是强制门禁。
- AI 端记录的 X-CUBE-AI STM32 输入顺序：`X_aux[1,4]`、`X_dwt[1,104]`、`X_feat[1,116]`、`X_spec[1,512,4]`。

### DAC8568 HIL 播放端

- 路径：`C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0`
- Git 状态：分支 `codex/v69-aux4-integration`，存在未提交的 aux4 播放链路改动。
- 正式 v69 播放契约以 `V69_AUX4_PLAYBACK_HANDOFF.md` 为准。
- A/B/C/D 继续读取七个严格 4 通道 D8CW `.bin`，不扩展为 8 通道。
- E/F/G/H 使用低速 sidecar：`0:/wave/aux4_schedule.a4b`。
- `aux4_schedule.json` 只作为人工检查镜像，不是播放端稳定运行输入。
- 一个 aux4 item 对应 `16384` 个 DAC 采样点，也对应监测端 `4096 @ 25.6 kHz` 的一个 160 ms AI 窗口。
- 已知小风险：回放端注入 E/F/G/H 帧会占用极少量 A/B/C/D sample slot；按回放端评估，该风险不阻塞 v69 aux4 bring-up，但若 HIL 异常再回到播放端优化更新方式。

### 监测端

- 路径：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32`
- Git 状态：分支 `codex/v69-aux4-integration`，工作区干净，本地领先远端 3 个提交。
- 当前固件：v69 publicfix RC 已编译并烧录成功，仍按 RC 验收线管理。
- Keil rebuild：`0 Error(s), 0 Warning(s)`。
- 下载结果：`Erase Done`、`Programming Done`、`Verify OK`、`Application running ...`。
- 运行日志已看到：`model=dataset_v69_wind_sensor_aux4_public_fused_single_publicfix`、`E00/normal conf=0.936`、`infer=267ms`、`total=307ms`。
- 当前 quick check 中 `aux4=[72.5,66.5,53.0,59.0] valid=0x00`，说明后四路未看到有效 0.5V..4.5V 输入，暂时使用默认均值；正式 v69 验收必须接上 E/F/G/H 或明确默认 aux 策略。

## 当前监测端状态

- 主工程：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\STM32H7+FreeRTOS+LVGL+ESP32`
- Keil 工程：`STM32H7+FreeRTOS+LVGL+ESP32\MDK-ARM\STM32H750XBH6.uvprojx`
- MCU：`STM32H750XBH6`
- 本地任务链路：
  - `DSP_Algorithm_Task` 高优先级消费采样窗口，执行 FFT、特征提取和 AI 推理。
  - `Upload_Task` 低优先级上传最新处理快照；ESP32/云端忙时允许丢弃旧上传快照，DSP 不被阻塞。
- 当前已烧录固件：v69 publicfix RC AI 模型版本；2026-06-17 已完成 Keil 编译、下载、串口 quick check。它是上板验收线，不是正式替代 v68 的结论。

## AI 模型契约

正式稳定回退线仍以 AI 训练端 v68 handoff 为准：

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

当前上板验收线以 AI 训练端 v69 publicfix RC 部署包和本端 `network_generate_report.txt` 为准。注意：Keras/TFLite 逻辑输入列表和 X-CUBE-AI 生成后的 STM32 输入顺序可能不同；本端实际绑定必须服从 `network_generate_report.txt`，当前为 `X_aux`、`X_dwt`、`X_feat`、`X_spec`。

## v69 aux4 RC 接入状态

v69 当前已按 release candidate 接入监测端固件，但尚未完成板端 normal 5 分钟和七类 HIL 回放验收。它仍不能直接声明正式替代 v68/v6；若验收退化，保持 v68 回退线。

当前监测端实现状态（2026-06-17）：

- 已接入 AD7606 ch4-ch7 的 aux4 采集、窗口均值、0.5V..4.5V 解码和 valid mask。
- 已新增 `ADSA_AUX4` / `ADSA_AUX4_2` 与 `ADSA_AUX4_valid_mask` / `ADSA_AUX4_2_valid_mask`，跟随 `ADSA_B` / `ADSA_B2` 双缓冲发布。
- `EdgeWind_AI_RunOnAnalogWindow()` 已改成 `analog_v + aux4 + result` 兼容接口；当前 v69 RC `AI_NETWORK_IN_NUM=4` 时 aux4 已进入模型。
- ESP32 SPI summary/full payload 暂不扩展 aux4 字段；v69 板端稳定后再单独改 Web/ESP32 协议。

- 模型族：`dataset_v69_wind_sensor_aux4_public_fused_single_publicfix`
- 部署包：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_deploy_packages\dataset_v69_wind_sensor_aux4_public_fused_single_publicfix_single7_20260617_001615_rc`
- 网络形态：仍是单个 7 类 `network`
- 新增输入：`X_aux[4]`
- X-CUBE-AI 生成输入顺序：`X_aux[4]`、`X_dwt[104]`、`X_feat[116]`、`X_spec[512,4]`
- `X_aux[4]` 物理顺序：
  - `T_igbt_C`
  - `T_dc_cap_C`
  - `RH_cabinet_pct`
  - `wind_load_pct`
- 保持不变：
  - `X_dwt[104]`
  - `X_feat[116]`
  - `X_spec[512,4]`
  - `probabilities[7]`
  - `E00` 到 `E06` 类别顺序
- 禁止恢复：
  - raw-lite 输入
  - E00 guard
  - router
  - 第二模型
  - 硬规则 masking

监测端接入 v69 的门禁：

1. 已接入 AI 端 v69 publicfix RC deploy package；该包仍带 release candidate 警告。
2. Keil rebuild 必须保持 `0 Error(s), 0 Warning(s)`，且 `python tools\check_stm32_clock_baseline.py` 通过。
3. v69 normal 回放必须连续 5 分钟不反复误报 E01/E04。
4. 七类 HIL 回放记录 top1、confidence、ppermil 和耗时；若退化，立即保持 v68，不继续扩展 Web 协议。
5. v69 板端验收前，ESP32/Web payload 不新增 aux4 字段。

v69 采样约定：

- AD7606 ch0-ch3 仍作为高速 4ch x 4096 AI 波形。
- AD7606 ch4-ch7 作为低速 aux4，每个 AI 窗口发布一组值。
- aux4 解码固定使用 `AD7606_RawToVoltsF()` 的外部低压模拟量，不再二次补偿前端分压。
- 0.5V..4.5V 映射范围固定为 `20..125 C`、`18..115 C`、`8..98 %RH`、`8..110 %`。
- 窗口均值低于 0.25V 或高于 4.75V 时，该通道使用默认均值 `72.5, 66.5, 53.0, 59.0` 并清除 valid mask 对应 bit。
- 若 timing pressure 出现，允许 aux4 持有上一窗口值，不允许拖慢 ch0-ch3 高速采样。

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
- `STM32H7+FreeRTOS+LVGL+ESP32\Core\Inc\edgewind_ai_preprocess_params.h`

当前 X-CUBE-AI 摘要：

- TFLite SHA256：`8ea157ca0c56ff9a0fcf9c9b47ec1b2bc90d6f5d4d932f0a4e980eed0bdd2761`
- preprocess SHA256：`bf0f3d9324d62f79b823697c272f43ff538bde8b73ce66ef750f0f3523b838c8`
- MACC：`6,225,536`
- weights：`273,020 B`
- activations：`295,920 B`
- input order：`X_aux[4]`、`X_dwt[104]`、`X_feat[116]`、`X_spec[512,4]`
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
- 串口 bring-up 日志额外输出 `model_version`、`aux4[4]`、`aux_valid`；当前不进入 ESP32 SPI payload。

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

串口观察约定：

- STM32 日志：COM7，`921600`。
- ESP32 日志：COM8，`115200`。
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
- QSPI/FMC/SDMMC：240 MHz
- LTDC：50 MHz

如果 CubeMX 改坏 HSE/LSE/PLL，不继续编译或烧录。

## 当前需要其他端注意

- AI 训练端：当前 v69 publicfix RC 已交付并接入监测端；后续若重新导出模型，必须继续交付 `network_output`、`preprocess_c`、`network_generate_report.txt`、golden vectors 和匹配回放包。
- AI 训练端：`docs\stm32_porting.md` 中 v6.3 双模型 guard/router 内容是历史计划，当前监测端只接受 v68/v69 单 `network` 路线。
- AI 训练端：任何输入数量、输入顺序、类别、单位或后处理变化，必须先更新 handoff；监测端代码绑定以本端 `network_generate_report.txt` 为最终依据。
- ESP32 端：summary payload 已带 `fault_code`，full frame 用于波形/频谱；后续若要上传 `probabilities[7]` 或 AI 耗时，需要扩展 SPI 协议和 Web 接口。
- DAC8568 播放端：v68/v69 的 D8CW `.bin` 仍严格 4 通道 A/B/C/D；v69 正式低速 sidecar 是 `aux4_schedule.a4b`，用于 DAC8568 E/F/G/H，不改变 `.bin` header 的 `channels=4`。
- DAC8568 播放端：`aux4_schedule.json` 只能作为人工调试镜像，不作为稳定播放输入。
- DAC8568 播放端：如果播放端文档出现模型输入绑定顺序描述，应视为监测端/AI 端契约信息；实际 STM32 绑定顺序以监测端 `network_generate_report.txt` 为准。
- Web/云端：DeepSeek 分析只基于工单、快照、历史和知识图谱做辅助解释，不是实时控制闭环。
- Web/云端：单 worker/eventlet 和 `EVENTLET_NO_GREENDNS=yes` 是当前稳定运行约束。

## 下一步对齐项

1. 先完成 v69 RC 板端验收：用匹配回放包跑 `normal.bin` 连续 5 分钟，确认不反复误报 E01/E04，并记录 `aux4`、`aux_valid`、`ppermil[7]`、`confidence`、`feature_ms/infer_ms/total_ms`。
2. 再跑七类 HIL 回放，逐类记录 top1、confidence、ppermil、耗时和 Web full frame 状态；若退化，回退到 `snapshot-before-v69-rc-network-20260617-133815` 或 v68 稳定标签。
3. 如果 aux4 一直 `valid=0x00`，先检查 DAC8568 E/F/G/H 到 AD7606 ch4-ch7 接线和 A4B 侧输出，不要直接归因于模型。
4. v69 板端稳定后，再单独计划把 `probabilities[7]`、AI 耗时、aux4 作为可选 JSON 字段加入 ESP32 上传和 Web 展示。
5. 若后续重新启用 guard/router，必须重新定义三端契约，不能直接覆盖 v68/v69 单模型接口。
