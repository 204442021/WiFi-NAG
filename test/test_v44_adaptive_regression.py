import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONTROLLER = (ROOT / "include" / "nag_adaptive_controller.h").read_text(encoding="utf-8")
HANDLERS = (ROOT / "include" / "handlers_base.h").read_text(encoding="utf-8")
DASH = (ROOT / "include" / "web" / "mcp2515_dashboard.h").read_text(encoding="utf-8")
UI_SRC = (ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8")
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()


class V44AdaptiveRegressionTests(unittest.TestCase):
    def test_version_is_v44_v13(self):
        self.assertEqual("V4.4 V13", VERSION)

    def test_maintenance_layer_has_default_on_persisted_switch(self):
        self.assertIn("bool maintenanceEnabled = true;", CONTROLLER)
        self.assertIn('"maintenanceEnabled"', DASH)
        self.assertIn('"nag_maint_en"', DASH)
        self.assertRegex(UI_SRC, r"维持层[^\n]{0,300}(开关|启用)")

    def test_maintenance_off_blocks_only_hos_0_to_2_output(self):
        self.assertIn("BLOCK_MAINTENANCE_DISABLED", CONTROLLER)
        self.assertRegex(
            CONTROLLER,
            r"PHASE_MAINTENANCE[\s\S]{0,500}maintenanceEnabled[\s\S]{0,300}BLOCK_MAINTENANCE_DISABLED",
        )
        self.assertRegex(
            CONTROLLER,
            r"PHASE_CORRECTIVE[\s\S]{0,300}selectRandomMagnitude\(true",
        )

    def test_direction_has_no_deadband_or_100ms_flip_confirmation(self):
        match = re.search(
            r"void updateDirection\([^)]*\)\s*\{(?P<body>[\s\S]*?)\n    \}\n\n    NagAdaptiveDecision releaseDecision",
            CONTROLLER,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertNotIn("torqueDeadbandCentiNm", body)
        self.assertNotIn("candidateStartedAtMs_", body)
        self.assertNotIn("100U", body)
        self.assertIn("torqueCentiNm > 0", body)
        self.assertIn("torqueCentiNm < 0", body)
        self.assertIn("DIRECTION_HOLD", body)

    def test_corrective_uses_one_shared_180_to_200_range(self):
        self.assertIn("correctiveMinCentiNm = 180", CONTROLLER)
        self.assertIn("correctiveMaxCentiNm = 200", CONTROLLER)
        self.assertNotIn("correctiveNegativeMinCentiNm", CONTROLLER)
        self.assertNotIn("correctivePositiveMinCentiNm", CONTROLLER)

    def test_adaptive_wire_path_can_reach_two_nm_without_changing_legacy_target(self):
        self.assertIn("kLegacyTorqueMaxCentiNm = 180", HANDLERS)
        self.assertIn("kAdaptiveTorqueMaxCentiNm = 200", HANDLERS)
        self.assertRegex(HANDLERS, r"targetTorqueCentiNm\(\) const[\s\S]{0,120}kLegacyTorqueMaxCentiNm")
        self.assertRegex(HANDLERS, r"adaptiveMode[\s\S]{0,500}kAdaptiveTorqueMaxCentiNm")

    def test_counter_metric_is_oem_sequence_anomaly_not_echo_reuse_collision(self):
        self.assertIn("nagOemCounterAnomalyCount", HANDLERS)
        self.assertIn("nagEchoCounterReuseGapUs", HANDLERS)
        self.assertRegex(HANDLERS, r"expectedOemCounter[\s\S]{0,250}nagOemCounterAnomalyCount")
        self.assertNotIn("nagCounterCollisionCount++", HANDLERS)
        self.assertIn("OEM计数异常", UI_SRC)
        self.assertIn("Echo复用间隔", UI_SRC)


if __name__ == "__main__":
    unittest.main()
