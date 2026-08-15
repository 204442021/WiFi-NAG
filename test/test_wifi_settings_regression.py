import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UI_BASE_FILE = ROOT / "include" / "web" / "mcp2515_dashboard_ui.base.h"
UI_WRAPPER_FILE = ROOT / "include" / "web" / "mcp2515_dashboard_ui.h"
DASH_FILE = ROOT / "include" / "web" / "mcp2515_dashboard.h"
GATEWAY_FILE = ROOT / "include" / "web" / "dash_gateway.h"
RUNTIME_FILE = ROOT / "src" / "espidf_runtime.cpp"
PROFILE_EXAMPLE_FILE = ROOT / "platformio_profile.example.h"


class WifiNagRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.ui = UI_BASE_FILE.read_text(encoding="utf-8-sig")
        cls.ui_wrapper = UI_WRAPPER_FILE.read_text(encoding="utf-8-sig")
        cls.dash = DASH_FILE.read_text(encoding="utf-8")
        cls.gateway = GATEWAY_FILE.read_text(encoding="utf-8")
        cls.runtime = RUNTIME_FILE.read_text(encoding="utf-8")
        cls.profile_example = PROFILE_EXAMPLE_FILE.read_text(encoding="utf-8")

    def assertHasUiId(self, element_id: str) -> None:
        pattern = rf'\bid=(?:"{re.escape(element_id)}"|{re.escape(element_id)}\b)'
        self.assertRegex(self.ui, pattern)

    def test_generated_ui_wrapper_loads_only_real_base_page(self) -> None:
        base_include = '#include "web/mcp2515_dashboard_ui.base.h"'
        extension_include = '#include "web/nag_sweep_dashboard.h"'
        self.assertEqual(self.ui_wrapper.count(base_include), 1)
        self.assertNotIn(extension_include, self.ui_wrapper)

    def test_wifi_ui_has_expected_fields(self) -> None:
        required_ids = [
            "wifi-status",
            "wifi-external-ip",
            "wifi-external-link",
            "top-wifi-ip",
            "wifi-ssid",
            "wifi-pass",
            "wifi-static",
            "wifi-ip",
            "wifi-gw",
            "wifi-mask",
            "wifi-dns",
            "wifi-nets",
            "scan-btn",
        ]

        for element_id in required_ids:
            with self.subTest(element_id=element_id):
                self.assertHasUiId(element_id)

    def test_external_ip_is_reported_linked_and_ota_uses_current_host(self) -> None:
        self.assertIn('j += ",\\\"ip\\\":\\\"" + staIp.toString() + "\\\"";', self.dash)
        self.assertIn("externalUrl=externalIp?'http://'+externalIp:''", self.ui)
        self.assertIn("link.href=externalUrl", self.ui)
        self.assertIn("xhr.open('POST','/update?ota_time='", self.ui)
        self.assertIn(
            'server.on("/update", HTTP_POST, handleOtaResult, handleOtaUpload);',
            self.dash,
        )
        self.assertNotIn("softap_only", self.dash)

    def test_wifi_backend_routes_exist(self) -> None:
        required_routes = [
            "/wifi_scan",
            "/wifi_config",
            "/wifi_status",
            "/wifi_networks",
            "/wifi_connect",
            "/wifi_delete",
        ]

        for route in required_routes:
            with self.subTest(route=route):
                self.assertIn(route, self.dash)

    def test_fixed_ap_identity_and_ota_credentials(self) -> None:
        defaults = dict(
            re.findall(
                r'^#define\s+(DASH_SSID|DASH_PASS|DASH_OTA_USER|DASH_OTA_PASS)\s+"([^"]+)"',
                self.profile_example,
                re.MULTILINE,
            )
        )
        self.assertEqual(defaults.get("DASH_SSID"), "Albert-FSD")
        self.assertEqual(defaults.get("DASH_PASS"), "12345678")
        self.assertEqual(defaults.get("DASH_OTA_USER"), "admin")
        self.assertEqual(defaults.get("DASH_OTA_PASS"), "12345678")
        self.assertIn('kDashFixedApSsid[] = "Albert-FSD"', self.dash)
        self.assertIn('kDashFactoryApPassword[] = "12345678"', self.dash)
        self.assertIn('kDashFixedOtaUser[] = "admin"', self.dash)
        self.assertIn('kDashFixedOtaPassword[] = "12345678"', self.dash)
        self.assertIn('server.authenticate(kDashFixedOtaUser, kDashFixedOtaPassword)', self.dash)
        self.assertIn('kDashApIdentityVersionKey[] = "apIdVer"', self.dash)
        self.assertIn('prefs.remove("ap_ssid")', self.dash)
        self.assertIn('prefs.putString("ap_pass", kDashFactoryApPassword)', self.dash)
        self.assertNotIn('prefs.putString("ap_ssid", newSsid)', self.dash)
        self.assertRegex(
            self.ui,
            r'id=(?:"ap-ssid"|ap-ssid)\s+value=(?:"Albert-FSD"|Albert-FSD)\s+disabled\s+readonly',
        )
        self.assertHasUiId("t2can-btn")
        self.assertIn("location.href='http://100.100.1.200/'", self.ui)

    def test_gateway_dns_routes_exist(self) -> None:
        required_routes = [
            "/gateway_status",
            "/gateway_dns",
            "/gateway_dns_test",
            "/gateway_dns_stats_reset",
            "/gateway_whitelist_add",
            "/gateway_blocked",
            "/gateway_blocked_clear",
        ]

        for route in required_routes:
            with self.subTest(route=route):
                self.assertIn(route, self.dash)

    def test_gateway_dns_defaults_cover_tesla_roots(self) -> None:
        for domain in ["tesla.cn", "tesla.com", "teslamotors.com", "tesla.services"]:
            with self.subTest(domain=domain):
                self.assertIn(domain, self.gateway)

    def test_nag_api_and_ui_controls_exist(self) -> None:
        for route in ["/api/config", "/api/stats", "/api/mode", "/api/update"]:
            with self.subTest(route=route):
                self.assertIn(route, self.dash)

        for element_id in ["can-write-tgl", "nag-mode", "nag-av2-min", "nag-av2-max"]:
            with self.subTest(element_id=element_id):
                self.assertHasUiId(element_id)

    def test_manual_ota_remains_online_ota_disabled(self) -> None:
        self.assertIn('server.on("/update", HTTP_POST, handleOtaResult, handleOtaUpload);', self.dash)
        for route in [
            'server.on("/plugins"',
            'server.on("/plugin_',
            'server.on("/settings_export"',
            'server.on("/settings_import"',
            'server.on("/task_stats"',
            'server.on("/rec_',
            'server.on("/can_debug"',
        ]:
            with self.subTest(route=route):
                self.assertNotIn(route, self.dash)

        for online_update_token in [
            "DASH_ENABLE_ONLINE_UPDATE",
            "GitHub",
            "github.com",
            "httpUpdate",
            "firmware_url",
            "ota_check",
            "update_check",
        ]:
            with self.subTest(token=online_update_token):
                self.assertNotIn(online_update_token, self.dash)
                self.assertNotIn(online_update_token, self.ui)

    def test_espidf_wifi_logging_is_not_info_verbose(self) -> None:
        self.assertIn('esp_log_level_set("wifi", ESP_LOG_WARN);', self.runtime)
        self.assertIn('esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);', self.runtime)


if __name__ == "__main__":
    unittest.main()
