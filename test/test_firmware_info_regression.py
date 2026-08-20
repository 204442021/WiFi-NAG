import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION_FILE = ROOT / "VERSION"
RELEASE_NOTES_FILE = ROOT / "RELEASE_NOTES_V1.0.7.md"
CMAKE_FILE = ROOT / "CMakeLists.txt"
DASH_FILE = ROOT / "include" / "web" / "mcp2515_dashboard.h"
UI_SOURCE_FILE = ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h"
UI_BASE_FILE = ROOT / "include" / "web" / "mcp2515_dashboard_ui.base.h"
UI_WRAPPER_FILE = ROOT / "include" / "web" / "mcp2515_dashboard_ui.h"


class FirmwareInfoRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.version = VERSION_FILE.read_text(encoding="utf-8").strip()
        cls.release_notes = (
            RELEASE_NOTES_FILE.read_text(encoding="utf-8")
            if RELEASE_NOTES_FILE.exists()
            else ""
        )
        cls.cmake = CMAKE_FILE.read_text(encoding="utf-8")
        cls.dash = DASH_FILE.read_text(encoding="utf-8")
        cls.ui_source = UI_SOURCE_FILE.read_text(encoding="utf-8-sig")
        cls.ui_base = UI_BASE_FILE.read_text(encoding="utf-8-sig")
        cls.ui_wrapper = UI_WRAPPER_FILE.read_text(encoding="utf-8-sig")

    def test_version_file_is_single_v4_1_v13_source(self) -> None:
        self.assertEqual(self.version, "V4.1 V13")
        self.assertNotIn("3.0.0-beta.5", self.version)

    def test_v1_0_7_release_notes_match_internal_version(self) -> None:
        self.assertIn("# WIFI-NAG V1.0.7", self.release_notes)
        self.assertIn("`V1.0.7`", self.release_notes)
        self.assertIn("每个真实 `0x370`", self.release_notes)
        self.assertIn("`+1.50 .. +1.80 Nm`", self.release_notes)
        self.assertIn("`2000 ms`", self.release_notes)
        self.assertIn("障碍物换挡开关强制关闭", self.release_notes)
        self.assertNotIn("5～8 秒发送间隔", self.release_notes)

    def test_espidf_internal_version_comes_from_version_file(self) -> None:
        self.assertRegex(
            self.cmake,
            r'file\(STRINGS\s+"\$\{CMAKE_CURRENT_LIST_DIR\}/VERSION"\s+PROJECT_VER\s+LIMIT_COUNT\s+1\)',
        )
        self.assertIn('string(STRIP "${PROJECT_VER}" PROJECT_VER)', self.cmake)
        self.assertIn(
            'string(REPLACE " " "\\\\\\\\\\\\\\\\x20" PROJECT_VER "${PROJECT_VER}")',
            self.cmake,
        )
        self.assertIn("esp_app_get_description()", self.dash)
        self.assertIn('\\"firmware\\":\\"', self.dash)

    def test_backend_reports_partition_and_persisted_ota_time(self) -> None:
        self.assertIn("esp_ota_get_running_partition()", self.dash)
        self.assertIn("ESP_PARTITION_SUBTYPE_APP_OTA_0", self.dash)
        self.assertIn("ESP_PARTITION_SUBTYPE_APP_OTA_1", self.dash)
        self.assertIn('\\"ota_partition\\":\\"', self.dash)
        self.assertIn('\\"ota_time\\":\\"', self.dash)
        self.assertIn('prefs.putString("ota_time", otaTime);', self.dash)

        success_guard = "upload.totalSize > 0 && Update.end(true) && Update.isFinished()"
        persist_call = 'dashPersistOtaTime(server.arg("ota_time"))'
        self.assertIn(success_guard, self.dash)
        self.assertIn(persist_call, self.dash)
        self.assertLess(self.dash.index(success_guard), self.dash.index(persist_call))

    def test_generated_ui_wrapper_loads_only_real_base_page(self) -> None:
        base_include = '#include "web/mcp2515_dashboard_ui.base.h"'
        extension_include = '#include "web/nag_sweep_dashboard.h"'
        self.assertEqual(self.ui_wrapper.count(base_include), 1)
        self.assertNotIn(extension_include, self.ui_wrapper)

    def test_firmware_update_card_has_exactly_required_metadata_fields(self) -> None:
        for label, ui in (("source", self.ui_source), ("base", self.ui_base)):
            with self.subTest(file=label):
                for element_id in ("fw-version", "fw-partition", "fw-ota-time"):
                    self.assertRegex(ui, rf'\bid=(?:"{element_id}"|{element_id}\b)')
                self.assertIn("Firmware Version", ui)
                self.assertIn("Current Partition", ui)
                self.assertIn("OTA Upload Time", ui)
                self.assertIn("formatOtaLocalTime", ui)
                self.assertIn("/update?ota_time=", ui)

    def test_ui_loads_firmware_metadata_without_enabling_live_monitor(self) -> None:
        for label, ui in (("source", self.ui_source), ("base", self.ui_base)):
            with self.subTest(file=label):
                self.assertIn("async function loadFirmwareInfo()", ui)
                self.assertIn("loadFirmwareInfo();", ui)

        function = re.search(
            r"async function loadFirmwareInfo\(\)\{(?P<body>.*?)\n\}",
            self.ui_source,
            re.DOTALL,
        )
        self.assertIsNotNone(function)
        self.assertIn("/system_status", function.group("body"))
        self.assertNotIn("systemStatusEnabled", function.group("body"))

    def test_old_repository_versions_are_not_left_in_runtime_code(self) -> None:
        runtime_files = [
            VERSION_FILE,
            CMAKE_FILE,
            DASH_FILE,
            UI_SOURCE_FILE,
            UI_BASE_FILE,
            UI_WRAPPER_FILE,
        ]
        for path in runtime_files:
            text = path.read_text(encoding="utf-8-sig")
            with self.subTest(path=path.as_posix()):
                self.assertNotIn("3.0.0-beta.5", text)
                self.assertNotIn("V1.0.0", text)
                self.assertNotIn("V1.0.1", text)
                self.assertNotIn("V1.0.2", text)
                self.assertNotIn("V1.0.3", text)
                self.assertNotIn("V1.0.4", text)
                self.assertNotIn("V1.0.5", text)
                self.assertNotIn("V1.0.6", text)
                self.assertNotIn("V1.0.8", text)


if __name__ == "__main__":
    unittest.main()
