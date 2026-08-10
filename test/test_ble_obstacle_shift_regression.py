from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BleObstacleShiftRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.client = (ROOT / "src/ble_bridge_client.cpp").read_text(encoding="utf-8")
        cls.header = (ROOT / "include/ble/bridge_client.h").read_text(encoding="utf-8")
        cls.controller = (ROOT / "include/obstacle_shift_controller.h").read_text(
            encoding="utf-8"
        )
        cls.bridge = (ROOT / "include/ble/bridge_webui.h").read_text(
            encoding="utf-8"
        )
        cls.app = (ROOT / "include/app.h").read_text(encoding="utf-8")
        cls.source = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text(
            encoding="utf-8-sig"
        )

    def test_hello_requires_old_caps_and_advertises_new_caps(self):
        self.assertIn("kAdvertisedCapabilities", self.client)
        protocol = (ROOT / "include/ble/bridge_protocol.h").read_text(
            encoding="utf-8"
        )
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
        handler = (ROOT / "include/handlers_base.h").read_text(encoding="utf-8")
        self.assertIn("{0x370, 0x255, 0x12B, 0x118}", self.bridge)
        self.assertIn("static constexpr uint32_t ids[] = {880};", handler)

    def test_can_loop_owns_condition_tick_and_frame_observation(self):
        self.assertIn("obstacleShiftController.tick", self.app)
        self.assertIn("obstacleShiftController.observeFrame", self.app)
        self.assertNotIn("obstacleShiftManualRequests", self.app)
        self.assertNotIn("manualRequestGeneration", self.app)

    def test_manual_request_chain_is_completely_removed(self):
        combined = self.header + self.bridge + self.controller + self.source
        for token in (
            "ObstacleShiftManualRequestMailbox",
            "obstacleShiftManualRequests",
            "/ble_shift_manual",
            "bleBridgeHandleManualShift",
            "manualRequestGeneration",
            "manualRequestReady",
            "manualRequestReason",
            "SHIFT_TRIGGER_MANUAL_BUTTON",
            "shift-manual-btn",
            "bleManualShift",
            "手动虚拟 P（1 秒）",
        ):
            with self.subTest(token=token):
                self.assertNotIn(token, combined)

    def test_twai_rejects_extended_and_remote_frames_before_business_logic(self):
        twai = (ROOT / "include/drivers/twai_driver.h").read_text(encoding="utf-8")
        self.assertIn("!msg.extd", twai)
        self.assertIn("!msg.rtr", twai)

    def test_brake_mailbox_serializes_all_writers(self):
        mailbox = (ROOT / "include/ble/brake_state.h").read_text(encoding="utf-8")
        self.assertIn("writerMux_", mailbox)
        self.assertIn("portENTER_CRITICAL(&writerMux_)", mailbox)
        self.assertIn("portEXIT_CRITICAL(&writerMux_)", mailbox)

    def test_hello_ack_can_reset_a_ready_session_before_sequence_rejection(self):
        self.assertIn("isHelloSessionBoundary", self.client)
        self.assertIn("classifyHelloAck", self.client)
        boundary = self.client.index("isHelloSessionBoundary")
        sequence = self.client.index("classifySequence")
        self.assertLess(boundary, sequence)

    def test_ble_disconnect_does_not_hot_swap_can_filters(self):
        self.assertNotIn("setFilters(", self.client)

    def test_controller_is_condition_driven_without_window_or_latch(self):
        for token in (
            "kVirtualParkWindowMs",
            "automaticLocked_",
            "OBSTACLE_SHIFT_WAIT_RELEASE",
            "OBSTACLE_SHIFT_LATCHED",
            "SHIFT_REASON_WINDOW_COMPLETE",
            "virtualParkRemainingMs",
            "automaticWindowCount",
            "manualWindowCount",
        ):
            with self.subTest(token=token):
                self.assertNotIn(token, self.controller)
        self.assertIn("OBSTACLE_SHIFT_IDLE", self.controller)
        self.assertIn("activationCount", self.controller)

    def test_ble_status_exposes_condition_driven_shift_state(self):
        for token in (
            "shiftEnabled",
            "shiftState",
            "shiftReason",
            "brakeStateAgeMs",
            "real118AgeMs",
            "virtualParkActive",
            "activationCount",
            "virtualParkTxCount",
            "virtualParkTxFailCount",
        ):
            self.assertIn(token, self.bridge)
        for token in (
            "shiftLatched",
            "shiftTriggerSource",
            "virtualParkRemainingMs",
            "automaticWindowCount",
            "manualWindowCount",
        ):
            self.assertNotIn(token, self.bridge)
        self.assertIn('bleBridgeArgEnabled("shift"', self.bridge)
        self.assertIn("setObstacleShiftEnabled", self.bridge)

    def test_dashboard_renders_continuous_injection_status(self):
        self.assertIn("shift:bleElement('shift-enabled').checked?'1':'0'", self.source)
        for token in (
            "data.shiftEnabled",
            "data.shiftStateName",
            "data.physicalPressed",
            "data.brakeStateAgeMs",
            "data.virtualParkActive",
            "data.shiftReasonName",
            "data.activationCount",
            "持续注入",
            "松开刹车立即停止",
        ):
            self.assertIn(token, self.source)
        for token in (
            "等待释放",
            "已锁存",
            "1000 ms",
            "Auto windows",
            "Manual windows",
        ):
            self.assertNotIn(token, self.source)


if __name__ == "__main__":
    unittest.main()
