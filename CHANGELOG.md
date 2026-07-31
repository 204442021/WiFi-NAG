# Changelog

## [Unreleased]

## [V3.0.1] - 2026-07-31

### Added

- Dedicated ESP32-S3 16 MB layout with two 3 MiB OTA application slots.
- OTA page details for firmware version, current partition, target partition,
  and partition capacity.

### Fixed

- Ported the V1.0.2 CAN restart and OTA flash safety protections.
- Kept the WebUI version, ESP-IDF application descriptor, and release artifact
  version aligned at V3.0.1.
- Pinned the ESP-IDF 6 compatible Xtensa GCC toolchain for reproducible builds.

### Validation

- ESP32-S3 3 MiB OTA firmware build succeeded.
- Python regression tests: 28 passed.
- Native PlatformIO tests: 54 passed.

