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

EXPECTED_DIAGNOSTIC_UI_IDS = (
    "nag-diag-health", "nag-diag-reason",
    "nag-diag-epas", "nag-diag-oem-torque", "nag-diag-counter",
    "nag-diag-das", "nag-diag-hos",
    "nag-diag-phase", "nag-diag-target", "nag-diag-direction",
    "nag-diag-timer", "nag-diag-burst",
    "nag-diag-tx", "nag-diag-last-tx", "nag-diag-collision",
    "nag-diag-ack", "nag-diag-latency", "nag-diag-timeout",
    "nag-diag-escalations", "nag-diag-events", "nag-diag-clear-events",
)

EXISTING_DIAGNOSTIC_IDS = (
    "s-fps", "s-rx", "s-tx", "s-txerr", "s-up", "btn-can-toggle",
    "ble-protocol", "ble-peer-id", "ble-radio", "ble-nag-config",
    "ble-nag-runtime", "ble-nag-sync", "ble-nag-revision", "ble-255",
    "ble-12b", "ble-summary", "ble-fsd-rx", "ble-counters",
    "wifi-diag-detail", "ap-diag-detail", "net-perf-status",
    "gw-diag-ap", "gw-diag-sta", "gw-diag-nat", "gw-diag-radio",
    "gw-diag-dns", "gw-diag-slow", "gw-diag-pending",
    "gw-diag-upstream", "gw-diag-clients", "sys-chip", "sys-cpu",
    "sys-clocks", "sys-board", "sys-reset", "sys-runtime", "sys-tasks",
    "sys-heap", "sys-internal", "sys-largest", "sys-minheap", "sys-psram",
    "sys-flash", "sys-spiffs", "sys-rssi", "sys-wifi-mode", "sys-apclients",
    "sys-ble", "sys-wireless", "sys-fw", "debug-log-section", "log",
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
    "nag_das_ms": "750",
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
        cls.exchange = (ROOT / "include/nag_adaptive_exchange.h").read_text(
            encoding="utf-8"
        )
        cls.config_input = (ROOT / "include/nag_adaptive_config_input.h").read_text(
            encoding="utf-8"
        )
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
            "dasFreshTimeoutMs:750};",
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
        self.assertIn("'已关闭'", self.source)
        self.assertIn("!d.nagDasFresh", compact)
        self.assertIn("'等待 DAS'", self.source)
        self.assertIn("phase==='fault-hold'", compact)
        self.assertIn("'保护停发'", self.source)
        self.assertIn("'就绪'", self.source)
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

    def test_diagnostic_information_architecture_is_visible_before_advanced_details(self):
        for element_id in EXPECTED_DIAGNOSTIC_UI_IDS + EXISTING_DIAGNOSTIC_IDS:
            self.assertRegex(self.source, rf'\bid="{re.escape(element_id)}"')

        health_index = self.source.index('id="nag-diag-health"')
        advanced_index = self.source.index('id="advanced-diagnostics"')
        self.assertLess(health_index, advanced_index)
        advanced_end = self.source.index("</details>", advanced_index)
        self.assertNotIn("nag-diag-health", self.source[advanced_index:advanced_end])
        for heading in ("原车输入", "控制器决策", "本地发送", "DAS 响应"):
            self.assertIn(heading, self.source[health_index:advanced_index])
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn(
            "constdefaultExpanded=card.id==='config-card'||card.id==='system-card'",
            compact,
        )

    def test_diagnostic_phase_names_and_color_semantics_are_exact(self):
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn(
            "constphaseNames={disabled:'关闭','wait-das':'等待DAS',"
            "arming:'确认OEM帧',maintenance:'预防扫动',release:'平滑释放',"
            "rest:'无发送休息',corrective:'纠正脉冲',verify:'等待DAS确认',"
            "'fault-hold':'保护停发'};",
            compact,
        )
        for selector in (
            ".nag-tone-fresh", ".nag-tone-ready", ".nag-tone-ack",
            ".nag-tone-caution", ".nag-tone-error", ".nag-tone-muted",
        ):
            self.assertIn(selector, self.source)
        self.assertIn("release: 'caution'", self.source)
        self.assertIn("rest: 'caution'", self.source)
        self.assertIn("verify: 'caution'", self.source)
        self.assertIn("collision: 'caution'", self.source)
        self.assertIn("sendFailure: 'error'", self.source)
        self.assertNotIn("sendSuccess: 'ready'", self.source)
        self.assertIn("const nagDiagnosticPhaseTones={", self.source)
        for mapping in (
            "disabled: 'muted'", "'wait-das': 'muted'", "arming: 'active'",
            "maintenance: 'active'", "release: 'caution'", "rest: 'caution'",
            "corrective: 'active'", "verify: 'caution'", "'fault-hold': 'error'",
        ):
            self.assertIn(mapping, self.source)
        self.assertNotIn("arming: 'ready'", self.source)
        self.assertIn("dasFresh?'fresh':(dasSeen?'error':'muted')", compact)
        self.assertIn("hos>=6?'error':(hos>=3?'caution':'active')", compact)

    def test_browser_event_timeline_is_deduplicated_capped_and_local_only(self):
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn("letnagDiagnosticPrevious=null", compact)
        self.assertIn("letnagDiagnosticEvents=[]", compact)
        for field in (
            "nagDasFresh", "nagDasHos", "nagAdaptivePhase",
            "nagAcknowledgementCount", "nagAcknowledgementTimeouts",
            "nagCounterCollisions",
        ):
            self.assertIn(f"'{field}'", self.source)
        self.assertIn("if(previous===next)return", compact)
        self.assertIn("nagDiagnosticEvents.unshift", self.source)
        self.assertIn("if(nagDiagnosticEvents.length>20)nagDiagnosticEvents.length=20", compact)
        self.assertIn("new Date().toLocaleTimeString()", self.source)
        self.assertIn("oldValue+' → '+newValue", self.source)
        clear_start = self.source.index("function clearNagDiagnosticEvents(")
        clear_end = self.source.index("\n}", clear_start)
        clear_body = self.source[clear_start:clear_end]
        self.assertIn("nagDiagnosticEvents=[]", clear_body)
        self.assertNotIn("fetch(", clear_body)

    def test_diagnostic_polling_is_page_monitor_visibility_scoped_and_isolated(self):
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn("letnagDiagnosticsTimer=null", compact)
        self.assertIn("document.querySelector('.ui-screen[data-page=\"diagnostics\"].active')", self.source)
        self.assertIn("systemStatusEnabled", self.source)
        self.assertIn("!document.hidden", self.source)
        self.assertIn("isCarUiActive()||networkPerformanceMode?1000:500", compact)
        self.assertIn("clearInterval(nagDiagnosticsTimer)", self.source)
        poll_start = self.source.index("async function pollNagDiagnostics(")
        poll_end = self.source.index("\n}", poll_start)
        poll_body = self.source[poll_start:poll_end]
        self.assertEqual(poll_body.count("fetch("), 1)
        self.assertIn("fetch('/api/nag-adaptive')", poll_body)
        self.assertIn("updateNagDiagnostics(data)", poll_body)
        for forbidden in (
            "poll()", "loadWifi", "loadAp", "loadGateway", "loadSystemStatus",
            "updateNagControl", "applyNagCustomResponse",
        ):
            self.assertNotIn(forbidden, poll_body)
        for lifecycle in (
            "setWifiNagPage", "setUiMode", "setNetworkPerformanceMode",
            "startSystemMonitor", "stopSystemMonitor", "visibilitychange",
            "stopDashboardPolling",
        ):
            self.assertIn(lifecycle, self.source)
        self.assertGreaterEqual(self.source.count("syncNagDiagnosticsPolling()"), 7)

    def test_diagnostic_polling_epochs_isolate_stale_fetches_and_loading_state(self):
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn("letnagDiagnosticsEpoch=0", compact)
        self.assertIn("letnagDiagnosticsLoadingEpoch=-1", compact)
        self.assertNotIn("letnagDiagnosticsLoading=false", compact)
        stop_start = self.source.index("function stopNagDiagnosticsPolling(")
        stop_end = self.source.index("\n}", stop_start)
        stop_body = re.sub(r"\s+", "", self.source[stop_start:stop_end])
        self.assertIn("nagDiagnosticsEpoch++", stop_body)
        self.assertIn("nagDiagnosticsLoadingEpoch=-1", stop_body)
        poll_start = self.source.index("async function pollNagDiagnostics(epoch)")
        poll_end = self.source.index("\n}", poll_start)
        poll_body = re.sub(r"\s+", "", self.source[poll_start:poll_end])
        self.assertGreaterEqual(poll_body.count("epoch!==nagDiagnosticsEpoch"), 2)
        self.assertIn("nagDiagnosticsLoadingEpoch===epoch", poll_body)
        self.assertIn("nagDiagnosticsLoadingEpoch=epoch", poll_body)
        self.assertIn(
            "if(nagDiagnosticsLoadingEpoch===epoch)nagDiagnosticsLoadingEpoch=-1",
            poll_body,
        )
        self.assertIn(
            "if(epoch!==nagDiagnosticsEpoch||!nagDiagnosticsShouldPoll())return",
            poll_body,
        )
        self.assertIn("constepoch=nagDiagnosticsEpoch", compact)
        self.assertIn("pollNagDiagnostics(epoch)", self.source)
        self.assertIn("setInterval(()=>pollNagDiagnostics(epoch),intervalMs)", compact)

    def test_diagnostic_format_helpers_reject_missing_invalid_and_sentinel_values(self):
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn("functionnagDiagnosticFinite(value)", compact)
        self.assertIn(
            "if(value===undefined||value===null||value==='')returnnull",
            compact,
        )
        self.assertIn("returnNumber.isFinite(number)?number:null", compact)
        self.assertIn("functionnagDiagnosticInteger(value)", compact)
        self.assertIn("functionnagDiagnosticFixed(value,digits,suffix)", compact)
        self.assertIn("functionnagDiagnosticAge(value)", compact)
        self.assertIn("if(age===null||age<0||age>=0xFFFFFFFF)return'--'", compact)
        self.assertIn("functionnagDiagnosticCollisionGap(value,collisions)", compact)
        self.assertIn(
            "if(count===null||count<=0||gap===null||gap<=0||gap>=0xFFFFFFFF)return'--'",
            compact,
        )
        self.assertNotIn("Math.max(0,Math.trunc(age))", self.source)
        self.assertIn("nagDiagnosticCollisionGap(d.nagLastCounterCollisionGapUs,collisions)", self.source)

    def test_das_freshness_is_visible_text_not_color_only(self):
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn('id="nag-diag-das">未见 · -- 帧 / --</strong>', self.source)
        self.assertIn("constfreshness=dasFresh?'新鲜':(dasSeen?'已超时':'未见')", compact)
        self.assertIn("freshness+' · '+dasFrameText+' / '+nagDiagnosticAge(d.nagDasAgeMs)", self.source)
        self.assertIn("固件过滤器已放行，请确认当前 CAN 总线是否存在该报文", self.source)

    def test_closed_loop_diagnostic_labels_are_chinese(self):
        start = self.source.index('class="nag-diag-panel"')
        end = self.source.index('id="advanced-diagnostics"', start)
        panel = self.source[start:end]
        for label in (
            "NAG 自适应闭环", "原车 EPAS 0x370", "原车扭矩", "原车计数器",
            "手握状态", "控制阶段", "目标扭矩", "方向来源", "阶段剩余时间",
            "纠偏尝试 / 连发进度", "尝试 / 成功 / 失败", "DAS 确认次数",
        ):
            self.assertIn(label, panel)
        for english in (
            "NAG CLOSED LOOP", "OEM torque", "OEM counter", "Hands-On state",
            "Target torque", "Direction source", "Phase remaining", "Acknowledgements",
        ):
            self.assertNotIn(english, panel)

    def test_diagnostic_last_tx_preserves_status_age_and_derives_send_success(self):
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn("d.nagInjectedAgeMs", self.source)
        self.assertIn(
            "if(d.nagInjectedTorqueValid!==undefined||d.nagInjectedTorqueNm!==undefined)",
            compact,
        )
        self.assertIn(
            "Math.max(0,Math.trunc(sends)-Math.trunc(failures))",
            compact,
        )
        self.assertNotIn(
            "success=Math.trunc(nagDiagnosticNumber(d,'nagEcho',0))",
            compact,
        )
        self.assertIn("nagLastCounterCollisionGapUs", self.source)
        self.assertIn("+' μs'", self.source)

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

    def test_post_validates_temporary_request_before_atomic_publish(self):
        parse_index = self.dashboard.find(
            "static bool dashParseNagConfigRequest(DashNagConfigRequest &request,"
        )
        self.assertGreaterEqual(parse_index, 0, "V5 POST parsing is missing")
        validate_index = self.dashboard.index(
            "NagAdaptiveConfigInput::validate(request.config)", parse_index
        )
        compare_index = self.dashboard.index(
            "dashNagAdaptiveConfigEqual(previous.config, request.config)", validate_index
        )
        publish_index = self.dashboard.index(
            "nag->publishAdaptiveCommand(request.config, request.mode, true)", compare_index
        )
        self.assertLess(parse_index, validate_index)
        self.assertLess(validate_index, compare_index)
        self.assertLess(compare_index, publish_index)
        self.assertNotIn("normalizeConfig(request.config)", self.dashboard[parse_index:publish_index])

    def test_numeric_parsing_rejects_nonfinite_partial_and_out_of_range_values(self):
        for header in ("<cerrno>", "<cctype>", "<cmath>", "<cstdlib>", "<limits>"):
            self.assertIn(f"#include {header}", self.config_input)
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
            self.assertIn(guard, self.config_input)
        self.assertNotIn("strtof(", self.config_input)
        self.assertNotIn("strtol(", self.config_input)

    def test_invalid_post_returns_field_error_without_publish_or_persist(self):
        self.assertIn('server.send(400, "application/json", dashNagConfigErrorJson(error))', self.dashboard)
        self.assertIn('\"field\":\"', self.dashboard)
        self.assertIn('\"errors\":{\"', self.dashboard)
        for handler in ("handleNagApiUpdate", "handleNagAdaptiveApiPost"):
            start = self.dashboard.index(f"static void {handler}()")
            end = self.dashboard.index("\n}", start)
            body = self.dashboard[start:end]
            reject = body.index(
                "if (NagAdaptiveConfigInput::shouldRejectRequest(adaptiveResult))"
            )
            save = body.index("dashSavePrefs()")
            self.assertLess(reject, save)
            self.assertIn("return;", body[reject:save])
        apply_start = self.dashboard.index(
            "static NagAdaptiveConfigInput::ApplyResult dashApplyNagConfigArgs(\n"
            "    DashNagConfigError &error)\n{"
        )
        apply_end = self.dashboard.index("\n}\n#endif", apply_start)
        apply_body = self.dashboard[apply_start:apply_end]
        self.assertNotIn("fallback", apply_body)
        self.assertNotIn("normalizeConfig", apply_body)

    def test_web_uses_pending_command_and_published_snapshot_only(self):
        self.assertNotIn("adaptiveController.", self.dashboard)
        self.assertIn("nag->adaptiveSnapshot()", self.dashboard)
        self.assertIn("nag->publishAdaptiveCommand", self.dashboard)
        self.assertIn("nag->requestedMode()", self.dashboard)
        self.assertIn("consumePendingAdaptiveCommandAtFrameBoundary", self.handler)
        self.assertIn("publishAdaptiveSnapshot", self.handler)
        self.assertIn("NagAdaptivePendingCommand", self.exchange)
        self.assertIn("std::mutex", self.exchange)
        self.assertIn("portMUX_TYPE", self.exchange)
        self.assertNotIn("driver.send", self.exchange)

    def test_zero_ack_latency_is_serialized_and_rendered_as_unknown(self):
        self.assertIn(
            'snapshot.acknowledgementCount == 0 ? "null"', self.dashboard
        )
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn(
            "constlatency=acknowledgements===null||acknowledgements===0?"
            "'--/--':nagDiagnosticAge(d.nagLastAcknowledgementLatencyMs)+"
            "'/'+nagDiagnosticAge(d.nagMaxAcknowledgementLatencyMs)",
            compact,
        )

    def test_unused_dash_nag_echo_helper_is_removed(self):
        self.assertNotIn("dashNagEchoCount", self.dashboard)

    def test_config_equality_covers_every_closed_loop_member(self):
        equality_start = self.dashboard.index("dashNagAdaptiveConfigEqual")
        equality_end = self.dashboard.index(
            "static NagAdaptiveConfigInput::ApplyResult dashApplyNagConfigArgs",
            equality_start,
        )
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
