# EdgeWind 子项目合并背景说明

更新日期：2026-05-07

本文档用于说明准备合并进 `EdgeWind_STM32_ESP32` 总项目的两个外部目录：

```text
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training
C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0
```

目标是让后续接手的 AI 或工程师快速理解：这两个目录分别是什么、已经做过什么、和 `EdgeWind_STM32_ESP32` 监测端主项目是什么关系，以及合并时需要注意哪些风险。

## 1. 三个项目的总体关系

当前大创项目是《基于边缘计算的风电场直流系统故障监测》。完整系统不是单个固件，而是由监测端、播放端、AI 训练工程、ESP32 上传链路、服务器和 Web 展示共同组成。

三个核心目录关系如下：

```text
EdgeWind_STM32_ESP32
    = 总项目 / 监测诊断端主工程 / ESP32、服务器、Web 展示工程集合

EdgeWind_AI_Training
    = PC 端 AI 训练、模型选择、X-CUBE-AI 转换、监测端 AI 接入验证工程

STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0
    = DAC8568 故障播放端 / HIL 故障注入源 / 给监测端喂模拟故障信号
```

总体工作链路：

```text
AI 训练工程生成并筛选模型
        ->
监测端 STM32H750XBH6 部署 AI 诊断
        ->
播放端 DAC8568 输出 normal + 6 类故障模拟信号
        ->
监测端 ADC 实采、FFT、AI 判断、ESP32 上传、Web 展示
```

职责边界：

| 目录 | 角色 | 是否直接上板运行 | 主要输出 |
|---|---|---:|---|
| `EdgeWind_STM32_ESP32` | 总项目和监测端主项目 | 是 | ADC/FFT/AI/ESP32/Web |
| `EdgeWind_AI_Training` | PC AI 训练与部署准备 | 部分生成工程可上板 | TFLite、X-CUBE-AI 代码、测试向量、报告 |
| `STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0` | HIL 故障播放端 | 是 | DAC8568 四通道模拟故障输出 |

## 2. `EdgeWind_AI_Training` 是什么

路径：

```text
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training
```

这个目录是 PC 端 AI 训练与部署准备工程。它不是 DAC8568 播放端固件，也不是正式监测端主工程。

它主要完成了：

1. 生成风电场直流系统故障诊断训练数据。
2. 按监测端真实采样率 `25600 Hz` 建模。
3. 每个诊断窗口固定为 `4ch x 4096`，即 `0.16s`。
4. 训练 `normal + 6 fault` 的 7 类故障诊断模型。
5. 从 v1 到 v5 逐步优化数据真实性、模型结构、频谱分辨率和小波基。
6. 把最终模型导出为 TFLite。
7. 使用 STM32CubeMX / X-CUBE-AI 生成 STM32H750 可运行的推理代码。
8. 在复制出来的监测端工程里加入 AI golden selftest，验证模型确实能在 H750 板端运行。

### 2.1 监测端建模参数

AI 训练和监测端推理统一使用监测端真实采样率：

```text
采样率：25600 Hz
窗口：4ch x 4096
窗口时长：4096 / 25600 = 0.16 s
FFT 分辨率：25600 / 4096 = 6.25 Hz
```

播放端 HIL 验证仍使用 `102400 Hz` 生成 DAC8568 波形，但监测端 AI 不直接吃播放端文件，而是吃监测端实际采样后的 4 通道窗口和特征。

### 2.2 当前推荐模型

当前推荐模型：

```text
models/dataset_v5_uniform_512_db3/model_float32.tflite
```

模型名称：

```text
dataset_v5_uniform_512_db3
```

模型输入：

| 输入 | 维度 | 含义 |
|---|---:|---|
| `X_feat` | 116 | 工程统计特征 |
| `X_spec` | 4 x 512 | 4 通道压缩频谱特征 |
| `X_dwt` | 104 | db3 小波特征 |

模型输出：

| ID | Code | 类别 |
|---:|---|---|
| 0 | E00 | normal |
| 1 | E01 | ac_coupling |
| 2 | E02 | insulation |
| 3 | E03 | cap_aging |
| 4 | E04 | igbt_fault |
| 5 | E05 | bus_ground |
| 6 | E06 | pwm_abnormal |

当前指标：

| 指标 | 数值 |
|---|---:|
| test accuracy | 约 `99.10%` |
| HIL holdout accuracy | 约 `99.53%` |
| TFLite 文件大小 | `78,284 bytes` |
| X-CUBE-AI Flash | 约 `90 KB` |
| X-CUBE-AI RAM | 约 `119 KB` |
| H750 板端模型推理时间 | 约 `35.7 ms/window` |

板端 golden selftest 最新结果：

```text
AI golden summary: pass=21 fail=0 avg_us=35746 min_us=35382 max_us=36273 core=480000000 hclk=240000000
[AI] golden selftest result=0
```

含义：

- 21 个 golden vectors 全部通过。
- 覆盖 E00 到 E06，每类 3 个样本。
- TFLite 转为 X-CUBE-AI 后，H750 板端输出与 PC 参考输出一致。
- 该时间只是模型推理时间，不包含实时 FFT、DWT、工程特征提取。

### 2.3 模型结构

当前模型是 TinyCNN + DWT + 工程特征的三分支融合模型。

```text
X_spec 4x512
    -> Conv1D
    -> SeparableConv1D
    -> SeparableConv1D
    -> GlobalAveragePooling
    -> 32维频谱表示

X_feat 116
    -> Dense(32)
    -> 32维工程特征表示

X_dwt 104
    -> Dense(32)
    -> 32维小波特征表示

三路 concat: 96维
    -> Dense(64)
    -> Dense(32)
    -> Dense(7)
    -> Softmax
```

这种方案不是单纯 MLP，也不是无法解释的大型黑箱网络。它融合了：

- 工程统计特征：均值、RMS、峰峰值、斜率、通道关系、泄漏电流等。
- FFT 频谱特征：周期纹波、谐波、高频开关异常。
- DWT 小波特征：短时冲击、瞬态毛刺、高频能量突变。

这样既有答辩时可解释的工程依据，又能体现 AI 模型融合判断的深度。

### 2.4 训练演进

`EdgeWind_AI_Training` 不是一次性生成干净数据刷准确率，而是经过了多轮迭代。

#### v1 / v2

用于打通基础流程：

- 生成 7 类数据。
- 提取 116 维工程特征。
- 训练轻量 MLP。
- 验证训练、评估、导出和 STM32 C 推理流程。

早期版本准确率过高，故障区分过于理想化，不适合作为最终答辩模型。

#### v3

v3 是当前正式原始数据集基础，重点提升真实性：

- 引入工况层：母线电压、负载、纹波、传感器增益、零漂、噪声、串扰、滤波差异。
- 按 `scenario_id` 切分 train/val/test/hil_holdout。
- 让不同故障之间存在合理重叠，例如：
  - `cap_aging` 和 `ac_coupling` 都可能有低频纹波。
  - `pwm_abnormal` 和 `igbt_fault` 都可能有高频毛刺。
  - `insulation` 和 `bus_ground` 都可能有泄漏升高。

这样减少模型只识别固定生成模板的风险。

#### v4

v4 引入 TinyCNN + DWT：

- `116` 维工程特征。
- `4 x 256` 频谱输入。
- `104` 维 db3 小波特征。

目标是增强 `pwm_abnormal` 和 `igbt_fault` 这类高频瞬态故障的区分。

#### v5

v5 做了受控消融实验，不再默认 `4x256 + db3` 就是最终方案。

频谱候选：

- `uniform_256`
- `uniform_512`
- `hybrid_512`

小波候选：

- `haar`
- `db2`
- `db3`
- `db4`
- `sym4`

最终选择 `uniform_512 + db3`，原因是：

- 比 `uniform_256` 保留更细的频谱信息。
- 资源仍能在 H750 上部署。
- IGBT 与 PWM 的召回更均衡。
- db3 在效果、解释性和 STM32 实现复杂度之间折中较好。

### 2.5 目录内容

`EdgeWind_AI_Training` 主要内容：

```text
config/
    数据集、类别、采样率、模型参数配置

scripts/
    数据生成、特征提取、训练、评估、HIL 包生成、CubeMX 后处理脚本

edgewind_ai/
    Python 核心库：波形生成、真实化工况、FFT、DWT、模型训练等

data_v3/
    当前正式原始数据集，约 22.8GB，按 scenario_id 做工况切分

data_v4/
    v4 特征缓存，约 2.8GB

data_v5_ablation/
    v5 频谱分辨率和小波基消融数据，约 9.9GB

models/
    训练好的模型、指标、混淆矩阵、TFLite、preprocess.npz

reports/
    训练报告、消融报告、H750 资源分析报告

DWT/
    旧的小波算法参考代码，只作参考，不建议直接加入主工程

Generate_Code/
    从原监测端 CubeMX 生成并接入 X-CUBE-AI 后复制出来的监测端工程

cube_ai_test_project/
    早期 AI 板端自检临时工程，不是最终主线

stm32_test_vectors/
    板端 golden selftest 用测试向量
```

注意：

- `data_v3/data_v4/data_v5_ablation` 体积很大，不建议直接提交到 git。
- `preprocess.npz` 不能丢。训练时所有输入都做了标准化，STM32 端最终部署必须同步 mean/scale。
- `Generate_Code` 是生成和验证出来的监测端工程副本，不一定等于正式监测端最终目录。

### 2.6 CubeMX / X-CUBE-AI 后处理脚本

X-CUBE-AI 生成代码后必须运行：

```powershell
cd C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training

powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\fix_cubemx_ai_generate.ps1 `
  -ProjectRoot "C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\Generate_Code\STM32H7+FreeRTOS+LVGL+ESP32"
```

该脚本用于修复或确认：

- 25MHz HSE 外部晶振配置。
- Keil X-CUBE-AI 路径。
- `STM32H7x_2048.FLM` 下载算法。
- SDRAM 初始化指针风险。
- 重复 AI runtime lib 条目。

固定流程：

```text
CubeMX Generate Code
    -> 运行 fix_cubemx_ai_generate.ps1
    -> Keil rebuild
    -> 下载
    -> 串口确认 AI golden selftest
```

## 3. `STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0` 是什么

路径：

```text
C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0
```

这个目录是故障播放演示端，也就是 HIL 故障注入端。它不是监测端，不负责 AI 诊断，不负责 ESP32 上传，不负责 Web 展示。

它的职责是：把预先生成的直流系统故障波形，通过 DAC8568 四通道模拟输出给监测端 ADC。

播放端工作链路：

```text
SD 卡 0:/wave/*.bin
        ->
STM32H750XBH6 读取
        ->
W25Q256 QSPI Flash 7 个 4MB 分区
        ->
QSPI memory-mapped
        ->
TIM12 节拍 + SPI1 DMA
        ->
DAC8568 A/B/C/D 四通道模拟输出
        ->
监测端 ADC 输入
```

### 3.1 四通道语义

| DAC8568 通道 | 含义 |
|---|---|
| A | 正母线电压 |
| B | 负母线电压 |
| C | 负载/故障电流 |
| D | 泄漏电流 |

这些输出应接入监测端 ADC 输入，用于验证监测端采样、FFT、AI 诊断和上传链路。

### 3.2 波形文件

播放端 SD 卡入口：

```text
0:/wave/*.bin
```

当前 7 个波形文件：

```text
normal.bin
ac_coupling.bin
bus_ground.bin
insulation.bin
cap_aging.bin
pwm_abnormal.bin
igbt_fault.bin
```

波形格式：

| 参数 | 值 |
|---|---:|
| 采样率 | `102400 Hz` |
| 单文件大小 | `4 MB` |
| 通道数 | 4 |
| 数据类型 | `uint16 DAC code` |
| 存储方式 | header + A/B/C/D 交织数据 |

注意：播放端的 102.4kHz 是 DAC 输出采样率；监测端 AI 仍按 25.6kHz 实采窗口诊断。

### 3.3 W25Q256 分区

W25Q256 前 4MB 保留，后面 7 个 4MB 分区存放 normal + 6 fault：

```text
0x00400000 - 0x007FFFFF  normal
0x00800000 - 0x00BFFFFF  ac_coupling
0x00C00000 - 0x00FFFFFF  bus_ground
0x01000000 - 0x013FFFFF  insulation
0x01400000 - 0x017FFFFF  cap_aging
0x01800000 - 0x01BFFFFF  pwm_abnormal
0x01C00000 - 0x01FFFFFF  igbt_fault
```

启动时从 SD 同步到 W25Q256。当前同步逻辑已经改成幂等同步：

- 如果 W25Q256 header 和 SD header 一致，且数据 checksum 一致，则跳过擦写。
- 只有数据缺失、checksum 不一致或 SD 文件更新时才擦写目标分区。
- 写入顺序为先写数据、校验数据、最后写 header，避免中途断电导致半包数据被误认为 ready。
- `DAC_WAVE_REQUIRE_SD_SYNC=0`，SD 偶发失败时，如果 W25Q256 已有有效数据，仍允许从 QSPI 启动 baseline。

成功日志应类似：

```text
[DAC WAVE] full sync done: ready_mask=0x7F sd_sync_mask=0x7F
[DAC WAVE] baseline source=QSPI sps=102400 count=...
[DAC] ok=... fail=0 skip=0 ready=0x7F sd=0x7F boot=1 stream=1 mmap=1 busy=0
```

### 3.4 UI 故障触发

播放端 UI 当前是 `EdgeWind_UI` 六个故障卡片：

```text
交流窜入
母线接地
绝缘劣化
电容老化
PWM异常
IGBT故障
```

触发逻辑：

- 默认故障持续时间约 10 秒。
- 触发时只切换 QSPI memory-mapped 波形指针。
- 不读 SD。
- 不擦写 QSPI。
- 不恢复旧 GUI 资源同步。
- 故障结束或手动停止后回到 normal。

### 3.5 播放端已完成的关键修复

这个播放端做过大量稳定性修复：

1. 修复 `sdram.c` 未初始化 `FMC_SDRAM_CommandTypeDef *Command` 的 HardFault 风险。
2. 修复 `sd_diskio.c` 非 4 字节对齐写路径等待错消息的问题。
3. 修复 SD 卡不存在时 `sdmmc.c` 直接 `Error_Handler()` 导致系统死机的问题。
4. 把 SD 到 W25Q256 同步改成幂等同步，避免每次复位都擦写 7 个 4MB 分区。
5. W25Q256 分区增加 header/data checksum 校验，防止半写入数据被误判为 ready。
6. 允许 SD 偶发失败时从已有有效 QSPI 数据启动 baseline。
7. 运行期故障触发只切 QSPI 波形指针，不读 SD、不擦写 QSPI。
8. 增加 QSPI/DAC 播放 guard，避免播放时被旧 GUI/QSPI FatFs 资源同步破坏。
9. 修复故障命令单槽覆盖，降低 UI 快速点击导致的误触发风险。
10. 优化 UI 卡顿，减少 LVGL 滚动刷新对主服务任务的干扰。
11. 修复 IGBT 最后分区被 QSPI 尾部 guard 截断的问题。
12. 保留 H750XBH6、自定义 `STM32H7x_2048.FLM` 和当前 scatter，不切 H743 假设。

### 3.6 播放端目录内容

主要内容：

```text
Core/
    STM32 主程序和 HAL 初始化

MDK-ARM/
    Keil 工程、scatter、HARDWORK 外设和 UI 代码

MDK-ARM/HARDWORK/DAC8568/
    DAC8568 DMA 播放核心

MDK-ARM/HARDWORK/SD_Card/
    SD 到 QSPI 波形同步逻辑

MDK-ARM/HARDWORK/W25Q256/
    QSPI Flash 驱动和 memory-mapped 管理

MDK-ARM/HARDWORK/EdgeWind_UI/
    当前播放端 UI

sd_card_payload/
    拷贝到 SD 卡的 wave/*.bin 数据

tools/
    波形生成、校验、Keil 审计等脚本

legacy_not_build/
    旧逻辑隔离区，不应重新加入 Keil 主线

NEXT_AI_PROJECT_HANDOFF.md
    播放端给下一个 AI 的技术交接文档

STM32H7x_2048.FLM
    当前 H750XBH6 项目使用的下载算法
```

### 3.7 播放端当前 git 状态

当前播放端目录本身是一个 git 仓库。最近检查到：

```text
HEAD: 3b22e92 Stabilize fault waveform playback and UI responsiveness
tag : V2.7.0-fault-playback-stability-20260505-234916-dirty
```

检查时工作区不是完全干净，存在 CubeMX 相关改动和两个 HTML 删除项。合并前建议先决定：

- 是否先提交当前播放端状态。
- 是否打新标签。
- 是否作为 git submodule 放入总项目。
- 是否移除内部 `.git` 后作为普通子目录合并。

不要在不确认的情况下覆盖或丢弃播放端已有改动。

## 4. 合并到 `EdgeWind_STM32_ESP32` 的建议结构

目标总项目：

```text
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32
```

推荐结构：

```text
EdgeWind_STM32_ESP32/
    monitoring_firmware/
        原 STM32H7+FreeRTOS+LVGL+ESP32 监测端主工程

    ai_training/
        原 EdgeWind_AI_Training

    playback_dac8568/
        原 STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0

    docs/
        总交接文档、系统架构、HIL 测试报告、答辩材料
```

如果暂时不想重命名，也可以保留原目录名：

```text
EdgeWind_STM32_ESP32/
    STM32H7+FreeRTOS+LVGL+ESP32/
    EdgeWind_AI_Training/
    STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0/
```

但长期看，`ai_training/` 和 `playback_dac8568/` 更清楚。

## 5. 合并时必须注意的风险

### 5.1 大数据不要直接提交

`EdgeWind_AI_Training` 体积很大：

```text
data_v3          约 22.8 GB
data_v4          约 2.8 GB
data_v5_ablation 约 9.9 GB
```

这些数据不建议直接提交到普通 git。建议：

- 本地保留。
- 或放到外部硬盘/网盘。
- 或使用 Git LFS。
- 或只提交配置、脚本、模型和小规模 smoke 数据。

### 5.2 绝对路径需要更新

AI 训练工程和 CubeMX 生成工程里存在不少绝对路径，例如：

```text
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training\...
```

移动到 `EdgeWind_STM32_ESP32` 内部后，需要检查：

- `config/*.json`
- `scripts/*.py`
- `scripts/*.ps1`
- `.ioc`
- Keil `.uvprojx`
- 文档中的模型路径
- X-CUBE-AI 的 LatestDirectoryUsed 和 ModelStructureFile

### 5.3 嵌套 git 问题

播放端目录当前有自己的 `.git`。如果直接复制到 `EdgeWind_STM32_ESP32`，会形成嵌套 git。

可选方案：

1. 作为 submodule 管理。
2. 移除内部 `.git` 后作为普通子目录管理。
3. 保持独立仓库，只在总项目文档中引用路径。

不建议无意识地把嵌套 `.git` 复制进去后再直接提交。

### 5.4 播放端不要恢复旧逻辑

播放端不能恢复旧 GUI-Guider/QSPI FatFs 资源同步主线，否则可能破坏 DAC 波形分区和播放仲裁。

特别不要恢复：

- 旧 `gui_resource_map.h`
- 旧 QSPI FS GUI 资源同步路线
- 运行期 QSPI 擦写或退出 memory-map 的逻辑
- `share_to_gemini_100`

### 5.5 H750XBH6 不要误改 H743

播放端和监测端都围绕 STM32H750XBH6 使用。

注意：

- 不要切到 H743 Device。
- 不要套用 H743 2MB 内部 Flash 假设。
- 不要删除当前可用的 `STM32H7x_2048.FLM`。
- 不要随意改 active scatter。

### 5.6 AI golden selftest 不是完整闭环

当前 AI 板端 golden selftest 证明的是：

- X-CUBE-AI 模型能在 H750 上创建、运行。
- 21 组已知输入的输出与 PC 参考一致。

它还不等于完整系统已经完成：

- 监测端实时特征提取尚需正式接入。
- 多窗口投票和置信度门限尚需完善。
- 上传协议和 Web 展示字段尚需扩展。
- 播放端 DAC8568 到监测端 ADC 的端到端 HIL 识别率尚需实测。

## 6. 合并后的推荐工作顺序

建议按下面顺序推进，避免一次性把所有内容搅在一起。

### 阶段 1：整理目录

1. 备份 `EdgeWind_STM32_ESP32` 当前状态。
2. 决定是否保留播放端 `.git`。
3. 把 `EdgeWind_AI_Training` 放入 `ai_training/`。
4. 把 `STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0` 放入 `playback_dac8568/`。
5. 大数据目录加入 `.gitignore` 或保留在本地不提交。

### 阶段 2：监测端 AI 接入

1. 确认正式监测端 Keil 工程能单独 rebuild。
2. 导入 X-CUBE-AI 生成代码。
3. 运行 `fix_cubemx_ai_generate.ps1` 或迁移其中的修复逻辑。
4. 保留 `EdgeWind_AI_RunGoldenSelfTest()` 作为下载后健康检查。
5. 添加 `edgewind_ai_runtime.c/h`。
6. 添加特征提取模块：
   - `edgewind_fault_features.c/h`
   - `edgewind_dwt_features.c/h`
7. 在每个 4096 点窗口 FFT 完成后调用 AI。
8. 串口打印：
   - `fault_code`
   - `confidence`
   - `probabilities[7]`

### 阶段 3：诊断状态机和上传

1. 增加多窗口投票。
2. 增加置信度门限。
3. 增加故障保持和恢复逻辑。
4. 扩展 ESP32 上传字段。
5. Web 页面显示 AI 诊断结果和概率。

### 阶段 4：播放端 HIL 验证

1. 播放端插 SD，确认 7 个波形同步到 W25Q256。
2. 播放端串口确认：

   ```text
   ready_mask=0x7F
   baseline source=QSPI
   stream=1
   mmap=1
   ```

3. DAC8568 A/B/C/D 接入监测端 ADC。
4. 依次播放：

   ```text
   normal
   ac_coupling
   bus_ground
   insulation
   cap_aging
   pwm_abnormal
   igbt_fault
   ```

5. 记录：
   - 识别率。
   - 误报率。
   - E04/E06 混淆。
   - E02/E05 混淆。
   - 平均告警延迟。
   - 故障结束后恢复 normal 的时间。
   - 上传成功率和 Web 展示延迟。

## 7. 给新对话 AI 的最短背景

如果放弃当前对话记录，可以把下面这段作为新对话开场：

```text
当前我要把 EdgeWind_AI_Training 和 STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0 合并进 EdgeWind_STM32_ESP32。

EdgeWind_AI_Training 是 PC 端 AI 训练和 STM32Cube.AI 部署准备工程。最终模型是 dataset_v5_uniform_512_db3，输入为 116维工程特征 + 4x512频谱 + 104维db3小波特征，输出 E00 normal + E01 ac_coupling + E02 insulation + E03 cap_aging + E04 igbt_fault + E05 bus_ground + E06 pwm_abnormal。模型已通过 X-CUBE-AI Analyze 和 H750 板端 golden selftest，21/21 pass，推理约 35.7ms。

STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0 是故障播放端，不是监测端。它从 SD:/wave/*.bin 同步 normal + 6 fault 到 W25Q256 七个 4MB 分区，通过 QSPI memory-mapped + TIM12 + SPI1 DMA 驱动 DAC8568 A/B/C/D 四通道输出，作为监测端 ADC 的 HIL 故障注入源。播放端已做幂等同步、checksum、QSPI/DAC guard、UI 故障触发和多项启动风险修复。

EdgeWind_STM32_ESP32 是总项目和监测端主工程，负责 ADC/FFT/AI诊断/ESP32上传/Web展示。后续目标是把 AI 模型正式接入监测端，再用 DAC8568 播放端做 normal + 6 fault 端到端测试。
```

## 8. 当前推荐保留的文档

合并后建议保留并优先阅读：

```text
EdgeWind_STM32_ESP32\EDGEWIND_HIL_PROJECT_ANALYSIS_20260506.md
EdgeWind_STM32_ESP32\NEXT_AI_HANDOFF_TECHNICAL_STATUS_20260504.md
EdgeWind_STM32_ESP32\MONITORING_SIDE_AI_DETAILED_INTRODUCTION.md
EdgeWind_STM32_ESP32\EDGEWIND_SUBPROJECTS_MERGE_CONTEXT.md
ai_training\EDGEWIND_AI_TRAINING_WORK_SUMMARY_AND_DEPLOYMENT.md
ai_training\MONITORING_SIDE_AI_DETAILED_INTRODUCTION.md
playback_dac8568\NEXT_AI_PROJECT_HANDOFF.md
```

如果文档之间出现冲突，以最新实测日志、Keil rebuild 结果、串口输出和当前源码为准。

