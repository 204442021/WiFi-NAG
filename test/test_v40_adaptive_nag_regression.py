import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class V42AdaptiveNagRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.handler = (ROOT / "include/handlers_base.h").read_text(encoding="utf-8")
        cls.controller = (ROOT / "include/nag_adaptive_controller.h").read_text(encoding="utf-8")
        cls.dashboard = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")
        cls.ui = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8")

    def test_adaptive_mode_keeps_safety_decoders_and_three_frame_arming(self):
        for token in (
            "MODE_ADAPTIVE = 5",
            "verifyChecksum(frame)",
            "isReservedTorqueRaw(observedTorqueRaw)",
            "kArmingFrameCount = 3",
        ):
            self.assertIn(token, self.handler + self.controller)

    def test_runtime_has_no_deadband_angle_or_pause_send_gate(self):
        self.assertIn("kDirectionStableMs = 100", self.controller)
        self.assertIn("DIRECTION_HOLD", self.controller)
        self.assertNotIn("beginPause(", self.controller)
        self.assertNotIn("updateAngleGate(", self.controller)
        self.assertNotIn("torqueDeadbandSkipCount_++", self.controller)
        self.assertIn("return {true, target", self.controller)

    def test_a_v2_is_retired_but_compatibility_number_is_kept(self):
        self.assertIn("MODE_A = 0", self.handler)
        self.assertIn("MODE_A_V2 = 4", self.handler)
        self.assertIn("return mode == MODE_A || mode == MODE_ADAPTIVE", self.handler)
        self.assertNotIn("A_V2", self.ui)
        self.assertNotIn('data-v="4"', self.ui)
        self.assertIn("CONTINUOUS", self.dashboard)
        self.assertIn("持续注入", self.ui)

    def test_handson_ranges_and_live_injection_contract_are_present(self):
        for token in (
            "handsOn1NegativeMinCentiNm",
            "handsOn1PositiveMinCentiNm",
            "handsOn2NegativeMinCentiNm",
            "handsOn2PositiveMinCentiNm",
            "lastInjectedValid",
            "lastInjectedAtMs",
            "injectedTorqueIsValid",
            "kInjectedFreshMs = 200",
        ):
            self.assertIn(token, self.handler)
        for field in (
            "nagInjectedTorqueNm",
            "nagInjectedTorqueValid",
            "nagInjectedAgeMs",
            "nagHandsOnRaw",
            "nagHandsOnTier",
            "nagDirectionSource",
        ):
            self.assertIn(field, self.dashboard)

    def test_only_successful_sends_update_actual_injection(self):
        send_index = self.handler.index("if (driver.send(echo))")
        value_index = self.handler.index("lastInjectedCentiNm = torqueCentiNm", send_index)
        valid_index = self.handler.index("lastInjectedValid = true", send_index)
        failure_index = self.handler.index("nagSendFailureCount++", send_index)
        self.assertLess(send_index, value_index)
        self.assertLess(value_index, valid_index)
        self.assertLess(valid_index, failure_index)

    def test_ui_has_ranges_and_actual_injection_without_legacy_gates(self):
        for element_id in (
            "nag-h1-neg-min", "nag-h1-neg-max", "nag-h1-pos-min", "nag-h1-pos-max",
            "nag-h2-neg-min", "nag-h2-neg-max", "nag-h2-pos-min", "nag-h2-pos-max",
            "nag-injected-meta", "nag-direction-meta",
        ):
            self.assertIn(f'id="{element_id}"', self.ui)
        for retired_id in (
            "nag-adaptive-deadband", "nag-adaptive-angle", "nag-adaptive-send",
            "nag-adaptive-pause-min", "nag-adaptive-pause-max",
        ):
            self.assertNotIn(f'id="{retired_id}"', self.ui)
        self.assertIn("--（未注入）", self.ui)


if __name__ == "__main__":
    unittest.main()
