"""
共享扩展实例（避免 app.py 与蓝图之间的循环导入）。

目前提供：
- limiter：Flask-Limiter 限流器（可选依赖）。

设计原则（与 app.py 对 CSRFProtect 的处理一致）：
- Flask-Limiter 为可选依赖。未安装时提供一个“空壳” limiter，
  其 .limit() 装饰器不做任何限流，保证在缺依赖的环境里应用照常启动。
- 真正的初始化在 app.py 中通过 init_limiter(app) 完成。
"""
import logging

logger = logging.getLogger(__name__)

try:
    from flask_limiter import Limiter
    from flask_limiter.util import get_remote_address
    _LIMITER_AVAILABLE = True
except Exception:  # ImportError 或其它导入期异常
    Limiter = None
    get_remote_address = None
    _LIMITER_AVAILABLE = False


class _NoopLimiter:
    """Flask-Limiter 未安装时的降级替身：.limit() 原样返回被装饰函数。"""

    def limit(self, *args, **kwargs):
        def decorator(func):
            return func
        return decorator

    def init_app(self, app):  # 兼容调用签名
        return None


if _LIMITER_AVAILABLE:
    # 关键：不要设置 default_limits！
    # 设备上报接口（/api/upload 每秒上报、/api/node/heartbeat）若被全局限流会直接中断遥测。
    # 因此仅对显式加了 @limiter.limit(...) 的路由（如 /login）生效。
    limiter = Limiter(
        key_func=get_remote_address,   # 按客户端 IP 限流
        default_limits=[],             # 不设全局限流，避免误伤设备接口
        storage_uri="memory://",       # 内存存储：适合单进程；生产多进程建议接 Redis
        strategy="fixed-window",
    )
else:
    limiter = _NoopLimiter()


def init_limiter(app):
    """在 app 创建后调用，完成 limiter 与 app 的绑定。"""
    if _LIMITER_AVAILABLE:
        limiter.init_app(app)
        app.logger.info("Flask-Limiter 已启用（仅对 /login 等显式限流路由生效）")
    else:
        app.logger.warning(
            "Flask-Limiter 未安装（pip install -r requirements.txt），登录限流将被跳过。"
        )
    return limiter
