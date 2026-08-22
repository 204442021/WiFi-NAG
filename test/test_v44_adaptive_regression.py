import importlib.util
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CONTROLLER = (ROOT / "include" / "nag_adaptive_controller.h").read_text(encoding="utf-8")
HANDLERS = (ROOT / "include" / "handlers_base.h").read_text(encoding="utf-8")
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()

spec = importlib.util.spec_from_file_location("minify_dashboard", ROOT / "scripts" / "minify_dashboard.py")
minify_dashboard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(minify_dashboard)
RAW_UI = (ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8")
UI_SRC = minify_dashboard.apply_v44_overrides(RAW_UI)


class V44AdaptiveRegressionTests(unittest.TestCase):
    def test_version_is_v44_v13(self):
        self.assertEqual("V4.4 V13", VERSION)
        self.assertIn("V4.4 V13", UI_SRC)

    def test_maintenance_layer_has_default_on_switch(self):
        self.assertIn("bool maintenanceEnabled = true;", CONTROLLER)
        self.assertIn("nag-maintenance-enabled", UI_SRC)
        self.assertRegex(UI_SRC, r"维持层[\s\S]{0,500}默认开启")
        self.assertIn("requestDraft.maintenanceEnabled?'0.05':'0.00'", UI_SRC)

    def test_maintenance_off_blocks_only_hos_0_to_2_output(self):
        self.assertIn("BLOCK_MAINTENANCE_DISABLED", CONTROLLER)
        self.assertRegex(
            CONTROLLER,
            r"PHASE_MAINTENANCE[\s\S]{0,650}maintenanceEnabled[\s\S]{0,350}BLOCK_MAINTENANCE_DISABLED",
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

    def test_corrective_uses_one_shared_180_to_200_runtime_range(self):
        self.assertIn("correctiveMinCentiNm = 180", CONTROLLER)
        self.assertIn("correctiveMaxCentiNm = 200", CONTROLLER)
        corrective_range = re.search(
            r"void correctiveRange\([^)]*\) const\s*\{(?P<body>[\s\S]*?)\n    \}",
            CONTROLLER,
        )
        self.assertIsNotNone(corrective_range)
        body = corrective_range.group("body")
        self.assertIn("config_.correctiveMinCentiNm", body)
        self.assertIn("config_.correctiveMaxCentiNm", body)
        self.assertNotIn("injectionSign_", body)
        self.assertIn('id="nag-cr-min"', UI_SRC)
        self.assertIn('id="nag-cr-max"', UI_SRC)
        self.assertNotIn('id="nag-cr-neg-min"', UI_SRC)
        self.assertNotIn('id="nag-cr-pos-min"', UI_SRC)

    def test_adaptive_wire_path_can_reach_two_nm_without_changing_legacy_target(self):
        self.assertIn("kLegacyTorqueMaxCentiNm = 180", HANDLERS)
        self.assertIn("kAdaptiveTorqueMaxCentiNm = 200", HANDLERS)
        self.assertRegex(HANDLERS, r"targetTorqueCentiNm\(\) const[\s\S]{0,120}kLegacyTorqueMaxCentiNm")
        self.assertIn("centiNmToAdaptiveRaw", HANDLERS)
        self.assertRegex(HANDLERS, r"if \(adaptiveMode\)[\s\S]{0,250}clampAdaptiveTorqueCentiNm")

    def test_counter_metric_is_oem_sequence_anomaly_not_echo_reuse_collision(self):
        self.assertIn("nagOemCounterAnomalyCount", HANDLERS)
        self.assertIn("nagEchoCounterReuseGapUs", HANDLERS)
        self.assertRegex(HANDLERS, r"expectedOemCounter[\s\S]{0,250}nagOemCounterAnomalyCount")
        self.assertNotIn("nagCounterCollisionCount++", HANDLERS)
        self.assertIn("OEM计数异常", UI_SRC)
        self.assertIn("Echo复用间隔", UI_SRC)

    def test_deadband_ui_is_removed(self):
        self.assertNotIn('id="nag-direction-deadband"', UI_SRC)
        self.assertNotIn("方向死区 / Nm", UI_SRC)


if __name__ == "__main__":
    unittest.main()
