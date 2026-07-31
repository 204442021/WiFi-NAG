# Changelog

## [Unreleased]

### Fixed

- Made the GitHub Release workflow locate the uploaded firmware artifact
  regardless of the download-artifact directory layout.

## [V3.0.2] - 2026-07-31

### Changed

- Localized the BLE FSD diagnostic receiver interface, including live status,
  scan results, and receiver reasons, for the Chinese UI.
- Made dashboard cards, configuration subsections, and BLE settings collapsed
  by default while preserving user collapse preferences.
- Enabled the BLE FSD diagnostic receiver by default for devices without a
  saved preference.

### Validation

- ESP32-S3 firmware build succeeded for `wifi_nag_ESP32_S3_CAN`.
- Native PlatformIO tests: 54 passed.

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
