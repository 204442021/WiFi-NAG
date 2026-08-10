import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION_FILE = ROOT / "VERSION"
CAN_DRIVER_FILE = ROOT / "include" / "drivers" / "can_driver.h"
TWAI_DRIVER_FILE = ROOT / "include" / "drivers" / "twai_driver.h"
APP_FILE = ROOT / "include" / "app.h"
MAIN_FILE = ROOT / "src" / "main.cpp"
SDKCONFIG_FILE = ROOT / "sdkconfig.defaults"
HISTORICAL_V1_0_2_WORKFLOW_FILE = (
    ROOT / ".github" / "workflows" / "release-v1.0.2.yml"
)


class CanRestartSafetyRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.version = VERSION_FILE.read_text(encoding="utf-8").strip()
        cls.can_driver = CAN_DRIVER_FILE.read_text(encoding="utf-8")
        cls.twai = TWAI_DRIVER_FILE.read_text(encoding="utf-8")
        cls.app = APP_FILE.read_text(encoding="utf-8")
        cls.main = MAIN_FILE.read_text(encoding="utf-8")
        cls.sdkconfig = SDKCONFIG_FILE.read_text(encoding="utf-8")
        cls.historical_workflow = (
            HISTORICAL_V1_0_2_WORKFLOW_FILE.read_text(encoding="utf-8")
            if HISTORICAL_V1_0_2_WORKFLOW_FILE.exists()
            else ""
        )

    def test_v1_0_6_is_the_single_internal_version(self) -> None:
        self.assertEqual(self.version, "V1.0.6")

    def test_read_only_starts_the_hardware_controller_in_listen_only_mode(self) -> None:
        self.assertIn("TWAI_MODE_LISTEN_ONLY", self.twai)
        self.assertRegex(
            self.twai,
            r"tx_queue_len\s*=\s*writeEnabled_\s*\?\s*TWAI_TX_QUEUE_LEN\s*:\s*0",
        )
        self.assertIn("virtual bool setWriteEnabled(bool enabled)", self.can_driver)

    def test_runtime_can_write_switch_controls_the_twai_mode(self) -> None:
        self.assertIn("appSyncCanWriteMode", self.app)
        self.assertRegex(
            self.app,
            r"desiredWriteEnabled\s*=\s*canActive\s*&&\s*!Update\.isRunning\(\)",
        )
        self.assertIn("appDriver->setWriteEnabled(desiredWriteEnabled)", self.app)

    def test_restart_handler_quiesces_can_before_esp_restart(self) -> None:
        self.assertIn("esp_register_shutdown_handler(appCanShutdownHandler)", self.main)
        self.assertIn("appPrepareCanForRestart", self.app)
        self.assertIn("prepareForRestart() override", self.twai)
        self.assertIn("waitForBusIdleLocked()", self.twai)
        self.assertIn("forceTxRecessiveLocked()", self.twai)

    def test_filter_is_installed_before_start_and_duplicate_filter_is_noop(self) -> None:
        set_filter = "appDriver->setFilters(appHandler->filterIds(), appHandler->filterIdCount());"
        init_driver = "if (!appDriver->init())"
        self.assertIn(set_filter, self.app)
        self.assertIn(init_driver, self.app)
        self.assertLess(self.app.index(set_filter), self.app.index(init_driver))
        self.assertIn("sameFilter", self.twai)

    def test_twai_is_flash_safe_and_listen_only_errata_is_fixed(self) -> None:
        self.assertIn("CONFIG_TWAI_ISR_IN_IRAM=y", self.sdkconfig)
        self.assertIn("CONFIG_TWAI_ERRATA_FIX_LISTEN_ONLY_DOM=y", self.sdkconfig)
        self.assertIn("ESP_INTR_FLAG_IRAM", self.twai)

    def test_historical_v1_0_2_release_workflow_is_preserved(self) -> None:
        self.assertIn('branches: ["V1.0.2"]', self.historical_workflow)
        self.assertIn("python -m unittest discover", self.historical_workflow)
        self.assertIn("pio test -e native_nag", self.historical_workflow)
        self.assertIn("pio test -e native_twai", self.historical_workflow)
        self.assertIn("pio run -e wifi_nag_ESP32_S3_CAN", self.historical_workflow)
        self.assertIn("WIFI-NAG-V1.0.2-OTA.bin", self.historical_workflow)
        self.assertIn("gh release", self.historical_workflow)


if __name__ == "__main__":
    unittest.main()
