import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = (ROOT / "include" / "ble_fsd_receiver.h").read_text(
    encoding="utf-8"
)
SOURCE = (ROOT / "src" / "ble_fsd_receiver.cpp").read_text(
    encoding="utf-8"
)
DASH = (ROOT / "include" / "web" / "mcp2515_dashboard.h").read_text(
    encoding="utf-8"
)
UI_SOURCE = (
    ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h"
).read_text(encoding="utf-8")


class BleConnectedDeviceStatusTests(unittest.TestCase):
    def test_receiver_tracks_connected_peer_identity(self) -> None:
        for token in (
            "char peerMac[18]",
            "char peerName[32]",
            "peerAddressType",
            "connectedAtMs",
            "lastDisconnectAtMs",
        ):
            with self.subTest(token=token):
                self.assertIn(token, HEADER)

        for token in (
            "updatePeerFromAdvertisement",
            "addressToText(disc.addr, gStatus.peerMac)",
            "gStatus.connectedAtMs = nowMs()",
            "gStatus.lastDisconnectAtMs = nowMs()",
        ):
            with self.subTest(token=token):
                self.assertIn(token, SOURCE)

    def test_dashboard_exposes_peer_identity_and_session(self) -> None:
        for token in (
            "peerMac",
            "peerName",
            "connectedAtMs",
            "lastDisconnectAtMs",
            "bleRxPeerMac",
            "bleRxPeerName",
            "bleRxSubscribed",
        ):
            with self.subTest(token=token):
                self.assertIn(token, DASH)

        for element_id in ("ble-rx-device", "ble-rx-session"):
            with self.subTest(element_id=element_id):
                self.assertIn(f'id="{element_id}"', UI_SOURCE)

        self.assertIn("const peerMac=", UI_SOURCE)
        self.assertIn("const peerName=", UI_SOURCE)
        self.assertIn("const subscribed=", UI_SOURCE)
        self.assertIn("setCanDiag('ble-rx-device'", UI_SOURCE)
        self.assertIn("setCanDiag('ble-rx-session'", UI_SOURCE)

    def test_ble_scan_is_wifi_coexistence_friendly(self) -> None:
        self.assertIn("params.itvl = BLE_GAP_SCAN_ITVL_MS", SOURCE)
        self.assertIn("params.window = BLE_GAP_SCAN_WIN_MS", SOURCE)
        self.assertIn("gDiscoveryMode ? 100 : 160", SOURCE)
        self.assertIn("gDiscoveryMode ? 40 : 30", SOURCE)
        self.assertIn("if (!config.enabled)", SOURCE)
        self.assertIn("ensureBleInitialized()", SOURCE)

        ap_start = DASH.index("dashStartAccessPoint(true)")
        ble_start = DASH.index("bleFsdReceiverStart(bleFsdConfig)")
        self.assertLess(ap_start, ble_start)

    def test_manual_rescan_is_distinct_from_background_scan(self) -> None:
        self.assertIn("bool discoveryActive", HEADER)
        self.assertIn("bool connected = false", HEADER)
        self.assertIn("bool saved = false", HEADER)
        self.assertIn("gDiscoveryStartPending", SOURCE)
        self.assertIn("(gStatus.connected && !gDiscoveryMode)", SOURCE)
        self.assertIn("seedConfiguredPeerScanEntry()", SOURCE)
        self.assertIn("status.discoveryActive", DASH)
        self.assertIn('\\"radioScanning\\":', DASH)

    def test_ble_controls_auto_collapse_and_can_be_reopened(self) -> None:
        for element_id in (
            "ble-rx-controls",
            "ble-rx-controls-toggle",
            "ble-rx-auto-collapse",
        ):
            with self.subTest(element_id=element_id):
                self.assertIn(f'id="{element_id}"', UI_SOURCE)
        self.assertIn("setBleFsdControlsCollapsed", UI_SOURCE)
        self.assertIn("toggleBleFsdControls", UI_SOURCE)
        self.assertIn("bleFsdAutoCollapsePending", UI_SOURCE)
        self.assertIn("initBleFsdControls()", UI_SOURCE)

    def test_ble_diagnostics_are_enabled_by_default(self) -> None:
        self.assertIn("bool enabled = true", HEADER)
        self.assertIn('prefs.getBool("ble_rx", true)', DASH)

    def test_ble_dashboard_has_complete_chinese_status_text(self) -> None:
        for text in (
            "'BLE FSD Diagnostic Receiver':'BLE FSD 诊断接收器'",
            "'Connected Device':'已连接设备'",
            "'CONNECTED':'已连接'",
            "'SUBSCRIBED':'已订阅'",
            "'TEST_ACTIVE':'测试激活'",
            "'duplicate_seq':'重复序列'",
            "'Scanning nearby BLE devices...':'正在扫描附近的 BLE 设备...'",
        ):
            with self.subTest(text=text):
                self.assertIn(text, UI_SOURCE)
        self.assertIn("trText(link)", UI_SOURCE)
        self.assertIn("trText(state)", UI_SOURCE)
        self.assertIn("trText(reject)", UI_SOURCE)

    def test_all_dashboard_panels_start_collapsed(self) -> None:
        self.assertIn("cardCollapse:v3:", UI_SOURCE)
        self.assertIn("subCollapse:v3:", UI_SOURCE)
        self.assertGreaterEqual(
            UI_SOURCE.count("const collapsed=stored===null?true:stored==='1';"), 2
        )
        self.assertIn("bleFsdControlsCollapsed:v2", UI_SOURCE)
        self.assertNotIn("expandCarEssentials", UI_SOURCE)
        self.assertNotIn("expandWifiNagDefaults", UI_SOURCE)

    def test_ble_runtime_services_a_mode_without_web_polling(self) -> None:
        self.assertIn("dashServiceBleFsdRuntime", DASH)
        self.assertIn("nag->triggerAModeWindow(windowMs)", DASH)
        self.assertIn("nag->cancelAModeWindow()", DASH)
        self.assertIn("dashServiceBleFsdRuntime();", DASH)
        self.assertIn("MODE_A && !aModeActive()", (ROOT / "include" / "handlers.h").read_text(encoding="utf-8"))
        self.assertIn("A: waiting for BLE trigger", UI_SOURCE)


if __name__ == "__main__":
    unittest.main()
