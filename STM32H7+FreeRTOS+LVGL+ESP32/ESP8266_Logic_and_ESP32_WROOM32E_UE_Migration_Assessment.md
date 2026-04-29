# ESP8266 现状逻辑、瓶颈与 ESP32-WROOM-32E/UE 迁移评估

## 1. 文档目的

本文基于当前仓库代码、运行日志和 Espressif 官方资料，系统梳理当前项目里 ESP8266 相关逻辑：

1. 它现在到底承担了哪些功能。
2. 它和 STM32H750、FreeRTOS、UI、服务端之间是如何配合的。
3. 当前瓶颈、脆弱点和后续维护风险在哪里。
4. 未来把 ESP8266 换成 `ESP32-WROOM-32E` 或 `ESP32-WROOM-32UE` 是否可行。
5. 如果迁移，难点、收益、性能效果和推荐路线分别是什么。

这份文档同时吸收了 6 个专题子分析的结论，主题覆盖：

- ESP8266 总体架构与状态机
- 发送路径与 HTTP 封包
- 接收路径、ACK 与重连
- UI 配置与 SD 持久化
- 瓶颈、可靠性与维护性风险
- ESP32-WROOM-32E/UE 迁移可行性

## 2. 结论先行

### 2.1 一句话结论

当前系统里的 ESP8266 已经不是一个“单纯联网模组”，而是一套 **STM32 侧主导的 AT 透传通信子系统**。真正重的工作并不在 ESP8266 内部，而在 STM32 侧：采样数据整理、FFT、JSON 拼包、HTTP 组包、命令解析、状态恢复都放在 `esp8266.c` 里完成。

### 2.2 我对迁移的总判断

- 把 ESP8266 换成 `ESP32-WROOM-32E/UE`，**整体可行**。
- 但要先区分两种迁移路线：

| 路线 | 可行性 | 改动风险 | 收益 | 我的判断 |
| --- | --- | --- | --- | --- |
| `ESP32 + ESP-AT`，尽量平替现有 AT 透传架构 | 高 | 低到中 | 中等 | 适合第一阶段验证 |
| `ESP32 原生固件`，把 Wi-Fi/HTTP/重连/缓存搬到 ESP32 | 高 | 中到高 | 很高 | 这是长期正确方向 |

- 如果只是“换模组但保持 STM32 拼 JSON + HTTP + 透传 TCP”，那么 **能跑，但不会产生数量级性能提升**。
- 如果把 ESP32 作为真正的通信协处理器来用，那么 **稳定性、扩展性和后续支持 HTTPS/MQTT/WebSocket/缓存/断线续传的能力都会明显提升**。

### 2.3 推荐路线

推荐按两阶段推进：

1. **阶段 1：ESP32-AT 兼容性平替**
   目标是尽快验证板级、电源、UART、透明模式、注册、心跳、重连是否兼容。
2. **阶段 2：ESP32 原生固件重构通信栈**
   目标是把当前最脆弱的 “AT + 透明模式 + 巨型 JSON + 字符串扫描” 架构替换掉。

---

## 3. 当前 ESP8266 的总体定位

### 3.1 它不是“开机自动联网驱动”，而是 UI 驱动的分阶段连接器

`ESP8266_Task()` 启动后不会直接自动全链路联网，而是：

- 初始化控制台 `ESP_Console_Init()`
- 初始化 UI 命令队列 `ESP_UI_TaskInit()`
- 在循环中执行：
  - `ESP_UI_TaskPoll()`
  - `ESP_Console_Poll()`
  - 若 UI 允许上报，则执行 `ESP_Update_Data_And_FFT()` 和 `ESP_Post_Data()/ESP_Post_Summary()`

关键位置：

- `Core/Src/freertos.c:541-572`
- `MDK-ARM/HARDWORK/ESP8266/esp8266.c:3085-3569`

正常连接链路是：

`ESP_UI_DoWiFi -> ESP_UI_DoTCP -> ESP_UI_DoRegister -> ESP_UI_ToggleReport`

也就是说，当前实现已经从“一个开机自动初始化的老式 AT 驱动”演变成了 **GUI 驱动的分步状态机**。

### 3.2 ESP8266 实际处在一个 STM32H7 的大通信系统里

当前 STM32H750 平台特点：

- 主控：STM32H750
- RTOS：FreeRTOS
- GUI：LVGL 9.4
- 采样：TIM2 中断驱动采样双缓冲
- 通信：USART2 + DMA + AT 透传 TCP

ESP8266 相关硬件链路：

- `USART2` 专用于 ESP8266
- 波特率：`2,000,000`
- TX：DMA Normal
- RX：DMA Circular
- 复位脚：`PD4`
- 串口脚：`PD5/PD6`

关键位置：

- `Core/Src/usart.c:85-126`
- `Core/Src/usart.c:237-276`
- `Core/Inc/main.h:61-70`

### 3.3 它依赖 H7 的 Cache / DMA / AXI SRAM / SDRAM 体系

当前驱动明显不是一个“轻量 UART 驱动”，而是重度依赖 H7 内存层次结构：

- `http_packet_buf`：固定到 SDRAM，大小 `512 KB`
- `g_stream_rx_buf[4096]`：AXI SRAM，32 字节对齐
- `node_channels[]`：AXI SRAM
- FFT 工作区：AXI SRAM

关键位置：

- `Core/Src/main.c:121-129`
- `MDK-ARM/HARDWORK/ESP8266/esp8266.c:113-177`
- `MDK-ARM/HARDWORK/ESP8266/esp8266.c:324-331`

这意味着当前 ESP8266 子系统其实已经把：

- 数据缓存
- 串口 DMA
- D-Cache clean/invalidate
- 超大包发送

都绑在 STM32H7 的资源模型上了。

---

## 4. 当前 ESP8266 相关功能总表

### 4.1 配置与参数类

当前实现具备以下配置能力：

| 功能 | 位置 | 说明 |
| --- | --- | --- |
| 编译期默认 Wi-Fi / 服务器 / Node 参数 | `esp8266_config.h` | 当前文件中仍有硬编码默认配置，包含明文 Wi-Fi 密码，此文不复述 |
| 运行时通信参数 | `ESP_CommParams_*` | 心跳、最小发送间隔、HTTP 超时、硬复位阈值、降采样、上传点数、分块大小、分块延时 |
| SD 覆盖 Wi-Fi 配置 | `0:/config/ui_wifi.cfg` | `SSID=...`、`PWD=...` |
| SD 覆盖服务器配置 | `0:/config/ui_server.cfg` | `IP=...`、`PORT=...`、`ID=...`、`LOC=...` |
| SD 覆盖通信参数 | `0:/config/ui_param.cfg` | 8 项通信参数 |
| 断电重连配置 | `0:/config/ui_autoreport.cfg` | `AUTO_RECONNECT`、`LAST_REPORTING` |

关键位置：

- `MDK-ARM/HARDWORK/ESP8266/esp8266.h:76-114`
- `MDK-ARM/HARDWORK/ESP8266/esp8266.c:496-829`
- `MDK-ARM/HARDWORK/GUI-Guider_Runtime/src/generated/events_init.c:306-1783`

### 4.2 UI 侧功能

GUI 已经暴露 4 组 ESP 相关页面/入口：

- `WiFiConfig`
- `ServerConfig`
- `ParamConfig`
- `DeviceConnect`

`DeviceConnect` 页面明确提供：

- `WiFi`
- `TCP`
- `REG`
- `开始上传`
- `一键连接`
- `断电重连`

关键位置：

- `MDK-ARM/HARDWORK/GUI-Guider_Runtime/src/generated/gui_guider.h:71-184`
- `MDK-ARM/HARDWORK/GUI-Guider_Runtime/src/generated/events_init.c:2262-2824`

一个容易被旧文档误导的点：

- UI 上的 `Scan` 实际不是扫描周围 Wi-Fi，而是 **从 SD 重新加载 Wi-Fi 配置**。

### 4.3 联网与注册类

当前 ESP 侧功能包括：

- 切换到 AT 模式
- Wi-Fi 连接 `CWJAP`
- TCP 连接 `CIPSTART`
- 进入透明传输 `CIPMODE=1 + CIPSEND`
- 注册 `POST /api/register`
- 周期性心跳/摘要/全量上传 `POST /api/node/heartbeat`
- 透明连接复用
- 软重连
- 硬复位

关键位置：

- `MDK-ARM/HARDWORK/ESP8266/esp8266.c:923-1106`
- `MDK-ARM/HARDWORK/ESP8266/esp8266.c:2507-2693`
- `MDK-ARM/HARDWORK/ESP8266/esp8266.c:3313-3508`

### 4.4 数据处理类

当前 ESP 子系统还承担了 STM32 侧数据准备工作：

- 从双缓冲读取 4 路采样
- 复制波形
- 计算均值
- 计算 4 路 FFT
- 生成摘要上传数据
- 生成全量上传数据

关键位置：

- `MDK-ARM/HARDWORK/ESP8266/esp8266.c:1194-1279`
- `Core/Src/main.c:336-390`
- `Core/Src/freertos.c:557-570`

### 4.5 服务端控制下发类

服务端现在可以反向控制设备：

- `report_mode`
- `downsample_step`
- `upload_points`
- `reset`

设备接收方式不是 JSON 反序列化，而是 **透传字节流里做关键字/数字扫描**。

关键位置：

- 固件接收与生效：`esp8266.c:1388-1577, 2263-2373`
- 服务端下发：`Edge_Wind_System/edgewind/routes/api.py:711-1018, 1160-1319`

---

## 5. 当前核心逻辑是怎么跑起来的

### 5.1 启动和线程关系

当前固件中相关线程主要有 4 个：

- `LVGL940`
- `LED`
- `Main`
- `ESP8266`

其中：

- `Main_Task` 会在 GUI 资源同步期间挂起 ESP 任务，避免资源冲突
- `ESP8266_Task` 是唯一的网络执行线程
- ESP 线程启动时先提到 `AboveNormal`，初始化后降回 `Normal`

关键位置：

- `Core/Src/freertos.c:281-308`
- `Core/Src/freertos.c:478-523`
- `Core/Src/freertos.c:541-572`

### 5.2 当前连接状态机

当前至少叠了 4 层状态：

| 状态层 | 关键变量 | 作用 |
| --- | --- | --- |
| UART 模式态 | `g_uart2_at_mode` | 区分 AT 阻塞收发与透明 DMA 收发 |
| 连接就绪态 | `g_ui_wifi_ok/g_ui_tcp_ok/g_ui_reg_ok/g_esp_ready/g_report_enabled` | 表示 Wi-Fi/TCP/REG/REPORT 各层是否成功 |
| HTTP TX 相位态 | `g_http_tx_phase` | Header 正在发 / Body 正在发 |
| 分段发送态 | `g_tx_chunk` | 大包分块发送上下文 |

关键位置：

- `esp8266.c:355-372`
- `esp8266.c:391-395`
- `esp8266.c:3051-3054`

### 5.3 发送路径

真实发送主链不是 `/api/upload`，而是：

1. `POST /api/register`
2. 透明传输建立成功
3. 周期性 `POST /api/node/heartbeat`

其中：

- `summary` 和 `full` 都发到 `/api/node/heartbeat`
- `summary` 只发通道标量值
- `full` 会额外发 `waveform[]` 和 `fft_spectrum[]`

关键位置：

- `esp8266.c:1685-1865`：摘要上传
- `esp8266.c:1874-2089`：全量上传
- `esp8266.c:2096-2144`：心跳上传
- `Edge_Wind_System/edgewind/routes/api.py:541-702`：旧兼容 `/api/upload`
- `Edge_Wind_System/edgewind/routes/api.py:711-1018`：主用 `/api/node/heartbeat`

### 5.4 HTTP 组包策略

当前 HTTP 发送是 STM32 手写组包：

- 先在 `512 KB` SDRAM 缓冲区预留 `256 B` Header 空间
- 后面拼 JSON Body
- 再回填 `POST ... HTTP/1.1` 和 `Content-Length`

小包：

- 走 “Header DMA -> TxCplt 回调 -> Body DMA” 的两段链式发送

大包：

- 走 `chunk_kb/chunk_delay_ms` 分块发送
- 默认 `4 KB` 一块，块间延时 `10 ms`

关键位置：

- `esp8266.c:79-103`
- `esp8266.c:169-176`
- `esp8266.c:1726-1844`
- `esp8266.c:1915-2067`
- `esp8266.c:2147-2186`

### 5.5 接收路径

接收路径完全基于 `USART2 RX DMA Circular + ReceiveToIdle`：

- `g_stream_rx_buf[4096]`：DMA 环形缓冲
- `g_stream_window[256]`：滑动窗口，处理跨包关键字
- 回调按 DMA 写指针增量读取新字节，不重复 stop/start DMA

关键位置：

- `esp8266.c:2232-2247`
- `esp8266.c:2263-2373`
- `esp8266.c:2381-2447`
- `esp8266.c:2458-2496`

### 5.6 ACK 与服务端控制

当前 ACK 机制并不是显式协议 ACK 帧，而是 **设备在后续上报里回带参数值**：

- 服务端把待下发值放进 `node_downsample_commands`、`node_upload_points_commands`
- 心跳响应里带回 `downsample_step/upload_points`
- STM32 从透明流里解析出新值并应用、持久化
- 后续设备再次上报顶层字段时，服务端对比是否一致
- 一致就视为 ACK 完成

关键位置：

- 固件：`esp8266.c:1388-1577, 1737-1741, 1938-1943, 2263-2373`
- 服务端：`api.py:287-372, 758-767, 1012-1018, 1218-1328`

`reset` 更弱一些，服务端基本是“看到 `fault_code == E00` 就算 reset 已执行”。

---

## 6. 当前瓶颈与脆弱点

### 6.1 最大瓶颈不是 ESP8266 算力，而是整条 AT 透传架构

当前全量模式的数据量非常大：

- 波形：`4 x 4096` 点
- 频谱：`4 x 2048` 点
- 还要加上 JSON 字段名、通道元数据、HTTP 头

保守估算：

- 一个全量包常见就会落在 **100 KB 以上**
- 极端情况下可以更高

而 `USART2` 当前只有 `2,000,000 baud`：

- 8N1 下理论有效吞吐约 `200,000 B/s`
- 也就是约 `195 KiB/s`
- 再扣掉 AT 透传、串口空隙、回包等待、DMA 切换、ESP8266 内部发送节流，实际吞吐会更低

这意味着：

- 全量包天然就是 **数百毫秒到 1 秒级** 的事务
- 但默认 `ESP_MIN_SEND_INTERVAL_MS = 200`
- 所以系统从设计上就容易堆积

关键位置：

- `usart.c:95-124`
- `esp8266.h:84-97`
- `freertos.c:557-572`
- `esp8266.c:1923-2037`

### 6.2 日志已经出现坏帧证据

服务端日志不是“怀疑有问题”，而是已经出现了明显的坏帧现象：

- `Bad HTTP/0.9 request type`
- `Bad request syntax`
- 明显能看到波形数字、`Content-Length`、HTTP 头残片混在一起

同时，同一段日志里还能看到：

- `/api/node/heartbeat` 的总耗时常见在 `~1220 ms`
- 也存在 `~210 ms` 的较轻事务

这与当前 full/summary 两种模式、分块传输、门控放行逻辑完全对得上。

关键证据：

- `Edge_Wind_System/logs/server_eventlet_stderr.log:161-285`

### 6.3 HTTP 边界控制不够严

当前 `g_waiting_http_response` 的放行条件偏宽：

- 只要 RX 回调收到新字节，就可能提前清掉等待标志
- 不一定等完整 HTTP 头/完整响应结束
- 另有 `HTTP/` 子串辅助判断

这会带来一个核心风险：

- 前一个请求的响应还没完全收稳，后一个请求就开始发
- 最终导致 HTTP 边界错乱，形成坏帧

关键位置：

- `esp8266.c:1711-1724`
- `esp8266.c:1900-1913`
- `esp8266.c:2269-2370`
- `esp8266.c:2402-2408`

### 6.4 同一 UART 同时承担两种完全不同的通信语义

当前 `USART2` 同时承担：

- 阻塞式 AT 命令收发
- 透明传输下的 DMA 流式收发

所以代码里才会出现大量：

- `Abort`
- `ForceStop_DMA`
- 强制状态位切换
- 收发模式隔离
- 错误后重启 DMA

这类代码本质上是在补偿架构脆弱性，而不是正常业务逻辑。

关键位置：

- `esp8266.c:391-395`
- `esp8266.c:857-878`
- `esp8266.c:2455-2502`
- `esp8266.c:3155-3225`

### 6.5 下行控制协议太脆

当前从服务器回包里识别命令，靠的是：

- `"command"` + `reset`
- `"report_mode"` + `full/summary`
- `"downsample_step"` 后找数字
- `"upload_points"` 后找数字

问题是：

- 不是严格 JSON 状态机
- 不校验 `Content-Length`
- 不校验 HTTP body 边界
- 容易误匹配
- 容错与服务端校验规则也不完全对称

关键位置：

- `esp8266.c:1388-1480`
- `esp8266.c:1513-1577`
- `esp8266.c:2263-2373`
- `api.py:252-372`

### 6.6 重连代价高

当前只要透明模式失效，恢复链路成本就不低：

- 退出透传前后 guard time：`1.2s + 1.2s`
- 硬复位后等待：`2.5s`
- Wi-Fi 连接单次等待：`20s`

轻微抖动都可能演变成秒级甚至十几秒级停报。

关键位置：

- `esp8266.c:2598-2612`
- `esp8266.c:2651-2696`
- `esp8266.c:2710-2725`
- `esp8266.c:3327-3357`

### 6.7 代码维护成本已经偏高

`esp8266.c` 当前已经成为一个巨型文件，职责横跨：

- Wi-Fi
- TCP
- AT 交互
- HTTP 拼包
- 接收解析
- UI 命令
- SD 参数
- 自动重连
- FFT 数据准备

这会导致：

- 修改一处容易影响多处
- 很难单元验证
- 很难引入严格的回归保护

### 6.8 安全与兼容性风险也已经浮出水面

除了性能和稳定性问题，当前实现还有 3 个不能忽略的工程风险：

1. **编译期默认配置里仍存在明文敏感信息**
   - `esp8266_config.h:12-26` 里仍保留 Wi-Fi、服务器、节点默认值，其中包含明文 Wi-Fi 密码。
   - 这说明当前仓库仍把“本应部署期注入”的信息放进了固件源码。

2. **服务端一旦开启设备 API Key 校验，当前固件会直接失配**
   - 服务端支持 `EDGEWIND_DEVICE_API_KEY`，并要求设备请求头带 `X-EdgeWind-ApiKey`。
   - 但当前固件自己手写 HTTP 头，现有 `/api/register` 和 `/api/node/heartbeat` 报文里并没有这个头。
   - 也就是说，今天后端之所以能正常收，是因为目前仍允许无密钥接入。

3. **服务端通道映射对中文 `label` 仍有依赖**
   - 服务端解析通道时优先看 `label/name`，按 `id/channel_id` 的兜底映射并不完整。
   - 一旦设备端标签文字、编码或 UI 命名发生变化，服务端可能把通道意义映射错位。

关键位置：

- `MDK-ARM/HARDWORK/ESP8266/esp8266_config.h:12-26`
- `Edge_Wind_System/edgewind/routes/api.py:379-399`
- `Edge_Wind_System/edgewind/routes/api.py:804-841`
- `MDK-ARM/HARDWORK/ESP8266/esp8266.c:1738-1757`
- `MDK-ARM/HARDWORK/ESP8266/esp8266.c:1949-1977`

---

## 7. 当前已具备的“全部功能”清单

从实现层面看，当前 ESP8266 子系统已经具备以下完整能力：

1. 编译期默认网络与节点配置加载
2. SD 卡覆盖式 Wi-Fi/Server/Param 配置加载
3. 断电重连状态持久化
4. UI 驱动的分步联网
5. 一键自动连接
6. Wi-Fi 连接
7. TCP 连接
8. 透明传输进入/退出
9. 注册到服务端
10. 摘要模式上传
11. 全量模式上传
12. 心跳上传
13. 运行时降采样与上传点数调整
14. 服务端回包解析
15. `report_mode` 下发
16. `downsample_step` 下发
17. `upload_points` 下发
18. `reset` 下发
19. 参数应用并回写 SD
20. 透明连接复用
21. 软重连
22. 硬复位
23. UART 错误统计与恢复
24. 调试控制台与故障码注入
25. 采样波形整理
26. 4 路 FFT 计算

换句话说，当前这部分代码已经远远超出了“ESP8266 驱动”的范畴，更准确的名称应该是：

**通信与上传子系统。**

---

## 8. 把 ESP8266 换成 ESP32-WROOM-32E/UE 是否可行

### 8.1 结论：可行，但不是直接焊上就完

从功能上看，迁移是可行的。

从工程上看，不能把它当作“换个更强的串口 Wi-Fi 模块”这么简单，因为当前系统已经深度依赖：

- `AT+CIPSTART`
- `AT+CIPMODE=1`
- `AT+CIPSEND`
- `+++` 退出透明模式
- 单连接透明传输语义

关键位置：

- `esp8266.c:923-1100`
- `esp8266.c:2507-2711`
- `esp8266.c:3435-3465`

根据 Espressif 官方资料：

- `ESP32-WROOM-32E/32UE` 基于 ESP32，双核 LX6，最高 `240 MHz`
- 片上 `520 KB SRAM`、`448 KB ROM`
- 模组提供 `4/8/16 MB flash`
- 支持 `26 GPIO`
- `32E` 为板载 PCB 天线
- `32UE` 为外接天线连接器

官方资料：

- <https://documentation.espressif.com/esp32-wroom-32e_esp32-wroom-32ue_datasheet_en.pdf>

### 8.2 `32E` 和 `32UE` 的实际差异

| 型号 | 天线 | 典型使用场景 | 备注 |
| --- | --- | --- | --- |
| `ESP32-WROOM-32E` | 板载 PCB 天线 | 结构空间允许、塑料外壳、常规场景 | 板级简单 |
| `ESP32-WROOM-32UE` | 外接天线连接器 | 金属外壳、复杂安装姿态、需要把天线引到外部 | 射频布局和天线线缆要求更高 |

关键点：

- `32UE` 不是“性能更强版”，核心差异主要是 **天线形式**
- `32UE` 用的是兼容 `U.FL / I-PEX / MHF I / AMC` 的外接天线连接器

官方资料：

- <https://documentation.espressif.com/esp32-wroom-32e_esp32-wroom-32ue_datasheet_en.pdf>

### 8.3 板级硬件难点

### 8.3.1 不是引脚直替

当前板级只明显暴露了：

- `TX`
- `RX`
- `RST`

但 ESP32 通常至少还要认真处理：

- `CHIP_PU / EN`
- strapping pins
- 下载模式相关引脚
- 更严格的上电时序

关键位置：

- `Core/Inc/main.h:61-70`
- `Core/Src/gpio.c` 中 ESP 复位脚配置

Espressif 官方硬件设计建议明确指出：

- 推荐 `3.3 V`
- 电源输出能力不低于 `500 mA`
- `CHIP_PU` 不能悬空
- 应注意 strapping pins、上电时序和电源去耦

官方资料：

- <https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32/schematic-checklist.html>

### 8.3.2 电源要重新审视

ESP32 的峰值电流和射频瞬态管理要比“随便带个 8266 模块”更严格。

这不是说 ESP32 一定更难供电，而是说：

- 如果原板的 3.3 V 裕量、去耦、电源入口保护做得不充分
- 换成 ESP32 后更容易暴露问题

特别是未来若启用：

- 更高发射占空比
- 蓝牙
- HTTPS / SSL
- 更重的网络任务

电源稳定性要求会更高。

### 8.3.3 `32E` 与 `32UE` 尺寸不同

官方资料显示：

- `32E` 典型尺寸：`18.0 x 25.5 x 3.1 mm`
- `32UE` 典型尺寸：`18.0 x 19.2 x 3.2 mm`

所以即便都叫 `WROOM-32`，封装/天线区域也不能想当然当作完全同一占位。

---

## 9. 两种迁移路线的真实差别

### 9.1 路线 A：ESP32 跑 ESP-AT，尽量平替现有 AT 架构

### 可行性

高。

原因：

- ESP-AT 仍然支持 `TCP/IP AT Commands`
- 支持 `AT+CIPSTART`
- 支持 `AT+CIPMODE`
- 支持 `AT+CIPSEND`
- 支持透明/透传相关工作模式

官方资料：

- <https://docs.espressif.com/projects/esp-at/en/latest/esp32/AT_Command_Set/TCP-IP_AT_Commands.html>

### 收益

- 模组供应和生命周期更好
- Wi-Fi/BLE 平台更现代
- 射频能力更强
- 后续想切到原生固件时，硬件基础已经换到 ESP32

### 限制

如果 STM32 端仍然保留当前做法：

- 自己做 FFT
- 自己拼 JSON
- 自己拼 HTTP
- 自己解析回包字符串
- 仍然使用单 UART 的 AT/透传双模式切换

那么大部分系统瓶颈都还在：

- UART 带宽还是瓶颈
- 超长 JSON 还是瓶颈
- HTTP 边界问题还是可能存在
- 维护复杂度还是高

### 结论

这条路线适合做 **低风险验证**，不适合当成最终架构。

### 9.2 路线 B：ESP32 跑原生固件，做真正的通信协处理器

### 可行性

高，而且长期价值明显更高。

建议职责分工变成：

- STM32：采样、FFT、本地 UI、参数总控
- ESP32：Wi-Fi、HTTP/HTTPS、命令解析、重连、缓存、重传、可选 OTA

### 收益

能直接删除或显著简化以下脆弱逻辑：

- `AT + 透传 + +++ 退出` 状态机
- `UART 阻塞收发 + DMA 流式收发` 混用
- 从 TCP 字节流里找 `report_mode/downsample_step/upload_points`
- 大量 HTTP 文本边界判断
- 复杂的链式 DMA 和分块发送门控

### 更重要的长期收益

#### 1. 可以把 STM32<->ESP32 协议改成结构化二进制帧

例如：

- 帧头
- 长度
- 类型
- CRC
- payload

这样可以直接替代现在的：

- 超长 JSON over UART
- HTTP over UART
- 关键字扫描式 ACK

#### 2. UART 数据量可以显著下降

只要不再把完整 HTTP 文本和 ASCII 数字大数组都压过串口，纯通信体积就会明显下降。

保守判断：

- 仅改成二进制帧，不做压缩，也有机会把 UART 负担降到当前的 `1/2 ~ 1/3`
- 如果再配合降采样、差分编码、压缩、摘要优先策略，收益还会更大

#### 3. 更容易支持安全与扩展能力

例如：

- HTTPS / TLS
- API Key 认证
- MQTT
- WebSocket
- 断线缓存
- 断点重发
- 更清晰的 ACK / NACK 协议

值得注意的是，`ESP-AT` 本身也支持 HTTP、MQTT、WebSocket 等命令族，但如果仍走 AT，复杂度依然会被串口命令流放大。原生固件的结构会更干净。

官方资料：

- <https://docs.espressif.com/projects/esp-at/en/latest/esp32/AT_Command_Set/index.html>

### 结论

如果你要的是：

- 更稳
- 更易维护
- 更好扩展
- 为后续云端、安全、OTA 留接口

那就不应该只做 “ESP8266 -> ESP32 的 AT 平替”，而应把目标定为：

**“ESP32 通信栈重构”。**

---

## 10. 未来迁移时的难点

### 10.1 如果只做 AT 平替，难点在兼容性

需要逐项验证：

1. `AT+CIPSTART`
2. `AT+CIPMODE=1`
3. `AT+CIPSEND`
4. `+++` 退出数据模式
5. `CIPCLOSE`
6. 单连接透明模式下回包文本
7. 当前 `2 Mbps UART` 是否稳定
8. 现有 `ReceiveToIdle + DMA Circular` 是否还能保持低误码

特别要注意：

- ESP32 ESP-AT 文档中，透传模式有自己的缓冲、发包节奏和等待时间要求
- 官方文档明确提到透传模式下每次串口发送有内部节流与块大小
- 单连接透传和多连接模式也有约束

官方资料：

- <https://docs.espressif.com/projects/esp-at/en/latest/esp32/AT_Command_Set/TCP-IP_AT_Commands.html>

### 10.2 如果做原生固件，难点在系统重分工

需要重新定义：

1. **谁是配置主**
   - STM32 UI 还是 ESP32 NVS
2. **芯片间协议**
   - 二进制帧还是精简 JSON
3. **ACK 语义**
   - 参数 ACK、命令 ACK、上传 ACK 是否分开
4. **缓存策略**
   - ESP32 是否缓存未发成功的数据
5. **上报粒度**
   - full/summary 模式由谁裁剪
6. **错误恢复边界**
   - STM32 负责何时重置 ESP32
   - ESP32 负责何时自行重连

### 10.3 不建议把 FFT 主计算迁到 ESP32

这是一个很重要的判断。

原因：

- 采样数据本来就在 STM32 侧双缓冲里
- 现在使用的是 `arm_rfft_fast_f32`
- 如果把 FFT 搬到 ESP32，先要把大块原始波形搬过串口
- 这会把芯片间带宽压力拉高

所以更合理的分工是：

- **STM32 继续做采样和 FFT**
- **ESP32 接管联网与协议**

---

## 11. 迁移后能带来哪些收益和性能效果

### 11.1 仅做 AT 平替时

### 预期收益

- 模组平台更现代
- 供应风险更低
- 射频与外围能力更好
- 为后续原生固件升级打基础

### 预期局限

- 全量上传时延不会有本质级改善
- 串口仍然要承载大文本
- 透明模式脆弱性大概率仍存在
- 代码复杂度仍主要留在 STM32 端

### 我的判断

这条路线的收益更偏：

- 工程可持续性
- 平台升级

而不是：

- 立刻大幅提速

### 11.2 做原生固件重构时

### 稳定性收益

- 不再依赖 `+++` 退出透传
- 不再把 HTTP 边界押在 UART 字节流关键字上
- 不再混用阻塞式 AT 收发与 DMA 流式收发

### 性能收益

- 串口只传结构化数据，不再传完整 HTTP 文本
- 二进制化后，串口数据量有机会降到当前的 `1/2 ~ 1/3`
- 重发范围可从“整包重发”优化成“帧级重发或块级重发”
- ESP32 可以本地处理回包和参数 ACK，减少 STM32 的等待和解析开销

### 系统收益

- 更容易接入 HTTPS/TLS
- 更容易加 API Key 或证书
- 更容易做离线缓存和网络自恢复
- 更容易演进成 MQTT/WebSocket 等更适合实时设备的协议

---

## 12. 我的推荐实施路线

### 12.1 阶段 0：先把现有问题显式化

在正式换模组前，建议先做两件事：

1. 给当前固件补一份链路观测指标
   - full/summary 上传大小
   - 平均发送耗时
   - 重连次数
   - UART 错误计数
2. 修一下当前最危险的 HTTP 边界门控
   - 不要把“收到任意新字节”直接等同于“上一个请求已完成”

这样做的价值是：

- 后面迁移到 ESP32 时，能量化收益，而不是只靠感觉

### 12.2 阶段 1：ESP32-AT 原型验证

目标：

- 最小代价确认 `ESP32-WROOM-32E/UE` 是否能兼容现有架构

建议验收项：

1. 板级供电稳定
2. `2 Mbps UART` 稳定
3. `CIPSTART/CIPMODE/CIPSEND/+++` 行为可接受
4. `/api/register` 正常
5. `/api/node/heartbeat` summary 正常
6. `/api/node/heartbeat` full 正常
7. `report_mode/downsample_step/upload_points/reset` 可完整往返
8. 重连逻辑可用

### 12.3 阶段 2：切到 ESP32 原生通信固件

建议目标：

- 保持服务端接口先不变：
  - `/api/register`
  - `/api/node/heartbeat`
- 只重构 STM32 和 ESP32 之间的内部通信

这样好处最大：

- Web 后端不需要大改
- 迁移风险被隔离在设备侧
- 可以逐步替换，不必一次性翻掉整套云端逻辑

### 12.4 阶段 3：再考虑协议演进

等设备侧稳定后，再评估是否要进一步演进到：

- HTTPS
- MQTT
- WebSocket
- 二进制上报
- 云侧流式处理

---

## 13. 最终建议

如果你的目标只是：

- 把 8266 换成一个更好买、更新的模块

那么：

- 直接做 `ESP32 + ESP-AT` 平替就够了。

如果你的目标是：

- 真正解决当前坏帧、重连慢、维护成本高、后续安全扩展困难的问题

那么：

- 应该把这次升级定义成 **“从 ESP8266 AT 透传架构迁移到 ESP32 通信协处理架构”**。

我给出的最终判断是：

- **可行：是**
- **值不值得：值得**
- **主要难点：不在换模组本身，而在是否继续保留 AT 透传旧架构**
- **最大收益来源：不是 ESP32 更强的 CPU，而是可以借这次机会清掉当前通信架构最脆的部分**

---

## 14. 关键本地证据文件

- `STM32H750XBH6_ESP8266_FreeRTOS_LVGL9.4.0/MDK-ARM/HARDWORK/ESP8266/esp8266.c`
- `STM32H750XBH6_ESP8266_FreeRTOS_LVGL9.4.0/MDK-ARM/HARDWORK/ESP8266/esp8266.h`
- `STM32H750XBH6_ESP8266_FreeRTOS_LVGL9.4.0/MDK-ARM/HARDWORK/ESP8266/esp8266_config.h`
- `STM32H750XBH6_ESP8266_FreeRTOS_LVGL9.4.0/Core/Src/freertos.c`
- `STM32H750XBH6_ESP8266_FreeRTOS_LVGL9.4.0/Core/Src/usart.c`
- `STM32H750XBH6_ESP8266_FreeRTOS_LVGL9.4.0/Core/Src/main.c`
- `STM32H750XBH6_ESP8266_FreeRTOS_LVGL9.4.0/Core/Src/ad_acq_buffers.c`
- `STM32H750XBH6_ESP8266_FreeRTOS_LVGL9.4.0/MDK-ARM/HARDWORK/GUI-Guider_Runtime/src/generated/events_init.c`
- `STM32H750XBH6_ESP8266_FreeRTOS_LVGL9.4.0/MDK-ARM/HARDWORK/GUI-Guider_Runtime/src/generated/gui_guider.h`
- `Edge_Wind_System/edgewind/routes/api.py`
- `Edge_Wind_System/logs/server_eventlet_stderr.log`

## 15. 官方参考资料

- Espressif, `ESP32-WROOM-32E & ESP32-WROOM-32UE Datasheet`
  - <https://documentation.espressif.com/esp32-wroom-32e_esp32-wroom-32ue_datasheet_en.pdf>
- Espressif, `ESP-AT User Guide`
  - <https://docs.espressif.com/projects/esp-at/en/latest/esp32/AT_Command_Set/index.html>
- Espressif, `ESP-AT TCP/IP AT Commands`
  - <https://docs.espressif.com/projects/esp-at/en/latest/esp32/AT_Command_Set/TCP-IP_AT_Commands.html>
- Espressif, `ESP32 Hardware Design Guidelines - Schematic Checklist`
  - <https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32/schematic-checklist.html>
