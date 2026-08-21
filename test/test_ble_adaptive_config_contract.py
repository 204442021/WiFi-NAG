import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "include" / "ble" / "bridge_webui.h"


class BleAdaptiveConfigContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = BRIDGE.read_text(encoding="utf-8")
        match = re.search(
            r"static void bleBridgeHandleUnifiedConfig\(\)\s*\{(.*?)\n\}",
            cls.source,
            re.DOTALL,
        )
        if match is None:
            raise AssertionError("bleBridgeHandleUnifiedConfig body not found")
        cls.body = match.group(1)

    def test_uses_tristate_adaptive_apply_contract(self):
        self.assertIn("DashNagConfigError nagConfigError;", self.body)
        self.assertIn("dashApplyNagConfigArgs(nagConfigError)", self.body)
        self.assertIn(
            "NagAdaptiveConfigInput::shouldRejectRequest(nagConfigResult)",
            self.body,
        )
        self.assertIn(
            "NagAdaptiveConfigInput::shouldPublishCommand(nagConfigResult)",
            self.body,
        )
        self.assertNotIn("dashApplyNagConfigArgs()", self.body)

    def test_rejects_invalid_adaptive_input_before_can_state_change(self):
        apply_pos = self.body.index("dashApplyNagConfigArgs(nagConfigError)")
        reject_pos = self.body.index(
            "NagAdaptiveConfigInput::shouldRejectRequest(nagConfigResult)"
        )
        return_pos = self.body.index("return;", reject_pos)
        can_pos = self.body.index('server.hasArg("can")')
        self.assertLess(apply_pos, reject_pos)
        self.assertLess(reject_pos, return_pos)
        self.assertLess(return_pos, can_pos)


if __name__ == "__main__":
    unittest.main()
