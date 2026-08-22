import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class FixRound2RegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.dashboard = (ROOT / "include/web/mcp2515_dashboard.h").read_text(
            encoding="utf-8"
        )
        cls.controller = (ROOT / "include/nag_adaptive_controller.h").read_text(
            encoding="utf-8"
        )
        cls.config_input = (ROOT / "include/nag_adaptive_config_input.h").read_text(
            encoding="utf-8"
        )
        cls.readme_zh = (ROOT / "README.zh-CN.md").read_text(encoding="utf-8")

    def _function_body(self, name: str, next_name: str) -> str:
        start = self.dashboard.index(name)
        end = self.dashboard.index(next_name, start)
        return self.dashboard[start:end]

    def test_apply_result_distinguishes_invalid_unchanged_and_changed(self) -> None:
        for token in ("INVALID", "VALID_UNCHANGED", "VALID_CHANGED"):
            self.assertIn(token, self.config_input)
        apply_body = self._function_body(
            "static NagAdaptiveConfigInput::ApplyResult dashApplyNagConfigArgs(\n"
            "    DashNagConfigError &error)\n{",
            "static void handleRoot",
        )
        self.assertIn("shouldPublishCommand", apply_body)
        self.assertIn("true, previous.mode, request.mode,", apply_body)

    def test_only_invalid_rejects_handlers_and_noop_reaches_nonadaptive_state(self) -> None:
        handlers = (
            ("static void handleConfig()", "static void handleNagApiStats()"),
            ("static void handleNagApiMode()", "static void handleNagApiUpdate()"),
            ("static void handleNagApiUpdate()", "static void handleNagAdaptiveApiGet()"),
            ("static void handleNagAdaptiveApiPost()", "static void handleLoggingConfig()"),
        )
        for start, end in handlers:
            body = self._function_body(start, end)
            self.assertIn("shouldRejectRequest", body)
            self.assertNotRegex(body, r"if\s*\(\s*!dashApplyNagConfigArgs")
        config_body = self._function_body(
            "static void handleConfig()", "static void handleNagApiStats()"
        )
        reject_at = config_body.index("shouldRejectRequest")
        can_at = config_body.index('server.hasArg("can")')
        self.assertLess(reject_at, can_at)
        self.assertIn('server.arg("can") == "1"', config_body)

    def test_invalid_adaptive_field_rejects_before_other_request_state(self) -> None:
        config_body = self._function_body(
            "static void handleConfig()", "static void handleNagApiStats()"
        )
        self.assertLess(
            config_body.index("server.send(400"),
            config_body.index('server.hasArg("can")'),
        )
        apply_body = self._function_body(
            "static NagAdaptiveConfigInput::ApplyResult dashApplyNagConfigArgs(\n"
            "    DashNagConfigError &error)\n{",
            "static void handleRoot",
        )
        self.assertLess(
            apply_body.index("dashParseNagConfigRequest"),
            apply_body.index("publishAdaptiveCommand"),
        )

    def test_web_recomputes_das_freshness_from_published_timestamps(self) -> None:
        for field in ("lastDasFrameMs", "dasFreshnessLimitMs"):
            self.assertIn(field, self.controller)
        telemetry = self._function_body(
            "static void dashAppendNagClosedLoopTelemetry", "static void dashApplyRuntimeState"
        )
        self.assertIn("nagAdaptiveDasFreshnessAt(snapshot, now)", telemetry)
        self.assertIn("dasStatus.fresh", telemetry)
        self.assertIn("dasStatus.ageMs", telemetry)
        self.assertNotIn("snapshot.dasFresh ?", telemetry)

    def test_chinese_adaptive_section_matches_hos_closed_loop_contract(self) -> None:
        start = self.readme_zh.index("### 模式 ADAPTIVE（V4.7-V13）")
        end = self.readme_zh.index("## WiFi / DNS 网关", start)
        section = self.readme_zh[start:end]
        for token in (
            "0x39B",
            "HOS",
            "WAIT_DAS",
            "ARMING",
            "MAINTENANCE",
            "CORRECTIVE",
            "H2_PENDING",
            "PRE_CORRECTIVE_PAUSE",
            "STABILITY_VERIFY",
            "FAULT_HOLD",
            "±2.00 Nm",
            "3 秒",
            "500 ms",
            "5 秒",
            "1～2 秒",
            "1 ms",
            "VERIFY",
            "Gate B/C",
            "docs/nag-adaptive-closed-loop.md",
        ):
            self.assertIn(token, section)
        self.assertIn("HOS 6..15", section)
        self.assertIn("9..14 为未定义", section)
        self.assertIn("保护停发", section)
        self.assertNotIn("HOS 8..15", section)
        for obsolete in (
            "50.0°",
            "45.0°",
            "默认预防幅值约为 `0.15 .. 0.18 Nm`",
            "执行 3..5 个成功发送帧",
        ):
            self.assertNotIn(obsolete, section)


if __name__ == "__main__":
    unittest.main()
