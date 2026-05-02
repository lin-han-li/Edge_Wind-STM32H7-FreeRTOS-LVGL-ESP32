"""
Gunicorn 配置文件
适用于 Flask-SocketIO 生产环境
"""
import os
import multiprocessing

# ==================== 服务器配置 ====================
# 绑定地址和端口
bind = f"{os.environ.get('HOST', '0.0.0.0')}:{os.environ.get('PORT', '5000')}"

# Worker 配置
# 使用 eventlet worker（必须，因为 Flask-SocketIO 需要异步支持）
worker_class = 'eventlet'

# 重要：
# 本项目当前把 active_nodes / node_report_modes / pending command cache
# 保存在进程内内存里，多 worker 会造成状态分裂：
# - /api/node/full_frame_bin 可能落到 worker A
# - /api/nodes/upload_points 可能落到 worker B
# 两边内存不共享，最终表现为 UI/命令状态错乱、偶发 409、命令延迟。
#
# 这里默认强制单 worker，优先保证实时链路和命令闭环一致性。
# 如果后续把运行态迁移到 Redis/DB，再考虑多 worker。
workers = 1

# 每个 worker 的线程数（eventlet worker 不使用此参数，但保留以兼容）
threads = 1

# Worker 连接数（eventlet 的并发连接数）
worker_connections = 1000

# ==================== 超时配置 ====================
timeout = 120  # Worker 超时时间（秒）
keepalive = 5  # Keep-alive 超时时间（秒）
graceful_timeout = 30  # 优雅关闭超时时间（秒）

# ==================== 日志配置 ====================
# 访问日志（None 表示禁用，因为 Flask 已有日志系统）
accesslog = None

# 错误日志
errorlog = '-'  # 输出到 stderr

# 日志级别
loglevel = os.environ.get('LOG_LEVEL', 'info').lower()

# ==================== 进程配置 ====================
# 进程名称（在进程列表中显示）
proc_name = 'edgewind'

# 用户和组（生产环境建议设置，Windows 下忽略）
# user = 'www-data'
# group = 'www-data'

# 工作目录
chdir = os.path.dirname(os.path.abspath(__file__))

# Python 路径（如果需要）
# pythonpath = '/path/to/your/project'

# ==================== 性能优化 ====================
# 预加载应用（加快启动速度，节省内存）
preload_app = False  # Flask-SocketIO 不建议预加载

# 最大请求数（防止内存泄漏，worker 处理指定数量请求后重启）
max_requests = 1000
max_requests_jitter = 50  # 添加随机性，避免所有 worker 同时重启

# ==================== 安全配置 ====================
# 限制请求行大小
limit_request_line = 4094
limit_request_fields = 100
limit_request_field_size = 8190

# ==================== SSL/TLS（如需 HTTPS，取消注释）====================
# keyfile = '/path/to/keyfile'
# certfile = '/path/to/certfile'
