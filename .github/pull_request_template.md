## Target branch

- [ ] This PR targets `dev`. Do not open feature or fix PRs directly against `main`.

## Summary

<!-- Brief description of what this PR does -->

## Changes

<!-- List the key changes -->

-

## Checklist

- [ ] Firmware builds (`pio run -e wifi_nag_ESP32_S3_CAN`)
- [ ] Native tests pass (`pio test -e native_nag`, `pio test -e native_twai`, `pio test -e native_log_buffer`)
- [ ] Linter passes (`clang-format --dry-run --Werror --style=file`)
- [ ] `CHANGELOG.md` updated for relevant user-facing or tooling changes
- [ ] Documentation updated in `docs/` (if user-facing change)
- [ ] `CHANGELOG.md` updated under `[Unreleased]` (if user-facing or behavioral change)
- [ ] WIFI-NAG compatibility notes updated (if behavior changes by vehicle firmware version)
