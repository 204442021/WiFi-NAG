import csv
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = (ROOT / "src" / "espidf_runtime.cpp").read_text(encoding="utf-8")
RUNTIME_HEADER = (
    ROOT / "include" / "platform" / "espidf_runtime.h"
).read_text(encoding="utf-8")
DASHBOARD = (
    ROOT / "include" / "web" / "mcp2515_dashboard.h"
).read_text(encoding="utf-8")
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
PLATFORMIO = (ROOT / "platformio.ini").read_text(encoding="utf-8")
SDK_DEFAULTS = (ROOT / "sdkconfig.wifi_nag.defaults").read_text(
    encoding="utf-8"
)
BASE_SDK_DEFAULTS = (ROOT / "sdkconfig.defaults").read_text(
    encoding="utf-8"
)
PARTITIONS = ROOT / "partitions_16mb_ota_4096k_nvs64_boot.csv"


class OtaRegressionTests(unittest.TestCase):
    def test_platformio_and_kconfig_use_the_same_dual_ota_layout(self) -> None:
        self.assertIn(
            "board_build.partitions = partitions_16mb_ota_4096k_nvs64_boot.csv",
            PLATFORMIO,
        )
        self.assertIn("CONFIG_PARTITION_TABLE_CUSTOM=y", BASE_SDK_DEFAULTS)
        self.assertIn(
            'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_16mb_ota_4096k_nvs64_boot.csv"',
            BASE_SDK_DEFAULTS,
        )
        self.assertIn(
            "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y", BASE_SDK_DEFAULTS
        )

        rows = {}
        with PARTITIONS.open(newline="", encoding="utf-8") as source:
            for row in csv.reader(
                line for line in source if not line.lstrip().startswith("#")
            ):
                if row and row[0].strip():
                    rows[row[0].strip()] = row
        self.assertEqual(rows["boot"][2].strip(), "ota_0")
        self.assertEqual(rows["app1"][2].strip(), "ota_1")
        self.assertEqual(int(rows["boot"][4].strip(), 0), 0x400000)
        self.assertEqual(int(rows["app1"][4].strip(), 0), 0x400000)

    def test_update_runtime_preflights_and_verifies_partitions(self) -> None:
        for token in (
            "esp_ota_get_running_partition()",
            "esp_ota_set_boot_partition(runningPartition_)",
            "esp_ota_get_next_update_partition(runningPartition_)",
            "ESP_PARTITION_SUBTYPE_APP_OTA_MIN",
            "ESP_PARTITION_SUBTYPE_APP_OTA_MAX",
            "OTA_WITH_SEQUENTIAL_WRITES",
            "validateImagePrefix()",
            "verifyWrittenImage()",
            "esp_ota_get_partition_description",
            "Boot partition selection failed",
            "Boot partition restore failed",
        ):
            with self.subTest(token=token):
                self.assertIn(token, RUNTIME)

        self.assertIn("kImagePrefixSize", RUNTIME_HEADER)
        self.assertIn("imagePrefix_", RUNTIME_HEADER)
        self.assertIn("bootPartitionBefore_", RUNTIME_HEADER)
        self.assertNotIn(
            "esp_ota_get_next_update_partition(nullptr)", RUNTIME
        )

    def test_dashboard_does_not_disable_the_task_watchdog_for_ota(self) -> None:
        ota_start = DASHBOARD.index("static void handleOtaUpload()")
        ota_end = DASHBOARD.index("// CAN RUNTIME MANAGEMENT", ota_start)
        ota = DASHBOARD[ota_start:ota_end]
        self.assertNotIn("esp_task_wdt_deinit", ota)
        self.assertIn("Update.begin(UPDATE_SIZE_UNKNOWN)", ota)
        self.assertIn("Update.end(true)", ota)
        self.assertIn("Update.abort()", ota)

    def test_pending_ota_image_requires_health_confirmation(self) -> None:
        for token in (
            "APP_OTA_HEALTH_WINDOW_MS = 15000UL",
            "esp_ota_mark_app_valid_cancel_rollback",
            "esp_ota_mark_app_invalid_rollback_and_reboot",
            "appStartOtaHealthObservation()",
            "appServiceOtaHealthObservation()",
            "CanDriverState::Running",
        ):
            with self.subTest(token=token):
                self.assertIn(token, MAIN)


if __name__ == "__main__":
    unittest.main()
