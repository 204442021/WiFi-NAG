import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW_FILE = ROOT / ".github" / "workflows" / "release-v1.0.7.yml"


class ReleaseV107RegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = (
            WORKFLOW_FILE.read_text(encoding="utf-8")
            if WORKFLOW_FILE.exists()
            else ""
        )

    def test_release_workflow_exists_for_v1_0_7(self) -> None:
        self.assertTrue(WORKFLOW_FILE.exists())

    def test_release_workflow_targets_v1_0_7_with_write_permission(self) -> None:
        self.assertIn('branches: ["V1.0.7"]', self.workflow)
        self.assertIn("workflow_dispatch:", self.workflow)
        self.assertIn("contents: write", self.workflow)
        self.assertIn("TAG: V1.0.7", self.workflow)

    def test_release_workflow_runs_all_verification_gates(self) -> None:
        self.assertIn(
            'python -m unittest discover -s test -p "test_*.py" -v',
            self.workflow,
        )
        self.assertIn("python scripts/minify_dashboard.py --check", self.workflow)
        for environment in (
            "native_nag",
            "native_nag_sweep",
            "native_twai",
            "native_log_buffer",
            "native_ble",
            "native_obstacle_shift",
        ):
            with self.subTest(environment=environment):
                self.assertIn(f"pio test -e {environment}", self.workflow)
        self.assertIn("pio run -e wifi_nag_ESP32_S3_CAN", self.workflow)

    def test_release_workflow_builds_complete_verified_asset_set(self) -> None:
        for asset in (
            "WIFI-NAG-V1.0.7-OTA.bin",
            "WIFI-NAG-V1.0.7-FACTORY-16MB.bin",
            "WIFI-NAG-V1.0.7-FLASH-GUIDE.txt",
            "WIFI-NAG-V1.0.7-MANIFEST.json",
            "WIFI-NAG-V1.0.7-SHA256SUMS.txt",
        ):
            with self.subTest(asset=asset):
                self.assertIn(asset, self.workflow)
        self.assertIn("--fill-flash-size 16MB", self.workflow)
        self.assertIn("sha256sum", self.workflow)
        self.assertIn("gh release create", self.workflow)
        self.assertIn("gh release upload", self.workflow)


if __name__ == "__main__":
    unittest.main()
