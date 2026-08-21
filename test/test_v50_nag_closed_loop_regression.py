import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_FILE = ROOT / "include/web/mcp2515_dashboard_ui.src.h"

EXPECTED_CUSTOM_UI_IDS = (
    "nag-custom-readiness", "nag-custom-readiness-reason",
    "nag-custom-dirty", "nag-custom-defaults", "nag-custom-save",
    "nag-pv-neg-min", "nag-pv-neg-max", "nag-pv-pos-min", "nag-pv-pos-max",
    "nag-cr-neg-min", "nag-cr-neg-max", "nag-cr-pos-min", "nag-cr-pos-max",
    "nag-active-min", "nag-active-max", "nag-release-min", "nag-release-max",
    "nag-rest-min", "nag-rest-max", "nag-direction-deadband",
    "nag-custom-hard-cap", "nag-custom-das-timeout",
)

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
    "nagDirectionSource",
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
        cls.source = SOURCE_FILE.read_text(encoding="utf-8-sig")

    def test_custom_strategy_ui_has_exact_controls_and_safety_copy(self):
        for element_id in EXPECTED_CUSTOM_UI_IDS:
            self.assertRegex(self.source, rf'\bid="{re.escape(element_id)}"')

        for text in (
            "预防层", "纠正层", "节奏与休息", "安全边界",
            "休息期不额外发送 0x370", "本地发送成功不等于 DAS 接受",
        ):
            self.assertIn(text, self.source)
        for retired in (
            "反方向持续注入", "Hands-On 1 扭矩范围", "Hands-On 2 扭矩范围",
        ):
            self.assertNotIn(retired, self.source)

        for element_id in ("nag-pv-neg-min", "nag-pv-neg-max", "nag-pv-pos-min", "nag-pv-pos-max"):
            self.assertRegex(
                self.source,
                rf'id="{element_id}"[^>]*min="0\.10"[^>]*max="0\.50"[^>]*step="0\.01"',
            )
        for element_id in ("nag-cr-neg-min", "nag-cr-neg-max", "nag-cr-pos-min", "nag-cr-pos-max"):
            self.assertRegex(
                self.source,
                rf'id="{element_id}"[^>]*min="0\.50"[^>]*max="1\.80"[^>]*step="0\.01"',
            )

    def test_custom_strategy_uses_structured_draft_and_explicit_save_contract(self):
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn(
            "constnagCustomDefaults={preventiveNegative:[0.15,0.18],"
            "preventivePositive:[0.15,0.18],correctiveNegative:[1.50,1.80],"
            "correctivePositive:[1.50,1.80],activity:[0.8,1.4],"
            "release:[0.2,0.4],rest:[1.5,2.5],directionDeadband:0.05,"
            "dasFreshTimeoutMs:500};",
            compact,
        )
        self.assertIn("letnagCustomDraft=cloneNagCustomDefaults();", compact)
        self.assertIn("input.addEventListener('input',updateNagCustomDraft);", compact)
        self.assertIn("setNagCustomDirty(true);", compact)
        self.assertIn("newURLSearchParams()", compact)
        for field in EXPECTED_CONFIG_FIELDS:
            self.assertIn(f"params.set('{field}'", self.source)
        self.assertIn("normalizeNagCustomDraft", self.source)
        self.assertIn("applyNagCustomResponse(data)", self.source)
        self.assertIn("setNagCustomDirty(false)", self.source)
        self.assertIn("if(!response.ok||!data.ok)thrownewError", compact)
        self.assertIn("保存失败，未保存修改仍保留", self.source)

    def test_custom_strategy_readiness_mapping_is_fail_closed(self):
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn("state.nagMode!==5", compact)
        self.assertIn("'DISABLED'", self.source)
        self.assertIn("!d.nagDasFresh", compact)
        self.assertIn("'WAIT_DAS'", self.source)
        self.assertIn("phase==='fault-hold'", compact)
        self.assertIn("'FAIL_CLOSED'", self.source)
        self.assertIn("'READY'", self.source)
        self.assertIn("反馈失效，已停止自适应注入", self.source)

    def test_custom_strategy_blocks_unhydrated_defaults_and_exposes_retry(self):
        compact = re.sub(r"\s+", "", self.source)
        for state in (
            "nagCustomHydrated=false", "nagCustomLoading=false",
            "nagCustomLoadError=''",
        ):
            self.assertIn(state, compact)
        self.assertIn("setNagCustomControlsDisabled(true)", self.source)
        self.assertIn("正在读取设备策略", self.source)
        self.assertIn("读取设备策略失败，点击重试", self.source)
        self.assertIn("nagCustomHydrated=true", compact)
        self.assertIn("if(!nagCustomHydrated){loadNagAdaptive();return;}", compact)
        self.assertNotIn(
            '<div class="nag-action-state" id="nag-custom-dirty">已与设备同步</div>',
            self.source,
        )

    def test_custom_strategy_save_uses_revision_snapshot_and_locks_every_control(self):
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn("letnagCustomRevision=0", compact)
        self.assertIn("nagCustomRevision++", compact)
        self.assertIn("constsaveRevision=nagCustomRevision", compact)
        self.assertIn("constrequestDraft=normalizeNagCustomDraft", compact)
        self.assertIn("setNagCustomControlsDisabled(true)", self.source)
        self.assertIn("if(nagCustomRevision!==saveRevision)", compact)
        self.assertIn("setNagCustomControlsDisabled(false)", self.source)

    def test_custom_strategy_empty_numeric_input_keeps_previous_draft_value(self):
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn("event.target.value.trim()===''", compact)
        self.assertIn("event.target.valueAsNumber", self.source)
        self.assertIn("if(!Number.isFinite(value))return", compact)

    def test_custom_strategy_dirty_metadata_wins_over_polling(self):
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn(
            "if(!nagCustomDirty)mirrorDashboardText('s-inj','nag-card-meta'",
            compact,
        )

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

    def test_numeric_parsing_rejects_nonfinite_partial_and_out_of_range_values(self):
        for header in ("<cerrno>", "<cctype>", "<cmath>", "<cstdlib>", "<limits>"):
            self.assertIn(f"#include {header}", self.dashboard)
        for guard in (
            "std::strtod",
            "errno = 0",
            "errno == ERANGE",
            "std::isfinite(parsed)",
            "std::isspace(static_cast<unsigned char>(*end))",
            "*end == '\\0'",
            "std::numeric_limits<int16_t>::min()",
            "std::numeric_limits<int16_t>::max()",
            "std::numeric_limits<uint32_t>::max()",
        ):
            self.assertIn(guard, self.dashboard)
        self.assertNotIn("strtof(", self.dashboard)
        self.assertNotIn("strtol(", self.dashboard)

    def test_invalid_post_values_fall_back_without_resetting_controller(self):
        for parser in (
            "dashNagParseNmCenti",
            "dashNagParseSecondsMs",
            "dashNagParseMilliseconds",
        ):
            parser_start = self.dashboard.index(f"static ", self.dashboard.index(parser) - 20)
            parser_end = self.dashboard.index("\n}", parser_start)
            self.assertIn("return fallback;", self.dashboard[parser_start:parser_end])
        self.assertIn(
            "dashNagParseNmCenti(server.arg(name), adaptive.member)", self.dashboard
        )
        self.assertIn(
            "dashNagParseSecondsMs(server.arg(name), adaptive.member)", self.dashboard
        )
        self.assertIn(
            'server.arg("dasFreshTimeoutMs"), adaptive.dasFreshTimeoutMs',
            self.dashboard,
        )

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

    def test_duration_json_uses_lossless_millisecond_precision(self):
        self.assertIn(
            "return String(static_cast<double>(milliseconds) / 1000.0, 3);",
            self.dashboard,
        )
        for member in (
            "activityMinMs", "activityMaxMs", "releaseMinMs", "releaseMaxMs",
            "restMinMs", "restMaxMs",
        ):
            self.assertIn(f"dashNagSecondsString(config.{member})", self.dashboard)

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
