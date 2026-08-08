from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BLE_WEBUI_FILE = ROOT / "include" / "ble" / "bridge_webui.h"
UI_BASE_FILE = ROOT / "include" / "web" / "mcp2515_dashboard_ui.base.h"

HOTSPOT_TAG = re.compile(
    r"<div\b[^>]*\bid\s*=\s*(?:\"wifi-hotspot-section\"|'wifi-hotspot-section'|wifi-hotspot-section)(?=[\s>])[^>]*>",
    re.IGNORECASE,
)


class BleDashboardCompositionRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.ble_webui = BLE_WEBUI_FILE.read_text(encoding="utf-8")
        cls.ui_base = UI_BASE_FILE.read_text(encoding="utf-8-sig")

    def test_real_generated_dashboard_uses_supported_hotspot_id_form(self) -> None:
        match = HOTSPOT_TAG.search(self.ui_base)
        self.assertIsNotNone(match)
        self.assertIn("id=wifi-hotspot-section", match.group(0))

    def test_locator_accepts_quoted_unquoted_and_reordered_attributes(self) -> None:
        variants = (
            '<div class="subsec" id="wifi-hotspot-section" data-x="1">',
            "<div class='subsec' id='wifi-hotspot-section' data-x='1'>",
            "<div class=subsec id=wifi-hotspot-section data-x=1>",
            "<div data-x=1 id=wifi-hotspot-section class=subsec>",
        )
        for html in variants:
            with self.subTest(html=html):
                self.assertIsNotNone(HOTSPOT_TAG.search(html))

    def test_ble_shell_uses_attribute_tolerant_hotspot_locator(self) -> None:
        self.assertIn("const hotspotTag=/<div\\b[^>]*\\bid\\s*=", self.ble_webui)
        self.assertIn("const hotspot=hotspotTag.exec(html);", self.ble_webui)
        self.assertIn(
            "html.slice(0,hotspot.index)+card+html.slice(hotspot.index)",
            self.ble_webui,
        )

    def test_missing_ble_insertion_point_fails_open_to_original_dashboard(self) -> None:
        self.assertNotIn(
            "throw new Error('BLE card insertion point missing')",
            self.ble_webui,
        )
        warning = (
            "console.warn('BLE card insertion point missing; "
            "showing dashboard without BLE card')"
        )
        self.assertIn(warning, self.ble_webui)
        warning_index = self.ble_webui.index(warning)
        write_index = self.ble_webui.index(
            "document.open();document.write(html);document.close();"
        )
        self.assertLess(warning_index, write_index)

    def test_ble_card_remains_before_wifi_hotspot_when_locator_matches(self) -> None:
        hotspot = HOTSPOT_TAG.search(self.ui_base)
        self.assertIsNotNone(hotspot)
        composed = (
            self.ui_base[: hotspot.start()]
            + '<div id="ble-bridge-section"></div>'
            + self.ui_base[hotspot.start() :]
        )
        self.assertLess(
            composed.index('id="ble-bridge-section"'),
            composed.index("id=wifi-hotspot-section"),
        )


if __name__ == "__main__":
    unittest.main()
