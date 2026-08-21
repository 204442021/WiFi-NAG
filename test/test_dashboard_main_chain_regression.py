from __future__ import annotations

import gzip
import re
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAIN_FILE = ROOT / "src" / "main.cpp"
BLE_FILE = ROOT / "include" / "ble" / "bridge_webui.h"
DASH_FILE = ROOT / "include" / "web" / "mcp2515_dashboard.h"
WRAPPER_FILE = ROOT / "include" / "web" / "mcp2515_dashboard_ui.h"
SOURCE_FILE = ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h"
BASE_FILE = ROOT / "include" / "web" / "mcp2515_dashboard_ui.base.h"
MINIFY_FILE = ROOT / "scripts" / "minify_dashboard.py"
MINIFY_REQUIREMENTS_FILE = ROOT / "scripts" / "requirements-dashboard.txt"
RUNTIME_HEADER_FILE = ROOT / "include" / "platform" / "espidf_runtime.h"
RUNTIME_SOURCE_FILE = ROOT / "src" / "espidf_runtime.cpp"

EXPECTED_CUSTOM_UI_IDS = (
    "nag-custom-readiness", "nag-custom-readiness-reason",
    "nag-custom-dirty", "nag-custom-defaults", "nag-custom-save",
    "nag-pv-neg-min", "nag-pv-neg-max", "nag-pv-pos-min", "nag-pv-pos-max",
    "nag-cr-neg-min", "nag-cr-neg-max", "nag-cr-pos-min", "nag-cr-pos-max",
    "nag-active-min", "nag-active-max",
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


def generated_dashboard_bytes(base_text: str) -> bytes:
    payload = re.search(
        r"static const uint8_t DASH_HTML_GZ\[\] PROGMEM = \{(?P<body>.*?)\};",
        base_text,
        re.DOTALL,
    )
    if payload is None:
        raise AssertionError("generated gzip payload not found")
    return bytes(
        int(value, 16)
        for value in re.findall(r"0x([0-9a-fA-F]{2})", payload.group("body"))
    )


def generated_dashboard_html(base_text: str) -> str:
    return gzip.decompress(generated_dashboard_bytes(base_text)).decode("utf-8")


def extract_element(html: str, element_id: str, tag: str = "section") -> str:
    start = re.search(
        rf'<{tag}\b[^>]*\bid=(?:"{re.escape(element_id)}"|\'{re.escape(element_id)}\'|{re.escape(element_id)}\b)[^>]*>',
        html,
        re.IGNORECASE,
    )
    if start is None:
        raise AssertionError(f"{tag} not found: {element_id}")
    depth = 0
    for match in re.finditer(rf"</?{tag}\b[^>]*>", html[start.start() :], re.IGNORECASE):
        depth += -1 if match.group(0).startswith("</") else 1
        if depth == 0:
            return html[start.start() : start.start() + match.end()]
    raise AssertionError(f"{tag} is not closed: {element_id}")


def extract_javascript_function(source: str, name: str) -> str:
    start = re.search(rf"(?:async\s+)?function\s+{re.escape(name)}\s*\([^)]*\)\s*\{{", source)
    if start is None:
        raise AssertionError(f"function not found: {name}")
    brace = source.find("{", start.start())
    depth = 0
    quote = ""
    escaped = False
    for index in range(brace, len(source)):
        char = source[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            continue
        if char in ("'", '"', "`"):
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start.start() : index + 1]
    raise AssertionError(f"function is not closed: {name}")


class DashboardMainChainRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.main = MAIN_FILE.read_text(encoding="utf-8")
        cls.ble = BLE_FILE.read_text(encoding="utf-8")
        cls.dash = DASH_FILE.read_text(encoding="utf-8")
        cls.wrapper = WRAPPER_FILE.read_text(encoding="utf-8-sig")
        cls.source = SOURCE_FILE.read_text(encoding="utf-8-sig")
        cls.base = BASE_FILE.read_text(encoding="utf-8-sig")
        cls.generated_html = generated_dashboard_html(cls.base)
        cls.minify = MINIFY_FILE.read_text(encoding="utf-8")
        cls.runtime_header = RUNTIME_HEADER_FILE.read_text(encoding="utf-8")
        cls.runtime_source = RUNTIME_SOURCE_FILE.read_text(encoding="utf-8")

    def assert_has_id(self, html: str, element_id: str) -> None:
        self.assertRegex(
            html,
            rf'\bid=(?:"{re.escape(element_id)}"|\'{re.escape(element_id)}\'|{re.escape(element_id)}\b)',
        )

    def test_root_route_has_one_owner_and_legacy_paths_redirect(self) -> None:
        root_route = 'server.on("/", HTTP_GET, handleRoot);'
        self.assertEqual(self.dash.count(root_route), 1)
        self.assertNotIn('server.on("/",', self.main)
        self.assertNotIn('server.on("/",', self.ble)
        self.assertIn("static void handleLegacyDashboardRedirect()", self.dash)
        self.assertIn('server.sendHeader("Location", "/");', self.dash)
        self.assertIn('server.sendHeader("Cache-Control", "no-store");', self.dash)
        self.assertIn('server.send(302, "text/plain", "Moved");', self.dash)
        self.assertIn('void sendHeader(const char *name, const char *value);', self.runtime_header)
        self.assertIn('code == 302 ? "302 Found"', self.runtime_source)
        self.assertIn('server.on("/dashboard", HTTP_GET, handleLegacyDashboardRedirect);', self.dash)
        self.assertIn(
            'server.on("/legacy-dashboard", HTTP_GET, handleLegacyDashboardRedirect);',
            self.dash,
        )

    def test_runtime_page_composition_is_removed(self) -> None:
        combined = "\n".join((self.main, self.ble, self.source, self.minify))
        for token in (
            "<iframe",
            "fetch('/dashboard')",
            'fetch("/dashboard")',
            "document.write(",
            "document.open(",
            "BLE_BRIDGE_SHELL",
            "hotspotTag",
            "NAG_SWEEP_UI_PATCH_GZ",
            "preparePatchedHtml",
            "patchedHtml_",
        ):
            with self.subTest(token=token):
                self.assertNotIn(token, combined)

    def test_generated_page_contains_every_core_region(self) -> None:
        core_ids = (
            "wifi-nag-header",
            "config-card",
            "config-hardware-section",
            *EXPECTED_CUSTOM_UI_IDS,
            "ble-card",
            "ble-bridge-section",
            "wifi-config-card",
            "wifi-hotspot-section",
            "wifi-internet-section",
            "gateway-section",
            "system-card",
            "status-panel",
            *EXPECTED_DIAGNOSTIC_UI_IDS,
            "nag-injected-meta",
            "nag-direction-meta",
            "debug-log-section",
            "firmware-update-card",
        )
        for label, html in (("source", self.source), ("generated", self.generated_html)):
            for element_id in core_ids:
                with self.subTest(file=label, element_id=element_id):
                    self.assert_has_id(html, element_id)

    def test_closed_loop_diagnostics_preserve_existing_groups_in_source_and_gzip(self) -> None:
        for label, html in (("source", self.source), ("generated", self.generated_html)):
            for element_id in EXISTING_DIAGNOSTIC_IDS:
                with self.subTest(file=label, element_id=element_id):
                    self.assert_has_id(html, element_id)
            health_match = re.search(r'\bid=(?:"nag-diag-health"|nag-diag-health\b)', html)
            advanced_match = re.search(r'\bid=(?:"advanced-diagnostics"|advanced-diagnostics\b)', html)
            self.assertIsNotNone(health_match)
            self.assertIsNotNone(advanced_match)
            health_index = health_match.start()
            advanced_index = advanced_match.start()
            self.assertLess(health_index, advanced_index)
            for heading in ("原车输入", "控制器决策", "本地发送", "DAS 响应"):
                self.assertIn(heading, html[health_index:advanced_index])
            self.assertIn("未见 · -- 帧 / --", html[health_index:advanced_index])
            compact = re.sub(r"\s+", "", html)
            for contract in (
                "letnagDiagnosticsEpoch=0",
                "letnagDiagnosticsLoadingEpoch=-1",
                "functionnagDiagnosticFinite(value)",
                "functionnagDiagnosticCollisionGap(value,collisions)",
                "arming:'active'",
                "'wait-das':'muted'",
            ):
                with self.subTest(file=label, contract=contract):
                    self.assertIn(contract, compact)

        for label, html in (("source", self.source), ("generated", self.generated_html)):
            for text in (
                "预防层", "纠正层", "注入与停发间隔", "安全边界",
                "间隔期不额外发送 0x370", "本地发送成功不等于 DAS 接受",
            ):
                with self.subTest(file=label, text=text):
                    self.assertIn(text, html)
            for retired in (
                "反方向持续注入", "Hands-On 1 扭矩范围", "Hands-On 2 扭矩范围",
            ):
                with self.subTest(file=label, retired=retired):
                    self.assertNotIn(retired, html)

    def test_custom_strategy_layout_and_navigation_ownership_are_preserved(self) -> None:
        config = extract_element(self.source, "config-card")
        for element_id in EXPECTED_CUSTOM_UI_IDS:
            self.assert_has_id(config, element_id)
        self.assertIn("@media(min-width:900px)", self.source)
        self.assertIn(".nag-custom-grid", self.source)
        self.assertIn(".nag-rhythm-grid", self.source)
        self.assertIn("grid-template-columns:repeat(2,minmax(0,1fr))", self.source)
        self.assertIn("grid-template-columns:repeat(3,minmax(0,1fr))", self.source)
        self.assertIn(".nag-custom-actionbar", self.source)
        self.assertIn("setWifiNagPage", self.source)
        self.assertIn(".bottom-nav", self.source)

    def test_custom_strategy_sticky_and_accessible_status_contract(self) -> None:
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn("body.ui-shell#config-card{overflow:visible}", compact)
        self.assertNotIn(".ui-main-card{overflow:visible}", compact)
        for element_id in (
            "nag-custom-readiness", "nag-custom-readiness-reason",
            "nag-custom-dirty", "nag-adaptive-msg",
        ):
            element = re.search(
                rf'<[^>]+\bid="{re.escape(element_id)}"[^>]*>', self.source
            )
            self.assertIsNotNone(element, element_id)
            markup = element.group(0)
            self.assertIn('role="status"', markup)
            self.assertIn('aria-live="polite"', markup)
            self.assertIn('aria-atomic="true"', markup)

    def test_custom_strategy_loading_and_save_disable_all_controls(self) -> None:
        compact = re.sub(r"\s+", "", self.source)
        self.assertIn("functionsetNagCustomControlsDisabled(disabled)", compact)
        self.assertIn("input.disabled=disabled", compact)
        self.assertIn("defaults.disabled=disabled", compact)
        self.assertIn("save.disabled=disabled", compact)
        self.assertIn("nagCustomSaving=true", compact)
        self.assertIn("nagCustomSaving=false", compact)

        retired_shift_ids = (
            "shift-manual-btn",
            "shift-manual-ready",
            "shift-release",
            "shift-source",
            "shift-latch",
            "shift-action-msg",
        )
        for label, html in (("source", self.source), ("generated", self.generated_html)):
            for element_id in retired_shift_ids:
                with self.subTest(file=label, retired_element_id=element_id):
                    self.assertNotRegex(
                        html,
                        rf'\bid=(?:"{re.escape(element_id)}"|\'{re.escape(element_id)}\'|{re.escape(element_id)}\b)',
                    )

    def test_static_page_regions_have_correct_owners(self) -> None:
        config = extract_element(self.source, "config-card")
        ble = extract_element(self.source, "ble-card")
        wifi = extract_element(self.source, "wifi-config-card")
        system = extract_element(self.source, "system-card")
        for element_id in ("config-hardware-section",):
            self.assert_has_id(config, element_id)
        self.assertNotIn('id="ble-bridge-section"', config)
        self.assert_has_id(ble, "ble-bridge-section")
        for element_id in ("wifi-hotspot-section", "wifi-internet-section", "gateway-section"):
            self.assert_has_id(wifi, element_id)
        for element_id in ("status-panel", "debug-log-section"):
            self.assert_has_id(system, element_id)

    def test_retired_nag_send_interval_has_no_ui_or_api(self) -> None:
        combined = "\n".join((self.source, self.generated_html, self.wrapper, self.dash))
        for token in (
            "nag-sweep-row",
            "nag-sweep-min",
            "nag-sweep-max",
            "nag-sweep-save",
            "/api/nag-sweep",
            "NagSweepWebServer",
            "initNagSweepUi",
        ):
            self.assertNotIn(token, combined)

    def test_ble_is_static_and_backend_is_api_only(self) -> None:
        ble_ids = (
            "ble-card",
            "ble-bridge-section",
            "ble-card-meta",
            "ble-enabled",
            "ble-obstacle",
            "ble-pair-btn",
            "ble-unbind-btn",
            "ble-action-msg",
            "ble-device-state",
            "ble-protocol",
            "ble-peer-id",
            "ble-radio",
            "ble-nag-config",
            "ble-nag-runtime",
            "ble-nag-sync",
            "ble-nag-revision",
            "ble-255",
            "ble-12b",
            "ble-summary",
            "ble-fsd-rx",
            "ble-counters",
        )
        for element_id in ble_ids:
            self.assert_has_id(self.source, element_id)
        routes = {
            "/ble_status": "HTTP_GET",
            "/ble_config": "HTTP_POST",
            "/ble_pair": "HTTP_POST",
            "/ble_unbind": "HTTP_POST",
            "/config": "HTTP_POST",
            "/disable": "HTTP_POST",
        }
        for route, method in routes.items():
            self.assertRegex(self.ble, rf'server\.on\("{re.escape(route)}",\s*{method},')
        for token in ("BLE_BRIDGE_SHELL", "BLE_BRIDGE_UI_JS", '"/dashboard"', '"/ble_ui.js"'):
            self.assertNotIn(token, self.ble)
        load = extract_javascript_function(self.source, "bleLoadStatus")
        self.assertIn("catch", load)
        self.assertIn("finally", load)
        self.assertIn("bleStatusLoading", load)
        self.assertIn("AbortController", load)
        self.assertIn("ble-card-meta", load)
        self.assertNotIn("document.write", load)
        self.assertNotIn("document.body", load)
        self.assertNotIn("location.reload", load)
        self.assertNotIn("bleManualShift", self.source)
        self.assertNotIn("/ble_shift_manual", self.ble)

    def test_static_main_cards_use_native_accordion(self) -> None:
        self.assertIn('<body class="wifi-nag ui-phone ui-shell">', self.source)
        self.assertEqual(self.source.count("card ui-main-card collapsed"), 5)
        order = [
            self.source.index('id="config-card"'),
            self.source.index('id="ble-card"'),
            self.source.index('id="wifi-config-card"'),
        ]
        self.assertEqual(order, sorted(order))
        for function in ("setMainCardExpanded", "initWifiNagAccordion"):
            self.assertIn(f"function {function}(", self.source)
        self.assertIn("wifiNagAccordionTouched", self.source)
        self.assertNotIn('class="car-side"', self.source)
        self.assertNotIn('id="ui-mode-strip"', self.source)
        initializer = (
            "initWifiNagAccordion();initBleBridgeUi();"
            "initSystemMonitor();syncDashboardSummary();loadFirmwareInfo();loadGatewayDnsCached();"
            "loadGatewayDns(true);loadGatewayStatus();poll();"
        )
        self.assertIn(initializer, re.sub(r"\s+", "", self.source))

    def test_obstacle_shift_card_is_not_rendered(self) -> None:
        for label, html in (("source", self.source), ("generated", self.generated_html)):
            with self.subTest(file=label):
                self.assertNotRegex(
                    html,
                    r'<section\b[^>]*\bid=(?:"obstacle-shift-card"|\'obstacle-shift-card\'|obstacle-shift-card\b)',
                )

    def test_generated_wrapper_and_payload_are_current(self) -> None:
        base_include = '#include "web/mcp2515_dashboard_ui.base.h"'
        self.assertEqual(self.wrapper.count(base_include), 1)
        self.assertNotIn("nag_sweep_dashboard.h", self.wrapper)
        self.assertIn("--check", self.minify)
        self.assertIn("generated payload is stale", self.minify)
        self.assertEqual(generated_dashboard_bytes(self.base)[9], 0xFF)
        self.assertIn("compressed[9] = 0xFF", self.minify)

        requirements = MINIFY_REQUIREMENTS_FILE.read_text(encoding="utf-8").splitlines()
        self.assertEqual(
            requirements,
            ["csscompressor==0.9.5", "htmlmin==0.1.12", "rjsmin==1.2.5"],
        )

        before = BASE_FILE.read_bytes()
        before_mtime = BASE_FILE.stat().st_mtime_ns
        result = subprocess.run(
            [sys.executable, str(MINIFY_FILE), "--check"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(BASE_FILE.read_bytes(), before)
        self.assertEqual(BASE_FILE.stat().st_mtime_ns, before_mtime)


if __name__ == "__main__":
    unittest.main()
