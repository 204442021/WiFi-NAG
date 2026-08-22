import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class V45PulsedCorrectionRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
        cls.controller = (ROOT / "include/nag_adaptive_controller.h").read_text(
            encoding="utf-8"
        )
        cls.handler = (ROOT / "include/handlers_base.h").read_text(encoding="utf-8")
        cls.dashboard = (ROOT / "include/web/mcp2515_dashboard.h").read_text(
            encoding="utf-8"
        )
        cls.ui = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text(
            encoding="utf-8-sig"
        )

    def test_internal_and_runtime_version_advance_to_v49_v13(self):
        self.assertEqual(self.version, "V4.9 V13")
        self.assertNotIn("V4.5 V13", self.ui)
        self.assertGreaterEqual(self.ui.count("V4.9 V13"), 3)

    def test_maintenance_switch_is_default_on_and_crosses_every_config_boundary(self):
        self.assertIn("bool maintenanceEnabled = true;", self.controller)
        self.assertIn(
            "left.maintenanceEnabled == right.maintenanceEnabled", self.dashboard
        )
        self.assertIn(
            "left.maintenanceEnabled == right.maintenanceEnabled", self.handler
        )
        self.assertIn('\\"maintenanceEnabled\\":', self.dashboard)
        self.assertIn('prefs.putBool("nag_maint", adaptive.maintenanceEnabled)', self.dashboard)
        self.assertIn('prefs.getBool("nag_maint", true)', self.dashboard)
        self.assertIn('server.hasArg("maintenanceEnabled")', self.dashboard)
        self.assertRegex(
            self.ui,
            r'id="nag-maintenance-enabled"[^>]*type="checkbox"[^>]*checked',
        )
        self.assertIn('aria-label="H0-H1 维持区"', self.ui)
        compact = re.sub(r"\s+", "", self.ui)
        self.assertIn("maintenanceEnabled:true", compact)
        self.assertIn("params.set('maintenanceEnabled'", self.ui)

    def test_direction_deadband_is_removed_from_every_runtime_boundary(self):
        self.assertNotIn("torqueDeadbandCentiNm", self.controller)
        self.assertNotIn("torqueDeadbandCentiNm", self.handler)
        self.assertNotIn("directionDeadbandNm", self.dashboard)
        self.assertNotIn('id="nag-direction-deadband"', self.ui)
        self.assertNotIn("params.set('directionDeadbandNm'", self.ui)
        self.assertIn('prefs.remove("nag_dir_db")', self.dashboard)

    def test_prevention_and_correction_use_confirmed_pulse_windows(self):
        self.assertIn("uint32_t activityMinMs = 2000;", self.controller)
        self.assertIn("uint32_t activityMaxMs = 3000;", self.controller)
        self.assertIn("uint32_t restMinMs = 0;", self.controller)
        self.assertIn("uint32_t restMaxMs = 0;", self.controller)
        self.assertIn("uint32_t correctiveSendMinMs = 3000;", self.controller)
        self.assertIn("uint32_t correctiveSendMaxMs = 3000;", self.controller)
        self.assertIn("uint32_t correctivePauseMinMs = 1000;", self.controller)
        self.assertIn("uint32_t correctivePauseMaxMs = 2000;", self.controller)
        self.assertNotIn("correctiveFrameIntervalMs", self.controller)
        self.assertNotIn("BLOCK_CORRECTIVE_INTERVAL", self.controller)

    def test_prevention_timing_uses_recommended_defaults_without_business_clamps(self):
        self.assertNotIn(
            "normalizeU32Range(value.activityMinMs, value.activityMaxMs, 1000, 2000)",
            self.controller,
        )
        self.assertNotIn(
            "normalizeU32Range(value.restMinMs, value.restMaxMs, 3000, 5000)",
            self.controller,
        )
        self.assertIn(
            "normalizeU32Range(value.activityMinMs, value.activityMaxMs, 100, UINT32_MAX)",
            self.controller,
        )
        self.assertIn("normalizeOptionalU32Range(value.restMinMs, value.restMaxMs", self.controller)
        self.assertNotIn(
            'DASH_NAG_PARSE_SEC_ARG("activityMinSec", activityMinMs, 1.0, 2.0)',
            self.dashboard,
        )
        self.assertNotIn(
            'DASH_NAG_PARSE_SEC_ARG("restMinSec", restMinMs, 3.0, 5.0)',
            self.dashboard,
        )
        for field_id in ("nag-active-min", "nag-active-max"):
            markup = re.search(
                rf'<input[^>]+id="{re.escape(field_id)}"[^>]*>', self.ui
            )
            self.assertIsNotNone(markup, field_id)
            self.assertIn('min="0.1"', markup.group(0))
            self.assertNotIn('max="2.0"', markup.group(0))
            self.assertNotIn('max="5.0"', markup.group(0))
        for field_id in ("nag-rest-min", "nag-rest-max"):
            markup = re.search(rf'<input[^>]+id="{re.escape(field_id)}"[^>]*>', self.ui)
            self.assertIsNotNone(markup, field_id)
            self.assertIn('min="0"', markup.group(0))

    def test_two_nm_torque_cap_is_unchanged_while_timing_is_unlocked(self):
        self.assertIn(
            'DASH_NAG_PARSE_NM_ARG("correctivePositiveMaxNm", correctivePositiveMaxCentiNm, 1.80, 2.50)',
            self.dashboard,
        )
        self.assertIn("config.correctivePositiveMaxCentiNm > 250", (
            ROOT / "include/nag_adaptive_config_input.h"
        ).read_text(encoding="utf-8"))

    def test_monitor_only_and_angle_limit_are_visible_in_diagnostics(self):
        self.assertIn(
            'case NagAdaptiveController::PHASE_MONITOR_ONLY: return "monitor-only";',
            self.dashboard,
        )
        self.assertIn(
            'case NagAdaptiveController::BLOCK_STEERING_ANGLE_LIMIT: return "steering-angle-limit";',
            self.dashboard,
        )
        self.assertIn(
            'case NagAdaptiveController::BLOCK_MAINTENANCE_DISABLED: return "maintenance-disabled";',
            self.dashboard,
        )

    def test_two_nm_limit_applies_only_to_adaptive_correction(self):
        self.assertIn("kCorrectiveTorqueMaxCentiNm = 250", self.handler)
        self.assertIn("adaptiveMode && decision.corrective", self.handler)
        self.assertIn("clampCorrectiveTorqueCentiNm(torqueCentiNm)", self.handler)
        self.assertIn("kTorqueMaxCentiNm = 180", self.handler)


if __name__ == "__main__":
    unittest.main()
