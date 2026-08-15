from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
DASH = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")
GATEWAY = (ROOT / "include/web/dash_gateway.h").read_text(encoding="utf-8")
DIAG = (ROOT / "include/web/memory_diagnostics.h").read_text(encoding="utf-8")
UI = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8-sig")
SDK_DEFAULTS = (ROOT / "sdkconfig.wifi_nag.defaults").read_text(encoding="utf-8")
SDK_GENERATED = (ROOT / "sdkconfig.wifi_nag_ESP32_S3_CAN").read_text(encoding="utf-8")


class V23NetworkMemoryRegression(unittest.TestCase):
    def test_client_and_socket_limits_are_not_reduced(self):
        self.assertIn("kDashApMaxConn = 4", DASH)
        self.assertIn("CONFIG_LWIP_MAX_SOCKETS=24", SDK_DEFAULTS)
        self.assertIn("CONFIG_LWIP_MAX_SOCKETS=24", SDK_GENERATED)

    def test_network_buffers_are_bounded_and_non_dma_lwip_can_use_psram(self):
        for text in (SDK_DEFAULTS, SDK_GENERATED):
            self.assertIn("CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=16", text)
            self.assertIn("CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=40", text)
            self.assertIn("CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=40", text)
            self.assertIn("CONFIG_LWIP_TCPIP_RECVMBOX_SIZE=32", text)
            self.assertIn("CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y", text)
        self.assertNotIn("CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=64", SDK_DEFAULTS)
        self.assertNotIn("CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=64", SDK_DEFAULTS)

    def test_polling_is_page_and_visibility_aware(self):
        self.assertIn("dashboardPageActive('diagnostics')", UI)
        self.assertIn("dashboardPageActive('network')", UI)
        self.assertIn("return !document.hidden&&!dashboardPollStopped", UI)
        self.assertIn("fetchPollJson('/network_status',3000)", UI)
        self.assertNotIn("intervalVisible(loadWifiStatus", UI)
        self.assertNotIn("intervalVisible(loadApStatus", UI)
        self.assertNotIn("intervalVisible(loadGatewayStatus", UI)

    def test_reusable_json_buffers_and_merged_network_route_exist(self):
        self.assertIn('server.on("/network_status", HTTP_GET, handleNetworkStatus)', DASH)
        self.assertIn("static String response;", DASH)
        self.assertIn("static String j;", DASH)
        self.assertIn("dashBuildWifiStatusJson", DASH)
        self.assertIn("dashBuildApStatusJson", DASH)
        self.assertIn("dashBuildGatewayStatusJson", GATEWAY)

    def test_failed_allocations_are_aggregated_with_dma_context(self):
        for token in (
            "dmaFree",
            "dmaLargest",
            "httpEndpoint",
            "allocationFailureCount",
            "dashDiagAllocationFailureBuckets",
            "lastAllocationEventMs_",
            "now - lastAllocationEventMs_ >= 30000U",
        ):
            self.assertIn(token, DIAG)
        self.assertIn("allocation_failure_histogram", DASH)
        self.assertIn("allocation_failure_count", DASH)
        self.assertIn("http_endpoint", DASH)

    def test_memory_changes_do_not_touch_can_or_ble_implementation(self):
        combined = DIAG + UI
        self.assertNotIn("setFilters(", combined)
        self.assertNotIn("sendFrame(", combined)
        self.assertNotIn("setEnabled(", DIAG)


if __name__ == "__main__":
    unittest.main()
