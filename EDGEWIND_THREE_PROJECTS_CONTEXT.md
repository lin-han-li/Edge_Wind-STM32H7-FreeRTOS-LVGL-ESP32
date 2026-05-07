# EdgeWind 三项目协作关系说明

更新日期：2026-05-07

当前项目视角：

```text
本项目：EdgeWind_STM32_ESP32
角色：监测诊断端主项目 / ESP32 上传 / 服务器与 Web 展示 / 大创总项目入口
```

本文档说明当前大创项目中三个独立项目之间的关系。三个项目不计划合并源码，也不互相覆盖目录。它们保持独立，通过文档、模型产物、波形产物和硬件接口协作。

## 1. 三个项目路径

```text
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training
C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0
```

职责划分：

| 项目 | 角色 | 主要责任 |
|---|---|---|
| `EdgeWind_STM32_ESP32` | 监测诊断端主项目 | STM32 采样、FFT、AI 诊断、ESP32 上传、服务器和 Web 展示 |
| `EdgeWind_AI_Training` | PC 端 AI 训练工程 | 生成数据、训练模型、评估模型、导出 TFLite、验证 X-CUBE-AI 部署 |
| `STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0` | DAC8568 故障播放端 | 输出 normal + 6 类故障模拟信号，作为监测端 ADC 的 HIL 故障注入源 |

## 2. 总体协作链路

```text
EdgeWind_AI_Training
    训练并导出 AI 模型
        |
        v
EdgeWind_STM32_ESP32
    监测端采样、FFT、AI 诊断、上传、Web 展示
        ^
        |
STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0
    DAC8568 输出四通道模拟故障波形
```

更具体的硬件和数据链路：

```text
AI 训练工程
    -> model_float32.tflite / preprocess.npz / golden vectors
    -> 监测端 STM32H750XBH6 X-CUBE-AI 推理

播放端 SD:/wave/*.bin
    -> W25Q256 QSPI
    -> DAC8568 A/B/C/D
    -> 监测端 ADC 输入
    -> 4ch x 4096 波形
    -> FFT / 特征提取 / AI 诊断
    -> ESP32 上传
    -> Flask / Web 展示
```

## 3. `EdgeWind_STM32_ESP32` 是什么

这是监测诊断端主项目，也是三个项目中最接近“总项目入口”的目录。

它主要包含：

- STM32H750XBH6 监测端固件。
- AD7606/ADC 采样。
- `4ch x 4096` 波形窗口。
- `4ch x 2048` FFT 结果。
- ESP32 SPI 协处理器通信。
- HTTP 上传和 full frame 上报。
- Flask 服务端和 Web 监测页面。
- 故障记录、实时波形、FFT、报告展示等上层功能。

当前监测端关键基线：

```text
采样窗口：4ch x 4096
FFT：4ch x 2048
监测端采样率：25600 Hz
单窗口时长：0.16 s
典型 full frame：约 49348 bytes
```

本项目应知道另外两个项目：

- 从 `EdgeWind_AI_Training` 获取 AI 模型和预处理常量。
- 从 `STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0` 获取真实硬件注入的模拟故障输入。

## 4. `EdgeWind_AI_Training` 是什么

路径：

```text
C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_AI_Training
```

这是 PC 端 AI 训练与部署准备工程，不是正式监测端主工程，也不是 DAC 播放端。

它完成了：

- 生成风电场直流系统 normal + 6 fault 训练数据。
- 构造更真实的工况随机化数据集。
- 按 `scenario_id` 做 train/val/test/hil_holdout 工况切分。
- 提取工程特征、FFT 频谱特征、小波特征。
- 训练并比较 v1 到 v5 多个模型。
- 最终选择 `dataset_v5_uniform_512_db3`。
- 导出 `model_float32.tflite`。
- 使用 STM32CubeMX / X-CUBE-AI 生成并验证 H750 板端推理代码。

当前推荐模型：

```text
models/dataset_v5_uniform_512_db3/model_float32.tflite
models/dataset_v5_uniform_512_db3/preprocess.npz
```

模型输入：

```text
X_feat: 116 维工程统计特征
X_spec: 4 x 512 频谱特征
X_dwt : 104 维 db3 小波特征
```

模型输出 7 类：

```text
E00 normal
E01 ac_coupling
E02 insulation
E03 cap_aging
E04 igbt_fault
E05 bus_ground
E06 pwm_abnormal
```

当前模型指标：

```text
test accuracy        约 99.10%
HIL holdout accuracy 约 99.53%
TFLite size          78,284 bytes
X-CUBE-AI Flash      约 90 KB
X-CUBE-AI RAM        约 119 KB
H750 实测模型推理    约 35.7 ms/window
```

板端 golden selftest 结果：

```text
AI golden summary: pass=21 fail=0 avg_us=35746 min_us=35382 max_us=36273 core=480000000 hclk=240000000
```

注意：

- `preprocess.npz` 不能丢，里面是模型输入标准化用的 mean/scale。
- `data_v3/data_v4/data_v5_ablation` 体积很大，不建议直接提交到普通 git。
- AI golden selftest 证明模型推理链路正确，不等于完整 ADC 实采闭环已经完成。

## 5. `STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0` 是什么

路径：

```text
C:\Users\pengjianzhong\Desktop\MY_Project\STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0
```

这是故障播放端，也就是 HIL 故障注入源。它不负责 AI 诊断，不负责 ESP32 上传，不负责 Web 展示。

它的任务是把预生成的直流系统故障波形通过 DAC8568 输出给监测端 ADC。

播放链路：

```text
SD 卡 0:/wave/*.bin
    -> STM32H750XBH6
    -> W25Q256 QSPI Flash
    -> QSPI memory-mapped
    -> TIM12 + SPI1 DMA
    -> DAC8568 A/B/C/D
    -> 监测端 ADC
```

通道语义：

| DAC 通道 | 含义 |
|---|---|
| A | 正母线电压 |
| B | 负母线电压 |
| C | 负载/故障电流 |
| D | 泄漏电流 |

波形文件：

```text
normal.bin
ac_coupling.bin
bus_ground.bin
insulation.bin
cap_aging.bin
pwm_abnormal.bin
igbt_fault.bin
```

波形参数：

```text
播放端采样率：102400 Hz
单文件大小：4 MB
通道数：4
数据类型：uint16 DAC code
```

播放端已完成的关键稳定性工作：

- SD 到 W25Q256 幂等同步。
- W25Q256 header/data checksum 校验。
- 断电半写入保护。
- QSPI memory-mapped 播放保护。
- DAC8568 DMA 连续播放。
- UI 六类故障触发。
- PWM/IGBT 等波形播放稳定性修复。
- H750XBH6 下载算法和 scatter 约束固化。

当前播放端应保持独立，不要把旧 GUI/QSPI FatFs 资源同步逻辑重新加入主线。

## 6. 三项目之间的接口约定

### 6.1 AI 训练工程到监测端

传递内容：

```text
model_float32.tflite
preprocess.npz
类别表
特征定义
golden vectors
X-CUBE-AI 生成代码
```

监测端需要实现：

```text
4ch x 4096 波形缓存
116维工程特征
4x512频谱压缩
104维db3 DWT小波特征
ai_network_run()
fault_code / confidence / probabilities[7]
```

### 6.2 播放端到监测端

硬件接口：

```text
DAC8568 A -> 监测端 ADC 正母线电压输入
DAC8568 B -> 监测端 ADC 负母线电压输入
DAC8568 C -> 监测端 ADC 负载/故障电流输入
DAC8568 D -> 监测端 ADC 泄漏电流输入
GND 共地
```

联调顺序：

```text
normal
ac_coupling
bus_ground
insulation
cap_aging
pwm_abnormal
igbt_fault
```

### 6.3 监测端到服务器/Web

监测端负责：

```text
采样
FFT
AI 诊断
ESP32 上传
服务器存储
Web 展示
```

后续需要扩展上传字段：

```text
fault_code
confidence
probabilities[7]
diagnosis_latency_ms
```

## 7. 不要做的事

三个项目保持独立，因此不要：

- 不要把三个项目源码硬合并成一个 Keil 工程。
- 不要让播放端代码覆盖监测端代码。
- 不要让 AI 训练数据进入嵌入式固件仓库。
- 不要把几十 GB 的 `data_v3/data_v4/data_v5_ablation` 直接提交到普通 git。
- 不要恢复播放端旧 QSPI FatFs GUI 资源同步逻辑。
- 不要把 H750XBH6 项目误改成 H743 配置。
- 不要丢失 `preprocess.npz`。
- 不要只看 AI golden selftest 就宣称端到端 HIL 已完成。

## 8. 后续推荐验证顺序

1. 监测端单独编译下载，确认采样、FFT、ESP32 上传正常。
2. 监测端接入 AI runtime，保留 golden selftest。
3. 实现正式实时特征提取和 `ai_network_run()`。
4. 串口打印每个窗口的 AI 输出。
5. Web 增加 AI 诊断结果展示。
6. 播放端插 SD，同步 7 个波形到 W25Q256。
7. 播放端 DAC8568 接监测端 ADC。
8. 依次播放 normal + 6 fault。
9. 记录识别率、误报率、混淆矩阵和告警延迟。

## 9. 新对话最短背景

如果后续放弃当前对话记录，可以把下面这段发给新 AI：

```text
当前有三个独立项目，不计划合并源码，只要求彼此知道存在并按接口协作。

EdgeWind_STM32_ESP32 是监测诊断端主项目，负责 STM32H750XBH6 采样、FFT、AI诊断、ESP32上传、服务器和Web展示。

EdgeWind_AI_Training 是 PC 端 AI 训练和 STM32Cube.AI 部署准备工程。最终模型是 dataset_v5_uniform_512_db3，输入为 116维工程特征 + 4x512频谱 + 104维db3小波特征，输出 E00 normal + E01 ac_coupling + E02 insulation + E03 cap_aging + E04 igbt_fault + E05 bus_ground + E06 pwm_abnormal。模型已通过 X-CUBE-AI Analyze 和 H750 板端 golden selftest，21/21 pass，推理约 35.7ms。

STM32H750XBH6_DAC8568_FreeRTOS_LVGL9.4.0 是故障播放端，不是监测端。它从 SD:/wave/*.bin 同步 normal + 6 fault 到 W25Q256 七个 4MB 分区，通过 QSPI memory-mapped + TIM12 + SPI1 DMA 驱动 DAC8568 A/B/C/D 四通道输出，作为监测端 ADC 的 HIL 故障注入源。
```

