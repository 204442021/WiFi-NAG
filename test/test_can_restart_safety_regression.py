import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "include" / "app.h").read_text(encoding="utf-8")
CAN_DRIVER = (ROOT / "include" / "drivers" / "can_driver.h").read_text(
    encoding="utf-8"
)
MOCK = (ROOT / "include" / "drivers" / "mock_driver.h").read_text(
    encoding="utf-8"
)
TWAI = (ROOT / "include" / "drivers" / "twai_driver.h").read_text(
    encoding="utf-8"
)
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
DASH = (ROOT / "include" / "web" / "mcp2515_dashboard.h").read_text(
    encoding="utf-8"
)
SDK_DEFAULTS = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
SDK_PROFILE = (ROOT / "sdkconfig.wifi_nag_ESP32_S3_CAN").read_text(
    encoding="utf-8"
)


class CanRestartSafetyRegressionTests(unittest.TestCase):
    def test_driver_contract_and_mock_cover_safe_transitions(self) -> None:
        for token in ("setWriteEnabled(bool enabled)", "prepareForRestart()"):
            with self.subTest(token=token):
                self.assertIn(token, CAN_DRIVER)
                self.assertIn(token.split("(")[0], MOCK)
                self.assertIn(token.split("(")[0], TWAI)

        for token in (
            "writeEnabled",
            "restartPrepared",
            "if (restartPrepared && enabled)",
            "sendResult && writeEnabled && !restartPrepared",
        ):
            with self.subTest(token=token):
                self.assertIn(token, MOCK)

    def test_disabled_mode_is_real_listen_only_with_no_tx_queue(self) -> None:
        self.assertIn("writeEnabled_ = false", TWAI)
        self.assertIn("TWAI_MODE_LISTEN_ONLY", TWAI)
        self.assertIn("writeEnabled_ ? TWAI_MODE_NORMAL : TWAI_MODE_LISTEN_ONLY", TWAI)
        self.assertIn("g_config_.tx_queue_len = writeEnabled_ ? TWAI_TX_QUEUE_LEN : 0", TWAI)
        self.assertIn("if (!driverOK_ || !writeEnabled_ || shutdown_)", TWAI)

    def test_filter_is_set_before_first_install_and_identical_filter_is_idempotent(self) -> None:
        filter_call = APP.index("appDriver->setFilters(")
        init_call = APP.index("if (!appDriver->init())", filter_call)
        self.assertLess(filter_call, init_call)

        for token in (
            "sameFilter",
            "if (sameFilter)",
            "exactFilterListMatchesLocked",
            "const bool reinstall = driverInstalled_ && !shutdown_",
        ):
            with self.subTest(token=token):
                self.assertIn(token, TWAI)

    def test_mode_changes_and_restart_wait_for_idle_then_force_recessive(self) -> None:
        self.assertIn("waitForBusIdleLocked", TWAI)
        self.assertIn("kBusIdleStableUs = 12", TWAI)
        self.assertIn("kBusIdleTimeoutUs = 5000", TWAI)
        self.assertIn("gpio_get_level(rxPin_)", TWAI)

        for token in (
            "waitForBusIdleLocked();\n            stopAndUninstallLocked();",
            "forceTxRecessiveLocked();",
            "gpio_pullup_en(txPin_)",
            "gpio_set_level(txPin_, 1)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, TWAI)

        # Both failure exits must leave TX safe, and normal teardown must do
        # the same even when the controller was not installed.
        self.assertGreaterEqual(TWAI.count("forceTxRecessiveLocked();"), 5)

    def test_shutdown_handler_and_rollback_use_the_same_prepare_path(self) -> None:
        self.assertIn("esp_register_shutdown_handler(appCanShutdownHandler)", MAIN)
        self.assertGreaterEqual(MAIN.count("appPrepareCanForRestart();"), 3)
        self.assertIn("static void appCanShutdownHandler()", MAIN)
        self.assertIn("appCanRestartPreparing", APP)
        self.assertIn("appDriver->prepareForRestart()", APP)

        reboot_start = DASH.index("static void handleReboot()")
        reboot_end = DASH.index("static void handleOtaResult()", reboot_start)
        self.assertLess(
            DASH.index("appPrepareCanForRestart();", reboot_start, reboot_end),
            DASH.index("ESP.restart();", reboot_start, reboot_end),
        )

    def test_ota_guard_stays_active_after_update_end_until_reboot_or_abort(self) -> None:
        self.assertIn("appCanOtaActive", APP)
        self.assertIn("const bool otaActive = (bool)appCanOtaActive || Update.isRunning();", APP)
        self.assertIn("appBeginCanOtaGuard()", DASH)
        self.assertIn("appEndCanOtaGuard()", DASH)

        upload_start = DASH.index("static void handleOtaUpload()")
        upload_end = DASH.index("// CAN RUNTIME MANAGEMENT", upload_start)
        upload = DASH[upload_start:upload_end]
        self.assertLess(upload.index("appBeginCanOtaGuard()"), upload.index("Update.begin("))
        self.assertGreaterEqual(upload.count("appEndCanOtaGuard()"), 3)

        result_start = DASH.index("static void handleOtaResult()")
        result_end = DASH.index("static void handleOtaUpload()", result_start)
        result = DASH[result_start:result_end]
        self.assertIn("appPrepareCanForRestart();", result)
        self.assertIn("Update.abort();", result)
        self.assertIn("dashApplyRuntimeState();", result)

    def test_iram_and_listen_only_errata_configuration_matches_idf6(self) -> None:
        for config in (SDK_DEFAULTS, SDK_PROFILE):
            with self.subTest(config="defaults" if config is SDK_DEFAULTS else "profile"):
                self.assertIn("CONFIG_TWAI_ISR_IN_IRAM_LEGACY=y", config)
                self.assertIn("CONFIG_TWAI_ISR_IN_IRAM=y", config)

        # IDF 6 removed the old Kconfig symbol; the S3 workaround is selected
        # by the HAL's TWAI_LL_HAS_LOM_DOM_ISSUE capability instead.
        self.assertIn("TWAI_LL_HAS_LOM_DOM_ISSUE", SDK_DEFAULTS)


if __name__ == "__main__":
    unittest.main()
