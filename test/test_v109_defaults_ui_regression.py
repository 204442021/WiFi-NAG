import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION_FILE = ROOT / "VERSION"
PLATFORMIO_FILE = ROOT / "platformio.ini"
DASH_FILE = ROOT / "include" / "web" / "mcp2515_dashboard.h"
UI_SOURCE_FILE = ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h"
BLE_CLIENT_FILE = ROOT / "src" / "ble_bridge_client.cpp"


class V109DefaultsAndUiRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.version = VERSION_FILE.read_text(encoding="utf-8").strip()
        cls.platformio = PLATFORMIO_FILE.read_text(encoding="utf-8")
        cls.dash = DASH_FILE.read_text(encoding="utf-8")
        cls.ui_source = UI_SOURCE_FILE.read_text(encoding="utf-8")
        cls.ble_client = BLE_CLIENT_FILE.read_text(encoding="utf-8")

    def test_internal_version_is_v4_0(self) -> None:
        self.assertEqual(self.version, "V4.0")

    def test_nag_defaults_on_without_overriding_saved_choice(self) -> None:
        self.assertRegex(
            self.platformio,
            r"(?m)^\s*-DDASH_INJECTION_ON_BOOT=1\s*$",
        )
        self.assertIn(
            'canActive = prefs.getBool("can", kDashInjectionDefaultEnabled);',
            self.dash,
        )
        self.assertIn('prefs.putBool("can", canActive);', self.dash)

    def test_obstacle_shift_card_is_hidden_while_backend_stays_forced_off(self) -> None:
        self.assertIn("#obstacle-shift-card{display:none!important}", self.ui_source)
        self.assertIn("obstacleShiftFeatureEnabled = false;", self.ble_client)
        self.assertIn('persistBool("shift_dr", false);', self.ble_client)


if __name__ == "__main__":
    unittest.main()
