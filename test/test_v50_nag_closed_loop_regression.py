import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

EXPECTED_NVS_KEYS = (
    "nag_pv_n_min", "nag_pv_n_max", "nag_pv_p_min", "nag_pv_p_max",
    "nag_cr_n_min", "nag_cr_n_max", "nag_cr_p_min", "nag_cr_p_max",
    "nag_act_min", "nag_act_max", "nag_rel_min", "nag_rel_max",
    "nag_rst_min", "nag_rst_max", "nag_dir_db", "nag_das_ms",
)

EXPECTED_NVS_DEFAULTS = {
    "nag_pv_n_min": "0.15",
    "nag_pv_n_max": "0.18",
    "nag_pv_p_min": "0.15",
    "nag_pv_p_max": "0.18",
    "nag_cr_n_min": "1.50",
    "nag_cr_n_max": "1.80",
    "nag_cr_p_min": "1.50",
    "nag_cr_p_max": "1.80",
    "nag_act_min": "0.8",
    "nag_act_max": "1.4",
    "nag_rel_min": "0.2",
    "nag_rel_max": "0.4",
    "nag_rst_min": "1.5",
    "nag_rst_max": "2.5",
    "nag_dir_db": "0.05",
    "nag_das_ms": "500",
}

EXPECTED_CONFIG_FIELDS = (
    "preventiveNegativeMinNm", "preventiveNegativeMaxNm",
    "preventivePositiveMinNm", "preventivePositiveMaxNm",
    "correctiveNegativeMinNm", "correctiveNegativeMaxNm",
    "correctivePositiveMinNm", "correctivePositiveMaxNm",
    "activityMinSec", "activityMaxSec", "releaseMinSec", "releaseMaxSec",
    "restMinSec", "restMaxSec", "directionDeadbandNm", "dasFreshTimeoutMs",
)

EXPECTED_STATUS_FIELDS = (
    "nagDasSeen", "nagDasFresh", "nagDasAgeMs", "nagDasHos",
    "nagDasFrames", "nagOemEpasFrames", "nagLastOemEpasAgeMs",
    "nagLastOemEpasCounter", "nagObservedTorqueNm",
    "nagAdaptivePhase", "nagAdaptiveBlockReason",
    "nagAdaptiveTargetTorqueNm", "nagAdaptivePhaseRemainingMs",
    "nagCorrectiveAttempt", "nagCorrectiveBurstFrame",
    "nagCorrectiveBurstFrameTarget", "nagHosEscalations",
    "nagAcknowledgementCount", "nagAcknowledgementTimeouts",
    "nagLastAcknowledgementLatencyMs", "nagMaxAcknowledgementLatencyMs",
    "nagSendAttempts", "nagSendFailures", "nagEcho",
    "nagCounterCollisions", "nagLastCounterCollisionGapUs",
)


class V50NagClosedLoopRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dashboard = (ROOT / "include/web/mcp2515_dashboard.h").read_text(
            encoding="utf-8"
        )
        cls.handler = (ROOT / "include/handlers_base.h").read_text(encoding="utf-8")

    def test_closed_loop_nvs_contract_uses_exact_keys_and_defaults(self):
        for key in EXPECTED_NVS_KEYS:
            self.assertIn(f'prefs.putString("{key}"', self.dashboard)
            self.assertIn(
                f'prefs.getString("{key}", "{EXPECTED_NVS_DEFAULTS[key]}")',
                self.dashboard,
            )

        for retired_prefix in ("nag_h1_", "nag_h2_", "nag_ad_"):
            self.assertNotIn(f'prefs.getString("{retired_prefix}', self.dashboard)

    def test_adaptive_api_uses_exact_v50_config_fields(self):
        for field in EXPECTED_CONFIG_FIELDS:
            self.assertIn(f'"{field}"', self.dashboard)

        for retired in (
            "adaptiveTorqueNm",
            "adaptiveDeadbandNm",
            "adaptiveAngleDeg",
            "adaptiveSendWindowSec",
            "adaptivePauseMinSec",
            "adaptivePauseMaxSec",
        ):
            self.assertNotIn(retired, self.dashboard)

    def test_post_normalizes_before_comparing_and_resetting_controller(self):
        parse_index = self.dashboard.find('server.hasArg("preventiveNegativeMinNm")')
        self.assertGreaterEqual(parse_index, 0, "V5 POST parsing is missing")
        normalize_index = self.dashboard.index(
            "NagAdaptiveController::normalizeConfig(adaptive)", parse_index
        )
        compare_index = self.dashboard.index(
            "dashNagAdaptiveConfigEqual(previous, adaptive)", normalize_index
        )
        set_index = self.dashboard.index("nag->setAdaptiveConfig(adaptive)", compare_index)
        self.assertLess(parse_index, normalize_index)
        self.assertLess(normalize_index, compare_index)
        self.assertLess(compare_index, set_index)

    def test_config_equality_covers_every_closed_loop_member(self):
        equality_start = self.dashboard.index("dashNagAdaptiveConfigEqual")
        equality_end = self.dashboard.index("static bool dashApplyNagConfigArgs", equality_start)
        equality = self.dashboard[equality_start:equality_end]
        for member in (
            "preventiveNegativeMinCentiNm", "preventiveNegativeMaxCentiNm",
            "preventivePositiveMinCentiNm", "preventivePositiveMaxCentiNm",
            "correctiveNegativeMinCentiNm", "correctiveNegativeMaxCentiNm",
            "correctivePositiveMinCentiNm", "correctivePositiveMaxCentiNm",
            "activityMinMs", "activityMaxMs", "releaseMinMs", "releaseMaxMs",
            "restMinMs", "restMaxMs", "torqueDeadbandCentiNm",
            "dasFreshTimeoutMs",
        ):
            self.assertIn(member, equality)

    def test_status_exposes_exact_closed_loop_telemetry(self):
        for field in EXPECTED_STATUS_FIELDS:
            self.assertIn(f'\\"{field}\\"', self.dashboard)

        self.assertIn("const NagAdaptiveSnapshot snapshot", self.dashboard)
        self.assertIn("snapshot.acknowledgementCount", self.dashboard)
        self.assertIn("nag->nagEchoCount", self.dashboard)
        self.assertNotIn("DAS accepted", self.dashboard)

    def test_api_and_status_share_one_nonduplicated_telemetry_serializer(self):
        self.assertEqual(
            self.dashboard.count("dashAppendNagClosedLoopTelemetry(j, nag);"), 2
        )
        for field in EXPECTED_STATUS_FIELDS:
            self.assertEqual(
                self.dashboard.count(f'\\"{field}\\"'),
                1,
                f"{field} must be serialized exactly once by the shared helper",
            )

    def test_phase_block_and_direction_names_match_controller_contract(self):
        for name in (
            "disabled", "wait-das", "arming", "maintenance", "release",
            "rest", "corrective", "verify", "fault-hold",
            "none", "das-missing", "das-stale", "no-direction", "das-state",
            "ack-timeout", "torque", "angle", "hold",
        ):
            self.assertIn(f'return "{name}"', self.dashboard)

    def test_das_39b_remains_receive_only(self):
        self.assertIn("0x39B", self.handler)
        self.assertRegex(self.handler, r"frame\.id == NagDasFeedbackTracker::kDasCanId")
        self.assertNotRegex(
            self.handler,
            re.compile(r"\b\w+\.id\s*=\s*(?:0x39[Bb]|923)\b"),
        )
        self.assertNotRegex(
            self.handler,
            re.compile(r"driver\.send\s*\([^;]*(?:0x39[Bb]|923)", re.DOTALL),
        )


if __name__ == "__main__":
    unittest.main()
