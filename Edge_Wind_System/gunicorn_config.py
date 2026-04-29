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

# Worker 数量（建议：CPU核心数 * 2 + 1，但 Flask-SocketIO 通常使用较少 worker）
# 对于 WebSocket 应用，通常使用 2-4 个 worker 即可
workers = int(os.environ.get('GUNICORN_WORKERS', multiprocessing.cpu_count() * 2 + 1))
# 限制最大 worker 数量，避免资源过度消耗
if workers > 4:
    workers = 4

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

