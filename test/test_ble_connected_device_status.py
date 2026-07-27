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


if __name__ == "__main__":
    unittest.main()
