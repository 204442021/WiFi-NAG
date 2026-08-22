import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class V47H2RecoveryRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.controller = (ROOT / "include/nag_adaptive_controller.h").read_text(encoding="utf-8")
        cls.dashboard = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")
        cls.source = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8-sig")
        cls.version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()

    def test_version_and_defaults_match_v47_contract(self):
        self.assertEqual(self.version, "V4.7 V13")
        for declaration in (
            "uint32_t restMinMs = 0;", "uint32_t restMaxMs = 0;",
            "uint32_t h2PersistenceMs = 3000;",
            "uint32_t preCorrectionPauseMs = 500;",
            "uint32_t stabilityVerifyMs = 5000;",
        ):
            self.assertIn(declaration, self.controller)

    def test_backend_persists_and_migrates_every_v47_timer(self):
        for key, default in (
            ("nag_h2_ms", "3000"), ("nag_pre_ms", "500"),
            ("nag_stab_ms", "5000"), ("nag_rst_min", "0.0"),
            ("nag_rst_max", "0.0"),
        ):
            self.assertIn(f'prefs.putString("{key}"', self.dashboard)
            self.assertIn(f'prefs.getString("{key}", "{default}")', self.dashboard)
        self.assertIn('prefs.putUChar("nag_pol_v", 9)', self.dashboard)
        self.assertIn('if (prefs.getUChar("nag_pol_v", 0) < 9)', self.dashboard)

    def test_api_allows_zero_disable_and_exposes_new_seconds_fields(self):
        for field in ("h2PersistenceSec", "preCorrectionPauseSec", "stabilityVerifySec"):
            self.assertIn(f'"{field}"', self.dashboard)
            self.assertIn(f'DASH_NAG_PARSE_SEC_ARG("{field}"', self.dashboard)
        self.assertIn('DASH_NAG_PARSE_SEC_ARG("restMinSec", restMinMs, 0.0,', self.dashboard)
        self.assertIn('DASH_NAG_PARSE_SEC_ARG("correctivePauseMinSec", correctivePauseMinMs, 0.0,', self.dashboard)

    def test_ui_exposes_zero_disable_and_three_state_machine_timers(self):
        for element_id in (
            "nag-h2-persistence-sec", "nag-pre-correction-pause-sec",
            "nag-stability-verify-sec",
        ):
            self.assertRegex(self.source, rf'id="{element_id}"[^>]*min="0"')
        for element_id in (
            "nag-rest-min", "nag-rest-max", "nag-corrective-pause-min",
            "nag-corrective-pause-max",
        ):
            self.assertRegex(self.source, rf'id="{element_id}"[^>]*min="0"')
        for text in ("0 为关闭停发", "H2 连续检测", "纠正前停发", "H1 稳定确认"):
            self.assertIn(text, self.source)

    def test_action_bar_stays_in_page_flow_and_firmware_section_follows_it(self):
        self.assertIn(".nag-custom-actionbar{position:static", re.sub(r"\s+", "", self.source))
        action = self.source.index('class="nag-custom-actionbar"')
        firmware = self.source.index('id="firmware-update-card"')
        self.assertLess(action, firmware)

    def test_new_phase_names_exist_in_backend_and_frontend(self):
        for phase in ("h2-pending", "pre-corrective-pause", "stability-verify"):
            self.assertIn(f'return "{phase}"', self.dashboard)
            self.assertIn(f"'{phase}'", self.source)


if __name__ == "__main__":
    unittest.main()
