import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class V44CorrectionZoneRegressionTests(unittest.TestCase):
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

    def test_internal_and_runtime_version_advance_to_v4_4_v13(self):
        self.assertEqual(self.version, "V4.4 V13")
        self.assertNotIn("V4.3 V13", self.ui)
        self.assertGreaterEqual(self.ui.count("V4.4 V13"), 3)

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
        self.assertIn('aria-label="H0-H2 维持区"', self.ui)
        compact = re.sub(r"\s+", "", self.ui)
        self.assertIn("maintenanceEnabled:true", compact)
        self.assertIn("params.set('maintenanceEnabled'", self.ui)

    def test_direction_deadband_is_removed_from_ui_and_forced_to_zero(self):
        self.assertIn("int16_t torqueDeadbandCentiNm = 0;", self.controller)
        self.assertIn("value.torqueDeadbandCentiNm = 0;", self.controller)
        self.assertNotIn('id="nag-direction-deadband"', self.ui)
        self.assertNotIn("params.set('directionDeadbandNm'", self.ui)
        self.assertIn('prefs.putString("nag_dir_db", "0.00")', self.dashboard)

    def test_monitor_only_and_direction_change_are_visible_in_diagnostics(self):
        self.assertIn(
            'case NagAdaptiveController::PHASE_MONITOR_ONLY: return "monitor-only";',
            self.dashboard,
        )
        self.assertIn(
            'case NagAdaptiveController::BLOCK_DIRECTION_CHANGE: return "direction-change";',
            self.dashboard,
        )
        self.assertIn(
            'case NagAdaptiveController::BLOCK_MAINTENANCE_DISABLED: return "maintenance-disabled";',
            self.dashboard,
        )

    def test_two_nm_limit_applies_only_to_adaptive_correction(self):
        self.assertIn("kCorrectiveTorqueMaxCentiNm = 200", self.handler)
        self.assertIn("adaptiveMode && decision.corrective", self.handler)
        self.assertIn("clampCorrectiveTorqueCentiNm(torqueCentiNm)", self.handler)
        self.assertIn("kTorqueMaxCentiNm = 180", self.handler)


if __name__ == "__main__":
    unittest.main()
