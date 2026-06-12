"""Asynchronous DeepSeek operations assistant service."""
from __future__ import annotations

import json
import logging
import threading
import time
import uuid
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta
from typing import Any

from edgewind.knowledge_graph import generate_ai_report
from edgewind.models import AIAnalysisTask, WorkOrder, db
from edgewind.services.ai_context import (
    build_daily_ops_context,
    build_fault_graph_context,
    build_fault_graph_target_key,
    build_snapshot_context,
    build_work_order_context,
    infer_fault_code,
)
from edgewind.services.deepseek_client import DeepSeekClient, DeepSeekClientError

logger = logging.getLogger(__name__)

TASK_WORK_ORDER = "work_order_diagnosis"
TASK_SNAPSHOT = "snapshot_explanation"
TASK_DAILY_OPS = "daily_ops_summary"
TASK_FAULT_GRAPH = "fault_graph_reasoning"
TASK_TYPES = (TASK_WORK_ORDER, TASK_SNAPSHOT, TASK_DAILY_OPS, TASK_FAULT_GRAPH)
SENTINEL_WORK_ORDER_ID = 0

TASK_TITLES = {
    TASK_WORK_ORDER: "DeepSeek V4 Pro 智能诊断",
    TASK_SNAPSHOT: "DeepSeek V4 Pro 快照证据解释",
    TASK_DAILY_OPS: "DeepSeek V4 Pro 24小时运维简报",
    TASK_FAULT_GRAPH: "DeepSeek V4 Pro 推理知识图谱",
}


class AIServiceDisabled(Exception):
    pass


class AIQueueFull(Exception):
    pass


class AIService:
    """Bounded background service for manual DeepSeek-assisted operations tasks."""

    def __init__(self, app):
        self.app = app
        self.executor = ThreadPoolExecutor(max_workers=1, thread_name_prefix="AI-Worker")
        self.queue_max = max(1, int(app.config.get("EDGEWIND_AI_QUEUE_MAX", 5)))
        # One running worker plus a bounded pending queue.
        self._slots = threading.BoundedSemaphore(self.queue_max + 1)
        self._lock = threading.Lock()
        self._consecutive_failures = 0
        self._circuit_open_until: datetime | None = None

    def _client(self) -> DeepSeekClient:
        return DeepSeekClient(
            api_key=self.app.config.get("DEEPSEEK_API_KEY", ""),
            base_url=self.app.config.get("DEEPSEEK_BASE_URL", "https://api.deepseek.com"),
            model=self.app.config.get("DEEPSEEK_MODEL", "deepseek-v4-pro"),
            timeout_sec=int(self.app.config.get("DEEPSEEK_TIMEOUT_SEC", 20)),
            max_retries=int(self.app.config.get("DEEPSEEK_MAX_RETRIES", 1)),
            max_tokens=int(self.app.config.get("DEEPSEEK_MAX_TOKENS", 1600)),
        )

    @property
    def model(self) -> str:
        return self.app.config.get("DEEPSEEK_MODEL", "deepseek-v4-pro")

    @property
    def prompt_version(self) -> str:
        return self.app.config.get("EDGEWIND_AI_PROMPT_VERSION", "v1")

    def prompt_version_for(self, task_type: str) -> str:
        return f"{task_type}_{self.prompt_version}"

    def enabled(self) -> bool:
        return bool(self.app.config.get("DEEPSEEK_ENABLED")) and bool(self.app.config.get("DEEPSEEK_API_KEY"))

    def status(self) -> dict[str, Any]:
        queued = AIAnalysisTask.query.filter(AIAnalysisTask.status.in_(["queued", "running"])).count()
        open_until = self._circuit_open_until
        return {
            "enabled": bool(self.app.config.get("DEEPSEEK_ENABLED")),
            "api_key_configured": bool(self.app.config.get("DEEPSEEK_API_KEY")),
            "model": self.model,
            "base_url": self.app.config.get("DEEPSEEK_BASE_URL", "https://api.deepseek.com"),
            "queue_depth": int(queued or 0),
            "queue_max": self.queue_max,
            "circuit_open": bool(open_until and datetime.utcnow() < open_until),
            "circuit_open_until": open_until.isoformat() if open_until else None,
            "prompt_version": self.prompt_version,
            "supported_task_types": list(TASK_TYPES),
        }

    def submit_work_order(self, work_order_id: int, *, user_id: int | None, force: bool = False) -> tuple[AIAnalysisTask, str]:
        order = WorkOrder.query.get(work_order_id)
        if not order:
            raise ValueError("work order not found")

        device = getattr(order, "device", None)
        fault_code = infer_fault_code(order, device)
        return self._submit_task(
            task_type=TASK_WORK_ORDER,
            target_key=f"work_order|{order.id}",
            target_label=f"工单 #{order.id}",
            work_order_id=order.id,
            device_id=order.device_id,
            fault_code=fault_code,
            user_id=user_id,
            force=force,
        )

    def submit_snapshot_explanation(
        self,
        *,
        device_id: str,
        fault_code: str,
        timestamp: str,
        user_id: int | None,
        force: bool = False,
    ) -> tuple[AIAnalysisTask, str]:
        # Validate early so the UI gets a useful 404/400 instead of a later failed task.
        build_snapshot_context(
            device_id=device_id,
            fault_code=fault_code,
            timestamp=timestamp,
            max_chars=min(4000, int(self.app.config.get("EDGEWIND_AI_CONTEXT_MAX_CHARS", 12000))),
        )
        return self._submit_task(
            task_type=TASK_SNAPSHOT,
            target_key=f"snapshot|{device_id}|{fault_code}|{timestamp}",
            target_label=f"{device_id} {fault_code} {timestamp}",
            work_order_id=SENTINEL_WORK_ORDER_ID,
            device_id=device_id,
            fault_code=fault_code,
            user_id=user_id,
            force=force,
        )

    def submit_daily_ops_summary(self, *, user_id: int | None, force: bool = False) -> tuple[AIAnalysisTask, str]:
        beijing_day = (datetime.utcnow() + timedelta(hours=8)).strftime("%Y-%m-%d")
        return self._submit_task(
            task_type=TASK_DAILY_OPS,
            target_key=f"daily_ops|{beijing_day}",
            target_label=f"{beijing_day} 24小时运维简报",
            work_order_id=SENTINEL_WORK_ORDER_ID,
            device_id="__fleet__",
            fault_code="OPS",
            user_id=user_id,
            force=force,
        )

    def submit_fault_graph(self, work_order_id: int, *, user_id: int | None, force: bool = False) -> tuple[AIAnalysisTask, str]:
        order = WorkOrder.query.get(work_order_id)
        if not order:
            raise ValueError("work order not found")
        target_key, target_label, fault_code, _summary = build_fault_graph_target_key(work_order_id)
        return self._submit_task(
            task_type=TASK_FAULT_GRAPH,
            target_key=target_key,
            target_label=target_label,
            work_order_id=order.id,
            device_id=order.device_id,
            fault_code=fault_code,
            user_id=user_id,
            force=force,
        )

    def submit_fault_graph_auto(self, work_order_id: int) -> tuple[AIAnalysisTask | None, str]:
        if not self.app.config.get("EDGEWIND_AI_AUTO_GRAPH_ON_FAULT"):
            return None, "auto_disabled"
        try:
            return self.submit_fault_graph(work_order_id, user_id=None, force=False)
        except (AIServiceDisabled, AIQueueFull, ValueError) as exc:
            logger.info("[AI] auto fault graph skipped work_order_id=%s reason=%s", work_order_id, exc)
            return None, "skipped"

    def _submit_task(
        self,
        *,
        task_type: str,
        target_key: str,
        target_label: str,
        work_order_id: int,
        device_id: str,
        fault_code: str,
        user_id: int | None,
        force: bool,
    ) -> tuple[AIAnalysisTask, str]:
        if task_type not in TASK_TYPES:
            raise ValueError(f"unsupported AI task type: {task_type}")
        if not self.enabled():
            raise AIServiceDisabled("DeepSeek AI is disabled or API key is not configured")
        if self._is_circuit_open():
            raise AIServiceDisabled("DeepSeek AI is temporarily circuit-open after recent failures")

        prompt_version = self.prompt_version_for(task_type)
        if not force:
            active = AIAnalysisTask.query.filter(
                AIAnalysisTask.task_type == task_type,
                AIAnalysisTask.target_key == target_key,
                AIAnalysisTask.model == self.model,
                AIAnalysisTask.prompt_version == prompt_version,
                AIAnalysisTask.status.in_(["queued", "running"]),
            ).order_by(AIAnalysisTask.created_at.desc()).first()
            if active:
                return active, "active"

            cached = AIAnalysisTask.query.filter(
                AIAnalysisTask.task_type == task_type,
                AIAnalysisTask.target_key == target_key,
                AIAnalysisTask.model == self.model,
                AIAnalysisTask.prompt_version == prompt_version,
                AIAnalysisTask.status == "succeeded",
            ).order_by(AIAnalysisTask.created_at.desc()).first()
            if cached:
                return cached, "cached"

        if not self._slots.acquire(blocking=False):
            raise AIQueueFull("DeepSeek AI queue is full")

        task = AIAnalysisTask(
            task_id=uuid.uuid4().hex,
            task_type=task_type,
            target_key=target_key,
            target_label=target_label,
            work_order_id=work_order_id,
            device_id=device_id,
            fault_code=fault_code,
            model=self.model,
            prompt_version=prompt_version,
            status="queued",
            created_by_user_id=user_id,
            created_at=datetime.utcnow(),
        )
        db.session.add(task)
        db.session.commit()

        try:
            self.executor.submit(self._run_task, task.task_id)
        except Exception:
            self._slots.release()
            task.status = "failed"
            task.error_message = "failed to enqueue AI worker"
            task.finished_at = datetime.utcnow()
            db.session.commit()
            raise

        return task, "queued"

    def _is_circuit_open(self) -> bool:
        with self._lock:
            return bool(self._circuit_open_until and datetime.utcnow() < self._circuit_open_until)

    def _record_success(self) -> None:
        with self._lock:
            self._consecutive_failures = 0
            self._circuit_open_until = None

    def _record_failure(self) -> None:
        with self._lock:
            self._consecutive_failures += 1
            if self._consecutive_failures >= 3:
                self._circuit_open_until = datetime.utcnow() + timedelta(minutes=2)

    def _run_task(self, task_id: str) -> None:
        try:
            with self.app.app_context():
                self._run_task_in_context(task_id)
        finally:
            try:
                self._slots.release()
            except ValueError:
                pass

    def _run_task_in_context(self, task_id: str) -> None:
        task = AIAnalysisTask.query.filter_by(task_id=task_id).first()
        if not task:
            return

        task.status = "running"
        task.started_at = datetime.utcnow()
        db.session.commit()
        started = time.perf_counter()

        try:
            max_chars = int(self.app.config.get("EDGEWIND_AI_CONTEXT_MAX_CHARS", 12000))
            context, context_hash, context_summary, fault_code = self._build_context(task, max_chars=max_chars)
            task.context_hash = context_hash
            task.sanitized_context_summary = context_summary
            task.fault_code = fault_code
            db.session.commit()

            task_type = task.task_type or TASK_WORK_ORDER
            provider_result = self._client().analyze_context(context, task.prompt_version, task_type)
            normalized = normalize_ai_result(provider_result, task_type=task_type)
            result_text = format_task_recommendation(normalized, model=self.model, task_type=task_type)

            if task_type == TASK_WORK_ORDER:
                order = WorkOrder.query.get(task.work_order_id)
                if order:
                    order.ai_recommendation = result_text

            task.result_json = json.dumps(normalized, ensure_ascii=False)
            task.result_text = result_text
            task.status = "succeeded"
            task.error_message = None
            task.latency_ms = int((time.perf_counter() - started) * 1000)
            task.finished_at = datetime.utcnow()
            db.session.commit()
            self._record_success()
        except DeepSeekClientError as exc:
            db.session.rollback()
            self._mark_failed(task_id, str(exc), started)
            self._record_failure()
        except Exception as exc:
            db.session.rollback()
            logger.exception("[AI] %s failed for task_id=%s", getattr(task, "task_type", "unknown"), task_id)
            self._mark_failed(task_id, str(exc), started)
            self._record_failure()

    def _build_context(self, task: AIAnalysisTask, *, max_chars: int) -> tuple[dict[str, Any], str, str, str]:
        task_type = task.task_type or TASK_WORK_ORDER
        if task_type == TASK_WORK_ORDER:
            return build_work_order_context(task.work_order_id, max_chars=max_chars)
        if task_type == TASK_SNAPSHOT:
            parts = (task.target_key or "").split("|", 3)
            if len(parts) != 4:
                raise ValueError("invalid snapshot AI target")
            _, device_id, fault_code, timestamp = parts
            return build_snapshot_context(
                device_id=device_id,
                fault_code=fault_code,
                timestamp=timestamp,
                max_chars=max_chars,
            )
        if task_type == TASK_DAILY_OPS:
            return build_daily_ops_context(max_chars=max_chars)
        if task_type == TASK_FAULT_GRAPH:
            return build_fault_graph_context(task.work_order_id, max_chars=max_chars)
        raise ValueError(f"unsupported AI task type: {task_type}")

    def _mark_failed(self, task_id: str, error: str, started: float) -> None:
        task = AIAnalysisTask.query.filter_by(task_id=task_id).first()
        if not task:
            return
        if (task.task_type or TASK_WORK_ORDER) == TASK_WORK_ORDER:
            order = WorkOrder.query.get(task.work_order_id)
            if order and not order.ai_recommendation:
                order.ai_recommendation = generate_ai_report(task.fault_code or "E00")
        task.status = "failed"
        task.error_message = sanitize_error(error)
        task.latency_ms = int((time.perf_counter() - started) * 1000)
        task.finished_at = datetime.utcnow()
        db.session.commit()


def sanitize_error(error: str) -> str:
    value = str(error or "AI analysis failed")
    lower = value.lower()
    if "nameresolutionerror" in lower or "failed to resolve" in lower:
        return "DeepSeek API 域名解析失败，请检查当前网络、DNS 或代理/TUN 状态，稍后重试。"
    if "connecttimeout" in lower or "request timed out" in lower or "read timed out" in lower:
        return "DeepSeek API 请求超时，请检查网络连通性，稍后重试。"
    if "authentication failed" in lower or "http 401" in lower or "http 403" in lower:
        return "DeepSeek API 鉴权失败，请检查服务器环境变量中的 DEEPSEEK_API_KEY。"
    if "http 429" in lower:
        return "DeepSeek API 当前限流，请稍后重试。"
    if "temporary error" in lower or "http 5" in lower:
        return "DeepSeek API 服务端临时异常，请稍后重试。"
    return value[:300]


def _dict_to_lines(value: dict[str, Any]) -> list[str]:
    lines: list[str] = []
    if value.get("required") is True:
        lines.append("需要人工复核")
    elif value.get("required") is False:
        lines.append("暂无强制人工复核项")
    for key in ("reason", "summary", "text", "description"):
        if value.get(key):
            lines.append(str(value.get(key))[:500])
            break
    review_items = value.get("review_items") or value.get("items") or value.get("checks")
    if isinstance(review_items, list):
        lines.extend(str(item)[:500] for item in review_items if str(item).strip())
    if not lines:
        lines.append(json.dumps(value, ensure_ascii=False, sort_keys=True)[:500])
    return lines


def _as_list(value: Any) -> list[str]:
    if value is None:
        return []
    if isinstance(value, bool):
        return ["需要人工复核"] if value else []
    if isinstance(value, dict):
        return _dict_to_lines(value)
    if isinstance(value, list):
        result: list[str] = []
        for item in value:
            result.extend(_as_list(item))
        return [item for item in result if item.strip()]
    if isinstance(value, str):
        return [line.strip() for line in value.splitlines() if line.strip()]
    return [str(value)[:500]]


def _as_confidence(value: Any) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return max(0.0, min(1.0, number))


def _as_object_list(value: Any) -> list[dict[str, Any]]:
    if value is None:
        return []
    items = value if isinstance(value, list) else [value]
    result = []
    for item in items:
        if isinstance(item, dict):
            cleaned = {}
            for key, raw in item.items():
                if isinstance(raw, (str, int, float, bool)) or raw is None:
                    cleaned[str(key)[:80]] = raw
                elif isinstance(raw, list):
                    cleaned[str(key)[:80]] = [str(v)[:500] for v in raw[:12]]
                else:
                    cleaned[str(key)[:80]] = str(raw)[:500]
            if cleaned:
                result.append(cleaned)
        elif str(item).strip():
            result.append({"text": str(item)[:500]})
    return result


def normalize_ai_result(result: dict[str, Any], task_type: str | None = None) -> dict[str, Any]:
    confidence_value = result.get("confidence")
    if confidence_value is None:
        confidence_value = result.get("置信度")
    normalized = {
        "summary": str(result.get("summary") or result.get("diagnosis") or result.get("诊断摘要") or "未返回诊断摘要")[:1200],
        "risk_level": str(result.get("risk_level") or result.get("风险等级") or "需人工复核")[:80],
        "confidence": _as_confidence(confidence_value),
        "evidence": _as_list(result.get("evidence") or result.get("证据")),
        "root_causes": _as_list(result.get("root_causes") or result.get("可能根因")),
        "actions": _as_list(result.get("actions") or result.get("建议步骤")),
        "human_review": _as_list(result.get("human_review") or result.get("需要人工确认")),
        "data_gaps": _as_list(result.get("data_gaps") or result.get("数据不足项")),
    }
    if task_type == TASK_FAULT_GRAPH:
        normalized["evidence"] = _as_object_list(result.get("evidence") or result.get("证据"))
        normalized["ranked_causes"] = _as_object_list(result.get("ranked_causes") or result.get("root_causes") or result.get("可能根因"))
        normalized["recommended_actions"] = _as_object_list(result.get("recommended_actions") or result.get("actions") or result.get("建议步骤"))
        normalized["graph_edges"] = _as_object_list(result.get("graph_edges") or result.get("edges"))
        manifest = result.get("evidence_manifest")
        normalized["evidence_manifest"] = manifest if isinstance(manifest, (dict, list)) else {}
    return normalized


def _section(title: str, lines: list[str]) -> list[str]:
    if not lines:
        return [f"{title}: 无"]
    return [f"{title}:"] + [f"- {line}" for line in lines]


def format_task_recommendation(result: dict[str, Any], *, model: str, task_type: str) -> str:
    confidence = result.get("confidence")
    confidence_text = "未给出" if confidence is None else f"{confidence:.2f}"
    title = TASK_TITLES.get(task_type, "DeepSeek V4 Pro 运维建议")
    lines = [
        f"【{title}】",
        f"模型: {model}",
        "说明: 本结果仅作为运维辅助意见，不构成安全认证、停送电许可或最终维修结论。",
        "",
        f"诊断摘要: {result.get('summary') or '无'}",
        f"风险等级: {result.get('risk_level') or '需人工复核'}",
        f"置信度: {confidence_text}",
        "",
    ]
    lines.extend(_section("证据", result.get("evidence") or []))
    lines.append("")
    lines.extend(_section("可能根因", result.get("root_causes") or []))
    lines.append("")
    lines.extend(_section("建议步骤", result.get("actions") or []))
    lines.append("")
    lines.extend(_section("需要人工确认", result.get("human_review") or []))
    lines.append("")
    lines.extend(_section("数据不足项", result.get("data_gaps") or []))
    return "\n".join(lines)


def format_work_order_recommendation(result: dict[str, Any], *, model: str) -> str:
    return format_task_recommendation(result, model=model, task_type=TASK_WORK_ORDER)
