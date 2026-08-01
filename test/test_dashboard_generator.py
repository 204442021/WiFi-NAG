import shutil
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def find_node() -> str:
    command = shutil.which("node") or shutil.which("node.exe")
    if command:
        return command
    candidate = (
        Path(sys.executable).resolve().parents[1]
        / "node"
        / "bin"
        / "node.exe"
    )
    if candidate.is_file():
        return str(candidate)
    raise RuntimeError("Node.js is required for dashboard syntax checks")


class DashboardGeneratorTests(unittest.TestCase):
    def test_generator_works_without_optional_minifier_packages(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            checkout = Path(temp_dir)
            (checkout / "scripts").mkdir(parents=True)
            (checkout / "include" / "web").mkdir(parents=True)
            shutil.copy2(
                ROOT / "scripts" / "minify_dashboard.py",
                checkout / "scripts" / "minify_dashboard.py",
            )
            for name in (
                "mcp2515_dashboard_ui.src.h",
                "ble_fsd_diagnostics.js",
            ):
                shutil.copy2(
                    ROOT / "include" / "web" / name,
                    checkout / "include" / "web" / name,
                )

            result = subprocess.run(
                [sys.executable, "-S", "scripts/minify_dashboard.py"],
                cwd=checkout,
                text=True,
                capture_output=True,
                check=False,
            )

            self.assertEqual(
                result.returncode,
                0,
                msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
            )
            generated = (
                checkout / "include" / "web" / "mcp2515_dashboard_ui.h"
            ).read_text(encoding="utf-8")
            self.assertIn("BleFsdDiagnostics", generated)
            self.assertIn("downloadBleFsdDiagnostics", generated)
            self.assertNotIn("__BLE_FSD_DIAGNOSTICS_CORE__", generated)
            self.assertIn("DASH_HTML_GZ_LEN", generated)

            html_match = re.search(r'R"HTML\((.*)\)HTML";', generated, re.DOTALL)
            self.assertIsNotNone(html_match)
            script_blocks = re.findall(
                r"<script\b[^>]*>(.*?)</script>",
                html_match.group(1),
                re.DOTALL | re.IGNORECASE,
            )
            self.assertTrue(script_blocks)
            script_path = checkout / "dashboard.generated.js"
            script_path.write_text("\n".join(script_blocks), encoding="utf-8")
            syntax = subprocess.run(
                [find_node(), "--check", str(script_path)],
                cwd=checkout,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(
                syntax.returncode,
                0,
                msg=f"stdout:\n{syntax.stdout}\nstderr:\n{syntax.stderr}",
            )


if __name__ == "__main__":
    unittest.main()
