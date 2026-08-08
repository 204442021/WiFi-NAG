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
    compressed = gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)
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
