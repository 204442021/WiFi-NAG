from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BleObstacleShiftRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = (ROOT / "src/ble_bridge_client.cpp").read_text(encoding="utf-8")
        cls.header = (ROOT / "include/ble/bridge_client.h").read_text(encoding="utf-8")

    def test_hello_requires_old_caps_and_advertises_new_caps(self):
        self.assertIn("kAdvertisedCapabilities", self.client)
        protocol = (ROOT / "include/ble/bridge_protocol.h").read_text(encoding="utf-8")
        self.assertIn("kRequiredCapabilities", protocol)
        self.assertIn("supportsRequiredCapabilities", self.client)
        self.assertNotIn("constexpr uint8_t kCaps = 0x0f", self.client)

    def test_brake_state_is_session_scoped_and_published(self):
        self.assertIn("brakeStateMailbox.beginSession", self.client)
        self.assertIn("brakeStateMailbox.endSession", self.client)
        self.assertIn("MSG_BRAKE_STATE", self.client)
        self.assertIn("brakeStateMailbox.publish", self.client)

    def test_shift_enable_is_persistent_and_defaults_true(self):
        self.assertIn('p.getBool("shift_dr", true)', self.client)
        self.assertIn('persistBool("shift_dr", value)', self.client)
        self.assertIn("setObstacleShiftEnabled", self.header)

    def test_ble_filter_includes_118_without_changing_base_nag_filter(self):
        bridge = (ROOT / "include/ble/bridge_webui.h").read_text(encoding="utf-8")
        handler = (ROOT / "include/handlers_base.h").read_text(encoding="utf-8")
        self.assertIn("{0x370, 0x255, 0x12B, 0x118}", bridge)
        self.assertIn("static constexpr uint32_t ids[] = {880};", handler)

    def test_can_loop_owns_shift_tick_and_frame_observation(self):
        app = (ROOT / "include/app.h").read_text(encoding="utf-8")
        self.assertIn("obstacleShiftController.tick", app)
        self.assertIn("obstacleShiftController.observeFrame", app)

    def test_twai_rejects_extended_and_remote_frames_before_business_logic(self):
        twai = (ROOT / "include/drivers/twai_driver.h").read_text(encoding="utf-8")
        self.assertIn("!msg.extd", twai)
        self.assertIn("!msg.rtr", twai)

    def test_ble_disconnect_does_not_hot_swap_can_filters(self):
        self.assertNotIn("setFilters(", self.client)

    def test_ble_status_exposes_shift_state_and_config_route(self):
        bridge = (ROOT / "include/ble/bridge_webui.h").read_text(encoding="utf-8")
        for token in (
            "shiftEnabled",
            "shiftState",
            "shiftReason",
            "brakeStateAgeMs",
            "releaseConfirmed",
            "real118AgeMs",
            "virtualParkActive",
            "shiftLatched",
            "virtualParkTxCount",
            "virtualParkTxFailCount",
        ):
            self.assertIn(token, bridge)
        self.assertIn('bleBridgeArgEnabled("shift"', bridge)
        self.assertIn("setObstacleShiftEnabled", bridge)

    def test_dashboard_posts_and_renders_obstacle_shift_status(self):
        source = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text(
            encoding="utf-8-sig"
        )
        self.assertIn("shift:bleElement('shift-enabled').checked?'1':'0'", source)
        for token in (
            "data.shiftEnabled",
            "data.shiftStateName",
            "data.brakePressed",
            "data.brakeStateAgeMs",
            "data.releaseConfirmed",
            "data.real118AgeMs",
            "data.virtualParkActive",
            "data.shiftLatched",
            "data.shiftReasonName",
        ):
            self.assertIn(token, source)


if __name__ == "__main__":
    unittest.main()
