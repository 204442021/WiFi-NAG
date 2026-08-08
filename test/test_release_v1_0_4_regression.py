import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW_FILE = ROOT / ".github" / "workflows" / "release-v1.0.4.yml"


class ReleaseV104RegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.workflow = WORKFLOW_FILE.read_text(encoding="utf-8")

    def test_release_workflow_targets_v1_0_4_with_write_permission(self) -> None:
        self.assertIn('branches: ["V1.0.4"]', self.workflow)
        self.assertIn("workflow_dispatch:", self.workflow)
        self.assertIn("contents: write", self.workflow)
        self.assertIn("TAG: V1.0.4", self.workflow)

    def test_release_workflow_runs_all_verification_gates(self) -> None:
        self.assertIn('python -m unittest discover -s test -p "test_*.py" -v', self.workflow)
        self.assertIn("python scripts/minify_dashboard.py --check", self.workflow)
        for environment in (
            "native_nag",
            "native_nag_sweep",
            "native_twai",
            "native_log_buffer",
            "native_ble",
        ):
            with self.subTest(environment=environment):
                self.assertIn(f"pio test -e {environment}", self.workflow)
        self.assertIn("pio run -e wifi_nag_ESP32_S3_CAN", self.workflow)

    def test_release_workflow_builds_complete_verified_asset_set(self) -> None:
        for asset in (
            "WIFI-NAG-V1.0.4-OTA.bin",
            "WIFI-NAG-V1.0.4-FACTORY-16MB.bin",
            "WIFI-NAG-V1.0.4-FLASH-GUIDE.txt",
            "WIFI-NAG-V1.0.4-MANIFEST.json",
            "WIFI-NAG-V1.0.4-SHA256SUMS.txt",
        ):
            with self.subTest(asset=asset):
                self.assertIn(asset, self.workflow)
        self.assertIn("--fill-flash-size 16MB", self.workflow)
        self.assertIn("sha256sum", self.workflow)
        self.assertIn("gh release create", self.workflow)
        self.assertIn("gh release upload", self.workflow)

    def test_release_profile_uses_existing_secret_contract_with_safe_fallback(self) -> None:
        for secret in ("DASH_SSID", "DASH_PASS", "DASH_OTA_USER", "DASH_OTA_PASS"):
            with self.subTest(secret=secret):
                self.assertIn(f"secrets.{secret}", self.workflow)
        self.assertIn('"DASH_SSID": "EVtools"', self.workflow)
        self.assertIn('"DASH_PASS": "changeme"', self.workflow)


if __name__ == "__main__":
    unittest.main()
