import re
import unittest
from html.parser import HTMLParser
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UI_SOURCE_FILE = ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h"
VOID_TAGS = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"}


class Node:
    def __init__(self, tag="root", attrs=None, parent=None):
        self.tag = tag
        self.attrs = dict(attrs or [])
        self.parent = parent
        self.children = []
        self.text_parts = []

    @property
    def text(self):
        return " ".join(" ".join(self.text_parts).split())

    def walk(self):
        yield self
        for child in self.children:
            yield from child.walk()

    def has_class(self, name):
        return name in self.attrs.get("class", "").split()

    def ancestor(self, *, node_id=None, data_page=None):
        current = self.parent
        while current:
            if node_id and current.attrs.get("id") == node_id:
                return current
            if data_page and current.attrs.get("data-page") == data_page:
                return current
            current = current.parent
        return None


class TreeParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.root = Node()
        self.current = self.root

    def handle_starttag(self, tag, attrs):
        node = Node(tag, attrs, self.current)
        self.current.children.append(node)
        if tag not in VOID_TAGS:
            self.current = node

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VOID_TAGS:
            self.current = self.current.parent

    def handle_endtag(self, tag):
        current = self.current
        while current is not self.root:
            if current.tag == tag:
                self.current = current.parent
                return
            current = current.parent

    def handle_data(self, data):
        if data.strip():
            current = self.current
            while current:
                current.text_parts.append(data)
                current = current.parent


class V109UiRedesignContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = UI_SOURCE_FILE.read_text(encoding="utf-8-sig")
        payload = re.search(r'R"HTML\((.*)\)HTML";', source, re.DOTALL)
        if payload is None:
            raise AssertionError("embedded dashboard HTML not found")
        cls.source = source
        parser = TreeParser()
        parser.feed(payload.group(1))
        cls.nodes = list(parser.root.walk())
        cls.by_id = {}
        for node in cls.nodes:
            node_id = node.attrs.get("id")
            if node_id:
                cls.by_id.setdefault(node_id, []).append(node)

    def one(self, node_id):
        matches = self.by_id.get(node_id, [])
        self.assertEqual(len(matches), 1, f"{node_id} must exist exactly once")
        return matches[0]

    def test_ids_are_unique_and_obstacle_shift_ui_is_absent(self):
        duplicates = sorted(node_id for node_id, nodes in self.by_id.items() if len(nodes) > 1)
        self.assertEqual(duplicates, [])
        self.assertNotIn("obstacle-shift-card", self.by_id)
        self.assertNotIn("shift-enabled", self.by_id)

    def test_brand_and_initial_page_copy_match_confirmed_ui(self):
        self.assertEqual(self.one("brand-title").text, "Albert FSD辅助系统")
        self.assertEqual(self.one("page-title").text, "功能")
        self.assertEqual(self.one("page-copy").text, "方向盘提醒与工作模式设置")

    def test_bottom_navigation_and_screen_structure_are_exact(self):
        nav = self.one("bottom-nav")
        buttons = [n for n in nav.walk() if n.tag == "button" and n.attrs.get("data-page-target")]
        self.assertEqual(
            [(n.attrs["data-page-target"], n.text) for n in buttons],
            [("features", "功能"), ("ble", "蓝牙"), ("network", "网络"), ("diagnostics", "诊断"), ("settings", "设置")],
        )
        screens = [n for n in self.nodes if n.attrs.get("data-page")]
        self.assertEqual([n.attrs["data-page"] for n in screens], ["features", "ble", "network", "diagnostics", "settings"])
        active = [n.attrs["data-page"] for n in screens if n.has_class("active")]
        self.assertEqual(active, ["features"])

    def test_real_cards_are_grouped_into_the_confirmed_pages(self):
        expected = {
            "config-card": "features",
            "ble-card": "ble",
            "wifi-config-card": "network",
            "system-card": "diagnostics",
            "firmware-update-card": "settings",
            "device-actions-card": "settings",
        }
        for card_id, page in expected.items():
            with self.subTest(card=card_id):
                card = self.one(card_id)
                self.assertTrue(card.has_class("ui-main-card"))
                self.assertEqual(card.has_class("collapsed"), card_id != "config-card")
                self.assertIsNotNone(card.ancestor(data_page=page))

    def test_advanced_diagnostics_is_closed_and_owns_non_nag_engineering_data(self):
        advanced = self.one("advanced-diagnostics")
        self.assertEqual(advanced.tag, "details")
        self.assertNotIn("open", advanced.attrs)
        self.assertIsNotNone(advanced.ancestor(node_id="system-card"))
        engineering_ids = {
            "s-fps", "s-rx", "s-tx", "s-txerr", "ble-protocol", "ble-peer-id",
            "ble-255", "ble-12b", "ble-counters", "gw-diag", "net-perf-status",
            "sys-chip", "sys-heap", "log",
        }
        advanced_ids = {n.attrs.get("id") for n in advanced.walk()}
        self.assertTrue(engineering_ids.issubset(advanced_ids))

        for page in ("features", "ble", "network", "settings"):
            screen = next(n for n in self.nodes if n.attrs.get("data-page") == page)
            screen_ids = {n.attrs.get("id") for n in screen.walk()}
            self.assertTrue(engineering_ids.isdisjoint(screen_ids), page)

    def test_real_controls_keep_their_existing_handlers(self):
        expected = {
            "can-write-tgl": ("onchange", "saveCanWrite()"),
            "ble-enabled": ("onchange", "bleSaveConfig()"),
            "ap-ssid": ("placeholder", "热点名称"),
            "wifi-save-btn": ("onclick", "saveWifi()"),
            "gw-enabled": ("onchange", "saveGatewayDns()"),
            "sys-monitor-tgl": ("onchange", "toggleSystemMonitor()"),
            "ota-upload-btn": ("onclick", "uploadFirmware()"),
        }
        for node_id, (attr, value) in expected.items():
            with self.subTest(node_id=node_id):
                self.assertEqual(self.one(node_id).attrs.get(attr), value)

    def test_fresh_ui_matches_v109_default_on_and_premium_dark_theme(self):
        self.assertIn("checked", self.one("can-write-tgl").attrs)
        self.assertIn(
            "const t=mode==='manual'?(localStorage.getItem('theme')||'dark'):'dark';",
            self.source,
        )

    def test_daily_network_status_is_simple_and_full_detail_is_diagnostic_only(self):
        wifi_status = self.source.split("async function loadWifiStatus(){", 1)[1].split(
            "async function connectWifiSlot", 1
        )[0]
        self.assertNotIn("switch to that WiFi and open this IP", wifi_status)
        self.assertIn("$('wifi-diag-detail').textContent=detail;", wifi_status)
        self.assertIn("$('ap-diag-detail').textContent=detail;", self.source)
        self.assertIn("setText('wifi-status','Connected: '+(d.ssid||''));", wifi_status)

    def test_new_navigation_copy_can_round_trip_between_chinese_and_english(self):
        self.assertIn(
            "Object.entries(I18N_EN).forEach(([zh,en])=>{if(!I18N_ZH[en])I18N_ZH[en]=zh;});",
            self.source,
        )

    def test_help_popovers_have_the_handlers_used_by_existing_controls(self):
        self.assertIn("function closeHelpPanels(root){", self.source)
        self.assertIn("function toggleHelp(trigger,event){", self.source)


if __name__ == "__main__":
    unittest.main()
