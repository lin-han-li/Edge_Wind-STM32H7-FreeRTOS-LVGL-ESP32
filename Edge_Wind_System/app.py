"""
风电场DC系统故障监测与智能诊断平台 - Flask后端主程序（重构版）

本文件是应用的入口点，负责：
1. 初始化Flask应用和各类扩展
2. 注册蓝图（路由模块化）
3. 配置日志和中间件
4. 启动应用

大部分业务逻辑已移至edgewind模块，保持此文件简洁。
"""
import os
import logging
import threading
import time
import secrets
import string
import importlib
from logging.handlers import RotatingFileHandler
from flask import Flask, request, jsonify, redirect, url_for
from flask_cors import CORS
from flask_socketio import SocketIO
from flask_login import LoginManager
from concurrent.futures import ThreadPoolExecutor

# Flask-WTF（可选）：用于 CSRFProtect
# 说明：如果你尚未安装依赖（flask-wtf），系统会自动跳过 CSRF 防护并给出提示，避免直接启动失败。
def _try_get_csrf_protect():
    try:
        mod = importlib.import_module("flask_wtf.csrf")
        return getattr(mod, "CSRFProtect", None)
    except Exception:
        return None

def _try_get_csrf_error():
    try:
        mod = importlib.import_module("flask_wtf.csrf")
        return getattr(mod, "CSRFError", None)
    except Exception:
        return None

CSRFProtect = _try_get_csrf_protect()
CSRFError = _try_get_csrf_error()

# ==================== 环境变量加载 ====================
try:
    from dotenv import load_dotenv
    # 说明：部分环境（例如某些 IDE/全局忽略规则）会阻止创建/读取 .env。
    # 为了让“配置文件激活”更稳定，这里支持按优先级加载：
    # 1) 环境变量 EDGEWIND_ENV_FILE 指定的文件
    # 2) 项目根目录下的 edgewind.env（推荐）
    # 3) 默认的 .env（如果存在）
    env_file = os.environ.get("EDGEWIND_ENV_FILE")
    if env_file and str(env_file).strip():
        load_dotenv(str(env_file).strip())
    else:
        # 先尝试 edgewind.env（不容易被忽略规则拦截）
        load_dotenv("edgewind.env")
        # 再尝试默认 .env（如果存在）
        load_dotenv()
except ImportError:
    pass

# ==================== 导入配置和模型 ====================
from edgewind.config import Config
from edgewind.models import db, User

# ==================== Flask应用初始化 ====================
app = Flask(__name__)
app.config.from_object(Config)

# ==================== CSRF 保护（浏览器会话安全） ====================
# 说明：
# - CSRF 主要针对“浏览器携带 Cookie 的后台操作”（例如设置页/故障管理页的 POST 请求）。
# - 设备侧上报接口（/api/register、/api/upload、/api/node/heartbeat）不会携带 CSRF Token，
#   因此会在注册蓝图后显式 exempt，避免影响模拟器/硬件。
csrf = None
if CSRFProtect is not None:
    csrf = CSRFProtect(app)
    app.logger.info("CSRFProtect 已启用")
else:
    app.logger.warning("CSRFProtect 未安装（请执行 pip install -r requirements.txt），将跳过 CSRF 防护。")

# ==================== 日志系统初始化 ====================
def setup_logging():
    """配置结构化日志系统"""
    os.makedirs('logs', exist_ok=True)
    
    log_level = getattr(logging, app.config['LOG_LEVEL'].upper(), logging.INFO)
    
    # 简化版格式用于控制台
    simple_formatter = logging.Formatter('%(levelname)s: %(message)s')
    detailed_formatter = logging.Formatter(
        '%(asctime)s - %(name)s - %(levelname)s - [%(filename)s:%(lineno)d] - %(message)s'
    )
    
    # 文件处理器
    file_handler = RotatingFileHandler(
        app.config['LOG_FILE'],
        maxBytes=10*1024*1024,  # 10MB
        backupCount=10,
        encoding='utf-8'
    )
    file_handler.setFormatter(detailed_formatter)
    file_handler.setLevel(log_level)
    
    # 控制台处理器
    console_handler = logging.StreamHandler()
    console_handler.setFormatter(simple_formatter)
    console_handler.setLevel(logging.INFO)  # 改为INFO以便看到启动信息
    
    # 配置根日志记录器
    root_logger = logging.getLogger()
    root_logger.setLevel(log_level)
    root_logger.addHandler(file_handler)
    root_logger.addHandler(console_handler)
    
    # 配置Flask日志
    app.logger.setLevel(log_level)
    app.logger.addHandler(file_handler)
    
    # 禁用werkzeug访问日志
    logging.getLogger('werkzeug').setLevel(logging.ERROR)
    
    app.logger.info("=" * 60)
    app.logger.info("EdgeWind 日志系统已初始化")
    app.logger.info(f"日志级别: {app.config['LOG_LEVEL']}")
    app.logger.info(f"日志文件: {app.config['LOG_FILE']}")
    app.logger.info("=" * 60)

setup_logging()

# 安全提示：SECRET_KEY 若仍为默认值，生产环境存在会话伪造风险
if app.config.get('SECRET_KEY') == 'wind-farm-secret-key-2024':
    app.logger.warning(
        "安全提示：SECRET_KEY 仍为默认值（wind-farm-secret-key-2024）。"
        "生产环境请务必通过环境变量 SECRET_KEY 设置为随机强密钥。"
    )

# ==================== 数据库初始化 ====================
db.init_app(app)

# ==================== CORS配置 ====================
allowed_origins = app.config['ALLOWED_ORIGINS']
if allowed_origins == '*':
    CORS(app)
    app.logger.warning("CORS: 允许所有来源（开发环境）")
else:
    origins_list = [origin.strip() for origin in allowed_origins.split(',')]
    CORS(app, origins=origins_list)
    app.logger.info(f"CORS: 限制为 {origins_list}")

# ==================== Flask-SocketIO 初始化 ====================
def _select_async_mode():
    """
    选择 SocketIO 异步模式（eventlet / gevent / threading）
    
    说明（非常重要）：
    - 在 Windows + Python 3.12+（你当前是 3.14）环境下，eventlet 0.33.x 可能因标准库变更而无法导入/运行，
      常见报错包括：
      - ModuleNotFoundError: No module named 'distutils'
      - AttributeError: module 'ssl' has no attribute 'wrap_socket'
    - 因此默认采用“自动探测”，并提供 FORCE_ASYNC_MODE 环境变量用于强制指定。
    
    环境变量：
    - FORCE_ASYNC_MODE=auto|eventlet|gevent|threading
      - auto（默认）：按 eventlet -> gevent -> threading 顺序尝试
      - eventlet/gevent/threading：强制使用；若失败将直接抛错，避免“悄悄回退”造成误判
    """
    import sys
    force = os.environ.get('FORCE_ASYNC_MODE', 'auto').strip().lower()
    app.logger.info(f"Python版本: {sys.version}")
    app.logger.info(f"FORCE_ASYNC_MODE={force}")

    def _try_eventlet():
        import eventlet  # noqa: F401
        # Windows + SQLAlchemy 场景下，thread 相关 monkey_patch 有概率导致锁语义差异，
        # 进而触发 “cannot notify on un-acquired lock”。
        # 这里禁用 thread patch，只保留 socket/select/time 等 I/O 相关 patch。
        eventlet.monkey_patch(thread=False)
        return 'eventlet'

    def _try_gevent():
        import gevent  # noqa: F401
        return 'gevent'

    if force in ('threading', 'gevent', 'eventlet'):
        try:
            if force == 'eventlet':
                mode = _try_eventlet()
            elif force == 'gevent':
                mode = _try_gevent()
            else:
                mode = 'threading'
            app.logger.info(f"异步模式(强制): {mode}")
            return mode
        except Exception as e:
            # 强制模式失败：直接抛错，避免误以为已启用 eventlet/gevent
            app.logger.exception(f"强制异步模式失败: {force} - {e}")
            raise RuntimeError(
                f"强制异步模式失败: {force}。"
                f"当前Python={sys.version}。"
                f"若要使用 eventlet，建议使用 Python 3.10/3.11 重新创建虚拟环境。"
            ) from e

    # auto：依次尝试
    try:
        mode = _try_eventlet()
        app.logger.info("使用 eventlet 异步模式")
        return mode
    except Exception as e:
        app.logger.warning(f"eventlet 不可用，回退尝试 gevent: {e}")

    try:
        mode = _try_gevent()
        app.logger.info("使用 gevent 异步模式")
        return mode
    except Exception as e:
        app.logger.warning(f"gevent 不可用，回退到 threading: {e}")
        return 'threading'


ASYNC_MODE = _select_async_mode()

socketio_cors_origins = app.config['ALLOWED_ORIGINS']
if socketio_cors_origins != '*':
    socketio_cors_origins = [origin.strip() for origin in socketio_cors_origins.split(',')]

socketio = SocketIO(
    app,
    cors_allowed_origins=socketio_cors_origins,
    async_mode=ASYNC_MODE,
    logger=False,
    engineio_logger=False,
    ping_timeout=app.config['SOCKET_PING_TIMEOUT'],
    ping_interval=app.config['SOCKET_PING_INTERVAL'],
    max_http_buffer_size=int(app.config['MAX_HTTP_BUFFER_SIZE']),
    transports=['websocket', 'polling'],
    allow_upgrades=True,
    cookie=None,
    max_connections=app.config['MAX_CONNECTIONS'],
    compression=True,
    cors_credentials=False
)

# ==================== Flask-Login 初始化 ====================
login_manager = LoginManager()
login_manager.init_app(app)
login_manager.login_view = 'auth.login'
login_manager.login_message = '请先登录以访问此页面。'
login_manager.login_message_category = 'info'

@login_manager.unauthorized_handler
def _edgewind_unauthorized():
    """
    未登录访问处理：
    - 页面路由：保持原行为，跳转到登录页
    - API 路由（/api/*）：返回 401 JSON，避免前端 fetch 跟随 302 后“看起来一直加载”
    """
    try:
        if request.path.startswith('/api/'):
            return jsonify({'success': False, 'error': '未登录或登录态已失效，请重新登录'}), 401
    except Exception:
        # 如果这里异常，兜底走原逻辑
        pass
    return redirect(url_for('auth.login', next=request.full_path))

@login_manager.user_loader
def load_user(user_id):
    """Flask-Login 需要的用户加载函数"""
    return User.query.get(int(user_id))

# ==================== 全局变量（节点管理） ====================
active_nodes = {}
node_commands = {}
node_report_modes = {}
node_downsample_commands = {}
node_upload_points_commands = {}
# 默认 60s，可用 EDGEWIND_NODE_TIMEOUT_SEC 调整（与 api.py / socket_events.py 保持一致）
NODE_TIMEOUT = max(10, int(os.environ.get("EDGEWIND_NODE_TIMEOUT_SEC", "60") or "60"))

# ==================== 后台任务线程池 ====================
db_executor = ThreadPoolExecutor(max_workers=2, thread_name_prefix="DB-Worker")

# ==================== 注册蓝图 ====================
from edgewind.routes.auth import auth_bp
from edgewind.routes.pages import pages_bp
from edgewind.routes.api import api_bp, init_api_blueprint, register_device, upload_data, node_heartbeat, delete_node_history

# 初始化API蓝图
init_api_blueprint(
    app,
    socketio,
    db_executor,
    active_nodes,
    node_commands,
    node_report_modes,
    node_downsample_commands,
    node_upload_points_commands,
)

# 注册蓝图
app.register_blueprint(auth_bp)
app.register_blueprint(pages_bp)
app.register_blueprint(api_bp)

app.logger.info("所有路由蓝图已注册")

# CSRF 豁免：设备上报接口（避免影响模拟器/硬件）+ 内部管理API
if csrf is not None:
    csrf.exempt(register_device)
    csrf.exempt(upload_data)
    csrf.exempt(node_heartbeat)
    csrf.exempt(delete_node_history)  # 历史数据删除API
    app.logger.info("CSRF：已豁免设备上报接口和内部管理API")

# ==================== WebSocket事件初始化 ====================
from edgewind.socket_events import init_socket_events
init_socket_events(socketio, active_nodes)
app.logger.info("WebSocket事件处理器已初始化")

# ==================== 数据库初始化 ====================
with app.app_context():
    db.create_all()
    
    # 创建默认管理员账户
    admin_username = (os.environ.get('EDGEWIND_ADMIN_USERNAME') or 'Edge_Wind').strip() or 'Edge_Wind'
    admin = User.query.filter_by(username=admin_username).first()
    if not admin:
        admin = User(username=admin_username)
        try:
            # 首次初始化密码：优先使用环境变量；否则随机生成强密码并仅在日志中提示一次
            env_pwd = (os.environ.get('EDGEWIND_ADMIN_INIT_PASSWORD') or '').strip()

            def _generate_admin_password():
                """生成符合当前密码策略的随机强密码（仅用于首次初始化）。"""
                min_len = max(16, int(app.config.get('PASSWORD_MIN_LENGTH', 8)))
                require_upper = bool(app.config.get('PASSWORD_REQUIRE_UPPERCASE', True))
                require_digits = bool(app.config.get('PASSWORD_REQUIRE_DIGITS', True))
                require_special = bool(app.config.get('PASSWORD_REQUIRE_SPECIAL', False))

                specials = "!@#$%^&*()-_=+"
                pool = string.ascii_letters + string.digits + specials

                # 先保证必需字符存在
                chars = []
                if require_upper:
                    chars.append(secrets.choice(string.ascii_uppercase))
                if require_digits:
                    chars.append(secrets.choice(string.digits))
                if require_special:
                    chars.append(secrets.choice(specials))

                # 填充剩余长度
                while len(chars) < min_len:
                    chars.append(secrets.choice(pool))

                # 打乱顺序
                secrets.SystemRandom().shuffle(chars)
                return ''.join(chars)

            pwd_source = 'env'
            init_pwd = env_pwd
            if not init_pwd:
                init_pwd = _generate_admin_password()
                pwd_source = 'generated'

            try:
                admin.set_password(init_pwd, app.config)
            except ValueError as ve:
                # 环境变量设置的密码不满足策略时，自动回退到随机强密码，避免启动后“无管理员可登录”
                app.logger.warning(f"EDGEWIND_ADMIN_INIT_PASSWORD 不满足密码策略：{ve}，将自动生成强密码用于初始化。")
                init_pwd = _generate_admin_password()
                pwd_source = 'generated'
                admin.set_password(init_pwd, app.config)

            db.session.add(admin)
            db.session.commit()
            if pwd_source == 'generated':
                app.logger.warning(
                    "已首次初始化管理员账户：%s。初始化密码（仅本次输出，请立即修改并妥善保存）：%s",
                    admin_username, init_pwd
                )
            else:
                app.logger.info("已首次初始化管理员账户：%s（密码来源：环境变量 EDGEWIND_ADMIN_INIT_PASSWORD）", admin_username)
        except ValueError as e:
            app.logger.warning(f"创建默认管理员失败: {e}")
    else:
        app.logger.info(f"管理员账户已存在：{admin_username}")

app.logger.info("数据库初始化完成")

# ==================== 后台定时任务 ====================
def auto_cleanup_old_data():
    """后台定时任务：自动清理过期数据"""
    from edgewind.models import DataPoint, WorkOrder
    from datetime import timedelta, datetime
    
    while True:
        try:
            time.sleep(86400)  # 24小时
            
            with app.app_context():
                retention_days = app.config['DATA_RETENTION_DAYS']
                
                if retention_days <= 0:
                    continue
                
                cutoff_date = datetime.utcnow() - timedelta(days=retention_days)
                
                old_datapoints = DataPoint.query.filter(DataPoint.timestamp < cutoff_date).all()
                for dp in old_datapoints:
                    db.session.delete(dp)
                
                old_workorders = WorkOrder.query.filter(
                    WorkOrder.fault_time < cutoff_date,
                    WorkOrder.status == 'fixed'
                ).all()
                for wo in old_workorders:
                    db.session.delete(wo)
                
                db.session.commit()
                app.logger.info(f"[AutoCleanup] 清理完成")
                
        except Exception as e:
            app.logger.error(f"[AutoCleanup] 失败: {str(e)}")
            db.session.rollback()

# 启动后台清理任务
cleanup_thread = threading.Thread(target=auto_cleanup_old_data, daemon=True, name="AutoCleanup")
cleanup_thread.start()
app.logger.info("后台定时清理任务已启动")

# ==================== 应用启动 ====================
if __name__ == '__main__':
    # 尝试多个端口
    PORT = int(os.environ.get('PORT', 5000))  # 默认改回5000（与模拟器默认一致）
    
    print("=" * 60)
    print("EdgeWind 系统启动")
    print(f"访问地址: http://localhost:{PORT}")
    print(f"模式: {'开发' if app.debug else '生产'}")
    print(f"异步: {ASYNC_MODE}")
    print("=" * 60)
    
    app.logger.info(f"准备在端口 {PORT} 启动服务器...")
    
    try:
        socketio.run(
            app,
            host='0.0.0.0',
            port=PORT,
            debug=False,
            use_reloader=False,
            log_output=False,
            allow_unsafe_werkzeug=True
        )
    except OSError as e:
        winerror = getattr(e, 'winerror', None)
        if 'address already in use' in str(e).lower() or winerror == 10048:
            print(f"\n错误: 端口 {PORT} 被占用（WinError 10048）！")
            print("解决方法:")
            print("1. 关闭/结束占用端口的程序（Windows 上 PID=4 的 System 通常无法结束）")
            print("2. 或改用备用端口，例如: set PORT=5002")
            app.logger.error(f"端口 {PORT} 被占用: {e}")
        else:
            raise
