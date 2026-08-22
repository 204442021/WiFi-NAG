#!/usr/bin/env python3
"""Build or verify the single embedded WiFi-NAG dashboard payload."""

from __future__ import annotations

import argparse
import gzip
import re
import sys
from pathlib import Path

import csscompressor
import htmlmin
import rjsmin


ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "include" / "web" / "mcp2515_dashboard_ui.src.h"
DST = ROOT / "include" / "web" / "mcp2515_dashboard_ui.base.h"


def _sub_once(text: str, pattern: str, replacement: str, label: str) -> str:
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.DOTALL)
    if count != 1:
        raise ValueError(f"V4.4 dashboard override failed: {label}")
    return updated


def apply_v44_overrides(source_text: str) -> str:
    """Apply V4.4 policy/UI changes while preserving the V4.3 transport API.

    The backend NVS/API field names stay backward compatible for OTA safety.
    The V4.4 UI presents one corrective range and uses the retired deadband
    transport slot as the maintenance-layer on/off value (0=off, 0.05=on).
    The controller no longer applies any direction deadband.
    """
    source_text = source_text.replace("V4.3 V13", "V4.4 V13")

    source_text = _sub_once(
        source_text,
        r'(<div class="nag-strategy-kicker">低风险维持</div><div class="nag-strategy-title">预防层</div><div class="nag-strategy-copy">用于维持 H0～H2 正常区间，H2 为当前系统常态。</div></div></div>)<div class="nag-field-grid">',
        r'''\1<div class="setting-row" id="nag-maintenance-row"><div class="setting-info"><div class="setting-name">维持层</div><div class="setting-desc">默认开启。关闭后 H0～H2 仅监测、不发送维持扭矩；H3～H5 纠正层仍独立工作。</div></div><label class="tgl"><input type="checkbox" id="nag-maintenance-enabled" checked><div class="tgl-track"><div class="tgl-thumb"></div></div></label></div><div class="nag-field-grid">''',
        "maintenance toggle",
    )

    source_text = _sub_once(
        source_text,
        r'<section class="nag-strategy-card"><div class="nag-strategy-head"><div><div class="nag-strategy-kicker">连续纠偏</div><div class="nag-strategy-title">纠正层</div><div class="nag-strategy-copy">仅在 H3～H5 使用；每个有效原车帧连续注入，直到回到 H0～H2。</div></div></div><div class="nag-field-grid">.*?</div></section>',
        '''<section class="nag-strategy-card"><div class="nag-strategy-head"><div><div class="nag-strategy-kicker">连续纠偏</div><div class="nag-strategy-title">纠正层</div><div class="nag-strategy-copy">仅在 H3～H5 使用；不区分正负参数，每个有效原车帧按实时扭矩反方向立即注入，直到回到 H0～H2。</div></div></div><div class="nag-field-grid"><div class="nag-field-group"><div class="nag-field-label">纠正扭矩 / Nm</div><div class="nag-field-pair"><label class="nag-adaptive-field"><span>最小</span><input class="sniff-input" id="nag-cr-min" type="number" min="1.80" max="2.00" step="0.01" value="1.80"></label><label class="nag-adaptive-field"><span>最大</span><input class="sniff-input" id="nag-cr-max" type="number" min="1.80" max="2.00" step="0.01" value="2.00"></label></div></div></div></section>''',
        "single corrective range",
    )

    source_text = _sub_once(
        source_text,
        r'\s*<div class="nag-field-group"><div class="nag-field-label">方向死区 / Nm</div><label class="nag-adaptive-field"><span>0–0\.50 Nm</span><input class="sniff-input" id="nag-direction-deadband"[^>]*></label></div>',
        "",
        "remove deadband field",
    )

    source_text = source_text.replace("计数器冲突 / 间隔", "OEM计数异常 / Echo复用间隔")
    source_text = source_text.replace("['nagCounterCollisions','计数器冲突']", "['nagCounterCollisions','OEM计数异常']")

    source_text = _sub_once(
        source_text,
        r'const nagCustomDefaults=\{\s*preventiveNegative:\[1\.50,1\.80\], preventivePositive:\[1\.50,1\.80\],\s*correctiveNegative:\[1\.80,2\.00\], correctivePositive:\[1\.80,2\.00\],\s*activity:\[10\.0,10\.0\], release:\[0\.2,0\.4\], rest:\[1\.0,2\.0\],\s*directionDeadband:0\.05, dasFreshTimeoutMs:750\s*\};',
        '''const nagCustomDefaults={\n  preventiveNegative:[1.50,1.80], preventivePositive:[1.50,1.80],\n  corrective:[1.80,2.00], maintenanceEnabled:true,\n  activity:[10.0,10.0], release:[0.2,0.4], rest:[1.0,2.0],\n  dasFreshTimeoutMs:750\n};''',
        "custom defaults",
    )

    source_text = _sub_once(
        source_text,
        r"\['correctiveNegative',0,'nag-cr-neg-min','correctiveNegativeMinNm',1\.80,2\.00,2\],\['correctiveNegative',1,'nag-cr-neg-max','correctiveNegativeMaxNm',1\.80,2\.00,2\],\s*\['correctivePositive',0,'nag-cr-pos-min','correctivePositiveMinNm',1\.80,2\.00,2\],\['correctivePositive',1,'nag-cr-pos-max','correctivePositiveMaxNm',1\.80,2\.00,2\],",
        "['corrective',0,'nag-cr-min','correctiveNegativeMinNm',1.80,2.00,2],['corrective',1,'nag-cr-max','correctiveNegativeMaxNm',1.80,2.00,2],",
        "corrective field definitions",
    )
    source_text = _sub_once(
        source_text,
        r",?\s*\['directionDeadband',null,'nag-direction-deadband','directionDeadbandNm',0,0\.50,2\]",
        "",
        "deadband field definition",
    )

    source_text = source_text.replace(
        "['preventiveNegative','preventivePositive','correctiveNegative','correctivePositive','activity','release','rest']",
        "['preventiveNegative','preventivePositive','corrective','activity','release','rest']",
    )

    source_text = source_text.replace(
        "function setNagCustomControlsDisabled(disabled){\n  nagCustomFieldDefs.forEach(def=>{const input=$(def[2]);if(input)input.disabled=disabled;});\n  const defaults=$('nag-custom-defaults'),save=$('nag-custom-save');if(defaults)defaults.disabled=disabled;if(save)save.disabled=disabled;\n}",
        "function setNagCustomControlsDisabled(disabled){\n  nagCustomFieldDefs.forEach(def=>{const input=$(def[2]);if(input)input.disabled=disabled;});\n  const maintenance=$('nag-maintenance-enabled');if(maintenance)maintenance.disabled=disabled;\n  const defaults=$('nag-custom-defaults'),save=$('nag-custom-save');if(defaults)defaults.disabled=disabled;if(save)save.disabled=disabled;\n}",
    )

    source_text = source_text.replace(
        "  next.dasFreshTimeoutMs=750;return next;\n}",
        "  next.maintenanceEnabled=next.maintenanceEnabled!==false;next.dasFreshTimeoutMs=750;return next;\n}",
    )

    source_text = source_text.replace(
        "function renderNagCustomDraft(){\n  nagCustomFieldDefs.forEach(def=>{const input=$(def[2]);if(!input||document.activeElement===input)return;const value=def[1]===null?nagCustomDraft[def[0]]:nagCustomDraft[def[0]][def[1]];input.value=Number(value).toFixed(def[6]);});\n  const timeout=$('nag-custom-das-timeout');if(timeout)timeout.textContent=nagCustomDraft.dasFreshTimeoutMs+' ms';\n}",
        "function renderNagCustomDraft(){\n  nagCustomFieldDefs.forEach(def=>{const input=$(def[2]);if(!input||document.activeElement===input)return;const value=def[1]===null?nagCustomDraft[def[0]]:nagCustomDraft[def[0]][def[1]];input.value=Number(value).toFixed(def[6]);});\n  const maintenance=$('nag-maintenance-enabled');if(maintenance)maintenance.checked=nagCustomDraft.maintenanceEnabled!==false;\n  const timeout=$('nag-custom-das-timeout');if(timeout)timeout.textContent=nagCustomDraft.dasFreshTimeoutMs+' ms';\n}",
    )

    source_text = source_text.replace(
        "function initNagCustomUi(){nagCustomFieldDefs.forEach(def=>{const input=$(def[2]);if(input)input.addEventListener('input',updateNagCustomDraft);});renderNagCustomDraft();setNagCustomControlsDisabled(true);setNagCustomDirty(false);}",
        "function initNagCustomUi(){nagCustomFieldDefs.forEach(def=>{const input=$(def[2]);if(input)input.addEventListener('input',updateNagCustomDraft);});const maintenance=$('nag-maintenance-enabled');if(maintenance)maintenance.addEventListener('change',()=>{nagCustomRevision++;nagCustomDraft.maintenanceEnabled=maintenance.checked;setNagCustomDirty(true);});renderNagCustomDraft();setNagCustomControlsDisabled(true);setNagCustomDirty(false);}",
    )

    source_text = source_text.replace(
        "  const timeout=Number(data.dasFreshTimeoutMs);next.dasFreshTimeoutMs=Number.isFinite(timeout)?timeout:750;nagCustomDraft=normalizeNagCustomDraft(next);renderNagCustomDraft();\n}",
        "  const legacyMaintenance=Number(data.directionDeadbandNm);next.maintenanceEnabled=!Number.isFinite(legacyMaintenance)||legacyMaintenance!==0;const timeout=Number(data.dasFreshTimeoutMs);next.dasFreshTimeoutMs=Number.isFinite(timeout)?timeout:750;nagCustomDraft=normalizeNagCustomDraft(next);renderNagCustomDraft();\n}",
    )

    source_text = _sub_once(
        source_text,
        r"  params\.set\('correctiveNegativeMinNm'.*?params\.set\('correctivePositiveMaxNm'.*?;\n",
        "  params.set('correctiveNegativeMinNm',requestDraft.corrective[0].toFixed(2));params.set('correctiveNegativeMaxNm',requestDraft.corrective[1].toFixed(2));params.set('correctivePositiveMinNm',requestDraft.corrective[0].toFixed(2));params.set('correctivePositiveMaxNm',requestDraft.corrective[1].toFixed(2));\n",
        "save shared corrective range",
    )
    source_text = source_text.replace(
        "params.set('directionDeadbandNm',requestDraft.directionDeadband.toFixed(2));params.set('dasFreshTimeoutMs',String(requestDraft.dasFreshTimeoutMs));",
        "params.set('directionDeadbandNm',requestDraft.maintenanceEnabled?'0.05':'0.00');params.set('dasFreshTimeoutMs',String(requestDraft.dasFreshTimeoutMs));",
    )

    source_text = source_text.replace(
        "function nagDiagnosticCollisionGap(value,collisions){\n  const count=nagDiagnosticFinite(collisions),gap=nagDiagnosticFinite(value);if(count===null||count<=0||gap===null||gap<=0||gap>=0xFFFFFFFF)return '--';return Math.trunc(gap)+' μs';\n}",
        "function nagDiagnosticCollisionGap(value,collisions){\n  const gap=nagDiagnosticFinite(value);if(gap===null||gap<=0||gap>=0xFFFFFFFF)return '--';return Math.trunc(gap)+' μs';\n}",
    )

    return source_text


def minify_blocks(html: str, tag: str, fn) -> str:
    pattern = re.compile(rf"(<{tag}\b[^>]*>)(.*?)(</{tag}>)", re.DOTALL | re.IGNORECASE)

    def replace(match: re.Match[str]) -> str:
        try:
            return match.group(1) + fn(match.group(2)) + match.group(3)
        except Exception as exc:
            print(f"warn: {tag} minify failed: {exc}", file=sys.stderr)
            return match.group(0)

    return pattern.sub(replace, html)


def hex_array(data: bytes, width: int = 16) -> str:
    rows = []
    for index in range(0, len(data), width):
        rows.append(",".join(f"0x{value:02x}" for value in data[index : index + width]))
    return ",\n    ".join(rows)


def build_dashboard_header(source_text: str) -> tuple[str, int, int, int]:
    source_text = apply_v44_overrides(source_text)
    match = re.search(r'R"HTML\((.*)\)HTML";', source_text, re.DOTALL)
    if match is None:
        raise ValueError("no HTML payload found")

    html = match.group(1)
    before = len(html)
    html = minify_blocks(html, "style", csscompressor.compress)
    html = minify_blocks(html, "script", rjsmin.jsmin)
    html = htmlmin.minify(
        html,
        remove_comments=True,
        remove_empty_space=True,
        remove_all_empty_space=True,
        reduce_boolean_attributes=True,
        keep_pre=True,
    )
    raw_len = len(html)
    compressed = bytearray(gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0))
    compressed[9] = 0xFF
    gz_len = len(compressed)

    lines = [
        "#pragma once",
        "#ifdef ESP_PLATFORM",
        '#include "platform/espidf_runtime.h"',
        "#else",
        "#include <Arduino.h>",
        "#endif",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "#ifndef ESP_PLATFORM",
        'static const char DASH_HTML[] PROGMEM = R"HTML(' + html + ')HTML";',
        "#endif",
        "",
        "static const uint8_t DASH_HTML_GZ[] PROGMEM = {",
        "    " + hex_array(compressed),
        "};",
        f"static constexpr size_t DASH_HTML_GZ_LEN = {gz_len};",
        "",
    ]
    return "\n".join(lines), before, raw_len, gz_len


def print_sizes(before: int, raw_len: int, gz_len: int) -> None:
    print(f"html: {before} -> {raw_len} bytes minified ({100 * raw_len / before:.1f}%)")
    print(
        f"gzip: {raw_len} -> {gz_len} bytes "
        f"({100 * gz_len / raw_len:.1f}% of minified, {100 * gz_len / before:.1f}% of original)"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify that mcp2515_dashboard_ui.base.h matches the source without writing",
    )
    args = parser.parse_args()

    try:
        expected, before, raw_len, gz_len = build_dashboard_header(
            SRC.read_text(encoding="utf-8")
        )
    except (OSError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 1

    if args.check:
        if not DST.exists() or DST.read_text(encoding="utf-8") != expected:
            print("generated payload is stale", file=sys.stderr)
            return 1
        print_sizes(before, raw_len, gz_len)
        print("generated payload is current")
        return 0

    DST.write_text(expected, encoding="utf-8")
    print_sizes(before, raw_len, gz_len)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
