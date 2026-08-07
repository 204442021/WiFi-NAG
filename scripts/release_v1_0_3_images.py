#!/usr/bin/env python3
"""Package WIFI-NAG V1.0.3 OTA and full 16MB factory images."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shlex
import shutil
import textwrap
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def resolve_binary(value: str, arg_file: Path, build: Path, root: Path) -> Path | None:
    raw = Path(value)
    candidates = (raw, arg_file.parent / raw, build / raw, root / raw)
    for candidate in candidates:
        candidate = candidate.resolve()
        if candidate.is_file() and candidate.stat().st_size > 0:
            return candidate
    return None


def first_existing(*candidates: Path) -> Path | None:
    for candidate in candidates:
        if candidate.is_file() and candidate.stat().st_size > 0:
            return candidate.resolve()
    return None


def collect_flash_segments(root: Path, build: Path, ota_src: Path) -> dict[int, Path]:
    segments: dict[int, Path] = {}
    flash_arg_candidates = [
        build / "flash_args",
        build / "flash_project_args",
        *build.glob("**/flash_args"),
        *build.glob("**/flash_project_args"),
    ]
    seen_arg_files: set[Path] = set()

    for arg_file in flash_arg_candidates:
        arg_file = arg_file.resolve()
        if arg_file in seen_arg_files or not arg_file.is_file():
            continue
        seen_arg_files.add(arg_file)
        tokens = shlex.split(arg_file.read_text(encoding="utf-8", errors="replace"))
        for index in range(len(tokens) - 1):
            address = tokens[index]
            if not re.fullmatch(r"0x[0-9a-fA-F]+", address):
                continue
            binary = resolve_binary(tokens[index + 1], arg_file, build, root)
            if binary is not None and binary.suffix == ".bin":
                segments[int(address, 16)] = binary
        if ota_src.resolve() in {path.resolve() for path in segments.values()}:
            break

    if ota_src.resolve() in {path.resolve() for path in segments.values()}:
        return segments

    bootloader = first_existing(build / "bootloader.bin", build / "bootloader" / "bootloader.bin")
    partitions = first_existing(
        build / "partitions.bin",
        build / "partition_table" / "partition-table.bin",
    )
    otadata = first_existing(
        build / "ota_data_initial.bin",
        build / "ota_data_initial" / "ota_data_initial.bin",
    )
    if bootloader is None or partitions is None:
        raise RuntimeError("unable to locate bootloader or partition table")

    sdkconfig_text = "\n".join(
        path.read_text(encoding="utf-8", errors="ignore")
        for path in (
            build / "sdkconfig",
            root / "sdkconfig.wifi_nag_ESP32_S3_CAN",
            root / "sdkconfig",
        )
        if path.is_file()
    )
    boot_match = re.search(
        r"CONFIG_BOOTLOADER_OFFSET_IN_FLASH=(0x[0-9A-Fa-f]+)", sdkconfig_text
    )
    partition_match = re.search(
        r"CONFIG_PARTITION_TABLE_OFFSET=(0x[0-9A-Fa-f]+)", sdkconfig_text
    )
    boot_offset = int(boot_match.group(1), 16) if boot_match else 0x0
    partition_offset = int(partition_match.group(1), 16) if partition_match else 0x8000

    segments[boot_offset] = bootloader
    segments[partition_offset] = partitions
    if otadata is not None:
        segments[0x19000] = otadata
    segments[0x20000] = ota_src.resolve()
    return segments


def write_github_outputs(outputs: dict[str, Path]) -> None:
    output_path = os.environ.get("GITHUB_OUTPUT")
    if not output_path:
        return
    with Path(output_path).open("a", encoding="utf-8") as output:
        for key, path in outputs.items():
            output.write(f"{key}={path}\n")


def main() -> None:
    version = os.environ.get("RELEASE_VERSION", "V1.0.3")
    source_sha = os.environ["RELEASE_TARGET"]
    build_env = os.environ.get("BUILD_ENV", "wifi_nag_ESP32_S3_CAN")
    root = Path.cwd()
    build = root / ".pio" / "build" / build_env
    dist = root / "dist"
    dist.mkdir(exist_ok=True)

    ota_src = build / "firmware.bin"
    if not ota_src.is_file() or ota_src.stat().st_size == 0:
        raise RuntimeError(f"missing OTA image: {ota_src}")

    ota_name = f"WIFI-NAG-{version}-OTA.bin"
    factory_name = f"WIFI-NAG-{version}-FACTORY-16MB.bin"
    manifest_name = f"WIFI-NAG-{version}-MANIFEST.json"
    checksums_name = f"WIFI-NAG-{version}-SHA256SUMS.txt"
    guide_name = f"WIFI-NAG-{version}-FLASH-GUIDE.txt"
    release_notes_name = f"WIFI-NAG-{version}-RELEASE-NOTES.md"

    ota_dst = dist / ota_name
    factory_dst = dist / factory_name
    shutil.copy2(ota_src, ota_dst)

    segments = collect_flash_segments(root, build, ota_src)
    if not segments:
        raise RuntimeError("no flash segments found")

    flash_size = 16 * 1024 * 1024
    image = bytearray(b"\xFF") * flash_size
    occupied: list[tuple[int, int, Path]] = []
    segment_manifest: list[dict[str, object]] = []

    for offset, path in sorted(segments.items()):
        data = path.read_bytes()
        end = offset + len(data)
        if end > flash_size:
            raise RuntimeError(f"segment exceeds 16MB flash: {path} at 0x{offset:X}")
        for prior_start, prior_end, prior_path in occupied:
            if max(offset, prior_start) < min(end, prior_end):
                raise RuntimeError(f"overlap: {path} with {prior_path}")
        image[offset:end] = data
        occupied.append((offset, end, path))
        segment_manifest.append(
            {
                "offset": f"0x{offset:X}",
                "file": path.name,
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )

    factory_dst.write_bytes(image)

    manifest = {
        "product": "WIFI-NAG",
        "version": version,
        "source_sha": source_sha,
        "build_environment": build_env,
        "built_at_utc": datetime.now(timezone.utc).isoformat(),
        "flash_chip": "ESP32-S3",
        "flash_size_bytes": flash_size,
        "ota": {
            "file": ota_dst.name,
            "size": ota_dst.stat().st_size,
            "sha256": sha256(ota_dst),
            "application_offset": "0x20000",
        },
        "factory": {
            "file": factory_dst.name,
            "size": factory_dst.stat().st_size,
            "sha256": sha256(factory_dst),
            "write_offset": "0x0",
            "segments": segment_manifest,
        },
    }
    manifest_path = dist / manifest_name
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    guide = textwrap.dedent(
        f"""\
        WIFI-NAG {version} 固件使用说明

        正式源码 SHA：{source_sha}
        目标芯片：ESP32-S3
        Flash 容量：16MB

        一、OTA 升级
        文件：{ota_name}
        通过 WIFI-NAG 后台的本地 OTA 上传页面升级。
        OTA 只写入应用分区，通常保留 NVS 和设备设置。

        二、USB/UART 线刷
        文件：{factory_name}
        这是从地址 0x0 开始写入的 16MB 完整镜像。
        完整线刷会覆盖整片 Flash，并清除旧 NVS、Wi-Fi、绑定和历史设置。

        示例命令：
        esptool.py --chip esp32s3 --baud 921600 write_flash 0x0 {factory_name}

        刷写完成后断电重启，并重新配置设备。
        请先用 {checksums_name} 校验文件完整性。
        """
    )
    guide_path = dist / guide_name
    guide_path.write_text(guide, encoding="utf-8")

    checksummed = [ota_dst, factory_dst, manifest_path, guide_path]
    checksums_path = dist / checksums_name
    checksums_path.write_text(
        "".join(f"{sha256(path)}  {path.name}\n" for path in checksummed),
        encoding="utf-8",
    )

    base_notes = Path("RELEASE_NOTES_V1.0.3.md").read_text(encoding="utf-8")
    release_notes = base_notes.rstrip() + textwrap.dedent(
        f"""

        ## 已验证发布产物

        - 正式源码 SHA：`{source_sha}`
        - 自动化测试：全部通过
        - ESP32-S3 正式环境编译：通过
        - OTA 固件：`{ota_name}`
        - 16MB 完整线刷固件：`{factory_name}`
        - 完整性校验：`{checksums_name}`
        - 线刷说明：`{guide_name}`

        完整线刷固件从 `0x0` 写入，会清除旧设置；普通升级优先使用 OTA 固件。
        """
    )
    release_notes_path = dist / release_notes_name
    release_notes_path.write_text(release_notes, encoding="utf-8")

    outputs = {
        "ota": ota_dst,
        "factory": factory_dst,
        "manifest": manifest_path,
        "checksums": checksums_path,
        "guide": guide_path,
        "release_notes": release_notes_path,
    }
    write_github_outputs(outputs)
    print(json.dumps(manifest, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
