import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UI_FILE = ROOT / "include" / "web" / "mcp2515_dashboard_ui.h"
DASH_FILE = ROOT / "include" / "web" / "mcp2515_dashboard.h"
GATEWAY_FILE = ROOT / "include" / "web" / "dash_gateway.h"
RUNTIME_FILE = ROOT / "src" / "espidf_runtime.cpp"
CAN_DRIVER_FILE = ROOT / "include" / "drivers" / "can_driver.h"
TWAI_DRIVER_FILE = ROOT / "include" / "drivers" / "twai_driver.h"
PLATFORMIO_FILE = ROOT / "platformio.ini"
PROFILE_EXAMPLE_FILE = ROOT / "platformio_profile.example.h"


class WifiNagRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.ui = UI_FILE.read_text(encoding="utf-8")
        cls.dash = DASH_FILE.read_text(encoding="utf-8")
        cls.gateway = GATEWAY_FILE.read_text(encoding="utf-8")
        cls.runtime = RUNTIME_FILE.read_text(encoding="utf-8")
        cls.can_driver = CAN_DRIVER_FILE.read_text(encoding="utf-8")
        cls.twai_driver = TWAI_DRIVER_FILE.read_text(encoding="utf-8")
        cls.platformio = PLATFORMIO_FILE.read_text(encoding="utf-8")
        cls.profile_example = PROFILE_EXAMPLE_FILE.read_text(encoding="utf-8")

    def assertHasUiId(self, element_id: str) -> None:
        pattern = rf'\bid=(?:"{re.escape(element_id)}"|{re.escape(element_id)}\b)'
        self.assertRegex(self.ui, pattern)

    def test_wifi_ui_has_expected_fields(self) -> None:
        required_ids = [
            "wifi-status",
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

    def test_wifi_ap_start_is_checked_and_uses_unique_wpa2_ssid(self) -> None:
        self.assertIn('#define DASH_SSID "WIFI-NAG"', self.profile_example)
        self.assertIn("WIFI_AUTH_WPA2_PSK", self.runtime)
        self.assertIn("esp_wifi_init failed", self.runtime)
        self.assertIn("softAP failed", self.runtime)
        self.assertIn("if (!initialized_)", self.runtime)

    def test_sta_retry_backoff_keeps_apsta_but_reduces_reconnect_churn(self) -> None:
        self.assertIn("kDashStaBackoffFailureThreshold = 3", self.dash)
        self.assertIn("kDashStaBackoffPollMs = 10000", self.dash)
        self.assertIn("dashStaRetryDelayMs()", self.dash)
        self.assertIn("AP+STA stays up", self.dash)
        self.assertIn("retryMs / 1000", self.dash)
        self.assertIn("dashStartAccessPoint(true)", self.dash)

    def test_twai_diagnostics_are_exposed_below_torque_controls(self) -> None:
        required_ids = [
            "can-diag-row",
            "can-diag-state",
            "can-diag-errors",
            "can-diag-queues",
            "can-diag-reject",
            "can-diag-txbus",
            "can-diag-arb",
            "can-diag-rxloss",
            "can-diag-busoff",
            "can-diag-warning",
            "can-diag-safety",
            "can-diag-reset",
        ]
        for element_id in required_ids:
            with self.subTest(element_id=element_id):
                self.assertHasUiId(element_id)

        self.assertIn("CanDriverDiagnostics", self.can_driver)
        self.assertIn("getDiagnostics", self.can_driver)
        for token in [
            "TWAI_ALERT_BUS_OFF",
            "TWAI_ALERT_BUS_ERROR",
            "TWAI_ALERT_ARB_LOST",
            "tx_error_counter",
            "rx_error_counter",
            "tx_failed_count",
            "rx_missed_count",
            "rx_overrun_count",
            "bus_error_count",
        ]:
            with self.subTest(token=token):
                self.assertIn(token, self.twai_driver)

        for json_field in [
            "twaiState",
            "twaiTec",
            "twaiRec",
            "twaiTxFailed",
            "twaiBusError",
            "twaiArbLost",
            "twaiBusOff",
            "twaiRecovered",
            "twaiStaleDrop",
            "twaiSafetyTripped",
            "twaiSafetyReason",
            "twaiArbRate",
        ]:
            with self.subTest(json_field=json_field):
                self.assertIn(json_field, self.dash)

        self.assertIn('server.on("/can_diag_reset"', self.dash)
        self.assertIn("-DTWAI_TX_QUEUE_LEN=1", self.platformio)
        self.assertIn("status.msgs_to_tx > 0", self.twai_driver)
        self.assertIn("staleDropCount_++", self.twai_driver)
        self.assertIn("twai_transmit(&msg, 0)", self.twai_driver)
        self.assertIn("kErrorCounterTripThreshold = 96", self.twai_driver)
        self.assertIn("kBusErrorBurstLimit = 10", self.twai_driver)
        self.assertIn("onSafetyTrip", self.can_driver)
        self.assertIn("bool needsRestart = safetyTripped_", self.twai_driver)
        self.assertIn("stopAndUninstallLocked();", self.twai_driver)
        self.assertIn("driverOK_ = installAndStartLocked();", self.twai_driver)
        self.assertIn('\\"requestedCan\\":', self.dash)
        self.assertIn("typeof d.can==='boolean'", self.ui)


if __name__ == "__main__":
    unittest.main()
