import os
import sys
import time
import socket
import subprocess
import re
import importlib
import traceback
import ipaddress

from jinja2 import FileSystemLoader
import webview


def _project_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", "Edge_Wind_System"))


def _base_dir():
    if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
        return sys._MEIPASS
    return _project_root()


def _ensure_sys_path():
    if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
        base_dir = sys._MEIPASS
        if base_dir not in sys.path:
            sys.path.insert(0, base_dir)
        return base_dir
    project_root = _project_root()
    if project_root not in sys.path:
        sys.path.insert(0, project_root)
    return project_root


_ensure_sys_path()

app = None
socketio = None
ADMIN_USERNAME = "Edge_Wind"
ADMIN_PASSWORD = "Gentle9532"
SERVER_MODE_ARG = "--server"


def _server_stdout_path():
    # 写到 exe 同级目录（_set_working_dir 会 chdir 到那里）
    return os.path.join(os.getcwd(), "edgewind_server_stdout.log")


def _webview_storage_path():
    """
    WebView2/pywebview 持久化目录（用于保存 cookie / localStorage）。
    这样“保持登录状态(remember me)”的 cookie 才能跨启动保留。
    """
    base = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA") or os.path.expanduser("~")
    path = os.path.join(base, "EdgeWind_Admin", "webview_storage")
    try:
        os.makedirs(path, exist_ok=True)
    except Exception:
        pass
    return path


def _desktop_db_path():
    """
    桌面版持久化数据库路径（避免 PyInstaller 临时目录导致“每次都是新库”）。
    位置：exe 同目录下的 instance\wind_farm.db
    """
    if getattr(sys, "frozen", False) and hasattr(sys, "executable"):
        base = os.path.dirname(sys.executable)
    else:
        base = os.getcwd()
    data_dir = os.path.join(base, "instance")
    try:
        os.makedirs(data_dir, exist_ok=True)
    except Exception:
        pass
    return os.path.join(data_dir, "wind_farm.db")


def _show_error_box(title: str, message: str):
    # windowed exe 没有控制台，用 MessageBox 兜底提示
    if os.name != "nt":
        return
    try:
        import ctypes

        ctypes.windll.user32.MessageBoxW(None, str(message), str(title), 0x10)
    except Exception:
        pass


def _show_info_box(title: str, message: str):
    if os.name != "nt":
        return
    try:
        import ctypes

        ctypes.windll.user32.MessageBoxW(None, str(message), str(title), 0x40)
    except Exception:
        pass


def _set_working_dir():
    if getattr(sys, "frozen", False) and hasattr(sys, "executable"):
        os.chdir(os.path.dirname(sys.executable))
    else:
        os.chdir(_project_root())


def _load_env_file(base_dir):
    cwd = os.getcwd()
    candidates = [
        os.path.join(cwd, "edgewind.env"),
        os.path.join(cwd, ".env"),
        os.path.join(base_dir, "edgewind.env"),
        os.path.join(base_dir, ".env"),
    ]
    for path in candidates:
        if os.path.exists(path):
            os.environ["EDGEWIND_ENV_FILE"] = path
            return path
    return None


def _force_light_nodes_off():
    """
    桌面版 exe 必须确保 LIGHT_ACTIVE_NODES=false，否则轮询时拿不到 waveform。
    只在进程环境变量层面强制设置，不改配置文件。
    """
    os.environ["EDGEWIND_LIGHT_ACTIVE_NODES"] = "false"


def _desktop_socketio_env_fix():
    """
    仅用于 PyInstaller 打包后的 exe（不影响“服务器开关/源码运行”）：
    - 强制使用 eventlet：提供真正 WebSocket，避免回退到 gevent/threading 触发前端高频轮询卡死
    - 禁用 eventlet 的 greendns：避免缺少 dnspython 时导入 dns.* 失败（日志里 No module named 'dns.rdtypes.ANY'）
    """
    if getattr(sys, "frozen", False):
        # 强制 eventlet
        os.environ["FORCE_ASYNC_MODE"] = "eventlet"
        # 禁用 greendns（不需要额外依赖 dnspython）
        os.environ["EVENTLET_NO_GREENDNS"] = "yes"


def _desktop_database_env_fix():
    """
    仅用于 exe：强制把数据库放到持久化目录，避免每次启动都创建新库。
    """
    if getattr(sys, "frozen", False):
        db_path = _desktop_db_path().replace("\\", "/")
        os.environ["DATABASE_URL"] = f"sqlite:///{db_path}"


def _patch_api_light_nodes():
    """
    强制设置 api.LIGHT_ACTIVE_NODES = False（运行时修改模块变量）。
    因为 app.py 导入时会调用 load_dotenv，可能会覆盖环境变量。
    """
    try:
        from edgewind.routes import api
        api.LIGHT_ACTIVE_NODES = False
        print(f"[Desktop] Forced api.LIGHT_ACTIVE_NODES = {api.LIGHT_ACTIVE_NODES}")
    except Exception as e:
        print(f"[Desktop] Failed to patch api.LIGHT_ACTIVE_NODES: {e}")


def _check_socketio_mode():
    """检查并打印 SocketIO 的异步模式"""
    try:
        if socketio:
            mode = getattr(socketio, 'async_mode', 'unknown')
            print(f"[Desktop] SocketIO async_mode = {mode}")
            if mode == 'threading':
                print(f"[Desktop] WARNING: threading mode has limited WebSocket support")
    except Exception as e:
        print(f"[Desktop] Failed to check socketio mode: {e}")


def _force_admin_password():
    try:
        print("  > Importing database models...")
        from edgewind.models import db, User
    except Exception as exc:
        print(f"  > Admin reset import failed: {exc}")
        return
    try:
        print("  > Creating app context...")
        with app.app_context():
            print("  > Creating database tables...")
            db.create_all()
            print("  > Querying admin user...")
            user = User.query.filter_by(username=ADMIN_USERNAME).first()
            if not user:
                print("  > Creating new admin user...")
                user = User(username=ADMIN_USERNAME)
                db.session.add(user)
            print("  > Setting password...")
            user.set_password(ADMIN_PASSWORD, app.config)
            print("  > Committing to database...")
            db.session.commit()
            print("  > Done!")
    except Exception as exc:
        try:
            db.session.rollback()
        except Exception:
            pass
        print(f"  > Admin reset failed: {exc}")


def _patch_flask_paths(base_dir):
    templates_dir = os.path.join(base_dir, "templates")
    static_dir = os.path.join(base_dir, "static")

    app.template_folder = templates_dir
    app.static_folder = static_dir

    loader = FileSystemLoader(templates_dir)
    app.jinja_loader = loader
    app.jinja_env.loader = loader


def _ensure_csrf_token():
    if "csrf_token" not in app.jinja_env.globals:
        app.jinja_env.globals["csrf_token"] = lambda: ""

def _run_server(port):
    if socketio is not None:
        socketio.run(
            app,
            host="0.0.0.0",
            port=port,
            debug=False,
            use_reloader=False,
            log_output=False,
            allow_unsafe_werkzeug=True,
        )
        return
    app.run(host="0.0.0.0", port=port, debug=False, use_reloader=False)

def _run_server_blocking(port: int):
    """服务器模式：阻塞运行（用于子进程）。"""
    try:
        _run_server(port)
    except Exception:
        # 写崩溃堆栈，避免“拒绝连接但不知道原因”
        try:
            with open(_server_stdout_path(), "a", encoding="utf-8") as f:
                f.write("\n\n=== SERVER CRASH ===\n")
                f.write(traceback.format_exc())
                f.write("\n")
        except Exception:
            pass
        raise


def _start_server_subprocess(port: int):
    """UI 模式：启动同一个 exe 的 server 子进程。"""
    exe = sys.executable
    args = [exe, SERVER_MODE_ARG, f"--port={port}"]
    creationflags = 0
    if os.name == "nt":
        try:
            creationflags = subprocess.CREATE_NO_WINDOW
        except Exception:
            creationflags = 0
    try:
        log_path = _server_stdout_path()
        f = open(log_path, "a", encoding="utf-8")
    except Exception:
        f = None
    try:
        p = subprocess.Popen(
            args,
            cwd=os.getcwd(),
            stdout=f if f else subprocess.DEVNULL,
            stderr=f if f else subprocess.DEVNULL,
            creationflags=creationflags,
        )
    except Exception:
        if f:
            try:
                f.close()
            except Exception:
                pass
        raise
    return p, f


def _wait_for_port(host="127.0.0.1", port=5000, timeout=8.0):
    start = time.time()
    while time.time() - start < timeout:
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return True
        except OSError:
            time.sleep(0.2)
    return False


def _is_port_free(host, port):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((host, port))
        sock.close()
        return True
    except OSError:
        return False


def _get_listening_pids(port):
    try:
        result = subprocess.run(
            ["netstat", "-ano"],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            return []
        pids = set()
        pattern = re.compile(rf":{port}\s+.*LISTENING\s+(\d+)\s*$")
        for line in result.stdout.splitlines():
            m = pattern.search(line)
            if m:
                pids.add(int(m.group(1)))
        return sorted(pids)
    except Exception:
        return []


def _kill_pids(pids):
    for pid in pids:
        if pid == 4:
            continue
        try:
            subprocess.run(
                ["taskkill", "/F", "/PID", str(pid)],
                capture_output=True,
                text=True,
                check=False,
            )
        except Exception:
            pass


def _ensure_port_5000(host="0.0.0.0"):
    port = 5000
    if _is_port_free(host, port):
        return True
    pids = _get_listening_pids(port)
    if pids:
        _kill_pids(pids)
        time.sleep(0.5)
    return _is_port_free(host, port)


def _ensure_firewall_rule(port):
    if os.name != "nt":
        return
    rule_name = f"EdgeWind_Admin_{port}"
    cmd = [
        "netsh",
        "advfirewall",
        "firewall",
        "add",
        "rule",
        f"name={rule_name}",
        "dir=in",
        "action=allow",
        "protocol=TCP",
        f"localport={port}",
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print("Firewall rule add failed. Run as admin to allow LAN access.")
    except Exception:
        print("Firewall rule add failed. Run as admin to allow LAN access.")


def _is_valid_lan_ip(ip: str) -> bool:
    try:
        ip_obj = ipaddress.ip_address(ip)
    except ValueError:
        return False
    if ip_obj.version != 4:
        return False
    if ip in ("127.0.0.1", "0.0.0.0"):
        return False
    if ip.startswith("169.254."):
        return False
    return True


def _ips_from_ipconfig():
    ips = []
    try:
        result = subprocess.run(["ipconfig"], capture_output=True, text=True, check=False)
        if result.returncode != 0:
            return ips
        pattern = re.compile(r"(IPv4.*地址|IPv4 Address)[^:]*:\s*([\d\.]+)")
        for line in result.stdout.splitlines():
            m = pattern.search(line)
            if m:
                ip = m.group(2).strip()
                if _is_valid_lan_ip(ip):
                    ips.append(ip)
    except Exception:
        pass
    return ips


def _ips_from_socket():
    ips = []
    try:
        hostname = socket.gethostname()
        for ip in socket.gethostbyname_ex(hostname)[2]:
            if _is_valid_lan_ip(ip):
                ips.append(ip)
    except Exception:
        pass
    return ips


def _get_lan_ips():
    ips = []
    ips.extend(_ips_from_ipconfig())
    if not ips:
        ips.extend(_ips_from_socket())
    if not ips:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.connect(("8.8.8.8", 80))
            ip = sock.getsockname()[0]
            sock.close()
            if _is_valid_lan_ip(ip):
                ips.append(ip)
        except OSError:
            pass

    uniq = []
    seen = set()
    for ip in ips:
        if ip not in seen:
            seen.add(ip)
            uniq.append(ip)

    def _rank(addr: str) -> int:
        if addr.startswith("192.168."):
            return 0
        if addr.startswith("10."):
            return 1
        if addr.startswith("172."):
            try:
                second = int(addr.split(".")[1])
                if 16 <= second <= 31:
                    return 2
            except Exception:
                pass
        return 3

    return sorted(uniq, key=lambda ip: (_rank(ip), ip))


def _write_runtime_info(port):
    lan_ips = _get_lan_ips()
    primary_ip = lan_ips[0] if lan_ips else "127.0.0.1"
    if lan_ips:
        lan_lines = "\n".join([f"  - http://{ip}:{port}" for ip in lan_ips])
    else:
        lan_lines = "  (not detected)"
    content = (
        "EdgeWind Runtime Info\n"
        f"Local URL: http://127.0.0.1:{port}\n"
        "LAN URLs:\n"
        f"{lan_lines}\n"
        "\n"
        "ESP8266 settings:\n"
        f"  SERVER_IP   \"{primary_ip}\"\n"
        f"  SERVER_PORT {port}\n"
        "\n"
        "Note: Choose the IP in the same subnet as your ESP8266.\n"
    )
    if not lan_ips:
        content += "Note: LAN IP not detected. Check network adapters.\n"
    try:
        with open("edgewind_runtime.txt", "w", encoding="utf-8") as f:
            f.write(content)
    except Exception:
        pass


def _show_runtime_info_popup(port):
    lan_ips = _get_lan_ips()
    primary_ip = lan_ips[0] if lan_ips else "127.0.0.1"
    if lan_ips:
        lan_lines = "\n".join([f"  - http://{ip}:{port}" for ip in lan_ips])
    else:
        lan_lines = "  (not detected)"
    message = (
        f"本机访问：http://127.0.0.1:{port}\n\n"
        "局域网可用地址：\n"
        f"{lan_lines}\n\n"
        "ESP8266 设置：\n"
        f'  SERVER_IP   "{primary_ip}"\n'
        f"  SERVER_PORT {port}\n\n"
        "请选与设备同网段的 IP。"
    )
    _show_info_box("EdgeWind Admin", message)


def _start_ui(port, server_proc=None, server_log_file=None):
    # 允许下载（例如 /api/workorder/export 返回的 docx，或前端用 Blob + a.download 触发的保存）
    try:
        webview.settings["ALLOW_DOWNLOADS"] = True
    except Exception:
        pass

    window = webview.create_window(
        f"EdgeWind Admin ({port})",
        f"http://127.0.0.1:{port}",
        confirm_close=True,
    )

    def _on_closed():
        # 关闭时确保杀掉后端子进程
        try:
            if server_proc and server_proc.poll() is None:
                server_proc.terminate()
        except Exception:
            pass
        try:
            if server_log_file:
                server_log_file.close()
        except Exception:
            pass
        os._exit(0)

    window.events.closed += _on_closed
    # Windows 下使用 edgechromium(WebView2) 更稳定，且支持下载
    try:
        webview.start(gui="edgechromium", private_mode=False, storage_path=_webview_storage_path())
    except Exception:
        webview.start(private_mode=False, storage_path=_webview_storage_path())

def _parse_port_from_argv(default=5000):
    for a in sys.argv[1:]:
        if a.startswith("--port="):
            try:
                return int(a.split("=", 1)[1].strip())
            except Exception:
                return default
    return default


def _server_main():
    """子进程入口：只跑后端服务器，不启动 UI。"""
    global app, socketio
    _set_working_dir()
    base_dir = _base_dir()
    _load_env_file(base_dir)
    _force_light_nodes_off()
    _desktop_socketio_env_fix()
    _desktop_database_env_fix()
    os.environ["EDGEWIND_ADMIN_USERNAME"] = ADMIN_USERNAME
    os.environ["EDGEWIND_ADMIN_INIT_PASSWORD"] = ADMIN_PASSWORD

    mod = importlib.import_module("app")
    app = getattr(mod, "app", None)
    socketio = getattr(mod, "socketio", None)
    if app is None:
        raise RuntimeError("Failed to import Flask app from app.py")

    _patch_api_light_nodes()
    _patch_flask_paths(base_dir)
    _ensure_csrf_token()
    _force_admin_password()

    port = _parse_port_from_argv(5000)
    os.environ["PORT"] = str(port)
    _run_server_blocking(port)


def main():
    # server 子进程模式
    if SERVER_MODE_ARG in sys.argv:
        _server_main()
        return

    # UI 模式（不在本进程内跑后端，避免线程模式导致崩溃/拒绝连接）
    _set_working_dir()
    port = 5000
    if not _ensure_port_5000():
        _show_error_box("EdgeWind Admin", "端口 5000 被占用且无法释放，请关闭占用进程后重试。")
        return

    _ensure_firewall_rule(port)
    _write_runtime_info(port)
    _show_runtime_info_popup(port)

    # 启动 server 子进程并等待端口 ready
    try:
        server_proc, server_log_file = _start_server_subprocess(port)
    except Exception as e:
        _show_error_box("EdgeWind Admin", f"启动后端服务失败：{e}")
        return

    if not _wait_for_port(port=port, timeout=20.0):
        try:
            if server_proc and server_proc.poll() is None:
                server_proc.terminate()
        except Exception:
            pass
        try:
            if server_log_file:
                server_log_file.close()
        except Exception:
            pass
        _show_error_box(
            "EdgeWind Admin",
            "后端服务未能在 20 秒内启动（127.0.0.1:5000）。\n"
            "请查看同目录下 edgewind_server_stdout.log 获取崩溃原因。",
        )
        return

    _start_ui(port, server_proc=server_proc, server_log_file=server_log_file)


if __name__ == "__main__":
    main()
