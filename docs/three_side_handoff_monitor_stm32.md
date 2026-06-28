# EdgeWind 三端交接 - 监测端 STM32/ESP32

更新日期：2026-06-22

本文是监测端自己的当前交接文档，用于和 AI 训练端、DAC8568 HIL 播放端、Web/云端对齐。当前正式回退基线仍是 v68 三输入单模型固件；当前上板验收线为 v74 conservative + R_iso aux4 四输入单模型 release candidate。撤回的失败候选不再作为板端、Web、ESP32 或演示部署依据，本项目内相关临时记录已清理。

## 当前状态

- 当前分支：`codex/v69-aux4-integration`
- 当前监测端模型：`dataset_v74_ad7606_sync_riso_public_single`
- 当前部署包：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\stm32_deploy_packages\dataset_v74_ad7606_sync_riso_single7_conservative`
- 当前网络形态：单个 7 类 `network`，无 guard、无 router、无第二模型、无硬规则 masking
- 当前上板状态：已回退程序、Keil rebuild 通过，并已通过 Keil 下载到 STM32
- 最近 STM32 下载结果：`Erase Done / Programming Done / Verify OK / Application running`
- 最近 STM32 下载日志：`STM32H7+FreeRTOS+LVGL+ESP32\MDK-ARM\flash_v74_rollback_20260622.log`
- 当前保留功能：8 通道展示、R_iso 语义、频谱上传开关、故障快照延迟确认、PH7 蜂鸣器反馈

## 三端目录

监测端：

```text
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\STM32H7+FreeRTOS+LVGL+ESP32
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\esp32_spi_coprocessor
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System
```

AI 训练端：

```text
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\three_project_alignment_current.md
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\docs\v74_ad7606_sync_riso_single7_handoff.md
```

DAC8568 HIL 播放端：

```text
C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0
C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\NEXT_AI_PROJECT_HANDOFF.md
C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0\V69_AUX4_PLAYBACK_HANDOFF.md
```

云端 Web：

```text
/var/www/edge_wind/Edge_Wind_System
edge_wind.service
```

## 已读取的外部结论

AI 训练端当前结论：

- 当前候选为 `dataset_v74_ad7606_sync_riso_public_single`
- 使用 conservative 部署包进行监测端和回放端 bring-up
- `wind_load_pct` 已弃用，不再是模型输入
- `X_aux[3]` 必须是工程量 `R_iso_kohm`
- X-CUBE-AI 输入顺序必须以生成报告为准，不按旧模型经验猜测
- v74 hardmix r1 只是实验线，不替代当前 conservative 包

DAC8568 播放端当前可用合同：

- A/B/C/D 仍是严格 4 通道 D8CW `.bin`
- E/F/G/H 由 `aux4_schedule.a4b` 提供低速上下文量
- 不把 D8CW `.bin` 扩成 8 通道
- 不把 TFLite、X-CUBE-AI 文件、golden vectors 或自测包放到回放 SD 卡
- 回放端旧文档中仍有早期 aux4 命名，当前监测端和 AI 端以 v74 R_iso 语义为准

## 模型合同

输出类别：

```text
E00 normal
E01 ac_coupling
E02 insulation
E03 cap_aging
E04 igbt_fault
E05 bus_ground
E06 pwm_abnormal
```

X-CUBE-AI 输入顺序：

```text
input[0] = serving_default_X_aux0  -> X_aux[4]
input[1] = serving_default_X_dwt0  -> X_dwt[104]
input[2] = serving_default_X_feat0 -> X_feat[116]
input[3] = serving_default_X_spec0 -> X_spec[512,4]
```

资源量：

```text
MACC        = 6,225,536
weights     = 273,020 B
activations = 295,920 B
output      = f32(1x7)
```

监测端 `network.h` 应保持：

```text
AI_NETWORK_IN_NUM = 4
input sizes       = 4 / 104 / 116 / 2048
output size       = 7
```

## 通道与单位

高速诊断通道：

```text
AD7606 ch0 -> 直流母线(+)
AD7606 ch1 -> 直流母线(-)
AD7606 ch2 -> 负载电流
AD7606 ch3 -> 漏电流
```

低速 aux4 上下文：

```text
AD7606 ch4 -> T_igbt_C
AD7606 ch5 -> T_dc_cap_C
AD7606 ch6 -> RH_cabinet_pct
AD7606 ch7 -> R_iso_kohm
```

aux4 解码约定：

```text
0.5V..4.5V -> 20..125 C
0.5V..4.5V -> 18..115 C
0.5V..4.5V -> 8..98 %RH
0.5V..4.5V -> 20..8000 kOhm
```

无效电压窗口：

```text
analog_V < 0.25V 或 analog_V > 4.75V -> 使用默认均值并清除 valid bit
```

当前默认均值：

```text
67.8480, 55.7832, 49.2850, 3016.0754
```

## 已集成的监测端文件

```text
STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network.c
STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network.h
STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_config.h
STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_data.c
STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_data.h
STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_data_params.c
STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_data_params.h
STM32H7+FreeRTOS+LVGL+ESP32\X-CUBE-AI\App\network_generate_report.txt
STM32H7+FreeRTOS+LVGL+ESP32\Core\Src\edgewind_ai_preprocess_params.c
STM32H7+FreeRTOS+LVGL+ESP32\Core\Inc\edgewind_ai_preprocess_params.h
```

运行标识：

```text
EdgeWind_AI_ModelVersion() -> dataset_v74_ad7606_sync_riso_public_single
```

## Web/ESP32 对齐

上传与展示规则：

- `channel_count=8`
- ch0-ch3 上传当前值、波形和可选频谱
- ch4-ch7 只上传当前值、有效标志、单位和范围，不上传波形或频谱
- `fft_enabled=0` 时 ch0-ch3 仍上传波形，`fft_count=0`
- 实时监测页四通道同窗只包含 ch0-ch3
- 故障快照状态机只用新鲜 ch0-ch3 full waveform 做 before/after 判断
- DeepSeek 只作为 Web 端异步辅助诊断，不参与 STM32 实时闭环控制

云端注意事项：

- 云端代码目录：`/var/www/edge_wind/Edge_Wind_System`
- systemd 服务：`edge_wind.service`
- eventlet DNS 规避项：`EVENTLET_NO_GREENDNS=yes`

## 编译与下载基线

Keil：

```text
D:\Keil_v542\UV4\UV4.exe
STM32H7+FreeRTOS+LVGL+ESP32\MDK-ARM\STM32H750XBH6.uvprojx
```

下载配置：

```text
CMSIS-DAP
Port: SW
Connect: under Reset
Reset: HW RESET
Max Clock: 1MHz
Reset after Connect: enabled
```

时钟保护基线：

```text
python tools\check_stm32_clock_baseline.py
```

必须保持：

```text
HSE 25 MHz external oscillator
LSE external oscillator
PLL source HSE
SYSCLK 480 MHz
HCLK 240 MHz
APB1/APB2/APB3/APB4 120 MHz
```

## 当前验收门禁

1. `python tools\check_stm32_clock_baseline.py` 必须通过。
2. Keil rebuild 必须为 `0 Error(s), 0 Warning(s)`。
3. 串口必须出现 `model=dataset_v74_ad7606_sync_riso_public_single`。
4. `aux4[3]` 必须是 `R_iso_kohm` 量级，不能是负载百分比。
5. v74 golden vectors 必须全部 top1 正确。
6. `normal.bin` 连续 5 分钟不能反复误报 E01/E04/E05。
7. 七类 HIL 回放需要记录 top1、confidence、ppermil、feature_ms、infer_ms、total_ms。
8. 若板端验收退化，保持 v68 回退线，不继续扩展协议或包装成正式替代。

## 下一步

1. 上电后读取 COM7 串口，确认当前固件 model/version/aux4 输出。
2. 跑 v74 golden vectors，确认 STM32 与 PC/TFLite 输出一致。
3. 准备 v74 D8CW + A4B SD 包，先跑 normal 5 分钟。
4. 通过七类回放后，再考虑是否把 v74 标为演示候选。
5. 任何新模型接入前，先建立回退点，再替换 `network*` 和 preprocess。
