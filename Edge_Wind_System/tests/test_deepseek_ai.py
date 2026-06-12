import os
import sys
import unittest
from unittest.mock import patch

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from edgewind.knowledge_graph import REASONING_GRAPH_SCHEMA_VERSION, build_reasoning_graph, get_fault_knowledge_graph
from edgewind.services.ai_service import AIService, TASK_FAULT_GRAPH, format_task_recommendation, format_work_order_recommendation, normalize_ai_result
from edgewind.services.deepseek_client import DeepSeekClient, DeepSeekClientError, parse_json_object


class FakeResponse:
    def __init__(self, status_code=200, body=None):
        self.status_code = status_code
        self._body = body if body is not None else {
            "choices": [{"message": {"content": '{"summary":"ok","confidence":0.7}'}}]
        }

    def json(self):
        return self._body


class DeepSeekAITests(unittest.TestCase):
    def test_parse_json_object_accepts_fenced_json(self):
        parsed = parse_json_object('```json\n{"summary":"ok","confidence":0.8}\n```')
        self.assertEqual(parsed["summary"], "ok")
        self.assertEqual(parsed["confidence"], 0.8)

    def test_parse_json_object_rejects_free_text(self):
        with self.assertRaises(DeepSeekClientError):
            parse_json_object("not json")

    def test_parse_json_object_rejects_non_object_json(self):
        with self.assertRaises(DeepSeekClientError):
            parse_json_object("[1, 2]")

    def test_normalize_ai_result_clamps_confidence_and_lists(self):
        result = normalize_ai_result({
            "summary": "诊断",
            "risk_level": "高",
            "confidence": 2,
            "evidence": "证据1\n证据2",
            "actions": ["检查A"],
        })
        self.assertEqual(result["confidence"], 1.0)
        self.assertEqual(result["evidence"], ["证据1", "证据2"])
        self.assertEqual(result["actions"], ["检查A"])

    def test_normalize_fault_graph_result_preserves_structured_fields(self):
        result = normalize_ai_result({
            "summary": "图谱推理",
            "confidence": 0.6,
            "evidence": [{"id": "ev1", "text": "FFT bin 12 异常", "node_ids": ["E01_cause_1"]}],
            "ranked_causes": [{"node_id": "E01_cause_1", "confidence": 0.7}],
            "recommended_actions": [{"node_id": "E01_solution_4", "text": "人工复核"}],
            "graph_edges": [{"source": "E01_cause_1", "target": "E01_solution_4"}],
            "evidence_manifest": {"kg_hash": "abc"},
        }, task_type=TASK_FAULT_GRAPH)
        self.assertEqual(result["evidence"][0]["id"], "ev1")
        self.assertEqual(result["ranked_causes"][0]["node_id"], "E01_cause_1")
        self.assertEqual(result["evidence_manifest"]["kg_hash"], "abc")

    def test_static_knowledge_graph_has_reasoning_schema(self):
        graph = get_fault_knowledge_graph("E01")
        self.assertEqual(graph["schema_version"], REASONING_GRAPH_SCHEMA_VERSION)
        self.assertEqual(graph["reasoning"]["mode"], "static")
        self.assertIn("node_type", graph["nodes"][0])
        self.assertGreaterEqual(len(graph["nodes"]), 30)
        self.assertGreaterEqual(len(graph["links"]), 40)
        category_names = {category["name"] for category in graph["categories"]}
        self.assertIn("证据", category_names)
        self.assertIn("人工复核", category_names)
        self.assertIn("风险", category_names)

    def test_build_reasoning_graph_overlays_evidence(self):
        graph = build_reasoning_graph("E01", result={
            "summary": "证据命中交流窜入",
            "risk_level": "高",
            "confidence": 0.8,
            "evidence": [{"id": "ev1", "text": "交流分量升高", "node_ids": ["E01_cause_1"], "confidence": 0.8}],
            "ranked_causes": [{"node_id": "E01_cause_1", "confidence": 0.8, "evidence_refs": ["ev1"]}],
            "recommended_actions": [{"node_id": "E01_solution_4", "text": "检查电容C1", "evidence_refs": ["ev1"]}],
        }, work_order_id=123)
        self.assertEqual(graph["reasoning"]["mode"], "hybrid")
        self.assertEqual(graph["evidence"][0]["status"], "matched")
        self.assertEqual(graph["graph_view"], "event")
        self.assertIn("graph_views", graph)
        self.assertLess(len(graph["graph_views"]["event"]["nodes"]), len(graph["graph_views"]["full"]["nodes"]))
        matched = [node for node in graph["nodes"] if node.get("id") == "E01_cause_1"][0]
        self.assertEqual(matched["status"], "matched")
        self.assertTrue([node for node in graph["nodes"] if node.get("node_type") == "evidence"])
        self.assertTrue([node for node in graph["nodes"] if node.get("source") == "deepseek"])
        self.assertEqual(graph["reasoning"]["paths"][0]["source"], "E01_cause_1")
        self.assertEqual(graph["reasoning"]["paths"][0]["target"], "E01_solution_4")
        self.assertTrue([link for link in graph["links"] if link.get("relation") == "ai_reasoning_path"])
        self.assertTrue([link for link in graph["links"] if link.get("relation") == "ai_evidence_supports"])

    def test_same_fault_code_can_build_different_event_subgraphs(self):
        first = build_reasoning_graph("E01", result={
            "summary": "母线纹波证据命中交流窜入",
            "confidence": 0.8,
            "evidence": [{"id": "ev1", "text": "交流分量升高", "node_ids": ["E01_cause_1"], "confidence": 0.8}],
            "ranked_causes": [{"node_id": "E01_cause_1", "confidence": 0.8, "evidence_refs": ["ev1"]}],
            "recommended_actions": [{"node_id": "E01_solution_4", "text": "检查电容 C1", "evidence_refs": ["ev1"]}],
        })
        second = build_reasoning_graph("E01", result={
            "summary": "接地共模噪声证据命中 SPD 泄漏",
            "confidence": 0.7,
            "evidence": [{"id": "ev2", "text": "SPD 漏电和接地共模噪声", "node_ids": ["E01_cause_extra_5"], "confidence": 0.7}],
            "ranked_causes": [{"node_id": "E01_cause_extra_5", "confidence": 0.7, "evidence_refs": ["ev2"]}],
            "recommended_actions": [{"node_id": "E01_action_5", "text": "更换泄漏 SPD", "evidence_refs": ["ev2"]}],
        })
        first_ids = {node["id"] for node in first["graph_views"]["event"]["nodes"]}
        second_ids = {node["id"] for node in second["graph_views"]["event"]["nodes"]}
        self.assertIn("E01_cause_1", first_ids)
        self.assertIn("E01_cause_extra_5", second_ids)
        self.assertNotEqual(first_ids, second_ids)

    def test_format_work_order_recommendation_contains_safety_note(self):
        text = format_work_order_recommendation(
            normalize_ai_result({"summary": "需要人工复核", "confidence": 0.5}),
            model="deepseek-v4-pro",
        )
        self.assertIn("DeepSeek V4 Pro", text)
        self.assertIn("不构成安全认证", text)
        self.assertIn("需要人工复核", text)

    def test_format_task_recommendation_daily_summary_title(self):
        text = format_task_recommendation(
            normalize_ai_result({"summary": "24小时内无新增高风险故障"}),
            model="deepseek-v4-pro",
            task_type="daily_ops_summary",
        )
        self.assertIn("24小时运维简报", text)
        self.assertIn("24小时内无新增高风险故障", text)

    def test_ai_service_task_prompt_version(self):
        class App:
            config = {
                "EDGEWIND_AI_QUEUE_MAX": 5,
                "EDGEWIND_AI_PROMPT_VERSION": "v1",
            }

        service = AIService(App())
        try:
            self.assertEqual(service.prompt_version_for("snapshot_explanation"), "snapshot_explanation_v1")
        finally:
            service.executor.shutdown(wait=False)

    def test_deepseek_client_analyze_context_parses_json_and_disables_thinking(self):
        client = DeepSeekClient(
            api_key="sk-test",
            base_url="https://api.deepseek.com",
            model="deepseek-v4-pro",
            timeout_sec=20,
            max_retries=0,
            max_tokens=256,
        )
        with patch("edgewind.services.deepseek_client.requests.post", return_value=FakeResponse()) as post:
            result = client.analyze_context({"scope": "daily_ops_summary"}, "daily_ops_summary_v1", "daily_ops_summary")
        self.assertEqual(result["summary"], "ok")
        payload = post.call_args.kwargs["json"]
        self.assertEqual(payload["thinking"], {"type": "disabled"})
        self.assertEqual(payload["response_format"], {"type": "json_object"})
        self.assertIn("24小时运维上下文", payload["messages"][1]["content"])

    def test_deepseek_client_fault_graph_prompt_contract(self):
        client = DeepSeekClient(
            api_key="sk-test",
            base_url="https://api.deepseek.com",
            model="deepseek-v4-pro",
            timeout_sec=20,
            max_retries=0,
            max_tokens=256,
        )
        with patch("edgewind.services.deepseek_client.requests.post", return_value=FakeResponse()) as post:
            client.analyze_context({"scope": "fault_graph_reasoning"}, "fault_graph_reasoning_v1", "fault_graph_reasoning")
        payload = post.call_args.kwargs["json"]
        content = payload["messages"][1]["content"]
        self.assertIn("ranked_causes", content)
        self.assertIn("graph_edges", content)
        self.assertIn("允许 evidence 描述本次事件专属证据", content)
        self.assertIn("不能自由虚构新根因节点", content)

    def test_deepseek_client_empty_content_reports_reasoning_len(self):
        body = {
            "choices": [{
                "finish_reason": "stop",
                "message": {"content": "", "reasoning_content": "hidden reasoning"},
            }]
        }
        client = DeepSeekClient(
            api_key="sk-test",
            base_url="https://api.deepseek.com",
            model="deepseek-v4-pro",
            timeout_sec=20,
            max_retries=0,
            max_tokens=256,
        )
        with patch("edgewind.services.deepseek_client.requests.post", return_value=FakeResponse(body=body)):
            with self.assertRaises(DeepSeekClientError) as cm:
                client.analyze_context({"scope": "work_order_diagnosis"}, "work_order_diagnosis_v1", "work_order_diagnosis")
        self.assertIn("empty content", str(cm.exception))


if __name__ == "__main__":
    unittest.main()
