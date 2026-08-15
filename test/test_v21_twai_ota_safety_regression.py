import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class V21TwaiOtaSafetyRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.app = (ROOT / "include/app.h").read_text(encoding="utf-8")
        cls.dashboard = (ROOT / "include/web/mcp2515_dashboard.h").read_text(
            encoding="utf-8"
        )
        cls.twai = (ROOT / "include/drivers/twai_driver.h").read_text(
            encoding="utf-8"
        )
        cls.controller = (ROOT / "include/nag_state_controller.h").read_text(
            encoding="utf-8"
        )
        cls.ble_client = (ROOT / "src/ble_bridge_client.cpp").read_text(
            encoding="utf-8"
        )
        cls.ble_protocol = (ROOT / "include/ble/bridge_protocol.h").read_text(
            encoding="utf-8"
        )

    def test_ota_quiesces_transmit_before_update_and_resumes_only_on_failure(self):
        start = self.dashboard.index("if (upload.status == UPLOAD_FILE_START)")
        begin = self.dashboard.index("Update.begin(UPDATE_SIZE_UNKNOWN)", start)
        quiesce = self.dashboard.index("appPrepareCanForOta()", start)
        self.assertLess(quiesce, begin)
        self.assertIn("appDriver->quiesceTransmit(100)", self.app)
        self.assertIn("appResumeCanAfterOtaFailure()", self.dashboard)

    def test_ota_boot_forces_nag_off_before_initial_twai_install(self):
        self.assertIn("dashPrepareCanBootPolicy()", (ROOT / "src/main.cpp").read_text())
        self.assertIn("kDashOtaNagForceOffKey", self.dashboard)
        self.assertIn("firmwareChanged", self.dashboard)
        self.assertIn('bootPreferences.putBool("can", false)', self.dashboard)
        self.assertIn("if (dashBootForcedNagOff)\n        canActive = false;", self.dashboard)
        self.assertIn("bootPolicy.initialWriteEnabled", (ROOT / "src/main.cpp").read_text())

    def test_restart_keeps_live_twai_installed_after_gate_and_queue_drain(self):
        restart = self.twai.split("void prepareForRestart() override", 1)[1]
        restart = restart.split("bool enableInterrupt", 1)[0]
        self.assertIn("transmitGateOpen_ = false", restart)
        self.assertIn("waitForTxIdleLocked", restart)
        self.assertNotIn("twai_stop", restart)
        self.assertNotIn("twai_driver_uninstall", restart)

    def test_ordinary_start_requires_three_valid_370_frames_before_tx(self):
        self.assertIn("appStableNagFrameCount >= 3", self.app)
        self.assertIn("appNagFrameChecksumValid", self.app)
        self.assertIn("appDriver->setTransmitGate(true)", self.app)
        self.assertLess(
            self.app.index("appObserveCanStability(frame)"),
            self.app.index("h->handleMessage(frame, *appDriver)"),
        )

    def test_bus_off_uses_controller_recovery_and_reopens_only_via_gate(self):
        self.assertIn("twai_initiate_recovery()", self.twai)
        self.assertIn("TWAI_STATE_RECOVERING", self.twai)
        self.assertIn("TWAI_STATE_STOPPED && recoveryInProgress_", self.twai)
        self.assertIn("twai_start()", self.twai)
        self.assertIn("transmitGateOpen_ = false", self.twai)

    def test_wifi_nag_remains_the_only_revision_authority(self):
        self.assertIn("revision_ = loadRevision()", self.controller)
        self.assertIn("persistRevision(nextRevision)", self.controller)
        self.assertIn("command->expectedRevision", self.controller)
        self.assertIn("NAG_RESULT_REVISION_CONFLICT", self.controller)
        self.assertIn("state.revision", self.ble_client)
        self.assertIn("command.expectedRevision", self.ble_client)
        self.assertIn("static constexpr uint8_t kVersion = 0x02", self.ble_protocol)


if __name__ == "__main__":
    unittest.main()
