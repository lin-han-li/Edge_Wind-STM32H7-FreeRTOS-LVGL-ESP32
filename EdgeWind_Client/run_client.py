import os
import sys
import urllib.error
import urllib.request

import webview


APP_NAME = "EdgeWind Client"
DEFAULT_SERVER_URL = "http://127.0.0.1:5000"


def _set_working_dir():
    if getattr(sys, "frozen", False) and hasattr(sys, "executable"):
        os.chdir(os.path.dirname(sys.executable))
    else:
        os.chdir(os.path.dirname(os.path.abspath(__file__)))


def _client_config_dir():
    base = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA") or os.path.expanduser("~")
    path = os.path.join(base, "EdgeWind_Client")
    try:
        os.makedirs(path, exist_ok=True)
    except Exception:
        pass
    return path


def _client_env_path():
    return os.path.join(_client_config_dir(), "edgewind_client.env")


def _ensure_client_env_template(path: str):
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
    except Exception:
        return
    if os.path.exists(path):
        return
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write("# EdgeWind Client config\n")
            f.write("# Example: EDGEWIND_SERVER_URL=http://192.168.1.100:5000\n")
            f.write("EDGEWIND_SERVER_URL=http://127.0.0.1:5000\n")
    except Exception:
        pass


def _read_env_file(path: str) -> dict:
    data = {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            for raw in f:
                line = raw.strip()
                if not line or line.startswith("#"):
                    continue
                if "=" not in line:
                    continue
                key, value = line.split("=", 1)
                key = key.strip()
                value = value.strip().strip('"').strip("'")
                if key:
                    data[key] = value
    except Exception:
        pass
    return data


def _apply_env_data(data: dict):
    for key, value in data.items():
        if key:
            os.environ[key] = value


def _apply_env_file(path: str):
    data = _read_env_file(path)
    _apply_env_data(data)
    return data


def _update_env_file(path: str, updates: dict):
    try:
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8") as f:
                lines = f.readlines()
        else:
            lines = ["# EdgeWind Client config\n"]
        found = set()
        new_lines = []
        for raw in lines:
            line = raw.rstrip("\n")
            stripped = line.strip()
            if stripped and not stripped.startswith("#") and "=" in stripped:
                key = stripped.split("=", 1)[0].strip()
                if key in updates:
                    new_lines.append(f"{key}={updates[key]}\n")
                    found.add(key)
                    continue
            new_lines.append(raw if raw.endswith("\n") else raw + "\n")
        for key, value in updates.items():
            if key not in found:
                new_lines.append(f"{key}={value}\n")
        with open(path, "w", encoding="utf-8") as f:
            f.writelines(new_lines)
    except Exception:
        pass


def _load_env_file():
    candidates = [
        os.path.join(os.getcwd(), "edgewind_client.env"),
        _client_env_path(),
    ]
    for path in candidates:
        if os.path.exists(path):
            data = _apply_env_file(path)
            os.environ["EDGEWIND_CLIENT_ENV_FILE"] = path
            return path, data
    path = _client_env_path()
    _ensure_client_env_template(path)
    if os.path.exists(path):
        data = _apply_env_file(path)
        os.environ["EDGEWIND_CLIENT_ENV_FILE"] = path
        return path, data
    return None, {}


def _parse_server_arg():
    for arg in sys.argv[1:]:
        if arg.startswith("--server="):
            return arg.split("=", 1)[1].strip()
    return ""


def _normalize_url(url: str) -> str:
    url = (url or "").strip().strip('"').strip("'")
    if not url:
        return ""
    if not url.startswith(("http://", "https://")):
        url = "http://" + url
    return url.rstrip("/")


def _server_url():
    url = _parse_server_arg() or os.environ.get("EDGEWIND_SERVER_URL") or DEFAULT_SERVER_URL
    url = _normalize_url(url) or DEFAULT_SERVER_URL
    os.environ["EDGEWIND_SERVER_URL"] = url
    return url


def _is_truthy(value: str) -> bool:
    return str(value or "").strip().lower() in ("1", "true", "yes", "y", "on")


def _prompt_server_url(default_url: str, message: str):
    try:
        import tkinter as tk
        from tkinter import simpledialog
    except Exception:
        return None

    root = tk.Tk()
    root.withdraw()
    try:
        root.attributes("-topmost", True)
    except Exception:
        pass
    try:
        return simpledialog.askstring(
            "EdgeWind Client",
            message,
            initialvalue=default_url,
            parent=root,
        )
    finally:
        try:
            root.destroy()
        except Exception:
            pass


def _maybe_prompt_server_url(env_path: str, env_data: dict):
    if _parse_server_arg():
        return
    if not env_path:
        return
    if _is_truthy(env_data.get("EDGEWIND_CLIENT_FIRST_RUN_DONE")):
        return
    default_url = env_data.get("EDGEWIND_SERVER_URL") or os.environ.get("EDGEWIND_SERVER_URL") or DEFAULT_SERVER_URL
    default_url = _normalize_url(default_url) or DEFAULT_SERVER_URL
    user_value = _prompt_server_url(
        default_url,
        "Enter server address (e.g. http://192.168.1.100:5000):",
    )
    updates = {"EDGEWIND_CLIENT_FIRST_RUN_DONE": "true"}
    if user_value is not None:
        user_value = _normalize_url(user_value)
        if user_value:
            updates["EDGEWIND_SERVER_URL"] = user_value
            os.environ["EDGEWIND_SERVER_URL"] = user_value
    _update_env_file(env_path, updates)


def _check_server_reachable(url: str, timeout: float = 3.0) -> bool:
    if not url:
        return False
    try:
        req = urllib.request.Request(url, method="GET")
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            resp.read(1)
        return True
    except urllib.error.HTTPError:
        return True
    except Exception:
        return False


def _maybe_prompt_on_unreachable(env_path: str, url: str) -> str:
    if _check_server_reachable(url):
        return url
    user_value = _prompt_server_url(
        url,
        "Cannot reach server. Enter a new server address:",
    )
    if user_value is None:
        return url
    user_value = _normalize_url(user_value)
    if not user_value:
        return url
    updates = {"EDGEWIND_SERVER_URL": user_value}
    if env_path:
        _update_env_file(env_path, updates)
    _apply_env_data(updates)
    return user_value


def _webview_storage_path():
    base = os.environ.get("LOCALAPPDATA") or os.environ.get("APPDATA") or os.path.expanduser("~")
    path = os.path.join(base, "EdgeWind_Client", "webview_storage")
    try:
        os.makedirs(path, exist_ok=True)
    except Exception:
        pass
    return path


def main():
    _set_working_dir()
    env_path, env_data = _load_env_file()
    _maybe_prompt_server_url(env_path, env_data)
    url = _server_url()
    url = _maybe_prompt_on_unreachable(env_path, url)

    try:
        webview.settings["ALLOW_DOWNLOADS"] = True
    except Exception:
        pass

    webview.create_window(f"{APP_NAME} ({url})", url, confirm_close=True)
    try:
        webview.start(gui="edgechromium", private_mode=False, storage_path=_webview_storage_path())
    except Exception:
        webview.start(private_mode=False, storage_path=_webview_storage_path())


if __name__ == "__main__":
    main()
