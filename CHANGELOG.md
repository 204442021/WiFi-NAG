# Changelog

## [Unreleased]

### Fixed

- Made the GitHub Release workflow locate the uploaded firmware artifact
  regardless of the download-artifact directory layout.
- Made release triggering and changelog extraction handle the repository's
  uppercase `Vx.y.z` version tags.
- Ensured manually triggered releases tag the exact workflow commit.

## [V3.0.3] - 2026-08-01

### Fixed

- Reset BLE FSD sequence state on disconnect, BLE reset, receiver re-enable,
  and peer changes so reconnects and sender restarts do not remain stuck on
  `old_seq` or `duplicate_seq`.
- Allowed a same-sequence heartbeat to retry after temporary CAN-health loss
  instead of permanently consuming the activation.
- Used the sender `holdMs` for the MODE_A window and made a matching clear
  packet stop the receiver and torque windows immediately.
- Retriggered MODE_A for newer sequences even while the receiver was already
  active, and preserved pending triggers until the CAN output gate is enabled.

### Added

- Added native BLE receiver state-machine tests and exposed `remoteActive`,
  accepted packet count, and disconnect count through `/ble_fsd`.
- Added a live V3.0.3 BLE repair assessment that distinguishes link,
  subscription, packet validation, CAN safety, CAN Write, and A-mode gates.
- Added one-click, redacted JSON diagnostics download using the existing
  `/status`, `/ble_fsd`, `/system_status`, `/ota_status`, and `/log` endpoints.
  Partial endpoint failures are recorded without blocking the download.

### Changed

- Made every dashboard card, configuration subsection, and BLE settings panel
  start collapsed after every page load instead of restoring expanded state.

### Validation

- Native PlatformIO tests: 68 passed.
- Python regression tests: 38 passed.
- Node.js BLE diagnostics behavior tests: 11 passed.
- ESP32 firmware build and binary publication were intentionally not performed
  for this source-only task.

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
