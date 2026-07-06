# RK3588S AI监测端使用说明（私有）

> 本文包含 SSH、热点和 Web 管理凭据，只允许在当前本机、当前项目和可信 AI 监测端交接范围内使用，不要对外公开、不要提交到公开仓库。

更新时间：2026-07-05  
适用设备：QuarkPi-CA2 / RK3588S / 8GB RAM + 64GB eMMC  
当前定位：现场边缘网关与运维节点

## 1. 当前角色说明

这台 RK3588S 不作为新的 EdgeWind 代码主端维护，也不替代 AI 监测端、STM32 或 ESP32 的实时判别链路。它在项目里承担的是现场网关和运维节点角色：

- 提供稳定的 SSH 入口，方便 AI 监测端或运维工具远程检查服务。
- 提供 VNC 入口，方便现场查看板端桌面和 Web 页面。
- 通过 RTL8852CE 无线网卡提供 2.4GHz Wi-Fi AP，让 ESP32 等设备直接接入现场网络。
- 承载 EdgeWind Web/API 服务，作为 ESP32 数据上报、现场页面展示和后续本地缓存的入口。
- 保留局域网有线 IP，方便 Windows 主机、开发机或 AI 监测端从路由器侧访问。

实时性边界需要保持清楚：

- STM32/ESP32/AI 监测端已有的采集、AI 判别和设备侧协议不要迁移到 RK3588S。
- RK3588S 主要负责网络、Web 服务、运维、展示、缓存和现场接入。
- 后续如果新增接口，应以 HTTP/API 协议对接，不应依赖 Windows 本地旧目录路径。

## 2. 快速连接信息

### 2.1 SSH

```powershell
ssh quark@192.168.0.124
```

- 主机名：`quarkpi-ca2`
- 用户名：`quark`
- 密码：`RK3588S`
- 有线局域网 IP：`192.168.0.124`
- SSH 端口：`22`

Windows 侧快速检测：

```powershell
Test-NetConnection 192.168.0.124 -Port 22
```

Linux/macOS/板端同网段检测：

```bash
ping -c 4 192.168.0.124
nc -vz 192.168.0.124 22
```

### 2.2 VNC

- VNC 地址：`192.168.0.124:5901`
- 推荐客户端：TigerVNC Viewer
- 当前板端服务：`rk3588-vnc.service`
- 当前 VNC 后端：`x11vnc`
- VNC 密码：已单独设置，当前说明不记录明文。

Windows 侧快速检测：

```powershell
Test-NetConnection 192.168.0.124 -Port 5901
```

### 2.3 EdgeWind Web/API

- 局域网访问：`http://192.168.0.124:5000/`
- 热点侧访问：`http://192.168.66.1:5000/`
- 板端本机访问：`http://127.0.0.1:5000/overview`
- 管理员账号：`Edge_Wind`
- 管理员密码：`Gentle9532`
- 当前模式：现场软件模式，已启用免登录，正常使用时不会显示登录页。

Windows 侧快速检测：

```powershell
Test-NetConnection 192.168.0.124 -Port 5000
Invoke-WebRequest http://192.168.0.124:5000/overview -UseBasicParsing
```

如果设备连接的是 RK3588S 热点，则访问：

```text
http://192.168.66.1:5000/
```

## 3. 当前系统基线

当前稳定基线以 eMMC 上的 Ubuntu 系统为准：

- 系统：`Ubuntu 24.04.4 LTS`
- 启动介质：纯 eMMC 启动，不依赖 SD 卡或 U 盘。
- `/boot`：`/dev/mmcblk0p2`，Label `boot`
- `/`：`/dev/mmcblk0p3`，Label `ubuntu-emmc-root`
- 桌面：`lightdm + XFCE + Xorg :0`
- 图形策略：稳定优先，当前使用 `llvmpipe`
- 有线接口：`end1`
- 固定有线 IP：`192.168.0.124/24`
- 默认网关：`192.168.0.1`
- 网络管理：`systemd-networkd`

关键服务：

| 功能 | 服务名 | 作用 |
| --- | --- | --- |
| SSH | `ssh` | 远程终端入口 |
| 桌面登录 | `lightdm` | 板端 XFCE 桌面 |
| VNC | `rk3588-vnc.service` | 5901 端口，同屏远程桌面 |
| Wi-Fi AP | `rk3588-hotspot.service` | EdgeWind 现场热点 |
| Web/API | `edgewind-gateway.service` | EdgeWind Web/API |
| 有线网络兜底 | `codex-eth0-up.service`、`codex-static-eth0.service` | 固定有线网口和 IP |

不建议轻易切换 Wayland、gdm3、其它 VNC 方案或 Mali/Rockchip 专有图形用户态。当前目标是稳定运维，不是图形性能验证。

## 4. 无线网卡与热点

### 4.1 硬件与热点信息

- 无线网卡：Realtek RTL8852CE PCIe 外置天线网卡
- 稳定接口名：`edgewind0`
- 热点 SSID：`EdgeWind-Gateway`
- 热点密码：`RK3588S1234`
- 热点频段：`2.4GHz`
- 信道：`6`
- 频点：`2437 MHz`
- 带宽：`20 MHz`
- 热点网关 IP：`192.168.66.1/24`
- DHCP 范围：`192.168.66.50` 到 `192.168.66.150`
- 实现方式：`hostapd + dnsmasq`
- 出口 NAT：通过有线接口 `end1`

ESP32 通常只支持 2.4GHz Wi-Fi，因此当前热点已经按 ESP32 兼容方向固定为 2.4GHz，不要改成 5GHz。

### 4.2 ESP32 连接参数

ESP32 固件或配置里应使用：

```text
WIFI_SSID=EdgeWind-Gateway
WIFI_PASSWORD=RK3588S1234
SERVER_HOST=192.168.66.1
SERVER_PORT=5000
```

如果 ESP32 是通过外部路由器接入，而不是连接 RK3588S 热点，则服务器地址可以使用：

```text
SERVER_HOST=192.168.0.124
SERVER_PORT=5000
```

现场优先推荐 ESP32 直接连接 RK3588S 热点，这样不依赖外部路由器。

### 4.3 板端热点检查命令

SSH 登录 RK3588S 后执行：

```bash
systemctl status rk3588-hotspot.service --no-pager
sudo /usr/local/sbin/rk3588-hotspot status
iw dev
ip -4 addr show edgewind0
```

查看热点相关日志：

```bash
journalctl -u rk3588-hotspot.service -n 120 --no-pager
journalctl -u hostapd -n 120 --no-pager
journalctl -u dnsmasq -n 120 --no-pager
```

查看热点侧端口是否可用：

```bash
ss -lntp | grep -E ':(22|5000|5901)\b'
curl -I http://192.168.66.1:5000/overview
```

查看已接入设备时，优先检查 dnsmasq 租约文件：

```bash
sudo cat /var/lib/misc/dnsmasq.leases
```

如果看到 `AP-STA-POSSIBLE-PSK-MISMATCH`，通常是 ESP32 写错了热点密码，先核对 `RK3588S1234` 是否完全一致。

## 5. EdgeWind Web/API 当前状态

### 5.1 服务位置

- systemd 服务：`edgewind-gateway.service`
- 当前运行入口：`/opt/edgewind/current`
- 当前发布目录：`/opt/edgewind/releases/20260705-1906-svg-toast-icons/Edge_Wind_System`
- 运行配置：`/opt/edgewind/edgewind.env`
- Gunicorn 配置：`/opt/edgewind/gunicorn_gateway.py`
- 当前运行方式：`gunicorn + gevent`
- 适配原因：Ubuntu 24.04 / Python 3.12 下不再使用旧 eventlet 方案。
- 当前代码来源：`C:\Users\pengjianzhong\Desktop\MY_Project\EdgeWind_STM32_ESP32\Edge_Wind_System` 的当前 Web 工作树。
- `C:\Users\pengjianzhong\Desktop\MY_Project\RK3588_Manager\EdgeWind_Gateway` 不再作为本项目 Web 代码来源，只作为历史差异参考。
- 当前 Web 已补齐 `/api/node/faults`、`/fault-monitor`、`FaultEvent` 和服务器时间同步字段。
- 当前 Web 通知弹窗使用内联 SVG 彩色图标，不依赖 emoji 字形或 Bootstrap Icons 字体，避免 RK3588S 本机浏览器字体缺失时出现方框。

检查服务：

```bash
systemctl status edgewind-gateway.service --no-pager
journalctl -u edgewind-gateway.service -n 120 --no-pager
```

重启服务：

```bash
sudo systemctl restart edgewind-gateway.service
```

确认页面：

```bash
curl -I http://127.0.0.1:5000/overview
curl -I http://192.168.66.1:5000/overview
curl -I http://192.168.0.124:5000/overview
```

### 5.2 当前免登录模式

当前现场软件模式已启用：

```text
EDGEWIND_DISABLE_LOGIN=true
```

表现：

- `/overview` 可直接打开。
- `/login` 正常应跳转到 `/overview`。
- 板端开机后会自动拉起浏览器打开 `http://127.0.0.1:5000/overview`。

相关文件：

```text
/home/quark/bin/edgewind-open-page
/home/quark/.config/autostart/edgewind-web.desktop
/home/quark/.local/state/edgewind-open-page.log
```

检查自动打开页面：

```bash
ls -l /home/quark/.config/autostart/edgewind-web.desktop
pgrep -u quark -af 'epiphany|chromium|firefox|falkon'
tail -n 80 /home/quark/.local/state/edgewind-open-page.log
```

### 5.3 AI监测端应注意的 API 边界

当前已上线并验证过的接口/页面包括：

```text
/overview
/monitor
/api/node/heartbeat
/api/node/full_frame_bin
/api/node/faults
/api/fault_monitor/summary
/api/fault_monitor/events
/api/fault_events/<id>
/api/faults
/faults
/fault-monitor
```

`/api/node/faults` 固定返回服务器时间同步字段，即使 `not_modified=true` 也会返回：

```text
server_time_unix
server_time_local
server_time_utc
server_tz_offset_minutes
```

`FaultEvent` 已作为 Web 数据模型上线，表示故障事实事件；`WorkOrder` 仍表示运维工单流程。ESP32 可低频请求 `/api/node/faults?node_id=<id>&since_rev=<rev>&limit=10&compact=1` 获取本节点故障短摘要和服务器时间。该接口不返回波形、FFT、知识图谱或 DeepSeek 长文本，不进入 `/api/node/full_frame_bin` 上传热路径。

2026-07-05 当前验证结果：

```text
/opt/edgewind/current -> /opt/edgewind/releases/20260705-1906-svg-toast-icons/Edge_Wind_System
edgewind-gateway.service = active
http://127.0.0.1:5000/overview = 200
http://127.0.0.1:5000/monitor = 200
http://127.0.0.1:5000/fault-monitor = 200
http://127.0.0.1:5000/api/node/faults?node_id=TEST&since_rev=0&limit=10&compact=1 = 200
```

日志中已观察到 ESP32 热点侧正常访问：

```text
GET /api/node/faults?... HTTP/1.1 200
POST /api/node/full_frame_bin HTTP/1.1 200
```

## 6. AI监测端接入建议

### 6.1 推荐网络拓扑

现场无路由器或希望独立运行时：

```text
ESP32/现场设备 -> EdgeWind-Gateway 热点 -> RK3588S 192.168.66.1:5000
Windows/AI监测端 -> 有线/同路由器 -> RK3588S 192.168.0.124:5000
```

只有热点网络时：

```text
ESP32/AI监测端 -> EdgeWind-Gateway 热点 -> RK3588S 192.168.66.1:5000
```

有线局域网优先用于维护，热点优先用于现场设备接入。

### 6.2 AI监测端需要保存的配置

AI 监测端后续应至少保存这些配置项：

```text
RK3588_LAN_HOST=192.168.0.124
RK3588_AP_HOST=192.168.66.1
RK3588_WEB_PORT=5000
RK3588_SSH_USER=quark
RK3588_SSH_PASSWORD=RK3588S
RK3588_WIFI_SSID=EdgeWind-Gateway
RK3588_WIFI_PASSWORD=RK3588S1234
```

如果 AI 监测端要自动探测 RK3588S，建议按这个顺序：

1. 优先测试 `192.168.0.124:22` 和 `192.168.0.124:5000`。
2. 如果设备连接了 RK3588S 热点，再测试 `192.168.66.1:5000`。
3. 不要用旧 Windows 路径判断 Web 是否存在。
4. 不要依赖 DHCP 随机地址，当前稳定入口就是 `192.168.0.124` 和 `192.168.66.1`。

### 6.3 上报方向

ESP32 连接 RK3588S 热点时，上报目标应是：

```text
http://192.168.66.1:5000/
```

ESP32 连接外部路由器时，上报目标应是：

```text
http://192.168.0.124:5000/
```

AI 监测端如果只需要读取 Web 状态，优先访问：

```text
http://192.168.0.124:5000/overview
```

## 7. 常用运维命令

### 7.1 一次性健康检查

SSH 登录后执行：

```bash
hostname
date
ip -4 addr
ip route
findmnt -no SOURCE,TARGET / /boot
systemctl is-active ssh lightdm rk3588-vnc.service rk3588-hotspot.service edgewind-gateway.service
ss -lntp | grep -E ':(22|5000|5901)\b'
```

期望：

- `ssh` 为 `active`
- `lightdm` 为 `active`
- `rk3588-vnc.service` 为 `active`
- `rk3588-hotspot.service` 为 `active`
- `edgewind-gateway.service` 为 `active`
- 端口 `22`、`5000`、`5901` 正在监听

### 7.2 查看服务日志

```bash
journalctl -u ssh -n 80 --no-pager
journalctl -u rk3588-vnc.service -n 120 --no-pager
journalctl -u rk3588-hotspot.service -n 160 --no-pager
journalctl -u edgewind-gateway.service -n 160 --no-pager
```

实时跟踪 Web/API 日志：

```bash
journalctl -u edgewind-gateway.service -f
```

实时跟踪热点日志：

```bash
journalctl -u rk3588-hotspot.service -f
```

### 7.3 重启关键服务

```bash
sudo systemctl restart ssh
sudo systemctl restart rk3588-vnc.service
sudo systemctl restart rk3588-hotspot.service
sudo systemctl restart edgewind-gateway.service
```

如果只改了 Web 配置，通常只需要：

```bash
sudo systemctl restart edgewind-gateway.service
```

如果只处理 ESP32 连不上热点，通常只需要：

```bash
sudo systemctl restart rk3588-hotspot.service
```

### 7.4 检查无线网卡

```bash
lspci -nn | grep -i -E 'realtek|8852|network|wireless'
ip link show edgewind0
iw dev
rfkill list
```

如果 `edgewind0` 不存在，说明系统没有正确识别或没有完成稳定命名，需要先检查 RTL8852CE 是否插好、驱动是否加载、系统日志是否报错。

相关日志：

```bash
dmesg | grep -i -E 'rtw|rtl|8852|wifi|wlan|pcie'
journalctl -b | grep -i -E 'rtw|rtl|8852|hostapd|edgewind0'
```

## 8. 故障排查

### 8.1 SSH 连不上

Windows 侧先测端口：

```powershell
Test-NetConnection 192.168.0.124 -Port 22
```

如果失败：

1. 确认电脑和 RK3588S 在同一个有线局域网。
2. 确认网线、路由器、交换机正常。
3. 如果屏幕能进入桌面，在板端终端执行：

```bash
ip -4 addr show end1
systemctl status ssh --no-pager
systemctl status systemd-networkd --no-pager
```

4. 如果 IP 不是 `192.168.0.124`，优先检查 `/etc/systemd/network/20-eth0.network` 和 `codex-static-eth0.service`。

### 8.2 RK3588S IP 看似变化

当前设计上有两个固定入口：

- 有线侧：`192.168.0.124`
- 热点侧：`192.168.66.1`

如果你看到其它地址，通常是额外 DHCP 地址或其它接口地址，不应把它当成主入口。以 `end1` 和 `edgewind0` 为准：

```bash
ip -4 addr show end1
ip -4 addr show edgewind0
ip route
```

### 8.3 热点不出现

板端检查：

```bash
systemctl status rk3588-hotspot.service --no-pager
sudo /usr/local/sbin/rk3588-hotspot status
ip link show edgewind0
rfkill list
```

常见原因：

- RTL8852CE 没插稳或启动时未识别。
- `edgewind0` 接口没有出现。
- `hostapd` 启动失败。
- Wi-Fi 被 rfkill 禁用。

处理顺序：

```bash
sudo rfkill unblock all
sudo systemctl restart rk3588-hotspot.service
journalctl -u rk3588-hotspot.service -n 160 --no-pager
```

### 8.4 ESP32 连不上热点

先核对这四项：

```text
SSID: EdgeWind-Gateway
Password: RK3588S1234
Band: 2.4GHz
Server: 192.168.66.1:5000
```

如果日志出现：

```text
AP-STA-POSSIBLE-PSK-MISMATCH
```

优先判断为密码错误或 ESP32 固件内旧密码未更新。

如果 ESP32 能看到热点但连不上：

1. 确认热点不是 5GHz。
2. 确认密码没有多余空格、换行或大小写错误。
3. 重启 `rk3588-hotspot.service`。
4. 重启 ESP32。
5. 查看 `dnsmasq.leases` 是否分配到 `192.168.66.x` 地址。

### 8.5 ESP32 连上但不上报

检查 RK3588S Web 服务：

```bash
systemctl status edgewind-gateway.service --no-pager
curl -I http://192.168.66.1:5000/overview
ss -lntp | grep ':5000'
```

检查 ESP32 配置：

```text
SERVER_HOST=192.168.66.1
SERVER_PORT=5000
```

不要让 ESP32 在热点网络里上报 `192.168.0.124`，因为 `192.168.0.124` 是有线 LAN 地址，不一定在 ESP32 当前网络可达。

### 8.6 Web 页面打不开

局域网侧：

```powershell
Test-NetConnection 192.168.0.124 -Port 5000
```

板端：

```bash
systemctl status edgewind-gateway.service --no-pager
journalctl -u edgewind-gateway.service -n 160 --no-pager
curl -I http://127.0.0.1:5000/overview
```

如果 `/login` 出现并要求登录，检查是否仍是现场软件模式：

```bash
grep -n 'EDGEWIND_DISABLE_LOGIN' /opt/edgewind/edgewind.env
```

期望：

```text
EDGEWIND_DISABLE_LOGIN=true
```

### 8.7 VNC 黑屏或分辨率异常

当前 VNC 依赖 `lightdm + XFCE + Xorg :0 + x11vnc`。不要先改 VNC 架构，按服务顺序检查：

```bash
systemctl status lightdm --no-pager
systemctl status rk3588-vnc.service --no-pager
pgrep -af 'Xorg|x11vnc|xfce'
journalctl -u lightdm -n 120 --no-pager
journalctl -u rk3588-vnc.service -n 120 --no-pager
```

如果只是 VNC 客户端显示比例不对，优先调整 TigerVNC Viewer 的显示缩放或 Fit Window，不要让客户端随便修改 RK3588S 桌面分辨率。

### 8.8 插入 RTL8852CE 后无法启动或屏幕无信号

处理原则：

1. 先拔掉 RTL8852CE，确认 RK3588S 能正常启动。
2. 能 SSH 后再插回网卡，观察内核和服务日志。
3. 不要在黑屏状态下盲目改桌面、VNC 或显示管理器。

插回网卡后检查：

```bash
lspci -nn | grep -i -E 'realtek|8852|network|wireless'
dmesg | tail -n 160
systemctl status rk3588-hotspot.service --no-pager
```

如果插卡导致启动异常，应优先判断为 PCIe/驱动/供电/初始化时序问题，而不是直接重装系统。

## 9. 不要随便改的内容

后续 AI 或维护人员不要轻易修改以下内容：

- 不要随意切换 `Wayland`、`gdm3`、其它 VNC 方案或 Mali/Rockchip 图形用户态。
- 不要删除 `rk3588-vnc.service`、`rk3588-hotspot.service`、`edgewind-gateway.service`。
- 不要随意改热点 SSID、密码、网段、DHCP 范围、2.4GHz 信道，除非同步修改 ESP32 配置。
- 不要把旧 SD/U 盘系统当成当前主系统依据。
- 不要把旧 Moonlight/Sunshine 低延迟方案当成当前远控方案。
- 不要把旧 Windows 本地目录当成板端运行目录。
- 不要在未确认 eMMC 启动状态前格式化板上存储设备。
- `/api/node/faults`、`/fault-monitor`、`FaultEvent` 和服务器时间字段已经上线；后续不要再按旧文档把它们当成“未实现”。
- 不要把 DeepSeek、知识图谱或全量工单查询加入 `/api/node/full_frame_bin`、heartbeat 或 SocketIO 实时图表热路径。

## 10. 给后续 AI监测端的最小执行清单

如果后续 AI 监测端第一次接手，只需要先完成这几步：

1. SSH 连通：

```powershell
ssh quark@192.168.0.124
```

2. 检查关键服务：

```bash
systemctl is-active ssh rk3588-vnc.service rk3588-hotspot.service edgewind-gateway.service
```

3. 确认热点：

```bash
sudo /usr/local/sbin/rk3588-hotspot status
ip -4 addr show edgewind0
```

4. 确认 Web：

```bash
curl -I http://127.0.0.1:5000/overview
curl -I http://192.168.66.1:5000/overview
```

5. 给 ESP32 使用：

```text
SSID=EdgeWind-Gateway
PASSWORD=RK3588S1234
SERVER=http://192.168.66.1:5000/
```

6. 给 PC/AI监测端使用：

```text
SSH=quark@192.168.0.124
WEB=http://192.168.0.124:5000/
VNC=192.168.0.124:5901
```

只要这六项正常，RK3588S 对 AI 监测端就是可用状态。
