import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class V49AngleAndUiCleanupRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.controller = (ROOT / "include/nag_adaptive_controller.h").read_text(encoding="utf-8")
        cls.handler = (ROOT / "include/handlers_base.h").read_text(encoding="utf-8")
        cls.app = (ROOT / "include/app.h").read_text(encoding="utf-8")
        cls.dashboard = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")
        cls.ui = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8-sig")
        cls.platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        cls.version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()

    def test_version_and_screenshot_defaults_are_v49(self):
        self.assertEqual(self.version, "V4.9 V13")
        for declaration in (
            "int16_t preventiveNegativeMinCentiNm = 150;",
            "int16_t preventiveNegativeMaxCentiNm = 180;",
            "int16_t preventivePositiveMinCentiNm = 150;",
            "int16_t preventivePositiveMaxCentiNm = 180;",
            "int16_t correctiveNegativeMinCentiNm = 180;",
            "int16_t correctiveNegativeMaxCentiNm = 240;",
            "int16_t correctivePositiveMinCentiNm = 180;",
            "int16_t correctivePositiveMaxCentiNm = 240;",
            "uint16_t correctivePositiveFrames = 100;",
            "uint16_t correctiveNegativeFrames = 100;",
            "uint32_t preCorrectionPauseMs = 1000;",
        ):
            self.assertIn(declaration, self.controller)

    def test_late_echo_and_minimum_send_interval_are_fully_removed(self):
        combined = "\n".join((self.controller, self.handler, self.app, self.dashboard, self.ui, self.platformio))
        for retired in (
            "lateEcho", "Late Echo", "NagLateEchoScheduler", "serviceTimedSend",
            "correctiveFrameIntervalMs", "BLOCK_CORRECTIVE_INTERVAL",
            "corrective-interval",
            "native_nag_late_echo",
        ):
            self.assertNotIn(retired, combined)
        self.assertFalse((ROOT / "include/nag_late_echo_scheduler.h").exists())
        self.assertFalse((ROOT / "test/test_native_nag_late_echo/test_nag_late_echo.cpp").exists())

    def test_angle_is_the_only_adaptive_direction_source_and_has_a_strict_gate(self):
        self.assertIn("BLOCK_STEERING_ANGLE_LIMIT", self.controller)
        self.assertIn('return "steering-angle-limit"', self.dashboard)
        self.assertNotIn("DIRECTION_TORQUE", self.controller)
        self.assertNotIn("candidateSign_", self.controller)
        self.assertNotIn("candidateStartedAtMs_", self.controller)
        self.assertRegex(self.controller, r"angleDeciDeg\s*>\s*10")
        self.assertRegex(self.controller, r"angleDeciDeg\s*<\s*-10")
        self.assertRegex(self.controller, r"(?:>|<)\s*500")

    def test_unused_ack_timeout_and_saturating_window_counter_are_removed(self):
        combined = self.controller + self.dashboard + self.ui
        for retired in ("BLOCK_ACK_TIMEOUT", "acknowledgementTimeoutCount", "nagAcknowledgementTimeouts", "旧策略超时计数"):
            self.assertNotIn(retired, combined)
        self.assertIn("uint32_t correctiveBurstFrame = 0;", self.controller)
        self.assertIn("uint32_t correctiveBurstFrame_ = 0;", self.controller)

    def test_revision_11_migrates_only_old_recommended_values_and_removes_keys(self):
        self.assertIn('prefs.getUChar("nag_pol_v", 0) < 11', self.dashboard)
        self.assertIn('prefs.putUChar("nag_pol_v", 11)', self.dashboard)
        self.assertIn('prefs.remove("nag_late_echo")', self.dashboard)
        self.assertIn('prefs.remove("nag_ci_ms")', self.dashboard)
        for retired_access in (
            'prefs.getBool("nag_late_echo"', 'prefs.putBool("nag_late_echo"',
            'prefs.getString("nag_ci_ms"', 'prefs.putString("nag_ci_ms"',
        ):
            self.assertNotIn(retired_access, self.dashboard)
        for old_value, new_value in (("1.70", "1.50"), ("2.00", "2.40"), ("500", "1000"), ("50", "100")):
            self.assertIn(old_value, self.dashboard)
            self.assertIn(new_value, self.dashboard)

    def test_ui_uses_effective_controls_and_does_not_submit_hidden_release(self):
        for retired in ("策略就绪状态", "nag-readiness-card", "旧策略超时计数", "发送间隔 / ms"):
            self.assertNotIn(retired, self.ui)
        for text in (
            "负方向峰值范围", "正方向峰值范围",
            "负方向单侧三角帧数", "正方向单侧三角帧数",
            "H0/H1 稳定确认", "纠正发送与停发",
            "方向盘角度", "当前持续注入；启用停发后生效",
        ):
            self.assertIn(text, self.ui)
        self.assertNotIn("params.set('releaseMinSec'", self.ui)
        self.assertNotIn("params.set('releaseMaxSec'", self.ui)
        self.assertIn("nag-steering-angle", self.ui)
        self.assertIn("steering-angle-limit", self.ui)


if __name__ == "__main__":
    unittest.main()
