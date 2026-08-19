import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class V40AdaptiveNagRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.handler = (ROOT / "include/handlers_base.h").read_text(encoding="utf-8")
        cls.controller = (ROOT / "include/nag_adaptive_controller.h").read_text(
            encoding="utf-8"
        )
        cls.dashboard = (ROOT / "include/web/mcp2515_dashboard.h").read_text(
            encoding="utf-8"
        )
        cls.ui = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text(
            encoding="utf-8"
        )

    def test_adaptive_mode_and_signal_decoders_are_present(self):
        self.assertIn("MODE_ADAPTIVE = 5", self.handler)
        self.assertIn("((frame.data[4] & 0x3F) << 8) | frame.data[5]", self.handler)
        self.assertIn("static_cast<int16_t>(raw) - 8192", self.handler)
        self.assertIn("verifyChecksum(frame)", self.handler)

    def test_angle_gate_covers_positive_and_negative_fifty_degrees(self):
        self.assertIn("absolute(steeringAngleDeciDeg)", self.controller)
        self.assertIn("magnitude >= value.angleLimitDeciDeg", self.controller)
        self.assertIn("kAngleHysteresisDeciDeg = 50", self.controller)
        self.assertIn("kAngleResumeFrameCount = 3", self.controller)

    def test_torque_direction_is_opposite_with_a_deadband(self):
        self.assertIn("observedTorqueCentiNm > value.torqueDeadbandCentiNm", self.controller)
        self.assertIn("target = static_cast<int16_t>(-value.torqueMagnitudeCentiNm)", self.controller)
        self.assertIn("observedTorqueCentiNm < -value.torqueDeadbandCentiNm", self.controller)
        self.assertIn("target = value.torqueMagnitudeCentiNm", self.controller)

    def test_send_and_random_pause_defaults_are_persisted(self):
        for token in [
            "sendWindowMs = 10000",
            "pauseMinMs = 1000",
            "pauseMaxMs = 3000",
            "beginPause(now, entropy)",
        ]:
            self.assertIn(token, self.controller)
        for key in [
            '"nag_ad_mag"',
            '"nag_ad_db"',
            '"nag_ad_ang"',
            '"nag_ad_send"',
            '"nag_ad_pmin"',
            '"nag_ad_pmax"',
        ]:
            self.assertIn(key, self.dashboard)

    def test_adaptive_api_and_ui_contract_are_present(self):
        self.assertIn('server.on("/api/nag-adaptive", HTTP_GET', self.dashboard)
        self.assertIn('server.on("/api/nag-adaptive", HTTP_POST', self.dashboard)
        for token in [
            'data-v="5"',
            'id="nag-adaptive-angle"',
            'id="nag-adaptive-send"',
            'id="nag-adaptive-pause-min"',
            'id="nag-adaptive-pause-max"',
            "async function saveNagAdaptive()",
            "'/api/nag-adaptive'",
        ]:
            self.assertIn(token, self.ui)

    def test_only_successful_sends_advance_success_counters(self):
        send_index = self.handler.index("if (driver.send(echo))")
        success_index = self.handler.index("nagEchoCount++", send_index)
        remember_index = self.handler.index("rememberSuccessfulEcho(echo)", send_index)
        failure_index = self.handler.index("nagSendFailureCount++", send_index)
        self.assertLess(send_index, success_index)
        self.assertLess(send_index, remember_index)
        self.assertLess(send_index, failure_index)


if __name__ == "__main__":
    unittest.main()
