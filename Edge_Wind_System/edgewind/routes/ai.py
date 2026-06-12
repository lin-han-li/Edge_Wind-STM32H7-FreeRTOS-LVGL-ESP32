"""Manual AI diagnosis endpoints for authenticated Web users."""
from __future__ import annotations

from flask import Blueprint, jsonify, request
from flask_login import current_user, login_required

from edgewind.models import AIAnalysisTask
from edgewind.services.ai_service import AIQueueFull, AIService, AIServiceDisabled

ai_bp = Blueprint("ai", __name__, url_prefix="/api/ai")
ai_service: AIService | None = None


def init_ai_blueprint(app):
    global ai_service
    ai_service = AIService(app)


def _service() -> AIService:
    if ai_service is None:
        raise RuntimeError("AI service is not initialized")
    return ai_service


def _force_requested() -> bool:
    value = request.args.get("force")
    if value is None:
        payload = request.get_json(silent=True) or {}
        value = payload.get("force")
    if isinstance(value, bool):
        return value
    return str(value or "").strip().lower() in ("1", "true", "yes", "on")


def _current_user_id() -> int | None:
    return int(current_user.get_id()) if current_user and current_user.is_authenticated else None


@ai_bp.route("/status", methods=["GET"])
@login_required
def ai_status():
    return jsonify({"success": True, "data": _service().status()}), 200


@ai_bp.route("/work-orders/<int:work_order_id>/diagnosis", methods=["POST"])
@login_required
def submit_work_order_diagnosis(work_order_id: int):
    try:
        task, submit_status = _service().submit_work_order(
            work_order_id,
            user_id=_current_user_id(),
            force=_force_requested(),
        )
        return jsonify({
            "success": True,
            "status": submit_status,
            "task": task.to_dict(include_result=True),
        }), 202 if submit_status == "queued" else 200
    except AIServiceDisabled as exc:
        return jsonify({
            "success": False,
            "status": "disabled",
            "error": str(exc),
            "ai": _service().status(),
        }), 200
    except AIQueueFull as exc:
        return jsonify({"success": False, "status": "queue_full", "error": str(exc)}), 429
    except ValueError as exc:
        return jsonify({"success": False, "status": "not_found", "error": str(exc)}), 404


@ai_bp.route("/work-orders/<int:work_order_id>/graph-reasoning", methods=["POST"])
@login_required
def submit_work_order_graph_reasoning(work_order_id: int):
    try:
        task, submit_status = _service().submit_fault_graph(
            work_order_id,
            user_id=_current_user_id(),
            force=_force_requested(),
        )
        return jsonify({
            "success": True,
            "status": submit_status,
            "task": task.to_dict(include_result=True),
        }), 202 if submit_status == "queued" else 200
    except AIServiceDisabled as exc:
        return jsonify({
            "success": False,
            "status": "disabled",
            "error": str(exc),
            "ai": _service().status(),
        }), 200
    except AIQueueFull as exc:
        return jsonify({"success": False, "status": "queue_full", "error": str(exc)}), 429
    except ValueError as exc:
        return jsonify({"success": False, "status": "not_found", "error": str(exc)}), 404


@ai_bp.route("/snapshots/explanation", methods=["POST"])
@login_required
def submit_snapshot_explanation():
    payload = request.get_json(silent=True) or {}
    device_id = str(payload.get("device_id") or "").strip()
    fault_code = str(payload.get("fault_code") or "").strip()
    timestamp = str(payload.get("timestamp") or "").strip()
    if not device_id or not fault_code or not timestamp:
        return jsonify({
            "success": False,
            "status": "bad_request",
            "error": "device_id, fault_code and timestamp are required",
        }), 400
    try:
        task, submit_status = _service().submit_snapshot_explanation(
            device_id=device_id,
            fault_code=fault_code,
            timestamp=timestamp,
            user_id=_current_user_id(),
            force=_force_requested(),
        )
        return jsonify({
            "success": True,
            "status": submit_status,
            "task": task.to_dict(include_result=True),
        }), 202 if submit_status == "queued" else 200
    except AIServiceDisabled as exc:
        return jsonify({
            "success": False,
            "status": "disabled",
            "error": str(exc),
            "ai": _service().status(),
        }), 200
    except AIQueueFull as exc:
        return jsonify({"success": False, "status": "queue_full", "error": str(exc)}), 429
    except ValueError as exc:
        return jsonify({"success": False, "status": "not_found", "error": str(exc)}), 404


@ai_bp.route("/ops/daily-summary", methods=["POST"])
@login_required
def submit_daily_ops_summary():
    try:
        task, submit_status = _service().submit_daily_ops_summary(
            user_id=_current_user_id(),
            force=_force_requested(),
        )
        return jsonify({
            "success": True,
            "status": submit_status,
            "task": task.to_dict(include_result=True),
        }), 202 if submit_status == "queued" else 200
    except AIServiceDisabled as exc:
        return jsonify({
            "success": False,
            "status": "disabled",
            "error": str(exc),
            "ai": _service().status(),
        }), 200
    except AIQueueFull as exc:
        return jsonify({"success": False, "status": "queue_full", "error": str(exc)}), 429


@ai_bp.route("/tasks/<task_id>", methods=["GET"])
@login_required
def get_ai_task(task_id: str):
    task = AIAnalysisTask.query.filter_by(task_id=task_id).first()
    if not task:
        return jsonify({"success": False, "error": "AI task not found"}), 404
    return jsonify({"success": True, "task": task.to_dict(include_result=True)}), 200
