from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = (ROOT / "include/ble/bridge_protocol.h").read_text(encoding="utf-8")
CLIENT = (ROOT / "include/ble/bridge_client.h").read_text(encoding="utf-8")
IMPL = (ROOT / "src/ble_bridge_client.cpp").read_text(encoding="utf-8")
WEB = (ROOT / "include/ble/bridge_webui.h").read_text(encoding="utf-8")
UI = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8")


class BlePairingUnbindRegression(unittest.TestCase):
    def test_unbind_protocol_has_request_ack_and_identity_validation(self):
        self.assertIn("MSG_UNBIND_REQUEST = 0x32", PROTOCOL)
        self.assertIn("MSG_UNBIND_ACK = 0x33", PROTOCOL)
        self.assertIn("decodeUnbindPayload", PROTOCOL)
        self.assertIn("transactionId == 0", PROTOCOL)
        self.assertIn("deviceId == 0", PROTOCOL)

    def test_ble_defaults_on_and_unbound_device_pairs_without_timeout(self):
        self.assertIn('p.getBool("enabled", true)', IMPL)
        self.assertIn(
            "g.pairing = static_cast<bool>(g.enabled) &&\n"
            "            static_cast<uint32_t>(g.peerId) == 0;",
            IMPL,
        )
        self.assertNotIn("kPairMs", IMPL)
        self.assertNotIn("pairUntil", IMPL)
        self.assertIn("持续配对", UI)

    def test_manual_disable_still_stops_pairing(self):
        self.assertIn(
            "else\n{\ng.pairing = false;\ng.ready = false;",
            IMPL,
        )

    def test_local_unbind_clears_state_and_resumes_pairing_without_peer_ack(self):
        self.assertIn('"unbind_pending"', IMPL)
        self.assertIn('"unbind_txn"', IMPL)
        self.assertIn('"unbind_peer"', IMPL)
        self.assertNotIn("sendUnbindRequest", IMPL)
        self.assertNotIn("sendUnbindAck", IMPL)
        self.assertNotIn("decodeUnbindPayload", IMPL)
        self.assertIn("void completeLocalUnbind()", IMPL)
        self.assertIn("p.remove(\"unbind_pending\")", IMPL)
        self.assertIn("p.remove(\"unbind_txn\")", IMPL)
        self.assertIn("p.remove(\"unbind_peer\")", IMPL)
        self.assertIn("g.pairing = static_cast<bool>(g.enabled);", IMPL)
        self.assertIn("unbindPending", CLIENT)
        self.assertIn('"unbindPending"', WEB)
        self.assertNotIn("等待对端上线完成解绑", UI)
        self.assertIn("确认解除本机绑定？解绑后本机会立即进入持续配对。", UI)
        self.assertIn("本机已解除绑定，正在持续配对", UI)
        self.assertIn("[BLE] local unbind requested", WEB)


if __name__ == "__main__":
    unittest.main()
