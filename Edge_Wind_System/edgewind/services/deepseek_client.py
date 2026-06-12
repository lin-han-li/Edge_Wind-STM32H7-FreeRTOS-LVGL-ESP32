"""DeepSeek chat-completions client used by the asynchronous AI worker."""
from __future__ import annotations

import json
import logging
import re
from typing import Any

import requests

logger = logging.getLogger(__name__)


class DeepSeekClientError(Exception):
    """Provider error with retry metadata."""

    def __init__(self, message: str, *, status_code: int | None = None, retryable: bool = False):
        super().__init__(message)
        self.status_code = status_code
        self.retryable = retryable


def _strip_json_fence(text: str) -> str:
    value = (text or "").strip()
    match = re.match(r"^```(?:json)?\s*(.*?)\s*```$", value, re.IGNORECASE | re.DOTALL)
    return match.group(1).strip() if match else value


def parse_json_object(text: str) -> dict[str, Any]:
    """Parse a provider JSON-object response, allowing a fenced JSON fallback."""
    value = _strip_json_fence(text)
    try:
        parsed = json.loads(value)
    except json.JSONDecodeError as exc:
        raise DeepSeekClientError("DeepSeek returned non-JSON content", retryable=False) from exc
    if not isinstance(parsed, dict):
        raise DeepSeekClientError("DeepSeek returned JSON that is not an object", retryable=False)
    return parsed


class DeepSeekClient:
    """Minimal OpenAI-compatible DeepSeek client.

    It intentionally uses the existing requests dependency instead of adding a
    new SDK dependency to the eventlet/gunicorn deployment.
    """

    def __init__(
        self,
        *,
        api_key: str,
        base_url: str,
        model: str,
        timeout_sec: int,
        max_retries: int,
        max_tokens: int,
    ):
        self.api_key = api_key
        self.base_url = (base_url or "https://api.deepseek.com").rstrip("/")
        self.model = model or "deepseek-v4-pro"
        self.timeout_sec = max(3, int(timeout_sec or 20))
        self.max_retries = max(0, int(max_retries or 0))
        self.max_tokens = max(256, int(max_tokens or 1600))

    @property
    def chat_completions_url(self) -> str:
        return f"{self.base_url}/chat/completions"

    def analyze_context(self, context: dict[str, Any], prompt_version: str, task_type: str) -> dict[str, Any]:
        if not self.api_key:
            raise DeepSeekClientError("DeepSeek API key is not configured", status_code=None, retryable=False)

        task_prompts = {
            "work_order_diagnosis": "请基于以下 EdgeWind 工单上下文生成中文诊断建议。",
            "snapshot_explanation": "请基于以下 EdgeWind 故障快照统计生成中文证据解释和复核建议。",
            "daily_ops_summary": "请基于以下 EdgeWind 近24小时运维上下文生成中文运维简报和风险排序建议。",
            "fault_graph_reasoning": "请基于以下 EdgeWind 工单、快照摘要、历史摘要和本地知识图谱，生成有证据依据的工单级推理知识图谱叠加数据。",
        }
        task_prompt = task_prompts.get(task_type, "请基于以下 EdgeWind 结构化上下文生成中文运维建议。")
        if task_type == "fault_graph_reasoning":
            output_contract = (
                "输出 JSON 字段必须包含：summary, risk_level, confidence, evidence, ranked_causes, "
                "recommended_actions, graph_edges, human_review, data_gaps, evidence_manifest。"
                "evidence 必须是对象数组，每项包含 id,text,source_node_ids 或 node_ids,confidence。"
                "ranked_causes/recommended_actions 必须引用 knowledge_graph.nodes 中已有 node_id 或 name；"
                "允许 evidence 描述本次事件专属证据或指标，但新根因必须映射到本地专家库已有根因节点；"
                "graph_edges 用来表达“根因 -> 建议动作”的推理路径，只能使用已有节点 id/name 作为 source/target，"
                "source 优先选根因节点，target 优先选解决方案节点；每条边必须给出 relation,confidence,explanation,evidence_refs。"
                "必须结合 knowledge_graph.diagnostic_rules、typical_evidence、verification_steps 和本次快照/历史摘要排序，不能只复述静态图谱。"
                "不能自由虚构新根因节点。"
                "没有匹配依据的证据必须保留为 unmatched，不要硬连到根因或方案节点。"
            )
        else:
            output_contract = (
                "输出 JSON 字段必须包含：summary, risk_level, confidence, evidence, "
                "root_causes, actions, human_review, data_gaps。"
                "其中 evidence/root_causes/actions/human_review/data_gaps 必须是字符串数组，"
                "confidence 为 0 到 1 的数字。"
            )

        messages = [
            {
                "role": "system",
                "content": (
                    "你是 EdgeWind 风电直流系统故障诊断助手。你只能基于用户提供的结构化证据分析，"
                    "不能编造未提供的测量值，不能给出送电、复位、绕过保护、自动控制或最终安全许可。"
                    "涉及停机、隔离、复位、送电和部件更换时，必须要求具备资质的现场人员按规程复核。"
                    "必须返回一个 JSON object。"
                ),
            },
            {
                "role": "user",
                "content": (
                    task_prompt +
                    output_contract +
                    "prompt_version="
                    f"{prompt_version}\n\n"
                    f"{json.dumps(context, ensure_ascii=False, separators=(',', ':'))}"
                ),
            },
        ]

        payload = {
            "model": self.model,
            "messages": messages,
            "temperature": 0.2,
            "max_tokens": self.max_tokens,
            "response_format": {"type": "json_object"},
            # V4 Pro enables thinking by default; for short JSON diagnosis we
            # need the answer in message.content, not only reasoning_content.
            "thinking": {"type": "disabled"},
        }
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json",
        }

        last_error: DeepSeekClientError | None = None
        attempts = self.max_retries + 1
        for attempt in range(attempts):
            try:
                response = requests.post(
                    self.chat_completions_url,
                    headers=headers,
                    json=payload,
                    timeout=(min(5, self.timeout_sec), self.timeout_sec),
                )
            except requests.Timeout as exc:
                last_error = DeepSeekClientError("DeepSeek request timed out", retryable=True)
                if attempt + 1 < attempts:
                    continue
                raise last_error from exc
            except requests.RequestException as exc:
                detail = str(exc).replace(self.api_key, "***")[:220]
                last_error = DeepSeekClientError(
                    f"DeepSeek request failed: {type(exc).__name__}: {detail}",
                    retryable=True,
                )
                if attempt + 1 < attempts:
                    continue
                raise last_error from exc

            if response.status_code >= 500 or response.status_code == 429:
                last_error = DeepSeekClientError(
                    f"DeepSeek temporary error: HTTP {response.status_code}",
                    status_code=response.status_code,
                    retryable=True,
                )
                if attempt + 1 < attempts:
                    continue
                raise last_error

            if response.status_code in (401, 403):
                raise DeepSeekClientError(
                    "DeepSeek authentication failed",
                    status_code=response.status_code,
                    retryable=False,
                )

            if response.status_code >= 400:
                raise DeepSeekClientError(
                    f"DeepSeek request rejected: HTTP {response.status_code}",
                    status_code=response.status_code,
                    retryable=False,
                )

            try:
                body = response.json()
            except ValueError as exc:
                raise DeepSeekClientError("DeepSeek returned invalid JSON envelope", retryable=False) from exc

            try:
                content = body["choices"][0]["message"]["content"]
            except (KeyError, IndexError, TypeError) as exc:
                raise DeepSeekClientError("DeepSeek response envelope missing message content", retryable=False) from exc
            if not str(content or "").strip():
                finish_reason = None
                reasoning_len = 0
                try:
                    choice = body["choices"][0]
                    finish_reason = choice.get("finish_reason")
                    reasoning_len = len(choice.get("message", {}).get("reasoning_content") or "")
                except Exception:
                    pass
                raise DeepSeekClientError(
                    f"DeepSeek returned empty content finish_reason={finish_reason} reasoning_len={reasoning_len}",
                    retryable=False,
                )
            return parse_json_object(content)

        raise last_error or DeepSeekClientError("DeepSeek request failed", retryable=True)

    def diagnose_work_order(self, context: dict[str, Any], prompt_version: str) -> dict[str, Any]:
        return self.analyze_context(context, prompt_version, "work_order_diagnosis")
