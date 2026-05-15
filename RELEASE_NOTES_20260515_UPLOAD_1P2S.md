# EdgeWind 上传链路 1.2s 稳定版说明

版本目标：在不改变 full_frame_bin 协议、4096 点波形、2048 点频谱、AI 输入和量程换算的前提下，降低全量上传链路长尾并恢复约 1.2s 的稳定帧间隔。

## 关键改动

- STM32 侧增强 full 上传状态自恢复：
  - 自动清理 stale full wait 和 blocked full tx 状态。
  - WiFi 连接改为等待 ESP32 实际 `wifi_connected` 状态，而不是只依赖命令响应。
  - `REPORT_FULL_END` 被 ESP32 拒绝时不再进入无效 HTTP 等待，直接 holdoff 后恢复。
  - full status 日志补充 active/phase，便于定位卡在哪个阶段。

- ESP32 侧优化 full binary HTTP：
  - `/api/node/full_frame_bin` 继续使用 `49348` 字节 full binary payload。
  - full binary 上传新增 raw socket keep-alive 路径，减少重复 TCP 建连开销。
  - raw socket 复用上限为 24 次，I/O 超时为 1200ms，body 写阻塞超过 1200ms 主动中止并重连。
  - full binary 写入仍按 4096 字节分块，`chunk_delay=0`。
  - full 上传失败时避免空闲 heartbeat 抢占下一帧 full 重试。

- 监测工具增强：
  - 串口监测时关闭 DTR/RTS，避免打开 ESP32 串口触发复位。
  - 云端 journal tail 增加 SSH 保活和自动重连。
  - 云端日志使用 `short-unix` 时间戳，并输出 cloud series 间隔统计。

## 10 分钟闭环监测结果

监测文件：`test_logs/cloud_series_live_20260515_10min_summary.json`

- STM32 full 帧间隔：p50 `1193ms`，p95 `1896ms`，最大 `2859ms`。
- STM32 侧无 `>5s` 或 `>8s` 长帧间隔。
- ESP32 HTTP full binary 阶段：p50 `628ms`，p95 `711ms`。
- ESP32 HTTP 成功率：`99.78%`。
- payload 长度稳定为 `49348`。
- `wave=4096/4096`，`spec=2048/2048`。

说明：云端 `[series]` 日志本身有 2 秒节流，不能直接作为每帧上传间隔；上传链路帧间隔以 STM32 `REPORT_FULL_BEGIN` 间隔和 ESP32 `report stage=read` 统计为准。
