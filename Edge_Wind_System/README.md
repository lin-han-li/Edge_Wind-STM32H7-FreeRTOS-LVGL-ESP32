# Edge_Wind_System（上位机 Web 后端 + Web 前端）

本目录是上位机系统（Flask + Web UI）的源码与运行入口。更完整的“全工程说明（含桌面端/固件端）”请看仓库根目录：

- `../README.md`

---

## 快速开始（开发/联调）

```powershell
cd Edge_Wind_System

# Windows 推荐 Python 3.11
py -3.11 -m venv venv311
.\venv311\Scripts\activate
pip install -r requirements.txt
python app.py

# 浏览器访问
# http://localhost:5000
```

你也可以直接双击：
- `服务器开关.bat`（彩色菜单：创建 venv / 安装依赖 / 启停服务）
- `模拟器开关.bat`（启动/停止 `sim.py`，用于无硬件演示）

---

## 目录结构（与你当前工程一致）

```text
Edge_Wind_System/
├─ app.py                 # Flask 入口（SocketIO/日志/DB/后台清理任务/默认管理员创建）
├─ wsgi.py                # 生产部署入口（Gunicorn + eventlet）
├─ gunicorn_config.py     # Gunicorn 配置（worker_class=eventlet）
├─ edgewind.env           # 推荐运行配置（也支持 EDGEWIND_ENV_FILE 指定）
├─ requirements.txt
├─ templates/             # overview/monitor/history/faults/settings/snapshots/login
├─ static/js/             # 前端 JS
├─ edgewind/              # 后端模块（routes/models/socket_events/工具）
└─ docs/
   ├─ 局域网访问指南.md
   └─ 阿里云部署实现详解.md   #（本次新增）
```

---

## 生产部署（简版）

> 强烈建议先阅读 `docs/阿里云部署实现详解.md`（含 Nginx/WebSocket/HTTPS/systemd/SQLite 运维细节）。

在 Linux 上可用：

```bash
pip install -r requirements.txt
gunicorn -c gunicorn_config.py wsgi:application
```

**注意（非常重要）**：
- 本项目存在 `active_nodes` 等 **内存态状态**，在不引入 Redis/消息队列共享状态的情况下，**Gunicorn 多 worker 会导致状态不一致**。
- 生产部署建议设置：`GUNICORN_WORKERS=1`（或你明确完成了“多进程共享状态”改造后再提升）。

---

## 数据库运维（SQLite）

删除历史数据后数据库文件大小不变是 SQLite 正常行为；你可以在系统设置页点击：

- `回收数据库空间（VACUUM）`（对应后端接口 `POST /api/admin/vacuum_db`）
