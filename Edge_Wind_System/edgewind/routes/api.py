"""
API路由蓝图
处理所有RESTful API请求
"""
from flask import Blueprint, request, jsonify
from flask_login import login_required
from datetime import datetime, timedelta
from edgewind.models import db, Device, DataPoint, WorkOrder, SystemConfig, FaultSnapshot, HistoryData, NodePendingCommand
from edgewind.full_frame_binary import FullFrameBinaryError, decode_full_frame_binary
from edgewind.knowledge_graph import FAULT_KNOWLEDGE_GRAPH, FAULT_CODE_MAP, generate_ai_report, get_fault_knowledge_graph
from edgewind.utils import (
    save_to_buffer, get_latest_normal_data, get_latest_fault_data,
    node_fault_states, node_snapshot_saved, save_fault_snapshot, create_work_order_from_fault
)
import time
import json
import logging
from urllib.parse import unquote
from collections import defaultdict
import os
import sys
from pathlib import Path
from io import BytesIO
import base64
from urllib.parse import quote
import re
try:
    import orjson  # type: ignore[import-not-found]
except Exception:
    orjson = None

from flask import send_file
from edgewind.time_utils import fmt_beijing, iso_beijing, to_beijing

api_bp = Blueprint('api', __name__, url_prefix='/api')
logger = logging.getLogger(__name__)
_device_api_key_warned = False

# 设备上报调试：限制心跳日志频率，避免刷屏
_last_hb_log_ts = {}
_last_perf_log_ts = {}  # {node_id: ts}
_last_processed_cache = {}  # {node_id: processed_data}：用于兜底填充空波形/频谱，避免前端周期性卡顿
_last_bad_frame_log_ts = {}  # {node_id: ts}：坏帧诊断限频日志
_last_series_log_ts = {}  # {node_id: ts}

# 全局变量（将从app传入）
active_nodes = {}  # 将在app.py中初始化并传入
node_commands = {}  # 将在app.py中初始化并传入
node_report_modes = {}  # {node_id: 'summary'|'full'}
node_downsample_commands = {}  # {node_id: int(1..64)}
node_upload_points_commands = {}  # {node_id: int(256..4096, step=256)}
DEFAULT_REPORT_MODE = 'summary'
MAX_CHUNK_DELAY_MS = 20
CONFIG_COMMAND_KEYS = {
    'report_mode',
    'downsample_step',
    'upload_points',
    'heartbeat_ms',
    'min_interval_ms',
    'http_timeout_ms',
    'chunk_kb',
    'chunk_delay_ms',
}

# 节点超时时间（秒）
# 说明：此前为 10s，网络/设备偶发抖动（或一次心跳解析失败）就会导致节点被清空，前端表现为“运行一段时间后停机/无节点”。
# 这里改为环境变量可配置，默认 60s，更贴合真实链路。
NODE_TIMEOUT = max(10, int(os.environ.get("EDGEWIND_NODE_TIMEOUT_SEC", "60") or "60"))
db_executor = None  # 后台线程池，将在注册蓝图时设置
socketio_instance = None  # SocketIO实例，将在注册蓝图时设置
app_instance = None  # Flask应用实例

# ==================== 实时推送性能参数（可通过环境变量调节）====================
# 说明：多节点接入时，心跳/数据上报频率往往很高（例如 50Hz）。
# 如果后端对每次心跳都广播/推送，会导致事件风暴：CPU/网络/浏览器主线程都会被压垮，表现为“卡、慢、延迟大”。
# 因此这里默认启用“按节点节流 + 波形/频谱降采样”（仍然足够实时，且更平滑）。

def _env_int(key, default):
    try:
        return int(os.environ.get(key, default))
    except Exception:
        return int(default)

def _env_float(key, default):
    try:
        return float(os.environ.get(key, default))
    except Exception:
        return float(default)

# 每个节点的状态推送频率（Hz）：影响 node_status_update（概览/列表/指标）
STATUS_EMIT_HZ = max(1.0, _env_float("EDGEWIND_STATUS_EMIT_HZ", 5))
# 每个节点的监控推送频率（Hz）：影响 monitor_update（波形/频谱）
MONITOR_EMIT_HZ = max(1.0, _env_float("EDGEWIND_MONITOR_EMIT_HZ", 20))

# 波形/频谱监控展示点数（默认显示全量：4096 waveform + 2048 FFT）
# 仍保留环境变量覆盖能力，便于低性能主机手动降回轻量显示。
MAX_WAVEFORM_POINTS = max(0, _env_int("EDGEWIND_WAVEFORM_POINTS", 4096))
MAX_SPECTRUM_POINTS = max(0, _env_int("EDGEWIND_SPECTRUM_POINTS", 2048))
MONITOR_WAVEFORM_POINTS = MAX_WAVEFORM_POINTS if MAX_WAVEFORM_POINTS > 0 else 4096
MONITOR_SPECTRUM_POINTS = MAX_SPECTRUM_POINTS if MAX_SPECTRUM_POINTS > 0 else 2048

# active_nodes 是否仅保存“轻量数据”（不保留 1024 点全量波形）
LIGHT_ACTIVE_NODES = str(os.environ.get("EDGEWIND_LIGHT_ACTIVE_NODES", "true")).strip().lower() == "true"

# 记录每个节点的上次推送时间（按节点节流）
_last_emit_status_ts = {}   # {node_id: ts}
_last_emit_monitor_ts = {}  # {node_id: ts}
# 节流：按节点减少数据库写入频率（避免高频心跳导致频繁落库）
_last_db_heartbeat_ts = {}  # {node_id: ts}
_last_active_db_rehydrate_ts = 0.0

# 默认每个节点最多每 N 秒写一次 Device.last_heartbeat（可通过环境变量调节）
DEVICE_DB_UPDATE_INTERVAL_SEC = max(1.0, _env_float("EDGEWIND_DEVICE_DB_UPDATE_SEC", 5))
# active_nodes is per-process under multi-worker deployment; periodically rehydrate from DB.
ACTIVE_DB_REHYDRATE_INTERVAL_SEC = max(0.5, _env_float("EDGEWIND_ACTIVE_DB_REHYDRATE_SEC", 2))

# 心跳性能诊断：仅在“慢请求”时打印耗时分解（避免刷屏）
HB_SLOW_MS = max(1.0, _env_float("EDGEWIND_HEARTBEAT_SLOW_MS", 80))
HB_PERF_LOG_SEC = max(1.0, _env_float("EDGEWIND_HEARTBEAT_PERF_LOG_SEC", 5))


def _get_json_payload() -> dict:
    """
    更稳健且更轻量的 JSON 解析：
    - 优先直接读取原始 body(bytes) 并解析，避免 request.get_json() + as_text=True 的双重开销。
    - 若安装了 orjson，则优先走更快的 bytes 解析路径。
    - 硬件端若漏发 Content-Type: application/json 也仍能正常解析。
    """
    raw = b""
    try:
        raw = request.get_data(cache=True) or b""
    except Exception:
        raw = b""
    if raw:
        if orjson is not None:
            try:
                obj = orjson.loads(raw)
                if isinstance(obj, dict):
                    return obj
            except Exception:
                pass
        try:
            obj = json.loads(raw)
            if isinstance(obj, dict):
                return obj
        except Exception:
            try:
                obj = json.loads(raw.decode('utf-8', errors='ignore'))
                if isinstance(obj, dict):
                    return obj
            except Exception:
                pass
    try:
        data = request.get_json(silent=True, cache=True)
        if isinstance(data, dict):
            return data
    except Exception:
        pass
    return {}

def _normalize_node_id(raw) -> str:
    """
    归一化 node_id/device_id：
    - 去首尾空白
    - 去掉控制字符（\\r\\n\\t）
    - 将“弯引号/智能引号”统一为标准 ASCII 引号，避免同一设备出现多种不可见变体
    注意：这里尽量不做“强制替换为下划线”等破坏性操作，以免设备侧继续上报旧ID导致对不上。
    """
    if raw is None:
        return ''
    s = str(raw)
    s = s.replace('\u00A0', ' ')  # NBSP -> space
    s = s.strip()
    # 删除控制字符（避免污染日志/数据库/URL）
    s = re.sub(r'[\r\n\t]+', '', s)
    # 统一弯引号/智能引号
    s = (s
         .replace('\u2018', "'")  # ‘
         .replace('\u2019', "'")  # ’
         .replace('\u201C', '"')  # “
         .replace('\u201D', '"')) # ”
    return s


def _submit_update_device_heartbeat(node_id: str, payload: dict, fault_code: str, current_ts: float) -> None:
    """后台更新 Device 表：last_heartbeat/status/fault_code/location/hw_version（节流后调用）。"""
    if not db_executor or not app_instance:
        return

    def _job():
        with app_instance.app_context():
            try:
                device = Device.query.filter_by(device_id=node_id).first()
                now_utc = datetime.utcnow()
                location = payload.get('location') or node_id
                hw_version = payload.get('hw_version') or payload.get('hardware_version') or payload.get('fw_version')
                status_in = (payload.get('status') or 'online').strip().lower()
                status = 'faulty' if (fault_code and fault_code != 'E00') else ('online' if status_in != 'offline' else 'offline')

                if not device:
                    device = Device(
                        device_id=node_id,
                        location=location,
                        hw_version=hw_version or 'v1.0',
                        status=status,
                        fault_code=fault_code or 'E00',
                        last_heartbeat=now_utc
                    )
                    db.session.add(device)
                else:
                    device.location = location
                    if hw_version:
                        device.hw_version = hw_version
                    device.status = status
                    device.fault_code = fault_code or 'E00'
                    device.last_heartbeat = now_utc

                db.session.commit()
            except Exception:
                db.session.rollback()

    try:
        db_executor.submit(_job)
    except Exception:
        # 后台线程池异常不影响接口返回
        pass

def _should_emit(node_id, now_ts, hz, last_map):
    """按 node_id 节流：达到间隔才允许 emit。"""
    interval = 1.0 / float(hz)
    last = last_map.get(node_id, 0)
    if now_ts - last >= interval:
        last_map[node_id] = now_ts
        return True
    return False

def _downsample_list(arr, max_points):
    """
    简单降采样（抽取），用于减少 JSON 体积/前端渲染压力。
    - max_points=0：不处理
    """
    if max_points <= 0:
        return arr
    if not isinstance(arr, list):
        return []
    n = len(arr)
    if n <= max_points:
        return arr
    step = max(1, n // max_points)
    sampled = arr[::step]
    # 可能多一点，最终裁剪到 max_points
    return sampled[:max_points]

def _submit_history_data(node_id: str, processed_data: dict) -> None:
    """后台保存轻量历史值，避免设备 HTTP 请求被 SQLite commit 阻塞。"""
    if not db_executor or not app_instance:
        return

    values = {
        'voltage_pos': processed_data.get('voltage', 0),
        'voltage_neg': processed_data.get('voltage_neg', 0),
        'current': processed_data.get('current', 0),
        'leakage': processed_data.get('leakage', 0),
    }

    def _job():
        with app_instance.app_context():
            try:
                history_record = HistoryData(device_id=node_id, **values)
                db.session.add(history_record)
                db.session.commit()
            except Exception as e:
                db.session.rollback()
                logger.warning(f"[HistoryData] 保存历史数据失败: {node_id} - {e}")

    try:
        db_executor.submit(_job)
    except Exception as e:
        logger.warning(f"[HistoryData] 提交后台保存任务失败: {node_id} - {e}")


def _emit_socket_updates_async(status_payload=None, monitor_payload=None, monitor_room=None):
    """
    SocketIO 推送任务。

    注意：设备侧 HTTP 上报必须尽快返回 200，不能被浏览器 WebSocket 慢连接/大数组序列化拖住；
    因此 node_heartbeat 只提交后台推送任务，不在请求线程内同步 emit。
    """
    try:
        if socketio_instance is None:
            return
        if status_payload is not None:
            socketio_instance.emit('node_status_update', status_payload, namespace='/')
        if monitor_payload is not None and monitor_room:
            socketio_instance.emit('monitor_update', monitor_payload, room=monitor_room, namespace='/')
    except Exception as e:
        logger.warning(f"[SocketIO] 后台推送失败: {e}")


def _lighten_channels(channels):
    """将 channels 转为轻量版：保留展示必须字段，剔除大数组。"""
    if not isinstance(channels, list):
        return []
    out = []
    for ch in channels:
        if not isinstance(ch, dict):
            continue
        out.append({
            'id': ch.get('id', 0),
            'label': ch.get('label', ''),
            'unit': ch.get('unit', ''),
            'type': ch.get('type', ''),
            'range': ch.get('range', []),
            'color': ch.get('color', ''),
            'value': ch.get('value', ch.get('current_value', 0)),
        })
    return out

def init_api_blueprint(app, socketio, executor, nodes, commands, report_modes, downsample_commands, upload_points_commands):
    """初始化API蓝图的全局变量"""
    global active_nodes, node_commands, node_report_modes, node_downsample_commands, node_upload_points_commands
    global db_executor, socketio_instance, app_instance
    active_nodes = nodes
    node_commands = commands
    node_report_modes = report_modes
    node_downsample_commands = downsample_commands
    node_upload_points_commands = upload_points_commands
    db_executor = executor
    socketio_instance = socketio
    app_instance = app


def _get_report_mode(node_id: str | None) -> str:
    if not node_id:
        return DEFAULT_REPORT_MODE
    try:
        node_data = ((active_nodes.get(node_id) or {}).get('data') or {})
        mode = str(node_data.get('report_mode') or '').strip().lower()
        if mode in ('summary', 'full'):
            node_report_modes[node_id] = mode
            return mode
    except Exception:
        pass
    mode = (node_report_modes.get(node_id) or '').strip().lower()
    if mode in ('summary', 'full'):
        return mode
    try:
        cmd = (NodePendingCommand.query
               .filter(NodePendingCommand.target_node == node_id,
                       NodePendingCommand.key == 'report_mode',
                       NodePendingCommand.status.in_(('pending', 'delivered', 'applied')))
               .order_by(NodePendingCommand.id.desc())
               .first())
        if cmd:
            mode = (cmd.value or '').strip().lower()
            if mode in ('summary', 'full'):
                node_report_modes[node_id] = mode
                return mode
    except Exception:
        pass
    return DEFAULT_REPORT_MODE


def _parse_int_range(value, lo: int, hi: int) -> int | None:
    if value is None or isinstance(value, bool):
        return None
    try:
        if isinstance(value, float):
            if not value.is_integer():
                return None
            v = int(value)
        else:
            raw = str(value).strip()
            if not raw or not re.match(r'^\d+$', raw):
                return None
            v = int(raw)
    except Exception:
        return None
    return v if lo <= v <= hi else None


def _normalize_command_value(key: str, value):
    key = (key or '').strip()
    if key == 'report_mode':
        mode = (value or '').strip().lower()
        if mode in ('summary', 'full'):
            return mode
        return None
    if key == 'downsample_step':
        return _parse_downsample_step(value)
    if key == 'upload_points':
        return _parse_upload_points(value)
    if key == 'heartbeat_ms':
        return _parse_int_range(value, 200, 54999)
    if key == 'min_interval_ms':
        return _parse_int_range(value, 0, 600000)
    if key == 'http_timeout_ms':
        return _parse_int_range(value, 1000, 600000)
    if key == 'chunk_kb':
        return _parse_int_range(value, 0, 16)
    if key == 'chunk_delay_ms':
        return _parse_int_range(value, 0, MAX_CHUNK_DELAY_MS)
    return None


def _new_command_id() -> str:
    # ESP32 status payload carries uint32_t command id, so keep this numeric.
    #
    # Do not use a random id here.  Some already-deployed ESP32 firmware builds
    # use command_id as an ordering/deduplication hint, so a later command with a
    # smaller random id can be treated as stale.  Generate a monotonic uint32 id
    # above every persisted command id and above the current epoch seconds.
    max_seen = int(time.time())
    try:
        for (raw_id,) in db.session.query(NodePendingCommand.command_id).all():
            try:
                value = int(str(raw_id).strip())
            except Exception:
                continue
            if 0 <= value < 0xFFFFFFFF and value > max_seen:
                max_seen = value
    except Exception as exc:
        logger.warning("[node_command] command_id scan failed, fallback to time: %s", exc)

    next_id = max_seen + 1
    if next_id > 0xFFFFFFFF:
        # Practically unreachable for this product, but keep the value valid for
        # the ESP32 uint32_t field if a corrupted DB ever contains huge IDs.
        next_id = max(1, int(time.time()) & 0xFFFFFFFF)
    return str(next_id)


def _sync_memory_command_cache(node_id: str, key: str, value) -> None:
    try:
        if key == 'report_mode':
            node_report_modes[node_id] = str(value)
            if node_id in active_nodes:
                active_nodes[node_id].setdefault('data', {})
                active_nodes[node_id]['data']['report_mode'] = str(value)
        elif key == 'downsample_step':
            node_downsample_commands[node_id] = int(value)
            _sync_active_node_downsample_step(node_id, int(value))
        elif key == 'upload_points':
            node_upload_points_commands[node_id] = int(value)
            _sync_active_node_upload_points(node_id, int(value))
        elif key in CONFIG_COMMAND_KEYS and node_id in active_nodes:
            active_nodes[node_id].setdefault('data', {})
            active_nodes[node_id]['data'][key] = int(value)
    except Exception:
        pass


def _enqueue_node_command(node_id: str, key: str, value):
    """Persist a config command and update in-memory target cache for quick UI feedback."""
    normalized = _normalize_command_value(key, value)
    if not node_id or key not in CONFIG_COMMAND_KEYS or normalized is None:
        return None

    try:
        # Supersede older not-yet-applied commands for the same node/key.
        old_items = NodePendingCommand.query.filter(
            NodePendingCommand.target_node == node_id,
            NodePendingCommand.key == key,
            NodePendingCommand.status.in_(('pending', 'delivered')),
        ).all()
        now = datetime.utcnow()
        for item in old_items:
            item.status = 'failed'
            item.error = 'superseded'
            item.updated_at = now

        cmd = NodePendingCommand(
            command_id=_new_command_id(),
            target_node=node_id,
            key=key,
            value=str(normalized),
            status='pending',
        )
        db.session.add(cmd)
        db.session.commit()
        _sync_memory_command_cache(node_id, key, normalized)
        logger.info("[node_command] queued node=%s key=%s value=%s command_id=%s",
                    node_id, key, normalized, cmd.command_id)
        return cmd
    except Exception as exc:
        db.session.rollback()
        logger.exception(f"[node_command] enqueue failed node={node_id} key={key}: {exc}")
        return None


def _latest_pending_command_values(node_id: str, mark_delivered: bool = False) -> dict:
    """Return latest pending/delivered commands per key; optionally mark pending as delivered."""
    if not node_id:
        return {}
    try:
        rows = (NodePendingCommand.query
                .filter(NodePendingCommand.target_node == node_id,
                        NodePendingCommand.status.in_(('pending', 'delivered')))
                .order_by(NodePendingCommand.id.asc())
                .all())
    except Exception as exc:
        logger.warning(f"[node_command] query pending failed node={node_id}: {exc}")
        return {}

    latest: dict[str, NodePendingCommand] = {}
    for row in rows:
        if row.key in CONFIG_COMMAND_KEYS:
            latest[row.key] = row

    if mark_delivered:
        now = datetime.utcnow()
        changed = False
        for row in latest.values():
            if row.status == 'pending':
                row.status = 'delivered'
                row.delivered_at = now
                row.updated_at = now
                changed = True
                logger.info("[node_command] delivered node=%s key=%s value=%s command_id=%s",
                            node_id, row.key, row.value, row.command_id)
        if changed:
            try:
                db.session.commit()
            except Exception:
                db.session.rollback()

    out = {}
    latest_cmd_id = None
    for key, row in latest.items():
        value = _normalize_command_value(key, row.value)
        if value is not None:
            out[key] = value
            latest_cmd_id = row.command_id
    if latest_cmd_id is not None:
        out['command_id'] = latest_cmd_id
    return out


def _latest_pending_command_value(node_id: str, key: str):
    """Return latest pending/delivered value for one key from DB without changing status."""
    if not node_id or key not in CONFIG_COMMAND_KEYS:
        return None
    try:
        row = (NodePendingCommand.query
               .filter(NodePendingCommand.target_node == node_id,
                       NodePendingCommand.key == key,
                       NodePendingCommand.status.in_(('pending', 'delivered')))
               .order_by(NodePendingCommand.id.desc())
               .first())
    except Exception as exc:
        logger.warning(f"[node_command] query pending value failed node={node_id} key={key}: {exc}")
        return None
    if not row:
        return None
    return _normalize_command_value(key, row.value)


def _append_pending_config_commands(node_id: str, response_payload: dict, mark_delivered: bool = True) -> dict:
    pending = _latest_pending_command_values(node_id, mark_delivered=mark_delivered)
    for key, value in pending.items():
        response_payload[key] = value
    return response_payload


def _mark_command_applied(node_id: str, key: str, value, failed: bool = False, error: str | None = None) -> None:
    normalized = _normalize_command_value(key, value)
    if not node_id or key not in CONFIG_COMMAND_KEYS or normalized is None:
        return
    try:
        rows = (NodePendingCommand.query
                .filter(NodePendingCommand.target_node == node_id,
                        NodePendingCommand.key == key,
                        NodePendingCommand.status.in_(('pending', 'delivered')))
                .order_by(NodePendingCommand.id.asc())
                .all())
        if not rows:
            return
        now = datetime.utcnow()
        changed = False
        for row in rows:
            if str(_normalize_command_value(key, row.value)) == str(normalized):
                row.status = 'failed' if failed else 'applied'
                row.error = error
                row.applied_at = None if failed else now
                row.updated_at = now
                changed = True
                logger.info("[node_command] %s node=%s key=%s value=%s command_id=%s error=%s",
                            row.status, node_id, key, normalized, row.command_id, error)
        if changed:
            db.session.commit()
    except Exception as exc:
        db.session.rollback()
        logger.warning(f"[node_command] ack failed node={node_id} key={key}: {exc}")


def _ack_config_commands_from_payload(node_id: str, payload: dict) -> None:
    if not node_id or not isinstance(payload, dict):
        return
    mode_raw = str(payload.get('report_mode') or '').strip().lower()
    if mode_raw in ('summary', 'full'):
        mode = mode_raw
        node_report_modes[node_id] = mode
        _mark_command_applied(node_id, 'report_mode', mode)
    # upload_points 不能只根据 JSON 顶层字段确认 applied。
    #
    # STM32/ESP32 的 full snapshot 在切换点数时，顶层 upload_points 字段可能仍是旧值，
    # 但 channels[*].waveform 的真实长度已经变化；如果这里直接相信顶层字段，会出现：
    # 1) 4096 命令还没真正下发到 STM32，就被服务器误标记为 applied；
    # 2) 2048/1024 已经按真实波形长度生效，却因为顶层旧值没有被标记 applied。
    # upload_points 的 ACK 统一交给 _ack_upload_points_command()，并优先使用实际波形长度。
    for key in ('downsample_step', 'heartbeat_ms', 'min_interval_ms', 'http_timeout_ms', 'chunk_kb', 'chunk_delay_ms'):
        value = _normalize_command_value(key, payload.get(key))
        if value is not None:
            try:
                if node_id in active_nodes:
                    active_nodes[node_id].setdefault('data', {})
                    active_nodes[node_id]['data'][key] = int(value)
            except Exception:
                pass
            _mark_command_applied(node_id, key, value)




def _rehydrate_active_nodes_from_db(current_time: float | None = None, force: bool = False) -> list[str]:
    """
    Rebuild this worker's in-memory active_nodes from Device.last_heartbeat.

    In gunicorn/eventlet multi-worker deployment, device heartbeats and browser polling
    may hit different workers. active_nodes is local memory, but Device.last_heartbeat
    is shared through the database, so it is the fallback source of online truth.
    """
    global _last_active_db_rehydrate_ts

    now_ts = time.time() if current_time is None else float(current_time)
    if (not force) and (now_ts - _last_active_db_rehydrate_ts < ACTIVE_DB_REHYDRATE_INTERVAL_SEC):
        return []
    _last_active_db_rehydrate_ts = now_ts

    try:
        cutoff = datetime.utcnow() - timedelta(seconds=NODE_TIMEOUT)
        recent_devices = Device.query.filter(
            Device.last_heartbeat != None,  # noqa: E711
            Device.last_heartbeat >= cutoff,
        ).all()
    except Exception as exc:
        logger.warning(f"[active_nodes] DB rehydrate failed: {exc}")
        return []

    added: list[str] = []
    for device in recent_devices:
        node_id = _normalize_node_id(getattr(device, 'device_id', None))
        if not node_id:
            continue

        node_info = active_nodes.get(node_id) or {}
        if node_info and (now_ts - node_info.get('timestamp', 0) <= NODE_TIMEOUT):
            continue

        fault_code = (getattr(device, 'fault_code', None) or 'E00').strip() or 'E00'
        status = getattr(device, 'status', None) or ('faulty' if fault_code != 'E00' else 'online')
        cached = _last_processed_cache.get(node_id)
        data = dict(cached) if isinstance(cached, dict) else {}
        data.update({
            'node_id': node_id,
            'device_id': node_id,
            'status': status,
            'fault_code': fault_code,
            'location': getattr(device, 'location', None),
            'hw_version': getattr(device, 'hw_version', None),
            'report_mode': _get_report_mode(node_id),
        })
        pending_step = node_downsample_commands.get(node_id)
        if pending_step is not None:
            data['downsample_step'] = int(pending_step)
        pending_points = node_upload_points_commands.get(node_id)
        if pending_points is not None:
            data['upload_points'] = int(pending_points)

        if node_id not in active_nodes:
            added.append(node_id)
        active_nodes[node_id] = {
            'timestamp': now_ts,
            'status': status,
            'fault_code': fault_code,
            'data': data,
        }

    if added:
        logger.info(f"[active_nodes] rehydrated from DB: {added}")
    return added
def _parse_downsample_step(value) -> int | None:
    """解析并校验 downsample_step（仅接受 1..64 的整数）。"""
    if value is None or isinstance(value, bool):
        return None

    step = None
    if isinstance(value, int):
        step = value
    elif isinstance(value, float):
        if not value.is_integer():
            return None
        step = int(value)
    else:
        raw = str(value).strip()
        if not raw or not raw.isdigit():
            return None
        step = int(raw)

    if 1 <= step <= 64:
        return step
    return None


def _sync_active_node_downsample_step(node_id: str, step: int) -> None:
    """把当前 downsample_step 同步到 active_nodes 的轻量数据。"""
    if not node_id:
        return
    try:
        if node_id in active_nodes:
            active_nodes[node_id].setdefault('data', {})
            active_nodes[node_id]['data']['downsample_step'] = int(step)
    except Exception:
        pass


def _ack_downsample_command(node_id: str, payload: dict) -> int | None:
    """
    设备上报 downsample_step 时做 ACK：
    - 更新 active_nodes 中的当前值
    - 若与 pending 命令一致则清除 pending
    """
    if not node_id or not isinstance(payload, dict):
        return None

    pending = node_downsample_commands.get(node_id)
    if pending is None:
        pending = _latest_pending_command_value(node_id, 'downsample_step')
        if pending is not None:
            try:
                node_downsample_commands[node_id] = int(pending)
            except Exception:
                pass
    reported_step = _parse_downsample_step(payload.get('downsample_step'))
    if reported_step is None:
        # 设备没上报时，用 pending 目标值回填（避免 UI 每次刷新回退到默认值）。
        if pending is not None:
            _sync_active_node_downsample_step(node_id, int(pending))
        return None

    # 若存在 pending 且设备仍在上报旧值，则 UI 保持显示“目标值”，直到 ACK。
    if pending is not None and int(pending) != reported_step:
        _sync_active_node_downsample_step(node_id, int(pending))
        return reported_step

    _sync_active_node_downsample_step(node_id, reported_step)
    if pending is not None and int(pending) == reported_step:
        node_downsample_commands.pop(node_id, None)
        _mark_command_applied(node_id, 'downsample_step', reported_step)
    return reported_step


def _parse_upload_points(value) -> int | None:
    """解析并校验 upload_points（仅接受 256..4096 且 256 步进的整数）。"""
    if value is None or isinstance(value, bool):
        return None

    points = None
    if isinstance(value, int):
        points = value
    elif isinstance(value, float):
        if not value.is_integer():
            return None
        points = int(value)
    else:
        raw = str(value).strip()
        if not raw or not raw.isdigit():
            return None
        points = int(raw)

    if 256 <= points <= 4096 and (points % 256) == 0:
        return points
    return None


def _sync_active_node_upload_points(node_id: str, points: int) -> None:
    """把当前 upload_points 同步到 active_nodes 的轻量数据。"""
    if not node_id:
        return
    try:
        if node_id in active_nodes:
            active_nodes[node_id].setdefault('data', {})
            active_nodes[node_id]['data']['upload_points'] = int(points)
    except Exception:
        pass


def _actual_upload_points_from_raw_series(raw_series_lens) -> int | None:
    """
    从服务器实际收到的各通道 waveform 长度推断当前上传点数。

    只在所有非零通道长度一致时确认，避免坏帧/半包导致命令被误 ACK。
    """
    wave_lengths: list[int] = []
    for item in raw_series_lens or []:
        try:
            wave_len = int(item[1])
        except Exception:
            continue
        if wave_len > 0:
            wave_lengths.append(wave_len)

    if not wave_lengths:
        return None
    if len(set(wave_lengths)) != 1:
        return None
    return _parse_upload_points(wave_lengths[0])


def _ack_upload_points_command(
    node_id: str,
    payload: dict,
    actual_points: int | None = None,
    allow_payload_fallback: bool = True,
) -> int | None:
    """
    设备上报 upload_points 时做 ACK：
    - 更新 active_nodes 中的当前值/目标值
    - 若与 pending 命令一致则清除 pending
    """
    if not node_id or not isinstance(payload, dict):
        return None

    pending = node_upload_points_commands.get(node_id)
    if pending is None:
        pending = _latest_pending_command_value(node_id, 'upload_points')
        if pending is not None:
            try:
                node_upload_points_commands[node_id] = int(pending)
            except Exception:
                pass
    raw_actual_points = None
    try:
        if actual_points is not None:
            raw_actual_points = int(actual_points)
    except Exception:
        raw_actual_points = None

    reported = _parse_upload_points(actual_points)
    payload_points = _parse_upload_points(payload.get('upload_points'))
    if reported is None and payload_points is not None:
        # Full binary frames can be downsampled before they reach the server, so
        # the raw waveform length may be 64 even when upload_points is 4096.  In
        # that case the device-reported top-level value is the only reliable ACK
        # signal.  Only trust it when it matches the outstanding target, or when
        # the caller explicitly allows payload fallback.
        if allow_payload_fallback or (pending is not None and int(pending) == payload_points):
            reported = payload_points
    if reported is None and raw_actual_points is not None:
        step = _parse_downsample_step(payload.get('downsample_step'))
        if step is not None:
            inferred_points = _parse_upload_points(raw_actual_points * int(step))
            if inferred_points is not None and (pending is None or int(pending) == inferred_points):
                reported = inferred_points
    if reported is None:
        if pending is not None:
            _sync_active_node_upload_points(node_id, int(pending))
        return None

    if pending is not None and int(pending) != reported:
        _sync_active_node_upload_points(node_id, int(pending))
        return reported

    _sync_active_node_upload_points(node_id, reported)
    if pending is not None and int(pending) == reported:
        node_upload_points_commands.pop(node_id, None)
        _mark_command_applied(node_id, 'upload_points', reported)
    return reported


def _device_auth_or_401():
    """
    设备侧接口鉴权（可选）：
    - 若设置了环境变量 EDGEWIND_DEVICE_API_KEY，则要求请求头携带 X-EdgeWind-ApiKey 且匹配。
    - 若未设置，则默认不鉴权（兼容开发/演示环境），并仅在首次请求时打印一次安全提示。

    注意：这里用于设备上报接口（register/upload/heartbeat），不影响管理员登录与页面鉴权。
    """
    expected = (os.environ.get('EDGEWIND_DEVICE_API_KEY') or '').strip()
    if not expected:
        global _device_api_key_warned
        if not _device_api_key_warned:
            logger.warning("安全提示：未设置 EDGEWIND_DEVICE_API_KEY，设备上报接口将不鉴权（仅建议开发环境）。")
            _device_api_key_warned = True
        return None

    provided = (request.headers.get('X-EdgeWind-ApiKey')
                or request.headers.get('X-Device-ApiKey')
                or request.headers.get('X-Device-Key')
                or '').strip()

    if not provided or provided != expected:
        return jsonify({'error': 'Unauthorized'}), 401

    return None


# ==================== 设备注册API ====================

@api_bp.route('/register', methods=['POST'])
def register_device():
    """设备注册API"""
    try:
        auth_resp = _device_auth_or_401()
        if auth_resp:
            return auth_resp

        data = _get_json_payload()
        # 兼容：部分固件可能用 node_id
        device_id = _normalize_node_id(data.get('device_id') or data.get('node_id'))
        # 兼容：location 允许缺省（回退为 device_id），避免注册失败导致后续全链路不可用
        location = data.get('location') or device_id
        hw_version = data.get('hw_version') or data.get('hardware_version') or data.get('fw_version') or 'v1.0'
        
        if not device_id:
            return jsonify({'error': 'Missing device_id'}), 400
        if len(device_id) > 100:
            return jsonify({'error': 'device_id too long (max 100)'}), 400
        logger.info(f"[/api/register] device_id={device_id}, location={location}, hw_version={hw_version}")
        
        # 检查设备是否已存在
        device = Device.query.filter_by(device_id=device_id).first()
        
        if device:
            was_new_in_active = device_id not in (active_nodes or {})
            # 更新现有设备信息
            device.location = location
            device.hw_version = hw_version
            device.status = 'online'
            device.last_heartbeat = datetime.utcnow()
            db.session.commit()
            # 同步 active_nodes（注册也视为一次上线）
            try:
                now_ts = time.time()
                active_nodes[device_id] = {
                    'timestamp': now_ts,
                    'status': 'online',
                    'fault_code': getattr(device, 'fault_code', 'E00') or 'E00',
                    'data': {
                        'node_id': device_id,
                        'status': 'online',
                        'fault_code': getattr(device, 'fault_code', 'E00') or 'E00',
                        'report_mode': _get_report_mode(device_id),
                        'channels': [],
                    }
                }
                ds = _parse_downsample_step(data.get('downsample_step'))
                if ds is not None:
                    active_nodes[device_id]['data']['downsample_step'] = ds
                up = _parse_upload_points(data.get('upload_points'))
                if up is not None:
                    active_nodes[device_id]['data']['upload_points'] = up
            except Exception:
                pass
            # WebSocket：通知前端立即刷新节点列表
            try:
                if socketio_instance:
                    if was_new_in_active:
                        socketio_instance.emit('nodes_changed', {'added': [device_id], 'removed': []}, namespace='/')
                    socketio_instance.emit('node_status_update', {
                        'node_id': device_id,
                        'status': 'online',
                        'fault_code': getattr(device, 'fault_code', 'E00') or 'E00',
                        'timestamp': time.time(),
                        'report_mode': _get_report_mode(device_id),
                        'downsample_step': (active_nodes.get(device_id, {}).get('data', {}) or {}).get('downsample_step'),
                        'upload_points': (active_nodes.get(device_id, {}).get('data', {}) or {}).get('upload_points'),
                        'metrics': {'voltage': 0, 'voltage_neg': 0, 'current': 0, 'leakage': 0}
                    }, namespace='/')
            except Exception:
                pass
            response_payload = {
                'message': 'Device updated',
                'device_id': device_id,
                'report_mode': _get_report_mode(device_id),
            }
            _append_pending_config_commands(device_id, response_payload, mark_delivered=True)
            return jsonify(response_payload), 200
        else:
            # 创建新设备
            device = Device(
                device_id=device_id,
                location=location,
                hw_version=hw_version,
                status='online',
                last_heartbeat=datetime.utcnow()
            )
            db.session.add(device)
            db.session.commit()
            # 同步 active_nodes（注册也视为一次上线）
            try:
                now_ts = time.time()
                active_nodes[device_id] = {
                    'timestamp': now_ts,
                    'status': 'online',
                    'fault_code': 'E00',
                    'data': {
                        'node_id': device_id,
                        'status': 'online',
                        'fault_code': 'E00',
                        'report_mode': _get_report_mode(device_id),
                        'channels': [],
                    }
                }
                ds = _parse_downsample_step(data.get('downsample_step'))
                if ds is not None:
                    active_nodes[device_id]['data']['downsample_step'] = ds
                up = _parse_upload_points(data.get('upload_points'))
                if up is not None:
                    active_nodes[device_id]['data']['upload_points'] = up
            except Exception:
                pass
            # WebSocket：通知前端立即刷新节点列表
            try:
                if socketio_instance:
                    socketio_instance.emit('nodes_changed', {'added': [device_id], 'removed': []}, namespace='/')
                    socketio_instance.emit('node_status_update', {
                        'node_id': device_id,
                        'status': 'online',
                        'fault_code': 'E00',
                        'timestamp': time.time(),
                        'report_mode': _get_report_mode(device_id),
                        'downsample_step': (active_nodes.get(device_id, {}).get('data', {}) or {}).get('downsample_step'),
                        'upload_points': (active_nodes.get(device_id, {}).get('data', {}) or {}).get('upload_points'),
                        'metrics': {'voltage': 0, 'voltage_neg': 0, 'current': 0, 'leakage': 0}
                    }, namespace='/')
            except Exception:
                pass
            response_payload = {
                'message': 'Device registered successfully',
                'device_id': device_id,
                'report_mode': _get_report_mode(device_id),
            }
            _append_pending_config_commands(device_id, response_payload, mark_delivered=True)
            return jsonify(response_payload), 201
            
    except Exception as e:
        db.session.rollback()
        return jsonify({'error': str(e)}), 500


# ==================== 兼容接口：波形上传（旧模拟器 /api/upload）====================
@api_bp.route('/upload', methods=['POST'])
def upload_data():
    """
    兼容旧版模拟器/硬件上报接口：/api/upload

    sim.py 当前会向该接口发送：
    - device_id
    - status（normal / fault）
    - fault_code（E00-E05）
    - waveform（1024点数组）
    """
    try:
        auth_resp = _device_auth_or_401()
        if auth_resp:
            return auth_resp

        data = _get_json_payload()
        device_id = _normalize_node_id(data.get('device_id'))
        status = data.get('status', 'normal')
        fault_code = data.get('fault_code', 'E00')
        waveform = data.get('waveform')

        if not device_id:
            return jsonify({'error': 'Missing device_id'}), 400
        if len(device_id) > 100:
            return jsonify({'error': 'device_id too long (max 100)'}), 400

        # 1) 确保设备存在
        device = Device.query.filter_by(device_id=device_id).first()
        if not device:
            device = Device(
                device_id=device_id,
                location=data.get('location', device_id),
                status='online',
                fault_code='E00',
                last_heartbeat=datetime.utcnow()
            )
            db.session.add(device)
            db.session.flush()

        # 2) 更新设备状态
        # 兼容 sim.py：status=normal/fault
        device.status = 'faulty' if status in ['fault', 'faulty'] or fault_code != 'E00' else 'online'
        device.fault_code = fault_code or 'E00'
        device.last_heartbeat = datetime.utcnow()

        # 2.1) 同步更新 active_nodes（统一“在线判定”口径）
        # 说明：
        # - 新版模拟器会调用 /api/node/heartbeat 更新 active_nodes
        # - 旧版/兼容链路会调用 /api/upload（仅更新数据库）
        # 为避免“统计显示在线=0，但卡片仍显示在线/有数据”的口径不一致，这里把 /api/upload 也视为一次心跳
        current_timestamp = time.time()
        active_nodes[device_id] = {
            'timestamp': current_timestamp,
            'status': 'faulty' if fault_code != 'E00' else 'online',
            'fault_code': fault_code or 'E00',
            'data': {
                **(data or {}),
                'node_id': device_id,
                'device_id': device_id,
                'location': device.location,
                'report_mode': _get_report_mode(device_id),
            }
        }
        reported_downsample_step = _ack_downsample_command(device_id, data)
        if reported_downsample_step is None:
            pending_step = node_downsample_commands.get(device_id)
            if pending_step is not None:
                _sync_active_node_downsample_step(device_id, int(pending_step))
        reported_upload_points = _ack_upload_points_command(device_id, data)
        if reported_upload_points is None:
            pending_points = node_upload_points_commands.get(device_id)
            if pending_points is not None:
                _sync_active_node_upload_points(device_id, int(pending_points))
        _ack_config_commands_from_payload(device_id, data)

        # 3) 保存波形数据点（用于历史趋势/后续分析）
        # 性能说明：多节点高频上报时，频繁落库会显著拖慢响应。
        # 可通过环境变量关闭：EDGEWIND_STORE_UPLOAD_DATAPOINTS=false
        store_upload_datapoints = str(os.environ.get("EDGEWIND_STORE_UPLOAD_DATAPOINTS", "true")).strip().lower() == "true"
        if store_upload_datapoints and waveform is not None:
            datapoint = DataPoint(
                device_id=device_id,
                waveform=json.dumps(waveform),
                status='fault' if fault_code != 'E00' else 'normal',
                fault_code=fault_code,
                timestamp=datetime.utcnow()
            )
            db.session.add(datapoint)

        # 4) 故障事件入库：/api/upload 也要遵循“故障事件(E00->E0X)创建工单”的口径
        # 说明：部分模拟器/旧链路仍使用 /api/upload 注入故障；如果这里只落库 datapoint 而不按事件建单，
        # 会导致“故障快照有，但故障管理/系统故障日志没有”的错觉。
        prev_fault = node_fault_states.get(device_id, 'E00')
        curr_fault = fault_code or 'E00'

        if prev_fault == 'E00' and curr_fault != 'E00':
            # 2秒内去重（防止网络重发/并发导致同秒两条）
            now_utc = datetime.utcnow()
            window_start = now_utc - timedelta(seconds=2)

            fault_info = FAULT_KNOWLEDGE_GRAPH.get(curr_fault) or FAULT_KNOWLEDGE_GRAPH.get(FAULT_CODE_MAP.get(curr_fault, '') or '')
            expected_fault_name = (fault_info or {}).get('name')

            recent = WorkOrder.query.filter(
                WorkOrder.device_id == device_id,
                WorkOrder.fault_time != None,
                WorkOrder.fault_time >= window_start
            ).order_by(WorkOrder.fault_time.desc()).first()

            if not (recent and expected_fault_name and (recent.fault_type == expected_fault_name or expected_fault_name in (recent.fault_type or ''))):
                create_work_order_from_fault(db, device_id, curr_fault, device.location, fault_time=now_utc)

        # 更新故障状态机（用于事件判定）
        node_fault_states[device_id] = curr_fault

        db.session.commit()

        # 4.1) WebSocket：/api/upload 也推送全局状态更新（保证实时监测“系统故障日志”能更新）
        try:
            if socketio_instance:
                socketio_instance.emit('node_status_update', {
                    'node_id': device_id,
                    'status': 'faulty' if curr_fault != 'E00' else 'online',
                    'fault_code': curr_fault,
                    'timestamp': current_timestamp,
                    'report_mode': _get_report_mode(device_id),
                    'downsample_step': (active_nodes.get(device_id, {}).get('data', {}) or {}).get('downsample_step'),
                    'upload_points': (active_nodes.get(device_id, {}).get('data', {}) or {}).get('upload_points'),
                    'heartbeat_ms': (active_nodes.get(device_id, {}).get('data', {}) or {}).get('heartbeat_ms'),
                    'min_interval_ms': (active_nodes.get(device_id, {}).get('data', {}) or {}).get('min_interval_ms'),
                    'http_timeout_ms': (active_nodes.get(device_id, {}).get('data', {}) or {}).get('http_timeout_ms'),
                    'chunk_kb': (active_nodes.get(device_id, {}).get('data', {}) or {}).get('chunk_kb'),
                    'chunk_delay_ms': (active_nodes.get(device_id, {}).get('data', {}) or {}).get('chunk_delay_ms'),
                    'metrics': {
                        'voltage': float((data or {}).get('voltage', 0) or 0),
                        'voltage_neg': float((data or {}).get('voltage_neg', 0) or 0),
                        'current': float((data or {}).get('current', 0) or 0),
                        'leakage': float((data or {}).get('leakage', 0) or 0)
                    }
                }, namespace='/')
        except Exception:
            # 推送失败不影响接口返回
            pass

        # 5) 命令下发（维修完成 -> reset）
        # 重要：不要 pop！否则设备若“没及时解析响应”，命令会丢失。
        # 策略：命令会在设备上报 fault_code=E00 后自动清除（视为已执行）。
        resp = {'success': True}
        cmd = node_commands.get(device_id)
        if cmd:
            resp['command'] = cmd
            # ack：设备已恢复正常，则认为 reset 已执行
            if cmd == 'reset' and (fault_code == 'E00'):
                node_commands.pop(device_id, None)
        # Do not echo normal report_mode as a command.  Pending config commands
        # are appended below with command_id; ESP32 treats command_id-bearing
        # fields as actionable server commands.
        pending_step = node_downsample_commands.get(device_id)
        if pending_step is not None:
            resp['downsample_step'] = int(pending_step)
        pending_points = node_upload_points_commands.get(device_id)
        if pending_points is not None:
            resp['upload_points'] = int(pending_points)
        _append_pending_config_commands(device_id, resp, mark_delivered=True)
        return jsonify(resp), 200

    except Exception as e:
        db.session.rollback()
        logger.error(f"/api/upload 处理失败: {e}")
        import traceback
        logger.error(traceback.format_exc())
        return jsonify({'error': str(e)}), 500


# ==================== 节点心跳API ====================

def _process_node_report(data: dict,
                         *,
                         request_tag: str,
                         decode_label: str,
                         decode_ms: float,
                         content_length: int | None,
                         response_extra: dict | None = None,
                         perf_extra: dict | None = None):
    """????????????? JSON heartbeat ? binary full frame?"""
    if not isinstance(data, dict):
        data = {}

    t_parse_start = time.perf_counter()
    node_id = _normalize_node_id(data.get('node_id') or data.get('device_id'))
    fault_code = (data.get('fault_code') or 'E00').strip() or 'E00'

    if not node_id:
        return jsonify({'error': 'Missing node_id'}), 400
    if len(node_id) > 100:
        return jsonify({'error': 'node_id too long (max 100)'}), 400

    incoming_report_mode = str(data.get('report_mode') or '').strip().lower()
    if incoming_report_mode not in ('summary', 'full'):
        incoming_report_mode = ''
    effective_report_mode = incoming_report_mode or _get_report_mode(node_id)
    if effective_report_mode:
        data['report_mode'] = effective_report_mode

    current_timestamp = time.time()
    last = _last_hb_log_ts.get(f'{request_tag}:{node_id}', 0)
    if current_timestamp - last >= 5:
        _last_hb_log_ts[f'{request_tag}:{node_id}'] = current_timestamp
        logger.info(f'[{request_tag}] node_id={node_id} fault={fault_code} ch={len(data.get("channels") or [])}')

    if LIGHT_ACTIVE_NODES:
        data_light = dict(data)
        if 'channels' in data_light:
            data_light['channels'] = _lighten_channels(data_light.get('channels'))
        data_light['report_mode'] = effective_report_mode or _get_report_mode(node_id)
        active_nodes[node_id] = {
            'timestamp': current_timestamp,
            'status': data.get('status', 'offline'),
            'fault_code': fault_code,
            'data': data_light,
        }
    else:
        data['report_mode'] = effective_report_mode or _get_report_mode(node_id)
        active_nodes[node_id] = {
            'timestamp': current_timestamp,
            'status': data.get('status', 'offline'),
            'fault_code': fault_code,
            'data': data,
        }

    reported_downsample_step = _ack_downsample_command(node_id, data)
    if reported_downsample_step is None:
        pending_step = node_downsample_commands.get(node_id)
        if pending_step is not None:
            _sync_active_node_downsample_step(node_id, int(pending_step))
    _ack_config_commands_from_payload(node_id, data)
    try:
        if node_id in active_nodes:
            active_nodes[node_id].setdefault('data', {})
            active_nodes[node_id]['data']['report_mode'] = _get_report_mode(node_id)
    except Exception:
        pass

    processed_data = {
        'voltage': 0, 'voltage_neg': 0, 'current': 0, 'leakage': 0,
        'voltage_waveform': [], 'voltage_spectrum': [],
        'voltage_neg_waveform': [], 'voltage_neg_spectrum': [],
        'current_waveform': [], 'current_spectrum': [],
        'leakage_waveform': [], 'leakage_spectrum': [],
        'channels': [],
    }

    raw_channels = data.get('channels') or []
    if not isinstance(raw_channels, list):
        raw_channels = []
    bad_wave_type = 0
    bad_spec_type = 0
    bad_id_type = 0
    bad_val = 0
    raw_series_lens = []
    processed_channels = []
    for ch in raw_channels:
        if not isinstance(ch, dict):
            continue
        ch_id = ch.get('id')
        if ch_id is None:
            ch_id = ch.get('channel_id')
        if isinstance(ch_id, str):
            try:
                ch_id = int(ch_id)
            except Exception:
                pass
        label = (ch.get('label') or '').strip()
        val = ch.get('value', ch.get('current_value', 0))
        wave = ch.get('waveform', [])
        spec = ch.get('fft_spectrum', ch.get('fft', []))

        if (ch_id is not None) and (not isinstance(ch_id, int)):
            bad_id_type += 1
        if not isinstance(wave, list):
            bad_wave_type += 1
            wave = []
        if not isinstance(spec, list):
            bad_spec_type += 1
            spec = []

        raw_wave_count = ch.get('waveform_count_raw', len(wave))
        raw_fft_count = ch.get('fft_count_raw', len(spec))
        try:
            raw_wave_count = int(raw_wave_count)
        except Exception:
            raw_wave_count = len(wave)
        try:
            raw_fft_count = int(raw_fft_count)
        except Exception:
            raw_fft_count = len(spec)
        raw_series_lens.append((ch_id, raw_wave_count, raw_fft_count))

        wave = _downsample_list(wave, MONITOR_WAVEFORM_POINTS)
        spec = _downsample_list(spec, MONITOR_SPECTRUM_POINTS)

        try:
            val_float = float(val) if val is not None else 0.0
        except Exception:
            val_float = 0.0
            bad_val += 1

        processed_channels.append({
            'id': ch_id,
            'channel_id': ch_id,
            'label': label,
            'name': ch.get('name', label),
            'unit': ch.get('unit', ''),
            'type': ch.get('type', ''),
            'range': ch.get('range', ''),
            'color': ch.get('color', ''),
            'value': val_float,
            'currentValue': val_float,
            'current_value': val_float,
            'waveform_count_raw': raw_wave_count,
            'fft_count_raw': raw_fft_count,
            'waveform': wave,
            'fft_spectrum': spec,
        })

        mapped = False
        if '??' in label:
            if ('-' in label) or ('?' in label):
                processed_data['voltage_neg'] = val_float
                processed_data['voltage_neg_waveform'] = wave
                processed_data['voltage_neg_spectrum'] = spec
            else:
                processed_data['voltage'] = val_float
                processed_data['voltage_waveform'] = wave
                processed_data['voltage_spectrum'] = spec
            mapped = True
        elif '?' in label:
            processed_data['leakage'] = val_float
            processed_data['leakage_waveform'] = wave
            processed_data['leakage_spectrum'] = spec
            mapped = True
        elif ('??' in label or '??' in label) and '?' not in label:
            processed_data['current'] = val_float
            processed_data['current_waveform'] = wave
            processed_data['current_spectrum'] = spec
            mapped = True

        if (not mapped) and isinstance(ch_id, int):
            if ch_id == 0:
                processed_data['voltage'] = val_float
                processed_data['voltage_waveform'] = wave
                processed_data['voltage_spectrum'] = spec
            elif ch_id == 1:
                processed_data['voltage_neg'] = val_float
                processed_data['voltage_neg_waveform'] = wave
                processed_data['voltage_neg_spectrum'] = spec
            elif ch_id == 2:
                processed_data['current'] = val_float
                processed_data['current_waveform'] = wave
                processed_data['current_spectrum'] = spec
            elif ch_id == 3:
                processed_data['leakage'] = val_float
                processed_data['leakage_waveform'] = wave
                processed_data['leakage_spectrum'] = spec

    processed_data['channels'] = processed_channels
    processed_data['waveform_points_raw_max'] = max((wave_len for _, wave_len, _ in raw_series_lens), default=0)
    processed_data['spectrum_points_raw_max'] = max((fft_len for _, _, fft_len in raw_series_lens), default=0)
    processed_data['waveform_points_emit_max'] = max((len(ch.get('waveform') or []) for ch in processed_channels), default=0)
    processed_data['spectrum_points_emit_max'] = max((len(ch.get('fft_spectrum') or []) for ch in processed_channels), default=0)

    actual_upload_points = _actual_upload_points_from_raw_series(raw_series_lens)
    reported_upload_points = _ack_upload_points_command(
        node_id,
        data,
        actual_points=actual_upload_points,
        allow_payload_fallback=(len(raw_channels) == 0),
    )
    if reported_upload_points is not None:
        data['upload_points_actual'] = int(reported_upload_points)
        top_points = _parse_upload_points(data.get('upload_points'))
        if top_points is not None and top_points != reported_upload_points:
            data['upload_points_top'] = top_points
        data['upload_points'] = int(reported_upload_points)
    else:
        pending_points = node_upload_points_commands.get(node_id)
        if pending_points is not None:
            _sync_active_node_upload_points(node_id, int(pending_points))

    t_parse = time.perf_counter()

    prev = _last_processed_cache.get(node_id)
    series_keys = (
        'voltage_waveform', 'voltage_spectrum',
        'voltage_neg_waveform', 'voltage_neg_spectrum',
        'current_waveform', 'current_spectrum',
        'leakage_waveform', 'leakage_spectrum',
    )
    has_any_series = any(isinstance(processed_data.get(k), list) and len(processed_data.get(k)) > 0 for k in series_keys)
    if has_any_series:
        last_series = _last_series_log_ts.get(f'{request_tag}:{node_id}', 0)
        if current_timestamp - last_series >= 2:
            _last_series_log_ts[f'{request_tag}:{node_id}'] = current_timestamp
            series_lens = [
                (
                    ch.get('id'),
                    len(ch.get('waveform') or []),
                    len(ch.get('fft_spectrum') or []),
                )
                for ch in processed_channels
                if isinstance(ch, dict)
            ]
            logger.info('[%s][series] node_id=%s mode=%s step=%s upload_points=%s ch=%d raw_lens=%s emit_lens=%s',
                        request_tag,
                        node_id,
                        data.get('report_mode'),
                        data.get('downsample_step'),
                        data.get('upload_points'),
                        len(processed_channels),
                        raw_series_lens,
                        series_lens)
    metrics_all_zero = (
        float(processed_data.get('voltage') or 0) == 0.0 and
        float(processed_data.get('voltage_neg') or 0) == 0.0 and
        float(processed_data.get('current') or 0) == 0.0 and
        float(processed_data.get('leakage') or 0) == 0.0
    )
    # ESP32 在 full 上传失败/holdoff 期间会发送 ch=0 的轻量 heartbeat。
    # 这类包是合法保活，不应覆盖 active_nodes 中最后一帧 full 波形，也不应被记为坏帧。
    is_empty_keepalive = (request_tag == '/api/node/heartbeat' and len(raw_channels) == 0)
    is_bad_frame = ((len(raw_channels) == 0 and not is_empty_keepalive) or
                    ((not has_any_series) and metrics_all_zero and not is_empty_keepalive) or
                    (bad_wave_type > 0) or
                    (bad_spec_type > 0) or
                    (bad_id_type > 0))

    if isinstance(prev, dict) and (is_bad_frame or is_empty_keepalive):
        processed_data = prev
        if is_bad_frame:
            last_bad = _last_bad_frame_log_ts.get(f'{request_tag}:{node_id}', 0)
            if current_timestamp - last_bad >= 5:
                _last_bad_frame_log_ts[f'{request_tag}:{node_id}'] = current_timestamp
                logger.warning(
                    '[%s][bad-frame] node_id=%s content_length=%s ch=%d bad_id=%d bad_wave=%d bad_spec=%d bad_val=%d',
                    request_tag,
                    node_id,
                    content_length,
                    len(raw_channels),
                    bad_id_type,
                    bad_wave_type,
                    bad_spec_type,
                    bad_val,
                )
    else:
        if isinstance(prev, dict) and not has_any_series:
            for key in series_keys:
                prev_series = prev.get(key)
                if isinstance(prev_series, list) and prev_series:
                    processed_data[key] = prev_series

            prev_channels = prev.get('channels') or []
            if isinstance(prev_channels, list) and processed_channels:
                prev_by_id = {
                    ch.get('id'): ch for ch in prev_channels
                    if isinstance(ch, dict) and ch.get('id') is not None
                }
                for ch in processed_channels:
                    if not isinstance(ch, dict):
                        continue
                    prev_ch = prev_by_id.get(ch.get('id'))
                    if not isinstance(prev_ch, dict):
                        continue
                    if not ch.get('waveform') and isinstance(prev_ch.get('waveform'), list):
                        ch['waveform'] = prev_ch['waveform']
                    if not ch.get('fft_spectrum') and isinstance(prev_ch.get('fft_spectrum'), list):
                        ch['fft_spectrum'] = prev_ch['fft_spectrum']
                processed_data['channels'] = processed_channels

        _last_processed_cache[node_id] = processed_data
        _submit_history_data(node_id, processed_data)

    active_data = dict(data)
    if isinstance(processed_data, dict):
        active_data.update(processed_data)
    active_data['node_id'] = node_id
    active_data['status'] = data.get('status', 'online')
    active_data['fault_code'] = fault_code
    active_data['report_mode'] = _get_report_mode(node_id)
    active_data['downsample_step'] = (data.get('downsample_step') if data.get('downsample_step') is not None
                                      else active_data.get('downsample_step'))
    active_data['upload_points'] = (data.get('upload_points') if data.get('upload_points') is not None
                                    else active_data.get('upload_points'))
    for key in ('heartbeat_ms', 'min_interval_ms', 'http_timeout_ms', 'chunk_kb', 'chunk_delay_ms'):
        if data.get(key) is not None:
            active_data[key] = data.get(key)
    active_nodes[node_id] = {
        'timestamp': current_timestamp,
        'status': data.get('status', 'online'),
        'fault_code': fault_code,
        'data': active_data,
    }

    if fault_code == 'E00':
        save_to_buffer(node_id, data, is_fault=False)
    else:
        save_to_buffer(node_id, data, is_fault=True)

    previous_fault = node_fault_states.get(node_id, 'E00')
    current_fault = fault_code
    db_op_submitted = False

    if previous_fault == 'E00' and current_fault != 'E00':
        logger.info(f'?? ???????: {node_id} -> {current_fault}')

        if node_id not in node_snapshot_saved or node_snapshot_saved[node_id].get('fault_code') != current_fault:
            before_data = get_latest_normal_data(node_id)
            if before_data:
                db_executor.submit(save_fault_snapshot, db, app_instance, node_id, current_fault, 'before', before_data['data'])

            db_executor.submit(save_fault_snapshot, db, app_instance, node_id, current_fault, 'after', data)

            node_snapshot_saved[node_id] = {
                'fault_code': current_fault,
                'saved_types': ['before', 'after']
            }

        if db_executor:
            db_executor.submit(_handle_fault_database_operation, node_id, current_fault, data)
            db_op_submitted = True

    elif previous_fault != 'E00' and current_fault == 'E00':
        logger.info(f'?? ???????: {node_id} {previous_fault} -> E00')

        fault_data = get_latest_fault_data(node_id)
        if fault_data:
            db_executor.submit(save_fault_snapshot, db, app_instance, node_id, previous_fault, 'before_recovery', fault_data['data'])

        db_executor.submit(save_fault_snapshot, db, app_instance, node_id, previous_fault, 'after_recovery', data)

        if node_id in node_snapshot_saved:
            del node_snapshot_saved[node_id]

    node_fault_states[node_id] = current_fault

    status_payload = None
    monitor_payload = None
    if _should_emit(node_id, current_timestamp, STATUS_EMIT_HZ, _last_emit_status_ts):
        status_payload = {
            'node_id': node_id,
            'status': data.get('status', 'online'),
            'fault_code': fault_code,
            'timestamp': current_timestamp,
            'report_mode': _get_report_mode(node_id),
            'downsample_step': (active_nodes.get(node_id, {}).get('data', {}) or {}).get('downsample_step'),
            'upload_points': (active_nodes.get(node_id, {}).get('data', {}) or {}).get('upload_points'),
            'heartbeat_ms': (active_nodes.get(node_id, {}).get('data', {}) or {}).get('heartbeat_ms'),
            'min_interval_ms': (active_nodes.get(node_id, {}).get('data', {}) or {}).get('min_interval_ms'),
            'http_timeout_ms': (active_nodes.get(node_id, {}).get('data', {}) or {}).get('http_timeout_ms'),
            'chunk_kb': (active_nodes.get(node_id, {}).get('data', {}) or {}).get('chunk_kb'),
            'chunk_delay_ms': (active_nodes.get(node_id, {}).get('data', {}) or {}).get('chunk_delay_ms'),
            'metrics': {
                'voltage': processed_data.get('voltage', 0),
                'voltage_neg': processed_data.get('voltage_neg', 0),
                'current': processed_data.get('current', 0),
                'leakage': processed_data.get('leakage', 0),
            }
        }

    if _should_emit(node_id, current_timestamp, MONITOR_EMIT_HZ, _last_emit_monitor_ts):
        monitor_payload = {
            'node_id': node_id,
            'data': processed_data,
            'fault_code': fault_code,
        }
    if socketio_instance and (status_payload is not None or monitor_payload is not None):
        try:
            socketio_instance.start_background_task(
                _emit_socket_updates_async,
                status_payload,
                monitor_payload,
                f'node_{node_id}',
            )
        except Exception as exc:
            logger.warning(f'[SocketIO] ??????????: {exc}')

    t_emit = time.perf_counter()

    if db_executor and (not db_op_submitted) and previous_fault == 'E00' and current_fault != 'E00':
        db_executor.submit(_handle_fault_database_operation, node_id, current_fault, data)

    response_payload = {
        'success': True,
        'node_id': node_id,
        'timestamp': current_timestamp,
    }
    if response_extra:
        response_payload.update(response_extra)

    cmd = node_commands.get(node_id)
    if cmd:
        response_payload['command'] = cmd
        if cmd == 'reset' and fault_code == 'E00':
            node_commands.pop(node_id, None)
    pending_step = node_downsample_commands.get(node_id)
    if pending_step is not None:
        response_payload['downsample_step'] = int(pending_step)
    pending_points = node_upload_points_commands.get(node_id)
    if pending_points is not None:
        response_payload['upload_points'] = int(pending_points)
    _append_pending_config_commands(node_id, response_payload, mark_delivered=True)

    last_db = _last_db_heartbeat_ts.get(node_id, 0)
    if current_timestamp - last_db >= DEVICE_DB_UPDATE_INTERVAL_SEC:
        _last_db_heartbeat_ts[node_id] = current_timestamp
        _submit_update_device_heartbeat(node_id, data, fault_code, current_timestamp)

    parse_ms = (t_parse - t_parse_start) * 1000.0
    emit_ms = (t_emit - t_parse) * 1000.0
    total_ms = decode_ms + parse_ms + emit_ms
    raw_waveform_max = max((wave_len for _, wave_len, _ in raw_series_lens), default=0)
    raw_spectrum_max = max((fft_len for _, _, fft_len in raw_series_lens), default=0)
    rx_bytes = None
    if isinstance(perf_extra, dict):
        try:
            raw_waveform_max = max(raw_waveform_max, int(perf_extra.get('waveform_max') or 0))
            raw_spectrum_max = max(raw_spectrum_max, int(perf_extra.get('fft_max') or 0))
        except Exception:
            pass
        rx_bytes = perf_extra.get('rx_bytes')

    if total_ms >= HB_SLOW_MS:
        perf_key = f'{request_tag}:{node_id}'
        lastp = _last_perf_log_ts.get(perf_key, 0)
        if current_timestamp - lastp >= HB_PERF_LOG_SEC:
            _last_perf_log_ts[perf_key] = current_timestamp
            logger.warning(
                '[%s][perf] node_id=%s total=%.1fms %s=%.1fms parse=%.1fms emit=%.1fms ch=%d rx=%s waveRaw=%d specRaw=%d waveEmit=%d specEmit=%d',
                request_tag,
                node_id,
                total_ms,
                decode_label,
                decode_ms,
                parse_ms,
                emit_ms,
                len(raw_channels) if isinstance(raw_channels, list) else 0,
                rx_bytes if rx_bytes is not None else '-',
                raw_waveform_max,
                raw_spectrum_max,
                MONITOR_WAVEFORM_POINTS,
                MONITOR_SPECTRUM_POINTS,
            )

    return jsonify(response_payload), 200


@api_bp.route('/node/heartbeat', methods=['POST'])
def node_heartbeat():
    """?????? - ?? STM32/ESP32 JSON ???"""
    try:
        t0 = time.perf_counter()
        auth_resp = _device_auth_or_401()
        if auth_resp:
            return auth_resp

        data = _get_json_payload()
        decode_ms = (time.perf_counter() - t0) * 1000.0
        return _process_node_report(
            data,
            request_tag='/api/node/heartbeat',
            decode_label='json',
            decode_ms=decode_ms,
            content_length=request.content_length,
        )
    except Exception as exc:
        db.session.rollback()
        logger.error(f'? Heartbeat failed: {exc}')
        import traceback
        logger.error(traceback.format_exc())
        return jsonify({'error': str(exc)}), 500


@api_bp.route('/node/full_frame_bin', methods=['POST'])
def node_full_frame_bin():
    """????? full frame ?????"""
    try:
        t0 = time.perf_counter()
        auth_resp = _device_auth_or_401()
        if auth_resp:
            return auth_resp

        proto = (request.headers.get('X-EdgeWind-Proto') or '').strip().lower()
        if proto and proto not in {'ewfull/1', 'ewfull/2'}:
            logger.warning('[/api/node/full_frame_bin][bad] reason=bad_proto proto=%s len=%s', proto, request.content_length)
            return jsonify({'error': f'Unsupported proto: {proto}'}), 400

        body = request.get_data(cache=True) or b''
        if not body:
            logger.warning('[/api/node/full_frame_bin][bad] reason=empty_body len=%s', request.content_length)
            return jsonify({'error': 'empty body'}), 400

        decoded = decode_full_frame_binary(
            body,
            waveform_limit=MONITOR_WAVEFORM_POINTS,
            fft_limit=MONITOR_SPECTRUM_POINTS,
        )
        decode_ms = (time.perf_counter() - t0) * 1000.0
        response_extra = {
            'seq': decoded.payload.get('seq'),
            'report_mode': decoded.payload.get('report_mode'),
            'downsample_step': decoded.payload.get('downsample_step'),
            'upload_points': decoded.payload.get('upload_points'),
            'rx_bytes': decoded.rx_bytes,
        }
        return _process_node_report(
            decoded.payload,
            request_tag='/api/node/full_frame_bin',
            decode_label='binary',
            decode_ms=decode_ms,
            content_length=len(body),
            response_extra=response_extra,
            perf_extra={
                'rx_bytes': decoded.rx_bytes,
                'waveform_max': decoded.waveform_max,
                'fft_max': decoded.fft_max,
            },
        )
    except FullFrameBinaryError as exc:
        db.session.rollback()
        logger.warning('[/api/node/full_frame_bin][bad] reason=%s len=%s', exc, request.content_length)
        return jsonify({'error': str(exc)}), 400
    except Exception as exc:
        db.session.rollback()
        logger.error(f'? Full frame binary failed: {exc}')
        import traceback
        logger.error(traceback.format_exc())
        return jsonify({'error': str(exc)}), 500


def _handle_fault_database_operation(node_id, fault_code, data):
    """后台处理故障数据库操作"""
    with app_instance.app_context():
        try:
            # Ensure device exists
            device = Device.query.filter_by(device_id=node_id).first()
            if not device:
                device = Device(device_id=node_id, location=data.get('location', 'N/A'), status='faulty')
                db.session.add(device)
                db.session.commit()
            
            # Create work order（按“故障事件”创建，而不是按“设备是否已有未关闭工单”去重）
            # 说明：
            # - 故障管理页的“故障日志”本质上是故障事件流；同一设备可能多次发生同一故障。
            # - 之前按 pending/processing 去重，会导致“快照有，但故障管理没新增记录”的错觉。
            # - 本函数只在 E00 -> E0X 事件发生时被调用（上层已做状态机判定），因此不会因高频心跳产生海量重复。
            # - 关键：同一秒内可能因为并发/重复提交导致建单两次，这里做 2 秒窗口去重。
            now_utc = datetime.utcnow()
            window_start = now_utc - timedelta(seconds=2)

            # 计算“当前故障事件”的标准故障名，用于更精确去重
            fault_info = FAULT_KNOWLEDGE_GRAPH.get(fault_code) or FAULT_KNOWLEDGE_GRAPH.get(FAULT_CODE_MAP.get(fault_code, '') or '')
            expected_fault_name = (fault_info or {}).get('name')

            recent = WorkOrder.query.filter(
                WorkOrder.device_id == node_id,
                WorkOrder.fault_time != None,
                WorkOrder.fault_time >= window_start
            ).order_by(WorkOrder.fault_time.desc()).first()

            if recent and expected_fault_name and (recent.fault_type == expected_fault_name or expected_fault_name in (recent.fault_type or '')):
                logger.info(f"⏭️ 跳过去重：{node_id} 在2秒内已创建同类故障工单 ({fault_code})")
                return

            create_work_order_from_fault(db, node_id, fault_code, device.location, fault_time=now_utc)
            db.session.commit()
            logger.info(f"✅ WorkOrder已创建: {node_id}")
        except Exception as e:
            db.session.rollback()
            logger.error(f"❌ 数据库操作失败: {node_id} - {str(e)}")


# ==================== 节点管理API ====================

@api_bp.route('/get_active_nodes', methods=['GET'])
@login_required
def get_active_nodes():
    """获取活动节点列表"""
    try:
        current_time = time.time()
        include_series = str(request.args.get('include_series', '')).strip().lower() in ('1', 'true', 'yes', 'on')
        target_node_id = _normalize_node_id(request.args.get('node_id') or request.args.get('device_id'))
        rehydrated_nodes = _rehydrate_active_nodes_from_db(current_time)
        expired_nodes = []

        if rehydrated_nodes:
            try:
                if socketio_instance:
                    socketio_instance.emit('nodes_changed', {'added': list(rehydrated_nodes), 'removed': []}, namespace='/')
            except Exception:
                pass
        
        # 清理超时节点
        for node_id, node_info in list(active_nodes.items()):
            if current_time - node_info['timestamp'] > NODE_TIMEOUT:
                expired_nodes.append(node_id)
                del active_nodes[node_id]

        # 若有注销节点：通过 WebSocket 推送（让前端立即刷新，而不是等下次轮询）
        if expired_nodes:
            try:
                if socketio_instance:
                    socketio_instance.emit('nodes_changed', {'added': [], 'removed': list(expired_nodes)}, namespace='/')
                    for nid in expired_nodes:
                        socketio_instance.emit('node_status_update', {
                            'node_id': nid,
                            'status': 'offline',
                            'fault_code': 'E00',
                            'timestamp': current_time,
                        }, namespace='/')
            except Exception:
                pass
        
        # 返回活动节点
        active_nodes_list = []
        for node_id, node_info in active_nodes.items():
            if target_node_id and node_id != target_node_id:
                continue
            node_data = node_info['data'].copy()
            node_data['node_id'] = node_id
            node_data['report_mode'] = _get_report_mode(node_id)
            parsed_step = _parse_downsample_step(node_data.get('downsample_step'))
            if parsed_step is not None:
                node_data['downsample_step'] = parsed_step
            if 'downsample_step' not in node_data:
                pending_step = node_downsample_commands.get(node_id)
                if pending_step is not None:
                    node_data['downsample_step'] = int(pending_step)
            parsed_points = _parse_upload_points(node_data.get('upload_points'))
            if parsed_points is not None:
                node_data['upload_points'] = parsed_points
            if 'upload_points' not in node_data:
                pending_points = node_upload_points_commands.get(node_id)
                if pending_points is not None:
                    node_data['upload_points'] = int(pending_points)
            if include_series:
                cached = _last_processed_cache.get(node_id)
                if isinstance(cached, dict):
                    for key in (
                        'voltage_waveform', 'voltage_spectrum',
                        'voltage_neg_waveform', 'voltage_neg_spectrum',
                        'current_waveform', 'current_spectrum',
                        'leakage_waveform', 'leakage_spectrum',
                        'channels',
                    ):
                        value = cached.get(key)
                        if isinstance(value, list) and value:
                            node_data[key] = value
            active_nodes_list.append(node_data)
        
        return jsonify({
            'success': True,
            'nodes': active_nodes_list,
            'count': len(active_nodes_list),
            'expired_count': len(expired_nodes)
        }), 200
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@api_bp.route('/nodes/report_mode', methods=['POST'])
@login_required
def set_node_report_mode():
    """设置节点上报模式：summary/full（仅管理员页面调用）"""
    try:
        payload = request.get_json() or {}
        node_id = _normalize_node_id(payload.get('node_id') or payload.get('device_id'))
        mode = (payload.get('mode') or '').strip().lower()

        if not node_id:
            return jsonify({'success': False, 'error': 'Missing node_id'}), 400
        if len(node_id) > 100:
            return jsonify({'success': False, 'error': 'node_id too long (max 100)'}), 400
        if mode not in ('summary', 'full'):
            return jsonify({'success': False, 'error': 'Invalid mode'}), 400

        cmd = _enqueue_node_command(node_id, 'report_mode', mode)
        if cmd is None:
            return jsonify({'success': False, 'error': 'Failed to persist command'}), 500
        node_report_modes[node_id] = mode

        # 同步到 active_nodes 的轻量数据（便于页面立即刷新）
        if node_id in active_nodes:
            try:
                active_nodes[node_id]['data']['report_mode'] = mode
            except Exception:
                pass

        # 主动推送给前端，立即刷新按钮状态
        try:
            if socketio_instance:
                node_info = active_nodes.get(node_id) or {}
                downsample_step = ((node_info.get('data', {}) or {}).get('downsample_step'))
                if downsample_step is None:
                    pending_step = node_downsample_commands.get(node_id)
                    if pending_step is not None:
                        downsample_step = int(pending_step)
                upload_points = ((node_info.get('data', {}) or {}).get('upload_points'))
                if upload_points is None:
                    pending_points = node_upload_points_commands.get(node_id)
                    if pending_points is not None:
                        upload_points = int(pending_points)
                socketio_instance.emit('node_status_update', {
                    'node_id': node_id,
                    'status': node_info.get('status', 'online'),
                    'fault_code': node_info.get('fault_code', 'E00'),
                    'timestamp': time.time(),
                    'report_mode': mode,
                    'downsample_step': downsample_step,
                    'upload_points': upload_points,
                }, namespace='/')
        except Exception:
            pass

        return jsonify({'success': True, 'node_id': node_id, 'report_mode': mode, 'command_id': cmd.command_id}), 200

    except Exception as e:
        logger.exception(f"[/api/nodes/report_mode] 失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@api_bp.route('/nodes/downsample_step', methods=['POST'])
@login_required
def set_node_downsample_step():
    """设置节点降采样步进（仅 full 模式允许）。"""
    try:
        payload = request.get_json() or {}
        node_id = _normalize_node_id(payload.get('node_id') or payload.get('device_id'))
        step = _parse_downsample_step(payload.get('step'))

        if not node_id:
            return jsonify({'success': False, 'error': 'Missing node_id'}), 400
        if len(node_id) > 100:
            return jsonify({'success': False, 'error': 'node_id too long (max 100)'}), 400
        if step is None:
            return jsonify({'success': False, 'error': 'Invalid step (expected integer 1..64)'}), 400

        mode = _get_report_mode(node_id)

        cmd = _enqueue_node_command(node_id, 'downsample_step', int(step))
        if cmd is None:
            return jsonify({'success': False, 'error': 'Failed to persist command'}), 500
        node_downsample_commands[node_id] = int(step)
        _sync_active_node_downsample_step(node_id, int(step))

        try:
            if socketio_instance:
                node_info = active_nodes.get(node_id) or {}
                upload_points = ((node_info.get('data', {}) or {}).get('upload_points'))
                if upload_points is None:
                    pending_points = node_upload_points_commands.get(node_id)
                    if pending_points is not None:
                        upload_points = int(pending_points)
                socketio_instance.emit('node_status_update', {
                    'node_id': node_id,
                    'status': node_info.get('status', 'online'),
                    'fault_code': node_info.get('fault_code', 'E00'),
                    'timestamp': time.time(),
                    'report_mode': mode,
                    'downsample_step': int(step),
                    'upload_points': upload_points,
                }, namespace='/')
        except Exception:
            pass

        resp = {
            'success': True,
            'node_id': node_id,
            'downsample_step': int(step),
            'report_mode': mode,
            'command_id': cmd.command_id,
        }
        if mode != 'full':
            resp['warning'] = 'command queued while current mode is not full; it will apply when full uploads are active'
        return jsonify(resp), 200

    except Exception as e:
        logger.exception(f"[/api/nodes/downsample_step] 失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@api_bp.route('/nodes/upload_points', methods=['POST'])
@login_required
def set_node_upload_points():
    """设置节点上传点数（降采样后的最大上传点数，仅 full 模式允许）。"""
    try:
        payload = request.get_json() or {}
        node_id = _normalize_node_id(payload.get('node_id') or payload.get('device_id'))
        raw_points = payload.get('points')
        if raw_points is None:
            raw_points = payload.get('upload_points')
        points = _parse_upload_points(raw_points)

        if not node_id:
            logger.warning("[/api/nodes/upload_points] bad payload: missing node_id keys=%s", sorted(payload.keys()))
            return jsonify({'success': False, 'error': 'Missing node_id'}), 400
        if len(node_id) > 100:
            logger.warning("[/api/nodes/upload_points] bad payload: node_id too long len=%s", len(node_id))
            return jsonify({'success': False, 'error': 'node_id too long (max 100)'}), 400
        if points is None:
            logger.warning("[/api/nodes/upload_points] bad payload: invalid points raw=%r keys=%s", raw_points, sorted(payload.keys()))
            return jsonify({'success': False, 'error': 'Invalid points (expected 256..4096 and step=256)'}), 400

        mode = _get_report_mode(node_id)

        cmd = _enqueue_node_command(node_id, 'upload_points', int(points))
        if cmd is None:
            return jsonify({'success': False, 'error': 'Failed to persist command'}), 500
        node_upload_points_commands[node_id] = int(points)
        _sync_active_node_upload_points(node_id, int(points))

        try:
            if socketio_instance:
                node_info = active_nodes.get(node_id) or {}
                downsample_step = ((node_info.get('data', {}) or {}).get('downsample_step'))
                if downsample_step is None:
                    pending_step = node_downsample_commands.get(node_id)
                    if pending_step is not None:
                        downsample_step = int(pending_step)
                socketio_instance.emit('node_status_update', {
                    'node_id': node_id,
                    'status': node_info.get('status', 'online'),
                    'fault_code': node_info.get('fault_code', 'E00'),
                    'timestamp': time.time(),
                    'report_mode': mode,
                    'downsample_step': downsample_step,
                    'upload_points': int(points),
                }, namespace='/')
        except Exception:
            pass

        resp = {
            'success': True,
            'node_id': node_id,
            'upload_points': int(points),
            'report_mode': mode,
            'command_id': cmd.command_id,
        }
        if mode != 'full':
            resp['warning'] = 'command queued while current mode is not full; it will apply when full uploads are active'
        return jsonify(resp), 200

    except Exception as e:
        logger.exception(f"[/api/nodes/upload_points] 失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@api_bp.route('/nodes/comm_params', methods=['POST'])
@login_required
def set_node_comm_params():
    """Persist and deliver communication/upload tuning commands to a node."""
    try:
        payload = request.get_json() or {}
        node_id = _normalize_node_id(payload.get('node_id') or payload.get('device_id'))
        if not node_id:
            return jsonify({'success': False, 'error': 'Missing node_id'}), 400
        if len(node_id) > 100:
            return jsonify({'success': False, 'error': 'node_id too long (max 100)'}), 400

        accepted = {}
        errors = {}
        for key in ('heartbeat_ms', 'min_interval_ms', 'http_timeout_ms', 'chunk_kb', 'chunk_delay_ms'):
            if key not in payload:
                continue
            value = _normalize_command_value(key, payload.get(key))
            if value is None:
                errors[key] = 'invalid'
                continue
            cmd = _enqueue_node_command(node_id, key, value)
            if cmd is None:
                errors[key] = 'persist_failed'
                continue
            accepted[key] = {'value': int(value), 'command_id': cmd.command_id}

        if errors:
            return jsonify({'success': False, 'node_id': node_id, 'accepted': accepted, 'errors': errors}), 400
        if not accepted:
            return jsonify({'success': False, 'error': 'No supported parameters'}), 400

        try:
            if socketio_instance:
                node_info = active_nodes.get(node_id) or {}
                update = {
                    'node_id': node_id,
                    'status': node_info.get('status', 'online'),
                    'fault_code': node_info.get('fault_code', 'E00'),
                    'timestamp': time.time(),
                    'report_mode': _get_report_mode(node_id),
                }
                for key, info in accepted.items():
                    update[key] = info['value']
                socketio_instance.emit('node_status_update', update, namespace='/')
        except Exception:
            pass

        return jsonify({'success': True, 'node_id': node_id, 'accepted': accepted}), 200

    except Exception as e:
        logger.exception(f"[/api/nodes/comm_params] failed: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


# ==================== 知识图谱API ====================

def _infer_fault_code_from_fault_type(fault_type: str | None) -> str:
    """根据工单里的故障中文名称推断故障代码（用于知识图谱等场景）"""
    if not fault_type:
        return 'E00'
    if '交流窜入' in fault_type:
        return 'E01'
    if '绝缘故障' in fault_type:
        return 'E02'
    if '电容老化' in fault_type or '电容' in fault_type:
        return 'E03'
    if 'IGBT' in fault_type or '开路' in fault_type:
        return 'E04'
    if '接地故障' in fault_type or '接地' in fault_type:
        return 'E05'
    return 'E00'


def _extract_actionable_bullets(detailed_report: str) -> list[str]:
    """从 AI 深度报告中提取“智能运维建议”部分的要点（以 '-' 开头的行）"""
    if not detailed_report:
        return []

    lines = [ln.strip() for ln in detailed_report.splitlines() if ln.strip()]
    bullets: list[str] = []
    in_advice = False

    for ln in lines:
        # 进入建议段落
        if ('智能运维建议' in ln) or ln.startswith('3.'):
            in_advice = True
            continue

        if in_advice:
            # 遇到下一段（如 4.）则停止
            if re.match(r'^\d+\.', ln) and (not ln.startswith('3.')):
                break
            if ln.startswith('-'):
                bullets.append(ln.lstrip('-').strip())

    return bullets


def _split_sentences(text: str) -> list[str]:
    """把中文描述按常见标点切分成短句，便于抽取“关键词标题”"""
    if not text:
        return []
    # 统一空白
    t = re.sub(r'\s+', ' ', str(text)).strip()
    # 用中文标点/分号/句号/顿号/换行切分
    parts = re.split(r'[。；;！!？?\n\r]+', t)
    out: list[str] = []
    for p in parts:
        p = p.strip(' ,，。；;:：\t')
        if p:
            out.append(p)
    return out


def _make_short_title(text: str, max_len: int = 12) -> str:
    """
    从一句话生成一个尽量“领域相关”的短标题，避免出现“说明/预防/立即”等泛词。
    规则（尽量简单稳定）：
    - 去掉前缀标签 [xx]
    - 若有 '：'，优先取冒号后的部分
    - 去掉常见动作前缀（使用/检查/更换/确认/停止...）但保留关键名词
    - 截断到 max_len
    """
    if not text:
        return ''
    s = str(text).strip()
    s = re.sub(r'^\[[^\]]+\]\s*', '', s)  # 去掉 [检测] 等
    if '：' in s:
        s = s.split('：', 1)[1].strip()
    if ':' in s:
        s = s.split(':', 1)[1].strip()

    # 去掉非常泛的前缀动词/提示词（只删开头，避免破坏内容）
    s = re.sub(r'^(请|建议|提示|立即|尽快|应|务必)\s*', '', s)
    s = re.sub(r'^(使用|检查|检测|更换|确认|排查|修复|处理|测试|测量)\s*', '', s)

    # 取到第一个逗号/顿号前，标题更像关键词
    s = re.split(r'[，,、;；]', s, 1)[0].strip()

    # 过滤掉太短/太泛的标题
    stop_titles = {'说明', '预防', '立即', '检查', '检测', '更换', '处理', '要点', '详情', '建议', '提示'}
    if (not s) or (s in stop_titles):
        return ''

    if len(s) > max_len:
        s = s[:max_len].rstrip()
    return s


@api_bp.route('/knowledge_graph/<fault_code>', methods=['GET'])
def get_knowledge_graph(fault_code):
    """获取故障诊断知识图谱"""
    try:
        graph_data = get_fault_knowledge_graph(fault_code)
        if graph_data:
            return jsonify(graph_data), 200
        else:
            return jsonify({'error': 'Fault code not found'}), 404
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@api_bp.route('/graph/details/<int:log_id>/<path:node_name>', methods=['GET'])
@login_required
def get_graph_node_details(log_id: int, node_name: str):
    """
    返回“节点二级展开”数据（给 templates/faults.html 的知识图谱点击展开使用）

    前端会请求：
      /api/graph/details/<log_id>/<node_name>

    返回格式：
    {
      "children": [
        {"name": "...", "description": "..."},
        ...
      ]
    }
    """
    try:
        # 前端已 encodeURIComponent，这里做一次解码兜底（避免重复编码导致中文不匹配）
        try:
            node_name_decoded = unquote(node_name)
        except Exception:
            node_name_decoded = node_name

        order = WorkOrder.query.get(log_id)
        if not order:
            return jsonify({'error': '工单不存在', 'children': []}), 404

        # 优先使用设备当前故障码；若设备已恢复为 E00，则根据工单故障类型推断
        device = Device.query.filter_by(device_id=order.device_id).first()
        device_fault_code = getattr(device, 'fault_code', None) if device else None
        fault_code = device_fault_code if (device_fault_code and device_fault_code != 'E00') else _infer_fault_code_from_fault_type(order.fault_type)

        # 兼容：若 fault_code 是映射键（如 DC_CAPACITOR_AGING），先映射到标准 E01-E05
        mapped = FAULT_CODE_MAP.get(fault_code)
        if mapped:
            fault_code = mapped

        fault_info = FAULT_KNOWLEDGE_GRAPH.get(fault_code)
        if not fault_info:
            return jsonify({'children': []}), 200

        # 仅对“根本原因/解决方案”节点做二级展开；故障根节点默认不展开
        # 目标：二级节点直接展示“故障相关关键词”，并尽量固定 3 个
        children: list[dict] = []
        seen_desc: set[str] = set()

        def _add_child_from_text(text: str):
            """从一段文本生成 child（短标题+详细描述），过滤泛词"""
            if not text:
                return
            desc = re.sub(r'\s+', ' ', str(text)).strip()
            if not desc:
                return
            if desc.lower() in seen_desc:
                return
            seen_desc.add(desc.lower())

            title = _make_short_title(desc, max_len=12)
            if not title:
                return
            children.append({'name': title, 'description': desc})

        # 从知识库中匹配该节点（按名称）
        matched_desc = None
        for cause in fault_info.get('root_causes', []) or []:
            if cause.get('name') == node_name_decoded:
                matched_desc = cause.get('description') or ''
                break
        if matched_desc is None:
            for sol in fault_info.get('solutions', []) or []:
                if sol.get('name') == node_name_decoded:
                    matched_desc = sol.get('description') or ''
                    break

        # 1) 先用“节点自身描述”生成 1-2 个关键词（信息最相关）
        if matched_desc:
            # 尝试按短句拆分，最多取 2 条（避免全是同一句话）
            for seg in _split_sentences(matched_desc)[:3]:
                if len(children) >= 2:
                    break
                _add_child_from_text(seg)

        # 2) 再从详细报告提取“运维建议”要点，补齐到 3 条（但必须与当前节点语义相关）
        bullets = _extract_actionable_bullets(fault_info.get('detailed_report', ''))
        if bullets:
            # 用一组行业关键词做轻量匹配：只返回与当前节点真正相关的内容
            kw_pool = [
                'ESR', '纹波', '电容', '绝缘', '接地', '隔离', '变压器', '滤波', 'IGBT', '门极', '驱动', '温度', '散热', '电桥', '选线'
            ]
            node_kws = [k for k in kw_pool if k and (k in node_name_decoded)]
            # 再补充几个动作词（只在节点名包含时才启用）
            for k in ['检测', '测试', '更换', '检查', '排查', '修复']:
                if k in node_name_decoded and k not in node_kws:
                    node_kws.append(k)

            def _is_relevant(b: str) -> bool:
                if not node_kws:
                    return False
                return any(k in b for k in node_kws)

            picked = [b for b in bullets if _is_relevant(b)]
            for b in picked:
                if len(children) >= 3:
                    break
                # 取内容部分作为描述；标题由 _make_short_title 自动从描述中抽取关键词
                m = re.match(r'^\[[^\]]+\]\s*(.*)$', b)
                desc = (m.group(1) if m else b).strip()
                _add_child_from_text(desc)

        # 3) 若仍不足 3 个，用 fault_info 的 root_cause/solution 等字段补齐（只取最短关键词）
        if len(children) < 3:
            for extra in [fault_info.get('root_cause', ''), fault_info.get('solution', '')]:
                if len(children) >= 3:
                    break
                for seg in _split_sentences(extra)[:2]:
                    _add_child_from_text(seg)

        # 4) 最终保证最多 3 个（不做“说明/预防/立即”等泛词标题）
        children = children[:3]

        return jsonify({'children': children}), 200
    except Exception as e:
        logger.exception(f"获取图谱节点详情失败: {e}")
        return jsonify({'error': str(e), 'children': []}), 500


@api_bp.route('/ai_report/<fault_code>', methods=['GET'])
def get_ai_report(fault_code):
    """获取AI诊断报告"""
    try:
        report = generate_ai_report(fault_code)
        return jsonify({'report': report}), 200
    except Exception as e:
        return jsonify({'error': str(e)}), 500


# ==================== 工单管理API ====================

@api_bp.route('/work_orders', methods=['GET'])
@login_required
def get_work_orders():
    """获取工单列表"""
    try:
        orders = WorkOrder.query.order_by(WorkOrder.fault_time.desc()).all()
        result = []
        for order in orders:
            result.append({
                'id': order.id,
                'device_id': order.device_id,
                'fault_time': (order.fault_time + timedelta(hours=8)).strftime('%Y-%m-%d %H:%M:%S') if order.fault_time else None,
                'location': order.location,
                'fault_type': order.fault_type,
                'status': order.status,
                'ai_recommendation': order.ai_recommendation
            })
        return jsonify(result), 200
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@api_bp.route('/work_orders/<int:order_id>', methods=['PATCH'])
@login_required
def update_work_order(order_id):
    """更新工单状态"""
    try:
        order = WorkOrder.query.get(order_id)
        if not order:
            return jsonify({'error': 'Work order not found'}), 404
        
        data = request.get_json()
        if 'status' in data:
            order.status = data['status']
            
            # 如果标记为已修复，发送重置命令给节点
            if data['status'] in ['fixed', 'resolved']:
                node_commands[order.device_id] = 'reset'
        
        db.session.commit()
        return jsonify({'message': 'Work order updated'}), 200
    except Exception as e:
        db.session.rollback()
        return jsonify({'error': str(e)}), 500


# ==================== 兼容接口：故障日志（templates/faults.html 使用）====================
@api_bp.route('/faults', methods=['GET'])
@login_required
def get_faults():
    """
    返回故障日志列表（与 faults.html 的 /api/faults 兼容）
    返回格式：
    {
      "faults": [ ... ]
    }
    """
    try:
        start_ts = time.time()
        logger.debug("[/api/faults] 开始获取故障日志（work_orders -> faults）")

        def _infer_fault_code(fault_type: str | None) -> str:
            """根据故障中文名称推断故障代码（用于兼容旧前端展示/知识图谱加载）"""
            if not fault_type:
                return 'E00'
            if '交流窜入' in fault_type:
                return 'E01'
            if '绝缘故障' in fault_type:
                return 'E02'
            if '电容老化' in fault_type or '电容' in fault_type:
                return 'E03'
            if 'IGBT' in fault_type or '开路' in fault_type:
                return 'E04'
            if '接地故障' in fault_type or '接地' in fault_type:
                return 'E05'
            return 'E00'

        def _infer_severity(fault_code: str | None, fault_type: str | None = None) -> str:
            """
            推断严重程度（供 faults.html 展示）

            返回值：severe | major | general
            说明：
            - 目前 WorkOrder 表没有 severity 字段，因此在接口层按故障码推断，确保前端不再显示“未知”
            - 规则可按业务需要调整
            """
            fc = (fault_code or '').strip()
            ft = (fault_type or '').strip()

            # 最高严重：IGBT 开路 / 直流母线接地
            if fc in ('E04', 'E05'):
                return 'severe'

            # 主要：交流窜入 / 绝缘故障
            if fc in ('E01', 'E02'):
                return 'major'

            # 一般：电容老化
            if fc in ('E03',):
                return 'general'

            # 兜底：根据中文名称再判断一次（兼容历史/异常数据）
            if 'IGBT' in ft or '开路' in ft or '接地' in ft:
                return 'severe'
            if '交流窜入' in ft or '绝缘故障' in ft:
                return 'major'
            if '电容' in ft:
                return 'general'

            return 'general'

        orders = WorkOrder.query.order_by(WorkOrder.fault_time.desc()).all()
        faults = []
        for order in orders:
            device = Device.query.filter_by(device_id=order.device_id).first()
            # 前端期望 status: pending/processing/resolved
            status = order.status or 'pending'
            if status == 'fixed':
                status = 'resolved'

            # 优先使用设备当前故障码；若设备已恢复为E00，则根据工单故障类型推断
            device_fault_code = getattr(device, 'fault_code', None) if device else None
            fault_code = device_fault_code if (device_fault_code and device_fault_code != 'E00') else _infer_fault_code(order.fault_type)
            severity = _infer_severity(fault_code, order.fault_type)

            # 时间统一口径：返回“北京时间”
            # - fault_time：ISO 8601（带 +08:00 时区偏移），前端 new Date() 解析不会受本机时区影响
            # - time：北京时间展示字符串（YYYY-MM-DD HH:MM:SS）
            local_dt = (order.fault_time + timedelta(hours=8)) if order.fault_time else None
            fault_time_iso = local_dt.strftime('%Y-%m-%dT%H:%M:%S+08:00') if local_dt else None
            fault_time_display = local_dt.strftime('%Y-%m-%d %H:%M:%S') if local_dt else None

            faults.append({
                'id': order.id,
                'device_id': order.device_id,
                'location': order.location or (device.location if device else 'N/A'),
                'fault_type': order.fault_type,
                'fault_code': fault_code,
                'severity': severity,
                'status': status,
                'ai_recommendation': order.ai_recommendation,
                # 兼容：faults.html 使用 fault_time；也保留 time 字段（旧逻辑可能用）
                'fault_time': fault_time_iso,
                'time': fault_time_display
            })
        cost_ms = int((time.time() - start_ts) * 1000)
        logger.debug(f"[/api/faults] 完成：数量={len(faults)} 耗时={cost_ms}ms")
        return jsonify({'faults': faults}), 200
    except Exception as e:
        logger.exception(f"[/api/faults] 失败: {e}")
        return jsonify({'faults': [], 'message': str(e)}), 500


@api_bp.route('/faults/<int:fault_id>/dispatch', methods=['POST'])
@login_required
def dispatch_fault(fault_id: int):
    """派单：pending -> processing（与 faults.html 兼容）"""
    try:
        order = WorkOrder.query.get(fault_id)
        if not order:
            return jsonify({'success': False, 'message': '工单不存在'}), 404
        order.status = 'processing'
        db.session.commit()
        return jsonify({'success': True}), 200
    except Exception as e:
        db.session.rollback()
        return jsonify({'success': False, 'message': str(e)}), 500


@api_bp.route('/faults/<int:fault_id>/resolve', methods=['POST'])
@login_required
def resolve_fault(fault_id: int):
    """维修完成：processing -> resolved，并向节点下发 reset（与 faults.html 兼容）"""
    try:
        order = WorkOrder.query.get(fault_id)
        if not order:
            return jsonify({'success': False, 'message': '工单不存在'}), 404
        order.status = 'resolved'
        # 下发复位命令（兼容 sim.py 支持 reset/reset_local_state）
        node_commands[order.device_id] = 'reset'
        db.session.commit()
        return jsonify({'success': True}), 200
    except Exception as e:
        db.session.rollback()
        return jsonify({'success': False, 'message': str(e)}), 500


# ==================== 故障快照API ====================

@api_bp.route('/snapshots', methods=['GET'])
@login_required
def get_snapshots():
    """获取故障快照列表"""
    try:
        device_id = request.args.get('device_id')
        fault_code = request.args.get('fault_code')
        
        query = FaultSnapshot.query
        if device_id:
            query = query.filter_by(device_id=device_id)
        if fault_code:
            query = query.filter_by(fault_code=fault_code)
        
        snapshots = query.order_by(FaultSnapshot.timestamp.desc()).all()
        result = [s.to_dict() for s in snapshots]
        
        return jsonify(result), 200
    except Exception as e:
        return jsonify({'error': str(e)}), 500


# ==================== 兼容接口：设备列表（overview/settings 使用）====================
@api_bp.route('/devices', methods=['GET'])
@login_required
def get_devices():
    """返回设备列表（简化版，满足前端展示/筛选）
    
    默认只返回“近期活跃设备”，避免概览页堆满历史离线节点。
    如需全部设备，调用方可显式传入 all=true。
    """
    try:
        # online_only=true：仅返回“实时在线”的设备（与 active_nodes 统计口径一致）
        # 用于系统概览页，避免“最近30分钟上报过但当前已离线”的设备仍显示为卡片
        online_only = request.args.get('online_only', 'false').lower() == 'true'

        # 默认启用活跃过滤；仅当 all=true 时返回全部
        all_devices = request.args.get('all', 'false').lower() == 'true'
        active_only = not all_devices or request.args.get('active_only', 'false').lower() == 'true'
        minutes = int(request.args.get('minutes', 30))
        devices_query = Device.query

        # 计算当前“实时在线”节点集合（NODE_TIMEOUT 秒内有心跳）
        current_time = time.time()
        _rehydrate_active_nodes_from_db(current_time)
        realtime_online_ids = {
            node_id for node_id, info in list(active_nodes.items())
            if current_time - info.get('timestamp', 0) <= NODE_TIMEOUT
        }

        if online_only:
            # 概览页：只展示实时在线节点（可包含故障节点）
            if not realtime_online_ids:
                logger.info(f"[/api/devices] 在线过滤=是，在线节点=0，args={dict(request.args)} -> 返回0")
                return jsonify({'success': True, 'devices': []}), 200
            devices_query = devices_query.filter(Device.device_id.in_(list(realtime_online_ids)))
        elif active_only:
            cutoff = datetime.utcnow() - timedelta(minutes=minutes)
            # 仅保留在截止时间后有心跳的设备
            devices_query = devices_query.filter(Device.last_heartbeat != None, Device.last_heartbeat >= cutoff)

        devices = devices_query.order_by(Device.registered_at.desc()).all()
        result = []
        for d in devices:
            # 计算返回给前端的状态：以“实时在线(active_nodes)”为准，避免数据库状态滞后
            computed_status = 'offline'
            if d.device_id in realtime_online_ids:
                node_info = active_nodes.get(d.device_id, {}) or {}
                fc = node_info.get('fault_code') or d.fault_code or 'E00'
                computed_status = 'faulty' if fc != 'E00' else 'online'

            result.append({
                'device_id': d.device_id,
                'location': d.location,
                'status': computed_status,
                'fault_code': d.fault_code,
                'last_heartbeat': (d.last_heartbeat + timedelta(hours=8)).strftime('%Y-%m-%d %H:%M:%S') if d.last_heartbeat else None
            })

        # 打印一次关键日志，便于定位“页面仍显示卡片”的原因（缓存/旧接口/参数未生效等）
        logger.info(
            f"[/api/devices] args={dict(request.args)} online_only={'是' if online_only else '否'} "
            f"active_only={'是' if active_only else '否'} minutes={minutes} "
            f"实时在线={len(realtime_online_ids)} 返回={len(result)}"
        )
        return jsonify({'success': True, 'devices': result}), 200
    except Exception as e:
        return jsonify({'success': False, 'message': str(e), 'devices': []}), 500


# ==================== 兼容接口：仪表盘统计（base/overview 使用）====================
@api_bp.route('/dashboard/stats', methods=['GET'])
@login_required
def get_dashboard_stats():
    """
    返回仪表盘统计数据（尽量与旧前端字段兼容）
    """
    try:
        # 基于 active_nodes 统计在线/离线/故障
        current_time = time.time()
        _rehydrate_active_nodes_from_db(current_time)
        total_nodes = 0
        online_nodes = 0
        faulty_nodes = 0
        offline_nodes = 0

        for node_id, node_info in list(active_nodes.items()):
            if current_time - node_info.get('timestamp', 0) > NODE_TIMEOUT:
                offline_nodes += 1
                continue
            total_nodes += 1
            fc = node_info.get('fault_code', 'E00')
            if fc and fc != 'E00':
                faulty_nodes += 1
            else:
                online_nodes += 1

        # 基于工单统计
        today_start = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0)
        today_cumulative = WorkOrder.query.filter(WorkOrder.fault_time >= today_start).count()
        today_resolved = WorkOrder.query.filter(WorkOrder.fault_time >= today_start, WorkOrder.status.in_(['resolved', 'fixed'])).count()
        current_pending = WorkOrder.query.filter(WorkOrder.status.in_(['pending', 'processing'])).count()

        # 简单健康分
        health_score = max(0, 100 - faulty_nodes * 20 - offline_nodes * 10)

        return jsonify({
            'total_nodes': total_nodes,
            'online_nodes': online_nodes,
            'faulty_nodes': faulty_nodes,
            'offline_nodes': offline_nodes,
            'current_fault_count': faulty_nodes,
            'offline_count': offline_nodes,
            'today_cumulative': today_cumulative,
            'today_resolved': today_resolved,
            'current_pending': current_pending,
            'system_health_score': health_score
        }), 200

    except Exception as e:
        logger.error(f"获取仪表盘统计失败: {str(e)}")
        return jsonify({'error': str(e)}), 500


# ==================== 兼容接口：故障分析数据（overview 使用）====================
@api_bp.route('/dashboard/fault_analytics', methods=['GET'])
@login_required
def get_fault_analytics():
    """
    返回故障分析数据：故障类型分布 + 24小时故障趋势
    """
    try:
        from edgewind.knowledge_graph import FAULT_CODE_MAP
        
        def _normalize_fault_type_name(name: str) -> str:
            """
            统一故障名称为“纯中文标准名”，避免出现：
            - 同一故障被统计成多条（中文/英文/中英混写）
            - 前端饼图标签过长导致重叠
            """
            if not name:
                return '未知故障'
            s = str(name).strip()

            # 1) 去掉括号内英文：例如 “交流窜入 (AC Intrusion)” -> “交流窜入”
            for sep in [' (', '（']:
                if sep in s:
                    s = s.split(sep, 1)[0].strip()
                    break

            # 2) 兼容英文名称（极少数老数据）
            english_map = {
                'AC Intrusion': '交流窜入',
                'Insulation Fault': '绝缘故障',
                'Capacitor Aging': '直流母线电容老化',
                'IGBT Open Circuit': '变流器IGBT开路',
                'DC Bus Grounding Fault': '直流母线接地故障'
            }
            if s in english_map:
                return english_map[s]

            # 3) 若是短中文别名，映射到标准故障名（与项目定义保持一致）
            alias_map = {
                '电容老化': '直流母线电容老化',
                'IGBT开路': '变流器IGBT开路',
                '接地故障': '直流母线接地故障'
            }
            return alias_map.get(s, s)

        # 1. 故障类型分布（从工单统计）
        fault_type_counter = defaultdict(int)
        orders = WorkOrder.query.all()
        for order in orders:
            fault_type = _normalize_fault_type_name(order.fault_type or '未知故障')
            fault_type_counter[fault_type] += 1
        
        # 构建饼图数据（按故障类型名称）
        fault_type_labels = []
        fault_type_values = []
        for fault_type, count in sorted(fault_type_counter.items(), key=lambda x: x[1], reverse=True):
            if count > 0:
                fault_type_labels.append(fault_type)
                fault_type_values.append(count)
        
        # 如果没有数据，返回空数据
        if not fault_type_labels:
            fault_type_labels = ['正常']
            fault_type_values = [0]
        
        # 2. 24小时故障趋势（按小时分组统计）
        # 重要：数据库存储的是 UTC 时间，但前端展示/用户认知都是北京时间（UTC+8）
        # 因此这里统一转成北京时间计算，确保图表横轴显示的"小时"与用户实际时间一致
        now_utc = datetime.utcnow()
        now_beijing = now_utc + timedelta(hours=8)  # 转换为北京时间
        hour_start_beijing = now_beijing.replace(minute=0, second=0, microsecond=0) - timedelta(hours=23)
        
        # 查询窗口：转回UTC用于数据库查询（数据库内部存UTC）
        hour_start_utc = hour_start_beijing - timedelta(hours=8)
        recent_orders = WorkOrder.query.filter(
            WorkOrder.fault_time >= hour_start_utc
        ).all()
        
        # 按小时分组统计（转为北京时间后再分组）
        hourly_count = defaultdict(int)
        for order in recent_orders:
            if order.fault_time:
                # 数据库 fault_time 是UTC，转为北京时间
                order_time_beijing = order.fault_time + timedelta(hours=8)
                order_hour_beijing = order_time_beijing.replace(minute=0, second=0, microsecond=0)
                hour_key = order_hour_beijing.strftime('%H:%M')
                hourly_count[hour_key] += 1
        
        # 生成24小时时间序列（北京时间，即使某小时没有数据也要显示0）
        hours_list = []
        faults_list = []
        for i in range(24):
            hour_time_beijing = hour_start_beijing + timedelta(hours=i)
            hour_label = hour_time_beijing.strftime('%H:%M')
            hours_list.append(hour_label)
            faults_list.append(hourly_count.get(hour_label, 0))
        
        return jsonify({
            'fault_type_distribution': {
                'labels': fault_type_labels,
                'values': fault_type_values
            },
            'hourly_fault_frequency': {
                'hours': hours_list,
                'faults': faults_list
            }
        }), 200
        
    except Exception as e:
        logger.error(f"获取故障分析数据失败: {str(e)}")
        import traceback
        logger.error(traceback.format_exc())
        return jsonify({
            'fault_type_distribution': {
                'labels': [],
                'values': []
            },
            'hourly_fault_frequency': {
                'hours': [],
                'faults': []
            },
            'error': str(e)
        }), 500

# ==================== 兼容接口：故障快照（前端 snapshots.html 使用）====================
@api_bp.route('/fault_snapshots', methods=['GET'])
@login_required
def get_fault_snapshots_events():
    """
    返回快照事件列表（与 templates/snapshots.html 兼容）
    """
    try:
        device_id = request.args.get('device_id')
        fault_code = request.args.get('fault_code')
        snapshot_type = request.args.get('snapshot_type')
        # limit=0 或 limit=all 表示不限制（加载全部历史）
        limit_param = request.args.get('limit', '5000')
        if limit_param.lower() == 'all' or limit_param == '0':
            limit = None  # 不限制
        else:
            limit = min(int(limit_param), 50000)  # 最大5万条原始记录

        query = FaultSnapshot.query
        if device_id:
            query = query.filter_by(device_id=device_id)
        if fault_code:
            query = query.filter_by(fault_code=fault_code)
        if snapshot_type:
            query = query.filter_by(snapshot_type=snapshot_type)

        # 获取总数（用于前端显示）
        total_count = query.count()
        
        # 查询快照
        query = query.order_by(FaultSnapshot.timestamp.desc())
        if limit is not None:
            snapshots = query.limit(limit).all()
        else:
            snapshots = query.all()

        # 按“设备 + 故障代码 + 本地时间(秒)”分组
        events_dict = {}
        for snapshot in snapshots:
            local_time = snapshot.timestamp + timedelta(hours=8)
            time_key = local_time.strftime('%Y%m%d%H%M%S')
            key = f"{snapshot.device_id}_{snapshot.fault_code}_{time_key}"
            if key not in events_dict:
                events_dict[key] = {
                    'device_id': snapshot.device_id,
                    'fault_code': snapshot.fault_code,
                    'timestamp': local_time.strftime('%Y-%m-%d %H:%M:%S'),
                    'snapshot_count': 0
                }
            events_dict[key]['snapshot_count'] += 1

        events_list = list(events_dict.values())
        return jsonify({
            'success': True,
            'events': events_list,
            'total_snapshots': total_count,           # 原始快照记录总数
            'event_count': len(events_list),          # 当前返回的事件数
            'loaded_snapshots': len(snapshots),       # 本次加载的快照数
            'has_more': limit is not None and len(snapshots) >= limit  # 是否还有更多
        })
    except Exception as e:
        logger.error(f"获取故障快照事件失败: {e}")
        return jsonify({'success': False, 'message': str(e)}), 500


@api_bp.route('/fault_snapshots/event/<device_id>/<fault_code>/<timestamp>', methods=['GET', 'DELETE'])
@login_required
def handle_fault_event_snapshots(device_id, fault_code, timestamp):
    """
    GET: 返回某个事件的所有快照（before/after/before_recovery/after_recovery）
    DELETE: 删除该事件的所有快照
    """
    try:
        timestamp = unquote(timestamp)
        local_time = datetime.strptime(timestamp, '%Y-%m-%d %H:%M:%S')
        # 前端传的是本地时间(UTC+8)，转回UTC用于查询
        utc_start = local_time - timedelta(hours=8)
        utc_end = utc_start + timedelta(seconds=1)

        query = FaultSnapshot.query.filter(
            FaultSnapshot.device_id == device_id,
            FaultSnapshot.fault_code == fault_code,
            FaultSnapshot.timestamp >= utc_start,
            FaultSnapshot.timestamp < utc_end
        )

        if request.method == 'DELETE':
            to_delete = query.all()
            deleted_count = len(to_delete)
            for s in to_delete:
                db.session.delete(s)
            db.session.commit()
            return jsonify({'success': True, 'deleted': deleted_count, 'message': '删除成功'})

        # GET
        snaps = query.order_by(FaultSnapshot.timestamp.asc()).all()
        grouped = {'before': [], 'after': [], 'before_recovery': [], 'after_recovery': []}
        for s in snaps:
            if s.snapshot_type in grouped:
                grouped[s.snapshot_type].append(s.to_dict())

        return jsonify({
            'success': True,
            'device_id': device_id,
            'fault_code': fault_code,
            'timestamp': timestamp,
            'snapshots': grouped
        })

    except Exception as e:
        logger.error(f"处理故障事件快照失败: {e}")
        db.session.rollback()
        return jsonify({'success': False, 'message': str(e)}), 500


# ==================== 系统配置API ====================

@api_bp.route('/config', methods=['GET', 'POST'])
@login_required
def manage_config():
    """获取或更新系统配置"""
    try:
        if request.method == 'GET':
            configs = SystemConfig.query.all()
            result = {}
            for config in configs:
                result[config.key] = json.loads(config.value) if config.value else None
            return jsonify(result), 200
        
        elif request.method == 'POST':
            data = request.get_json()
            for key, value in data.items():
                config = SystemConfig.query.filter_by(key=key).first()
                if config:
                    config.value = json.dumps(value)
                    config.updated_at = datetime.utcnow()
                else:
                    config = SystemConfig(key=key, value=json.dumps(value))
                    db.session.add(config)
            
            db.session.commit()
            return jsonify({'message': 'Config updated'}), 200
    except Exception as e:
        db.session.rollback()
        return jsonify({'error': str(e)}), 500



# ==================== 管理接口：设置页（templates/settings.html）====================

def _get_sqlite_db_path_from_uri(db_uri: str):
    """
    从 SQLAlchemy SQLite URI 中提取数据库文件路径。
    支持：
    - sqlite:///relative/path.db
    - sqlite:////absolute/path.db
    """
    if not isinstance(db_uri, str):
        return None
    if not db_uri.startswith("sqlite:///"):
        return None

    raw = db_uri[len("sqlite:///"):]
    if not raw:
        return None

    # 这里 raw 可能是：
    # - instance/wind_farm.db（相对路径）
    # - C:/xxx/instance/wind_farm.db（绝对路径）
    try:
        return Path(raw)
    except Exception:
        return None


@api_bp.route('/admin/system_info', methods=['GET'])
@login_required
def admin_system_info():
    """系统信息（供系统设置页展示）"""
    try:
        # 1) 版本号：优先环境变量，未配置则给一个默认值
        version = os.environ.get('EDGEWIND_VERSION', 'v1.4.0')

        # 2) 数据库大小（仅对 SQLite 计算文件大小）
        db_uri = (app_instance.config.get('SQLALCHEMY_DATABASE_URI') if app_instance else '') or ''
        db_size_mb = 0.0
        sqlite_path = _get_sqlite_db_path_from_uri(db_uri)
        if sqlite_path is not None:
            # 相对路径以项目根目录为基准（与 Config 的绝对化逻辑保持一致）
            if not sqlite_path.is_absolute():
                project_root = Path(__file__).resolve().parents[1]  # edgewind/
                project_root = project_root.parent  # 项目根
                sqlite_path = (project_root / sqlite_path).resolve()
            if sqlite_path.exists():
                db_size_mb = round(sqlite_path.stat().st_size / (1024 * 1024), 2)

        # 3) 活跃节点（NODE_TIMEOUT 秒内有心跳）
        current_time = time.time()
        _rehydrate_active_nodes_from_db(current_time)
        active_node_ids = [
            node_id for node_id, info in list(active_nodes.items())
            if current_time - info.get('timestamp', 0) <= NODE_TIMEOUT
        ]

        # 4) 工单统计
        total_orders = WorkOrder.query.count()
        pending_orders = WorkOrder.query.filter(WorkOrder.status.in_(['pending', 'processing'])).count()
        resolved_orders = WorkOrder.query.filter(WorkOrder.status.in_(['resolved', 'fixed'])).count()

        # 5) 异步模式（eventlet/gevent/threading）
        async_mode = getattr(socketio_instance, 'async_mode', None) or os.environ.get('FORCE_ASYNC_MODE', 'auto')

        return jsonify({
            'success': True,
            'data': {
                'version': version,
                'database_size_mb': db_size_mb,
                'database_uri': 'sqlite' if str(db_uri).startswith('sqlite') else 'other',
                'active_nodes': len(active_node_ids),
                'workorders': {
                    'total': total_orders,
                    'pending': pending_orders,
                    'resolved': resolved_orders
                },
                'async_mode': async_mode,
                'python_version': sys.version.split()[0]
            }
        }), 200
    except Exception as e:
        logger.error(f"[admin_system_info] 获取系统信息失败: {e}")
        import traceback
        logger.error(traceback.format_exc())
        return jsonify({'success': False, 'error': str(e)}), 500


@api_bp.route('/admin/vacuum_db', methods=['POST'])
@login_required
def admin_vacuum_db():
    """
    执行 SQLite VACUUM 回收数据库文件空间。

    重要说明：
    - DELETE 只会释放可复用页，不会缩小 .db 文件。
    - VACUUM 会重写数据库文件来回收空间，期间会加锁，可能短暂阻塞写入。
    - 建议在设备/模拟器暂停上报时执行。
    """
    try:
        # 仅支持 SQLite
        db_uri = (app_instance.config.get('SQLALCHEMY_DATABASE_URI') if app_instance else '') or ''
        sqlite_path = _get_sqlite_db_path_from_uri(db_uri)
        if sqlite_path is None:
            return jsonify({'success': False, 'error': '当前数据库不是 SQLite，无法执行 VACUUM'}), 400

        # 绝对化路径（与 admin_system_info 保持一致）
        if not sqlite_path.is_absolute():
            project_root = Path(__file__).resolve().parents[1]  # edgewind/
            project_root = project_root.parent  # 项目根
            sqlite_path = (project_root / sqlite_path).resolve()

        if not sqlite_path.exists():
            return jsonify({'success': False, 'error': f'数据库文件不存在: {sqlite_path}'}), 404

        size_before = sqlite_path.stat().st_size

        # 先提交并清理 session，减少 “database is locked” 概率
        try:
            db.session.commit()
        except Exception:
            db.session.rollback()
        try:
            db.session.close()
        except Exception:
            pass

        t0 = time.perf_counter()
        engine = db.engine

        # VACUUM 不能在事务内执行；对 sqlite3 连接设置 isolation_level=None
        conn = engine.raw_connection()
        try:
            try:
                conn.isolation_level = None
            except Exception:
                # 某些 DBAPI 不支持该属性（非 SQLite），但我们前面已限制为 SQLite
                pass

            cur = conn.cursor()
            try:
                # 若处于 WAL 模式，先 checkpoint，避免 wal 文件占用空间
                try:
                    cur.execute("PRAGMA journal_mode;")
                    row = cur.fetchone()
                    journal_mode = (row[0] if row else '') or ''
                    if str(journal_mode).lower() == 'wal':
                        cur.execute("PRAGMA wal_checkpoint(TRUNCATE);")
                except Exception:
                    pass

                cur.execute("VACUUM;")
            finally:
                try:
                    cur.close()
                except Exception:
                    pass
        finally:
            try:
                conn.close()
            except Exception:
                pass

        elapsed_ms = int((time.perf_counter() - t0) * 1000)
        size_after = sqlite_path.stat().st_size

        return jsonify({
            'success': True,
            'data': {
                'db_path': str(sqlite_path),
                'size_before_bytes': int(size_before),
                'size_after_bytes': int(size_after),
                'saved_bytes': int(max(0, size_before - size_after)),
                'elapsed_ms': elapsed_ms,
            }
        }), 200
    except Exception as e:
        logger.exception(f"[admin_vacuum_db] VACUUM 失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@api_bp.route('/admin/config', methods=['GET', 'POST'])
@login_required
def admin_config():
    """
    设置页配置读写接口（与 templates/settings.html 对齐）
    返回格式：{success: true, data: {...}}
    """
    try:
        keys = [
            'poll_interval',
            'voltage_max',
            'leakage_threshold',
            'auto_refresh',
            'fft_enabled',
            'show_debug_log',
            'log_retention'
        ]

        if request.method == 'GET':
            data = {}
            for k in keys:
                config = SystemConfig.query.filter_by(key=k).first()
                if config and config.value is not None:
                    try:
                        data[k] = json.loads(config.value)
                    except Exception:
                        data[k] = config.value
            return jsonify({'success': True, 'data': data}), 200

        # POST
        payload = request.get_json() or {}
        for k in keys:
            if k not in payload:
                continue
            v = payload.get(k)
            row = SystemConfig.query.filter_by(key=k).first()
            if row:
                row.value = json.dumps(v, ensure_ascii=False)
                row.updated_at = datetime.utcnow()
            else:
                row = SystemConfig(key=k, value=json.dumps(v, ensure_ascii=False), description='系统设置')
                db.session.add(row)

        db.session.commit()
        return jsonify({'success': True, 'message': '配置已保存'}), 200
    except Exception as e:
        db.session.rollback()
        return jsonify({'success': False, 'error': str(e)}), 500


@api_bp.route('/admin/cleanup_old_data', methods=['POST'])
@login_required
def admin_cleanup_old_data():
    """按保留天数清理历史数据（波形数据 + 历史曲线数据 + 已完成工单）"""
    try:
        payload = request.get_json() or {}
        retention_days = int(payload.get('retention_days', 30))
        if retention_days <= 0:
            return jsonify({'success': False, 'error': 'retention_days 必须大于 0'}), 400

        cutoff = datetime.utcnow() - timedelta(days=retention_days)

        # 1) 删除过期波形数据
        datapoints_deleted = DataPoint.query.filter(DataPoint.timestamp < cutoff).delete(synchronize_session=False)

        # 2) 删除过期历史曲线数据
        history_deleted = HistoryData.query.filter(HistoryData.timestamp < cutoff).delete(synchronize_session=False)

        # 3) 删除已完成工单（resolved/fixed）
        workorders_deleted = WorkOrder.query.filter(
            WorkOrder.fault_time < cutoff,
            WorkOrder.status.in_(['resolved', 'fixed'])
        ).delete(synchronize_session=False)

        db.session.commit()
        return jsonify({
            'success': True,
            'details': {
                'datapoints_deleted': int(datapoints_deleted or 0),
                'history_deleted': int(history_deleted or 0),
                'workorders_deleted': int(workorders_deleted or 0)
            }
        }), 200
    except Exception as e:
        db.session.rollback()
        return jsonify({'success': False, 'error': str(e)}), 500


@api_bp.route('/admin/clear_all_data', methods=['POST'])
@login_required
def admin_clear_all_data():
    """清空所有历史数据（高危）：波形数据 + 历史曲线数据 + 工单 + 故障快照"""
    try:
        # 注意：保留用户/设备表，避免系统不可登录或设备列表丢失
        datapoints_deleted = DataPoint.query.delete(synchronize_session=False)
        history_deleted = HistoryData.query.delete(synchronize_session=False)
        workorders_deleted = WorkOrder.query.delete(synchronize_session=False)
        snapshots_deleted = FaultSnapshot.query.delete(synchronize_session=False)
        db.session.commit()
        return jsonify({
            'success': True,
            'details': {
                'datapoints_deleted': int(datapoints_deleted or 0),
                'history_deleted': int(history_deleted or 0),
                'workorders_deleted': int(workorders_deleted or 0),
                'snapshots_deleted': int(snapshots_deleted or 0)
            }
        }), 200
    except Exception as e:
        db.session.rollback()
        return jsonify({'success': False, 'error': str(e)}), 500


# ==================== 兼容接口：导出工单（templates/faults.html 使用）====================

@api_bp.route('/workorder/export', methods=['POST'])
@login_required
def export_workorder_docx():
    """
    导出工单 Word 文档（.docx）

    前端（faults.html）会提交：
    - log_id: 工单ID（WorkOrder.id）
    - graph_image: 可选，ECharts dataURL（base64 PNG）
    """
    try:
        payload = request.get_json() or {}
        log_id = payload.get('log_id')
        graph_image = payload.get('graph_image')

        if not log_id:
            return jsonify({'success': False, 'error': '缺少 log_id'}), 400

        order = WorkOrder.query.get(int(log_id))
        if not order:
            return jsonify({'success': False, 'error': f'工单不存在: {log_id}'}), 404

        device = Device.query.filter_by(device_id=order.device_id).first()
        if not device:
            # 兼容：设备可能被清理/未入库
            device = Device(device_id=order.device_id, location=order.location or 'Unassigned', status='offline')

        # 使用“专业排版版”导出模板（与旧版 app.py 效果对齐）
        from edgewind.report_generator import generate_workorder_docx
        doc = generate_workorder_docx(order, device, graph_image_dataurl=graph_image)

        # 写入内存并返回
        buf = BytesIO()
        doc.save(buf)
        buf.seek(0)

        # 文件名：避免同名覆盖
        # - 加入工单ID
        # - 加入微秒级时间戳（同一秒内多次导出也不会重名）
        ts = datetime.now().strftime('%Y%m%d_%H%M%S_%f')
        safe_device = (device.device_id or 'device').replace('/', '_').replace('\\', '_').replace(':', '_')
        filename_cn = f"工单_{safe_device}_ID{int(order.id)}_{ts}.docx"

        # 关键：HTTP 响应头必须是 ASCII/latin-1 可编码内容
        # 如果直接把中文写进 Content-Disposition: filename="..."，可能导致后端在发送响应时抛异常，
        # 浏览器侧表现为 “Failed to fetch”（连接被中断，拿不到响应）。
        # 因此这里用 ASCII 回退名作为 download_name，并通过 filename* / 自定义头传递中文文件名。
        ascii_device = re.sub(r'[^A-Za-z0-9._-]+', '_', safe_device).strip('_') or 'device'
        filename_ascii = f"workorder_{ascii_device}_ID{int(order.id)}_{ts}.docx"

        resp = send_file(
            buf,
            as_attachment=True,
            download_name=filename_ascii,
            mimetype='application/vnd.openxmlformats-officedocument.wordprocessingml.document'
        )

        # 兼容性说明：
        # - filename= 放 ASCII（避免后端编码异常）
        # - filename*=UTF-8''... 放中文（标准写法）
        # - X-EdgeWind-Filename 额外给前端用（URL 编码，ASCII 安全），确保前端下载名一定包含中文
        quoted_cn = quote(filename_cn)
        resp.headers['Content-Disposition'] = f"attachment; filename=\"{filename_ascii}\"; filename*=UTF-8''{quoted_cn}"
        resp.headers['X-EdgeWind-Filename'] = quoted_cn
        return resp

    except Exception as e:
        logger.error(f"[workorder/export] 导出失败: {e}")
        import traceback
        logger.error(traceback.format_exc())
        return jsonify({'success': False, 'error': str(e)}), 500


# ==================== 历史曲线数据API ====================

MAX_HISTORY_LIMIT = 20000  # 最大返回点数

@api_bp.route('/history_nodes', methods=['GET'])
@login_required
def get_history_nodes():
    """
    获取所有有历史数据的节点列表（包括离线节点）
    用于历史曲线页面的节点选择
    """
    try:
        # 从 HistoryData 表中获取所有不重复的 device_id
        from sqlalchemy import func
        history_nodes = db.session.query(
            HistoryData.device_id,
            func.count(HistoryData.id).label('record_count'),
            func.max(HistoryData.timestamp).label('last_record')
        ).group_by(HistoryData.device_id).all()
        
        # 获取当前在线的节点列表
        current_time = time.time()
        _rehydrate_active_nodes_from_db(current_time)
        online_node_ids = set()
        for node_id, node_info in active_nodes.items():
            if current_time - node_info['timestamp'] <= NODE_TIMEOUT:
                online_node_ids.add(node_id)
        
        # 构建节点列表
        nodes = []
        for row in history_nodes:
            device_id = row.device_id
            record_count = row.record_count
            last_record = row.last_record
            is_online = device_id in online_node_ids
            
            # 转换时间为北京时间显示
            last_record_str = None
            if last_record:
                last_record_str = iso_beijing(last_record, with_seconds=True)
            
            nodes.append({
                'device_id': device_id,
                'node_id': device_id,  # 兼容前端
                'status': 'online' if is_online else 'offline',
                'record_count': record_count,
                'last_record': last_record_str
            })
        
        # 按在线状态和记录数排序（在线优先，记录数多的优先）
        nodes.sort(key=lambda x: (0 if x['status'] == 'online' else 1, -x['record_count']))
        
        return jsonify({
            'success': True,
            'nodes': nodes,
            'count': len(nodes)
        })
    except Exception as e:
        logger.error(f"[history_nodes] 获取历史节点列表失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@api_bp.route('/history_data', methods=['GET'])
@login_required
def get_history_data():
    """
    查询历史曲线数据（用于历史曲线回放页面）
    
    参数：
      - device_id: 设备ID（必填）
      - limit: 返回记录数（默认600，最大20000）
      - start_time: 开始时间（可选，格式：YYYY-MM-DD HH:MM:SS 或 ISO格式）
      - end_time: 结束时间（可选，格式：YYYY-MM-DD HH:MM:SS 或 ISO格式）
    
    返回：
      - success: 是否成功
      - data: 时间序列数据数组
      - total: 总记录数（满足条件的）
    """
    try:
        device_id = request.args.get('device_id')
        if not device_id:
            return jsonify({'success': False, 'error': '缺少 device_id 参数'}), 400
        
        # 解析 limit 参数
        try:
            limit = int(request.args.get('limit', 600))
            limit = max(1, min(limit, MAX_HISTORY_LIMIT))
        except ValueError:
            limit = 600
        
        # 解析时间范围参数
        start_time_str = request.args.get('start_time', '').strip()
        end_time_str = request.args.get('end_time', '').strip()
        
        def parse_time(time_str):
            """解析时间字符串，支持多种格式"""
            if not time_str:
                return None
            # 尝试多种格式
            formats = [
                '%Y-%m-%d %H:%M:%S',
                '%Y-%m-%d %H:%M',
                '%Y-%m-%dT%H:%M:%S',
                '%Y-%m-%dT%H:%M:%S%z',
                '%Y-%m-%dT%H:%M:%S+08:00',
                '%Y/%m/%d %H:%M:%S',
                '%Y/%m/%d %H:%M',
            ]
            for fmt in formats:
                try:
                    dt = datetime.strptime(time_str.replace('+08:00', ''), fmt.replace('%z', '').replace('+08:00', ''))
                    # 假设输入是北京时间，转换为 UTC 存储
                    return dt - timedelta(hours=8)
                except ValueError:
                    continue
            return None
        
        start_time = parse_time(start_time_str)
        end_time = parse_time(end_time_str)
        
        # 构建查询
        query = HistoryData.query.filter_by(device_id=device_id)
        
        if start_time:
            query = query.filter(HistoryData.timestamp >= start_time)
        if end_time:
            query = query.filter(HistoryData.timestamp <= end_time)
        
        # 获取总数
        total_count = query.count()
        
        # 按时间排序并限制数量
        # 如果设置了 start_time，从起点向后取 limit 条
        # 如果只设置了 end_time 或都没设置，取最近的 limit 条
        if start_time:
            records = query.order_by(HistoryData.timestamp.asc()).limit(limit).all()
        else:
            records = query.order_by(HistoryData.timestamp.desc()).limit(limit).all()
            records = list(reversed(records))  # 反转为时间正序
        
        # 转换为字典格式
        data = [r.to_dict() for r in records]
        
        return jsonify({
            'success': True,
            'data': data,
            'total': total_count,
            'limit': limit,
            'device_id': device_id,
            'start_time': start_time_str or None,
            'end_time': end_time_str or None
        })
    
    except Exception as e:
        logger.error(f"[history_data] 查询历史数据失败: {e}")
        return jsonify({'success': False, 'error': str(e)}), 500


@api_bp.route('/delete_node_history', methods=['POST'])
@login_required
def delete_node_history():
    """
    删除指定节点的所有历史数据
    
    参数（JSON body）：
        - device_id: 设备ID
    
    返回：
        - success: 是否成功
        - deleted_count: 删除的记录数
    """
    try:
        data = request.get_json() or {}
        device_id = data.get('device_id', '').strip()
        
        if not device_id:
            return jsonify({'success': False, 'error': '缺少 device_id 参数'}), 400
        
        # 查询该节点的历史数据数量
        count = HistoryData.query.filter_by(device_id=device_id).count()
        
        if count == 0:
            return jsonify({
                'success': True,
                'deleted_count': 0,
                'message': f'节点 {device_id} 没有历史数据'
            })
        
        # 删除该节点的所有历史数据
        HistoryData.query.filter_by(device_id=device_id).delete()
        db.session.commit()
        
        logger.info(f"[history_data] 已删除节点 {device_id} 的 {count} 条历史数据")
        
        return jsonify({
            'success': True,
            'deleted_count': count,
            'message': f'已删除节点 {device_id} 的 {count} 条历史数据'
        })
    
    except Exception as e:
        db.session.rollback()
        logger.error(f"[history_data] 删除历史数据失败: {device_id} - {e}")
        return jsonify({'success': False, 'error': str(e)}), 500
