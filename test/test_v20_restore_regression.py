from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
DASH = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")
RUNTIME_H = (ROOT / "include/platform/espidf_runtime.h").read_text(encoding="utf-8")
RUNTIME_CPP = (ROOT / "src/espidf_runtime.cpp").read_text(encoding="utf-8")
UI = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8")


class V20RestoreRegression(unittest.TestCase):
    def test_v4_4_v13_version_metadata(self):
        self.assertEqual(VERSION, "V4.4 V13")
        self.assertIn('<span class="version-badge">V4.4 V13</span>', UI)
        self.assertIn('id="diag-version">V4.4 V13</div>', UI)
        self.assertIn("mirrorDashboardText('fw-version','diag-version','V4.4 V13')", UI)

    def test_wifi_channel_cache_is_loaded_saved_removed_and_relearned(self):
        self.assertIn("uint8_t channel;", DASH)
        self.assertIn('getUChar(dashWifiKey(i, "c").c_str(), 0)', DASH)
        self.assertIn('putUChar(dashWifiKey(slot, "c").c_str(), n.channel)', DASH)
        self.assertIn('remove(dashWifiKey(slot, "c").c_str())', DASH)
        self.assertIn("esp_wifi_sta_get_ap_info(&staInfo)", DASH)
        self.assertIn("wifiNetworks[wifiActiveSlot].channel = staInfo.primary", DASH)

    def test_wifi_runtime_uses_fast_scan_only_for_known_channel(self):
        self.assertIn("void begin(const char *ssid, const char *pass, uint8_t channel = 0);", RUNTIME_H)
        self.assertIn("void WiFiClass::begin(const char *ssid, const char *pass, uint8_t channel)", RUNTIME_CPP)
        self.assertIn("cfg.sta.channel = channel;", RUNTIME_CPP)
        self.assertIn("channel ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN", RUNTIME_CPP)
        self.assertIn("WiFi.begin(staSSID, staPass, channel);", DASH)

    def test_wifi_retry_policy_falls_back_then_backs_off(self):
        self.assertIn("kDashStaDirectedRetryMs = 15000", DASH)
        self.assertIn("kDashStaSavedPollMs = 60000", DASH)
        self.assertIn("kDashStaUnknownChannelBootDelayMs = 5000", DASH)
        self.assertIn("staForceFullScan = true", DASH)
        self.assertIn("staAttemptUsedSavedChannel", DASH)
        self.assertIn('"full scan"', DASH)
        self.assertIn('"saved networks"', DASH)

    def test_dashboard_cards_and_monitor_persist_expected_defaults(self):
        self.assertIn('class="card ui-main-card" id="config-card"', UI)
        self.assertIn('id="config-card" data-ui-kind="nag">', UI)
        self.assertIn('wifiNagCardStoragePrefix=\'wifiNagMainCard:\'', UI)
        self.assertIn("localStorage.setItem(key,expanded?'1':'0')", UI)
        self.assertIn("const defaultExpanded=card.id==='config-card'", UI)
        self.assertIn("localStorage.getItem(systemMonitorStorageKey)!=='0'", UI)
        self.assertIn("localStorage.setItem(systemMonitorStorageKey", UI)


if __name__ == "__main__":
    unittest.main()
