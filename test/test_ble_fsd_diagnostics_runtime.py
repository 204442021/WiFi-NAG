import shutil
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "test" / "test_ble_fsd_diagnostics_ui.js"


def find_node() -> str:
    command = shutil.which("node") or shutil.which("node.exe")
    if command:
        return command

    dependencies = Path(sys.executable).resolve().parents[1]
    candidate = dependencies / "node" / "bin" / "node.exe"
    if candidate.is_file():
        return str(candidate)

    raise RuntimeError("Node.js is required for dashboard behavior tests")


class BleFsdDiagnosticsRuntimeTests(unittest.TestCase):
    def test_dashboard_diagnostics_behavior(self) -> None:
        result = subprocess.run(
            [find_node(), str(SCRIPT)],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(
            result.returncode,
            0,
            msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        )
        self.assertIn("11 tests passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
