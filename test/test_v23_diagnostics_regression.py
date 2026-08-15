from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
DASH = (ROOT / "include/web/mcp2515_dashboard.h").read_text(encoding="utf-8")
DIAG = (ROOT / "include/web/memory_diagnostics.h").read_text(encoding="utf-8")
UI = (ROOT / "include/web/mcp2515_dashboard_ui.src.h").read_text(encoding="utf-8-sig")
BUILD_SCRIPT = (ROOT / "scripts/platformio_sync_profile.py").read_text(encoding="utf-8")


class V23DiagnosticsRegression(unittest.TestCase):
    def test_version_is_v23(self):
        self.assertEqual(VERSION, "V2.3")
        self.assertIn('<span class="version-badge">V2.3</span>', UI)
        self.assertIn('id="diag-version">V2.3</div>', UI)

    def test_memory_regions_are_separated(self):
        self.assertIn("MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT", DIAG)
        self.assertIn("MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT", DIAG)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", DIAG)
        self.assertIn("internal_largest", DASH)
        self.assertIn("internal_min", DASH)
        self.assertIn("internal_fragmentation", DASH)
        self.assertIn("dma_largest", DASH)
        self.assertIn("psram_largest", DASH)

    def test_recorder_is_fixed_capacity_psram_and_low_rate(self):
        self.assertIn("kSampleIntervalMs = 5000", DIAG)
        self.assertIn("kSampleCapacity = 1440", DIAG)
        self.assertIn("heap_caps_calloc", DIAG)
        self.assertIn("MALLOC_CAP_SPIRAM", DIAG)
        self.assertNotIn("xTaskCreate", DIAG)

    def test_export_and_control_routes_are_registered(self):
        for route in (
            "/diagnostics_export",
            "/diagnostics_tasks",
            "/diagnostics_mark",
            "/diagnostics_clear",
        ):
            self.assertIn(f'server.on("{route}"', DASH)
        self.assertIn("application/json", DASH)
        self.assertIn("Content-Disposition", DASH)
        self.assertIn("SPIFFS.open(kDashDiagExportPath, \"w\")", DASH)
        self.assertIn("server.streamFile", DASH)

    def test_ui_exposes_diagnostics_controls(self):
        for element_id in (
            "diag-memory-risk",
            "diag-recording-state",
            "diag-task-list",
            "diag-export-btn",
            "sys-dma",
            "sys-internal-frag",
        ):
            self.assertIn(f'id="{element_id}"', UI)
        self.assertIn("downloadDiagnostics()", UI)
        self.assertIn("markDiagnostics()", UI)
        self.assertIn("clearDiagnostics()", UI)

    def test_allocation_failure_and_previous_boot_are_captured_without_flash_writes(self):
        self.assertIn("heap_caps_register_failed_alloc_callback", DIAG)
        self.assertIn("RTC_NOINIT_ATTR", DIAG)
        callback = DIAG.split("static void dashDiagAllocationFailedHook", 1)[1].split("class DashMemoryDiagnostics", 1)[0]
        self.assertNotIn("String", callback)
        self.assertNotIn("SPIFFS", callback)
        self.assertNotIn("Preferences", callback)

    def test_export_metadata_contains_git_sha_but_not_credentials(self):
        self.assertIn("FIRMWARE_GIT_SHA", BUILD_SCRIPT)
        export_body = DASH.split("static bool dashWriteDiagnosticsExport", 1)[1].split("static void handleDiagnosticsExport", 1)[0]
        self.assertIn("FIRMWARE_GIT_SHA", export_body)
        for secret in ("staPass", "apPass", "kDashFixedOtaPassword", "peerDeviceId"):
            self.assertNotIn(secret, export_body)

    def test_can_and_ble_implementation_files_are_not_part_of_diagnostics_feature(self):
        self.assertNotIn("setFilters(", DIAG)
        self.assertNotIn("sendFrame(", DIAG)
        self.assertNotIn("setEnabled(", DIAG)


if __name__ == "__main__":
    unittest.main()
