"""Build compact, sanitized context for work-order AI diagnosis."""
from __future__ import annotations

import hashlib
import json
import math
from collections import Counter
from datetime import datetime, timedelta
from statistics import mean, pstdev
from typing import Any

from edgewind.knowledge_graph import FAULT_KNOWLEDGE_GRAPH, get_fault_knowledge_graph, get_fault_reasoning_knowledge
from edgewind.models import Device, FaultSnapshot, HistoryData, WorkOrder


HISTORY_FIELDS = (
    "voltage_pos",
    "voltage_neg",
    "current",
    "leakage",
    "t_igbt_c",
    "t_dc_cap_c",
    "rh_cabinet_pct",
    "wind_load_pct",
)
SNAPSHOT_TYPES = ("before", "after", "before_recovery", "after_recovery")


def _as_float(value: Any) -> float | None:
    try:
        if value is None:
            return None
        result = float(value)
        if math.isnan(result) or math.isinf(result):
            return None
        return result
    except (TypeError, ValueError):
        return None


def _load_numeric_array(raw: str | None, *, max_items: int = 4096) -> list[float]:
    if not raw:
        return []
    try:
        data = json.loads(raw)
    except Exception:
        return []
    if not isinstance(data, list):
        return []
    values: list[float] = []
    for item in data[:max_items]:
        if isinstance(item, dict):
            item = item.get("value", item.get("y", item.get("amplitude")))
        num = _as_float(item)
        if num is not None:
            values.append(num)
    return values


def _series_stats(values: list[float]) -> dict[str, Any]:
    if not values:
        return {"points": 0}
    return {
        "points": len(values),
        "min": round(min(values), 6),
        "max": round(max(values), 6),
        "mean": round(mean(values), 6),
        "std": round(pstdev(values), 6) if len(values) > 1 else 0.0,
    }


def _top_bins(values: list[float], *, limit: int = 12) -> list[dict[str, Any]]:
    indexed = [(idx, abs(value), value) for idx, value in enumerate(values)]
    indexed.sort(key=lambda item: item[1], reverse=True)
    return [{"bin": idx, "amplitude": round(value, 6)} for idx, _, value in indexed[:limit]]


def _round_or_none(value: Any) -> float | None:
    num = _as_float(value)
    return round(num, 6) if num is not None else None


def _truncate_text(text: Any, limit: int) -> str:
    value = str(text or "")
    return value if len(value) <= limit else value[:limit] + "...[truncated]"


def _device_alias(device_id: str | None) -> str:
    digest = hashlib.sha256((device_id or "").encode("utf-8")).hexdigest()[:10]
    return f"NODE_{digest}"


def infer_fault_code(order: WorkOrder, device: Device | None = None) -> str:
    text = f"{order.fault_type or ''} {getattr(device, 'fault_code', '') or ''}"
    for code, info in FAULT_KNOWLEDGE_GRAPH.items():
        if not str(code).startswith("E"):
            continue
        name = str(info.get("name") or "")
        if code in text or (name and (name == order.fault_type or name in text or (order.fault_type or "") in name)):
            return code
    device_fault = (getattr(device, "fault_code", None) or "").strip()
    return device_fault if device_fault and device_fault != "E00" else "E00"


def _knowledge_for_fault(fault_code: str) -> dict[str, Any]:
    info = FAULT_KNOWLEDGE_GRAPH.get(fault_code) or {}
    reasoning_kb = get_fault_reasoning_knowledge(fault_code)
    return {
        "fault_code": fault_code,
        "name": info.get("name"),
        "root_cause": info.get("root_cause"),
        "solution": info.get("solution"),
        "typical_evidence": reasoning_kb.get("typical_evidence") or [],
        "diagnostic_rules": reasoning_kb.get("diagnostic_rules") or [],
        "verification_steps": reasoning_kb.get("verification_steps") or [],
        "root_causes": [
            {
                "name": item.get("name"),
                "description": _truncate_text(item.get("description"), 260),
            }
            for item in (info.get("root_causes") or [])[:6]
            if isinstance(item, dict)
        ],
        "solutions": [
            {
                "name": item.get("name"),
                "description": _truncate_text(item.get("description"), 260),
            }
            for item in (info.get("solutions") or [])[:6]
            if isinstance(item, dict)
        ],
        "detailed_report_excerpt": _truncate_text(info.get("detailed_report"), 1800),
    }


def _snapshot_summary(order: WorkOrder, fault_code: str) -> list[dict[str, Any]]:
    if not order.fault_time:
        return []
    start = order.fault_time - timedelta(minutes=10)
    end = order.fault_time + timedelta(minutes=10)
    query = FaultSnapshot.query.filter(
        FaultSnapshot.device_id == order.device_id,
        FaultSnapshot.timestamp >= start,
        FaultSnapshot.timestamp <= end,
    )
    if fault_code and fault_code != "E00":
        query = query.filter(FaultSnapshot.fault_code == fault_code)
    snapshots = query.order_by(FaultSnapshot.timestamp.desc()).limit(32).all()

    result = []
    for snap in snapshots:
        waveform = _load_numeric_array(snap.waveform_data, max_items=4096)
        fft = _load_numeric_array(snap.fft_data, max_items=2048)
        result.append({
            "snapshot_type": snap.snapshot_type,
            "timestamp_utc": snap.timestamp.isoformat() if snap.timestamp else None,
            "channel_id": snap.channel_id,
            "channel_label": snap.channel_label,
            "channel_type": snap.channel_type,
            "current_value": _round_or_none(snap.current_value),
            "stored_statistics": {
                "mean": _round_or_none(snap.mean_value),
                "std": _round_or_none(snap.std_value),
                "max": _round_or_none(snap.max_value),
                "min": _round_or_none(snap.min_value),
            },
            "waveform_summary": _series_stats(waveform),
            "fft_summary": {
                **_series_stats(fft),
                "top_bins": _top_bins(fft, limit=10),
            },
        })
    return result


def _history_summary(order: WorkOrder) -> dict[str, Any]:
    if order.fault_time:
        start = order.fault_time - timedelta(minutes=60)
        end = order.fault_time + timedelta(minutes=5)
    else:
        end = datetime.utcnow()
        start = end - timedelta(minutes=60)
    rows = HistoryData.query.filter(
        HistoryData.device_id == order.device_id,
        HistoryData.timestamp >= start,
        HistoryData.timestamp <= end,
    ).order_by(HistoryData.timestamp.asc()).limit(3000).all()

    field_stats: dict[str, Any] = {}
    for field in HISTORY_FIELDS:
        values = [_as_float(getattr(row, field, None)) for row in rows]
        numbers = [value for value in values if value is not None]
        stats = _series_stats(numbers)
        if numbers:
            stats.update({
                "first": round(numbers[0], 6),
                "last": round(numbers[-1], 6),
                "delta": round(numbers[-1] - numbers[0], 6),
            })
        field_stats[field] = stats
    return {
        "window_start_utc": start.isoformat(),
        "window_end_utc": end.isoformat(),
        "points": len(rows),
        "fields": field_stats,
    }


def _history_window_summary(device_id: str, start: datetime, end: datetime, *, limit: int = 3000) -> dict[str, Any]:
    rows = HistoryData.query.filter(
        HistoryData.device_id == device_id,
        HistoryData.timestamp >= start,
        HistoryData.timestamp <= end,
    ).order_by(HistoryData.timestamp.asc()).limit(limit).all()

    field_stats: dict[str, Any] = {}
    for field in HISTORY_FIELDS:
        values = [_as_float(getattr(row, field, None)) for row in rows]
        numbers = [value for value in values if value is not None]
        stats = _series_stats(numbers)
        if numbers:
            stats.update({
                "first": round(numbers[0], 6),
                "last": round(numbers[-1], 6),
                "delta": round(numbers[-1] - numbers[0], 6),
            })
        field_stats[field] = stats
    return {
        "window_start_utc": start.isoformat(),
        "window_end_utc": end.isoformat(),
        "points": len(rows),
        "fields": field_stats,
    }


def _snapshot_row_summary(snap: FaultSnapshot) -> dict[str, Any]:
    waveform = _load_numeric_array(snap.waveform_data, max_items=4096)
    fft = _load_numeric_array(snap.fft_data, max_items=2048)
    return {
        "snapshot_type": snap.snapshot_type,
        "timestamp_utc": snap.timestamp.isoformat() if snap.timestamp else None,
        "channel_id": snap.channel_id,
        "channel_label": snap.channel_label,
        "channel_type": snap.channel_type,
        "current_value": _round_or_none(snap.current_value),
        "stored_statistics": {
            "mean": _round_or_none(snap.mean_value),
            "std": _round_or_none(snap.std_value),
            "max": _round_or_none(snap.max_value),
            "min": _round_or_none(snap.min_value),
        },
        "waveform_summary": _series_stats(waveform),
        "fft_summary": {
            **_series_stats(fft),
            "top_bins": _top_bins(fft, limit=10),
        },
    }


def _finalize_context(context: dict[str, Any], max_chars: int) -> tuple[dict[str, Any], str]:
    encoded = json.dumps(context, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    if len(encoded) > max_chars:
        fault = context.get("fault")
        if isinstance(fault, dict):
            fault["detailed_report_excerpt"] = ""
        for snap in context.get("snapshots", []) or []:
            if isinstance(snap, dict):
                snap.get("fft_summary", {}).pop("top_bins", None)
        encoded = json.dumps(context, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    if len(encoded) > max_chars:
        if isinstance(context.get("snapshots"), list):
            context["snapshots"] = context["snapshots"][:12]
        if isinstance(context.get("open_work_orders"), list):
            context["open_work_orders"] = context["open_work_orders"][:12]
        encoded = json.dumps(context, ensure_ascii=False, sort_keys=True, separators=(",", ":"))

    context_hash = hashlib.sha256(encoded.encode("utf-8")).hexdigest()
    return context, context_hash


def _stable_hash(payload: Any) -> str:
    encoded = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(encoded.encode("utf-8")).hexdigest()


def _compact_static_graph(fault_code: str) -> dict[str, Any]:
    graph = get_fault_knowledge_graph(fault_code) or {}
    reasoning_kb = get_fault_reasoning_knowledge(fault_code)
    return {
        "schema_version": graph.get("schema_version"),
        "fault_code": graph.get("fault_code") or fault_code,
        "diagnostic_rules": reasoning_kb.get("diagnostic_rules") or [],
        "typical_evidence": reasoning_kb.get("typical_evidence") or [],
        "verification_steps": reasoning_kb.get("verification_steps") or [],
        "nodes": [
            {
                "id": node.get("id"),
                "name": node.get("name"),
                "node_type": node.get("node_type"),
                "value": _truncate_text(node.get("value"), 220),
            }
            for node in (graph.get("nodes") or [])
            if isinstance(node, dict)
        ],
        "links": [
            {
                "source": link.get("source"),
                "target": link.get("target"),
                "relation": link.get("relation"),
                "value": link.get("value"),
            }
            for link in (graph.get("links") or [])
            if isinstance(link, dict)
        ],
    }


def _fault_graph_material(work_order_id: int) -> dict[str, Any]:
    order = WorkOrder.query.get(work_order_id)
    if not order:
        raise ValueError("work order not found")

    device = Device.query.filter_by(device_id=order.device_id).first()
    fault_code = infer_fault_code(order, device)
    if not fault_code or fault_code == "E00":
        raise ValueError("work order has no actionable fault code")

    static_graph = _compact_static_graph(fault_code)
    snapshots = _snapshot_summary(order, fault_code)
    history = _history_summary(order)
    kg_hash = _stable_hash(static_graph)[:12]
    evidence_hash = _stable_hash({
        "work_order": {
            "id": order.id,
            "fault_time_utc": order.fault_time.isoformat() if order.fault_time else None,
            "fault_type": order.fault_type,
            "status": order.status,
        },
        "snapshots": snapshots,
        "history": history,
    })[:12]
    return {
        "order": order,
        "device": device,
        "fault_code": fault_code,
        "static_graph": static_graph,
        "snapshots": snapshots,
        "history": history,
        "kg_hash": kg_hash,
        "evidence_hash": evidence_hash,
    }


def build_fault_graph_target_key(work_order_id: int) -> tuple[str, str, str, str]:
    material = _fault_graph_material(work_order_id)
    order = material["order"]
    fault_code = material["fault_code"]
    kg_hash = material["kg_hash"]
    evidence_hash = material["evidence_hash"]
    target_key = f"fault_graph|work_order|{order.id}|{fault_code}|kg:{kg_hash}|ev:{evidence_hash}"
    target_label = f"工单 #{order.id} 推理图谱"
    summary = (
        f"fault_graph work_order={order.id} fault_code={fault_code} "
        f"kg_hash={kg_hash} evidence_hash={evidence_hash}"
    )
    return target_key, target_label, fault_code, summary


def build_fault_graph_context(work_order_id: int, *, max_chars: int) -> tuple[dict[str, Any], str, str, str]:
    material = _fault_graph_material(work_order_id)
    order = material["order"]
    device = material["device"]
    fault_code = material["fault_code"]

    context = {
        "scope": "fault_graph_reasoning",
        "safety_policy": {
            "advisory_only": True,
            "no_device_commands": True,
            "require_human_review": True,
            "no_final_safety_clearance": True,
        },
        "work_order": {
            "id": order.id,
            "fault_time_utc": order.fault_time.isoformat() if order.fault_time else None,
            "fault_type": order.fault_type,
            "status": order.status,
            "location": order.location,
        },
        "device": {
            "alias": _device_alias(order.device_id),
            "status": getattr(device, "status", None),
            "fault_code": getattr(device, "fault_code", None),
            "hw_version": getattr(device, "hw_version", None),
            "last_heartbeat_utc": device.last_heartbeat.isoformat() if getattr(device, "last_heartbeat", None) else None,
        },
        "fault": _knowledge_for_fault(fault_code),
        "knowledge_graph": material["static_graph"],
        "snapshots": material["snapshots"],
        "history": material["history"],
        "hashes": {
            "knowledge_graph_hash": material["kg_hash"],
            "evidence_hash": material["evidence_hash"],
        },
        "output_contract": {
            "schema_version": "edgewind.reasoning_graph.v1",
            "must_use_existing_node_ids": True,
            "unmatched_evidence_allowed": True,
            "do_not_return_thinking": True,
        },
        "data_contract": {
            "raw_upload_not_sent_to_llm": True,
            "waveform_full_points": 4096,
            "fft_full_points": 2048,
            "context_is_feature_summary": True,
            "real_device_id_not_sent": True,
        },
    }
    context, context_hash = _finalize_context(context, max_chars)
    summary = (
        f"fault_graph work_order={order.id} device_alias={context['device']['alias']} "
        f"fault_code={fault_code} kg_hash={material['kg_hash']} ev_hash={material['evidence_hash']} "
        f"snapshots={len(context.get('snapshots', []))} history_points={context.get('history', {}).get('points', 0)}"
    )
    return context, context_hash, summary, fault_code


def build_work_order_context(work_order_id: int, *, max_chars: int) -> tuple[dict[str, Any], str, str, str]:
    order = WorkOrder.query.get(work_order_id)
    if not order:
        raise ValueError("work order not found")

    device = Device.query.filter_by(device_id=order.device_id).first()
    fault_code = infer_fault_code(order, device)

    context = {
        "scope": "work_order_diagnosis",
        "safety_policy": {
            "advisory_only": True,
            "no_device_commands": True,
            "require_human_review": True,
        },
        "work_order": {
            "id": order.id,
            "fault_time_utc": order.fault_time.isoformat() if order.fault_time else None,
            "fault_type": order.fault_type,
            "status": order.status,
            "location": order.location,
        },
        "device": {
            "alias": _device_alias(order.device_id),
            "status": getattr(device, "status", None),
            "fault_code": getattr(device, "fault_code", None),
            "hw_version": getattr(device, "hw_version", None),
            "last_heartbeat_utc": device.last_heartbeat.isoformat() if getattr(device, "last_heartbeat", None) else None,
        },
        "fault": _knowledge_for_fault(fault_code),
        "snapshots": _snapshot_summary(order, fault_code),
        "history": _history_summary(order),
        "data_contract": {
            "raw_upload_not_sent_to_llm": True,
            "waveform_full_points": 4096,
            "fft_full_points": 2048,
            "context_is_feature_summary": True,
        },
    }

    context, context_hash = _finalize_context(context, max_chars)
    summary = (
        f"work_order={order.id} device_alias={context['device']['alias']} "
        f"fault_code={fault_code} snapshots={len(context.get('snapshots', []))} "
        f"history_points={context.get('history', {}).get('points', 0)}"
    )
    return context, context_hash, summary, fault_code


def build_snapshot_context(
    *,
    device_id: str,
    fault_code: str,
    timestamp: str,
    max_chars: int,
) -> tuple[dict[str, Any], str, str, str]:
    try:
        local_time = datetime.strptime(str(timestamp), "%Y-%m-%d %H:%M:%S")
    except ValueError as exc:
        raise ValueError("snapshot timestamp must be YYYY-MM-DD HH:MM:SS") from exc

    utc_start = local_time - timedelta(hours=8)
    utc_end = utc_start + timedelta(seconds=1)
    snaps = FaultSnapshot.query.filter(
        FaultSnapshot.device_id == device_id,
        FaultSnapshot.fault_code == fault_code,
        FaultSnapshot.timestamp >= utc_start,
        FaultSnapshot.timestamp < utc_end,
    ).order_by(FaultSnapshot.snapshot_type.asc(), FaultSnapshot.channel_id.asc()).all()
    if not snaps:
        raise ValueError("snapshot event not found")

    device = Device.query.filter_by(device_id=device_id).first()
    snapshots = [_snapshot_row_summary(snap) for snap in snaps]
    type_counts = Counter(snap.snapshot_type for snap in snaps)
    channel_counts = Counter(str(snap.channel_id) for snap in snaps)
    history_start = utc_start - timedelta(minutes=10)
    history_end = utc_start + timedelta(minutes=10)

    context = {
        "scope": "snapshot_explanation",
        "safety_policy": {
            "advisory_only": True,
            "no_device_commands": True,
            "require_human_review": True,
        },
        "event": {
            "device_alias": _device_alias(device_id),
            "fault_code": fault_code,
            "timestamp_local": timestamp,
            "timestamp_utc": utc_start.isoformat(),
            "snapshot_types": dict(type_counts),
            "channels": dict(channel_counts),
        },
        "device": {
            "alias": _device_alias(device_id),
            "status": getattr(device, "status", None),
            "fault_code": getattr(device, "fault_code", None),
            "hw_version": getattr(device, "hw_version", None),
            "last_heartbeat_utc": device.last_heartbeat.isoformat() if getattr(device, "last_heartbeat", None) else None,
        },
        "fault": _knowledge_for_fault(fault_code),
        "snapshots": snapshots,
        "history": _history_window_summary(device_id, history_start, history_end),
        "data_contract": {
            "raw_upload_not_sent_to_llm": True,
            "waveform_full_points": 4096,
            "fft_full_points": 2048,
            "context_is_feature_summary": True,
        },
    }
    context, context_hash = _finalize_context(context, max_chars)
    summary = (
        f"snapshot device_alias={context['device']['alias']} fault_code={fault_code} "
        f"timestamp={timestamp} snapshots={len(context.get('snapshots', []))}"
    )
    return context, context_hash, summary, fault_code


def build_daily_ops_context(*, max_chars: int) -> tuple[dict[str, Any], str, str, str]:
    now = datetime.utcnow()
    start = now - timedelta(hours=24)
    devices = Device.query.order_by(Device.device_id.asc()).all()
    orders = WorkOrder.query.filter(WorkOrder.fault_time >= start).order_by(WorkOrder.fault_time.desc()).all()
    open_orders = WorkOrder.query.filter(
        WorkOrder.status.in_(["pending", "processing"])
    ).order_by(WorkOrder.fault_time.desc()).limit(30).all()

    device_status = Counter((d.status or "unknown") for d in devices)
    recent_faults = []
    fault_type_counts: Counter[str] = Counter()
    fault_code_counts: Counter[str] = Counter()
    device_fault_counts: Counter[str] = Counter()
    device_code_counts: Counter[tuple[str, str]] = Counter()
    for order in orders:
        device = Device.query.filter_by(device_id=order.device_id).first()
        code = infer_fault_code(order, device)
        fault_type = order.fault_type or code or "未知故障"
        fault_type_counts[fault_type] += 1
        fault_code_counts[code] += 1
        device_fault_counts[order.device_id] += 1
        device_code_counts[(order.device_id, code)] += 1
        recent_faults.append({
            "work_order_id": order.id,
            "device_alias": _device_alias(order.device_id),
            "fault_time_utc": order.fault_time.isoformat() if order.fault_time else None,
            "fault_type": fault_type,
            "fault_code": code,
            "status": order.status,
        })

    repeat_clusters = [
        {
            "device_alias": _device_alias(device_id),
            "fault_code": code,
            "count_24h": count,
        }
        for (device_id, code), count in device_code_counts.most_common(12)
        if count >= 2
    ]
    open_order_payload = [
        {
            "work_order_id": order.id,
            "device_alias": _device_alias(order.device_id),
            "fault_time_utc": order.fault_time.isoformat() if order.fault_time else None,
            "fault_type": order.fault_type,
            "status": order.status,
        }
        for order in open_orders
    ]
    context = {
        "scope": "daily_ops_summary",
        "safety_policy": {
            "advisory_only": True,
            "no_device_commands": True,
            "require_human_review": True,
        },
        "window": {
            "start_utc": start.isoformat(),
            "end_utc": now.isoformat(),
            "hours": 24,
        },
        "fleet": {
            "device_count": len(devices),
            "status_counts": dict(device_status),
            "devices_with_faults_24h": len(device_fault_counts),
        },
        "faults_24h": {
            "total": len(orders),
            "by_fault_type": dict(fault_type_counts.most_common()),
            "by_fault_code": dict(fault_code_counts.most_common()),
            "repeat_clusters": repeat_clusters,
            "recent": recent_faults[:20],
        },
        "open_work_orders": open_order_payload,
        "data_contract": {
            "raw_upload_not_sent_to_llm": True,
            "summary_is_from_database": True,
            "no_realtime_frame_data": True,
        },
    }
    context, context_hash = _finalize_context(context, max_chars)
    summary = (
        f"daily_ops faults_24h={len(orders)} devices={len(devices)} "
        f"open_orders={len(open_order_payload)} repeats={len(repeat_clusters)}"
    )
    return context, context_hash, summary, "OPS"
