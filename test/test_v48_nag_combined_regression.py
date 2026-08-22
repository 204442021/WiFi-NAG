import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class V48NagCombinedRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.controller = (ROOT / "include/nag_adaptive_controller.h").read_text(encoding="utf-8")
        cls.config_input = (ROOT / "include/nag_adaptive_config_input.h").read_text(encoding="utf-8")
        cls.handler = (ROOT / "include/handlers_base.h").read_text(encoding="utf-8")
        cls.app = (ROOT / "include/app.h").read_text(encoding="utf-8")
        cls.backend = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")
        cls.ui = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8")
        cls.platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")

    def test_version_is_v48_v13(self):
        self.assertEqual((ROOT / "VERSION").read_text(encoding="utf-8").strip(), "V4.8 V13")
        self.assertGreaterEqual(self.ui.count("V4.8 V13"), 3)

    def test_config_defaults_keep_old_torque_but_add_new_controls(self):
        self.assertIn("bool lateEchoEnabled = false;", self.controller)
        self.assertIn("uint16_t correctivePositiveFrames = 50;", self.controller)
        self.assertIn("uint16_t correctiveNegativeFrames = 50;", self.controller)
        self.assertIn("correctiveNegativeMaxCentiNm = 200", self.controller)
        self.assertIn("correctivePositiveMaxCentiNm = 200", self.controller)
        self.assertIn("180, 250", self.controller)
        self.assertIn("correctivePositiveFrames", self.config_input)
        self.assertIn("correctiveNegativeFrames", self.config_input)

    def test_separate_maintenance_and_corrective_caps_are_preserved(self):
        self.assertIn("kTorqueMaxCentiNm = 180", self.handler)
        self.assertIn("kCorrectiveTorqueMaxCentiNm = 250", self.handler)
        self.assertIn("kCorrectiveTorqueMinCentiNm = -250", self.handler)

    def test_late_echo_has_one_nonblocking_scheduler_and_loop_service(self):
        scheduler = (ROOT / "include/nag_late_echo_scheduler.h").read_text(encoding="utf-8")
        self.assertNotIn("delay(", scheduler)
        self.assertNotIn("vTaskDelay(", scheduler)
        self.assertNotIn("std::vector", scheduler)
        self.assertIn("serviceTimedSend", self.handler)
        self.assertIn("serviceTimedSend", self.app)
        self.assertIn("[env:native_nag_late_echo]", self.platformio)

    def test_nvs_api_and_ui_cover_all_new_fields(self):
        for token in (
            "lateEchoEnabled",
            "correctivePositiveFrames",
            "correctiveNegativeFrames",
            "lateEchoReady",
            "lateEchoEstimatedPeriodUs",
            "lateEchoPending",
            "lateEchoScheduledCount",
            "lateEchoSentCount",
            "lateEchoEarlyCancelCount",
            "lateEchoExpiredCount",
            "lateEchoLastLeadUs",
            "nagCorrectiveSweepSign",
            "nagCorrectiveSweepFrame",
            "nagCorrectiveSweepFrameTarget",
            "nagCorrectiveSweepPeakNm",
        ):
            self.assertIn(token, self.backend)
        for token in (
            'id="nag-late-echo-enabled"',
            'id="nag-corrective-positive-frames"',
            'id="nag-corrective-negative-frames"',
            'max="2.50"',
            "±2.50 Nm",
            'id="nag-diag-sweep"',
            "nagCorrectiveSweepPeakNm",
        ):
            self.assertIn(token, self.ui)
        self.assertIn('prefs.putUChar("nag_pol_v", 10)', self.backend)
        self.assertIn('prefs.putUChar("nag_cr_pos_fr"', self.backend)
        self.assertIn('prefs.putUChar("nag_cr_neg_fr"', self.backend)

    def test_ui_source_is_the_only_hand_edited_dashboard_payload(self):
        result = __import__("subprocess").run(
            [str(ROOT / ".venv/bin/python"), "scripts/minify_dashboard.py", "--check"],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
