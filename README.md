# EVtools WIFI-NAG

[中文说明](README.zh-CN.md)

WIFI-NAG is firmware for the Waveshare ESP32-S3 RS485/CAN board. This repository now maintains only one target:

- ESP32-S3 native TWAI CAN
- Nag echo on CAN ID `880 / 0x370`
- WiFi AP + STA
- AP-to-STA NAPT gateway
- DNS proxy / filter / cache
- Chinese WebUI
- Manual local OTA

It is not a Legacy/HW3/HW4 FSD activation firmware. MCP2515, SAME51, plugin runtime/examples, FSD activation handlers, CAN recorder/sniffer/debug tools, online OTA, settings import/export, and task-stats pages are outside the maintained scope.

## Safety Notice

This firmware is for open-source learning, research, and testing only. Selling, reselling, or commercial distribution is prohibited.

CAN write behavior can affect vehicle behavior. Use `CAN Write OFF` first, confirm that RX/status data is normal, and enable writes only when you fully understand the effect and accept all risk. Always stay alert, look at the road, and keep both hands ready to take over steering.

## Hardware

- Board: Waveshare ESP32-S3 RS485/CAN
- CAN controller: ESP32-S3 native TWAI
- CAN transceiver: onboard CAN transceiver
- CAN speed: `500 kbit/s`
- Default TWAI TX: `GPIO_NUM_15`
- Default TWAI RX: `GPIO_NUM_16`
- WebUI address: `http://100.100.1.1/`

External wiring should use the board terminal labels:

```text
CANH -> vehicle CAN-H
CANL -> vehicle CAN-L
```

Do not wire vehicle CAN directly to ESP32 GPIO pins.

## Build Scope

The maintained PlatformIO environment is:

```text
wifi_nag_ESP32_S3_CAN
```

This target uses ESP-IDF, not Arduino. It is expected to build with the default ESP-IDF toolchain used by this project, currently `toolchain-xtensa-esp-elf 15.2.0+20251204`.

The separate Arduino/T-2CAN toolchain convention does not apply to this WIFI-NAG ESP-IDF target.

## Current CAN Behavior

The only active CAN business logic is Nag echo on `0x370 / 880`.

### Common Rules

- Listens only to CAN ID `880 / 0x370`.
- Ignores frames with DLC less than 8.
- `CAN Write OFF`: read-only monitoring; no Nag echo is sent.
- `CAN Write ON`: sends Nag echo for every real `0x370` frame.
- Writes `EPAS3S_handsOnLevel = 1` in outgoing echo frames.
- Updates the low-nibble counter in `data[6]`.
- Recalculates checksum byte `data[7]`.
- Skips its own echo frames to avoid feedback loops.

### Mode A

- Fixed output torque: `+1.80 Nm`.
- Sends on every real `0x370` frame while `CAN Write ON`.

### Mode A_V2

- Pseudo-random sweep inside the configured range.
- Sweep period: `2000 ms`.
- Default range: `+1.50 .. +1.80 Nm`.
- Range is clamped to `-1.80 .. +1.80 Nm`.
- If min is greater than max, values are automatically swapped.

## WiFi / DNS Gateway

- Starts AP + STA mode.
- Default AP subnet: `100.100.1.x`.
- Device/gateway IP: `100.100.1.1`.
- Saves multiple upstream WiFi networks.
- Supports static STA IP, gateway, mask, and DNS.
- Enables AP-to-STA NAPT when STA is connected.
- Runs a DNS proxy on UDP 53 for AP clients.
- Supports blacklist and whitelist rules.
- Caches DNS responses and coalesces duplicate pending DNS queries.

## Persistent Settings

Runtime settings are stored on the device so they survive normal firmware updates:

- AP hotspot SSID/password/hidden flag
- Saved upstream WiFi networks
- Optional static STA IP/gateway/mask/DNS
- CAN Write state
- Nag mode
- A_V2 min/max range
- DNS gateway enable state
- DNS blacklist/whitelist and upstream DNS mode

Large DNS lists are stored in SPIFFS. Gateway metadata and smaller runtime settings are stored in NVS.

A full 16MB flash image overwrites the entire flash chip, including NVS and SPIFFS, so it resets saved WiFi, AP, DNS, and Nag settings.

## WebUI

The WebUI provides:

- CAN status, RX/TX/errors, FPS, uptime
- CAN Write toggle
- Nag mode and A_V2 range controls
- AP hotspot settings
- WiFi scan/connect/delete
- STA-AP gateway controls
- DNS upstream, blacklist, whitelist, diagnostics, and blocked-domain list
- System status
- Debug log viewer
- Manual firmware upload OTA
- Safety notice popup on every page load

## System Status Panel

The WebUI `System Status` panel is read-only. It does not change CAN, WiFi, or DNS behavior.

When enabled, it polls `/system_status` once per second and displays:

- Chip/module/target/revision
- CPU frequency, APB clock, XTAL clock
- CPU0/CPU1 load estimate
- Temperature when available
- Reset reason
- Uptime and current core
- Free heap, minimum free heap, largest free block
- Internal RAM and PSRAM usage
- Flash size, running app partition, app size, app used bytes
- SPIFFS total/used state
- WiFi mode, STA RSSI, AP client count
- BLE support/firmware state
- MAC address, firmware version, and IDF version

CPU load is estimated from FreeRTOS idle task runtime counters. The first sample may show `warming up` until a second sample is available.

## Build

Create local credentials first:

```powershell
Copy-Item platformio_profile.example.h platformio_profile.h
```

Then edit `platformio_profile.h` if you need a custom AP name, AP password, or OTA password. The file is ignored by git.

```powershell
pio run -e wifi_nag_ESP32_S3_CAN
```

Firmware output:

```text
.pio/build/wifi_nag_ESP32_S3_CAN/firmware.bin
```

## Quick Verification

Before enabling CAN writes on a vehicle:

1. Boot the device with `CAN Write OFF`.
2. Open `http://100.100.1.1/`.
3. Confirm CAN RX/FPS updates when connected to the target CAN bus.
4. Confirm only `0x370 / 880` Nag behavior is intended.
5. Enable `CAN Write ON` only when ready to test.
6. Watch TX errors and bus status.
7. Remove the device immediately if unexpected behavior occurs.

## Upload

Example for `COM15`:

```powershell
pio run -e wifi_nag_ESP32_S3_CAN -t upload --upload-port COM15
```

## Full 16MB Image

The full merged 16MB image can be generated with:

```powershell
py -3 $env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32s3 merge_bin -o dist\wifi_nag_ESP32_S3_CAN_16MB_full.bin --flash_mode dio --flash_freq 80m --flash_size 16MB --fill-flash-size 16MB 0x0 .pio\build\wifi_nag_ESP32_S3_CAN\bootloader.bin 0x8000 .pio\build\wifi_nag_ESP32_S3_CAN\partitions.bin 0x19000 .pio\build\wifi_nag_ESP32_S3_CAN\ota_data_initial.bin 0x20000 .pio\build\wifi_nag_ESP32_S3_CAN\firmware.bin
```

The full image writes the entire flash and will overwrite NVS/SPIFFS settings.

## Troubleshooting

### No CAN RX

- Check `CANH` and `CANL` wiring.
- Confirm the device is connected to the intended CAN bus.
- Confirm the bus is `500 kbit/s`.
- Check that the board is powered correctly.
- Try swapping CANH/CANL if the wiring source is uncertain.

### CAN RX Works But No Nag Echo

- Confirm `CAN Write ON`.
- Confirm incoming frames include CAN ID `0x370 / 880`.
- Confirm the frame DLC is at least 8.
- Check WebUI TX/error counters.

### DNS Gateway Does Not Route

- Confirm STA WiFi is connected.
- Confirm AP clients are connected to the device hotspot.
- Confirm gateway/NAPT is enabled.
- Check the gateway diagnostics section in WebUI.
- Try a public upstream DNS mode such as Ali or Tencent.

### WebUI Does Not Show Latest Changes

- Refresh the browser page.
- Reconnect to the device hotspot.
- Confirm the firmware was rebuilt after WebUI changes.
- For WebUI source edits, always regenerate `mcp2515_dashboard_ui.h` with `py -3 scripts/minify_dashboard.py`.

## Test

```powershell
pio test -e native_nag
pio test -e native_twai
pio test -e native_log_buffer
py -3 -m unittest test/test_wifi_settings_regression.py
```

## WebUI Generation

Edit:

```text
include/web/mcp2515_dashboard_ui.src.h
```

Then regenerate:

```powershell
py -3 scripts/minify_dashboard.py
```

Do not manually edit `include/web/mcp2515_dashboard_ui.h` except through this generation flow.

## Maintained File Groups

```text
include/                         Core firmware headers
include/drivers/                 CAN driver abstractions and TWAI driver
include/web/                     WebUI, dashboard, DNS gateway
src/                             ESP-IDF entry point and runtime compatibility
scripts/minify_dashboard.py      WebUI generator
scripts/platformio_*.py          PlatformIO helper scripts
test/                            Native regression tests
platformio.ini                   Build environments
sdkconfig*.defaults              ESP-IDF configuration defaults
partitions_16mb_ota_4096k_nvs64_boot.csv
```

## Out Of Scope

The clean WIFI-NAG project intentionally excludes:

- Legacy/HW3/HW4 FSD activation
- MCP2515 external CAN
- SAME51
- CAN recorder/sniffer/debug tools
- Plugin docs/examples/runtime
- Online OTA
- Settings import/export
- Task-stats page
- Old documentation site and release CI metadata

## Clean Project Folder

For a minimal source-only copy, this workspace can be reduced to:

```text
include/
src/
scripts/
test/
platformio.ini
platformio_profile.example.h
sdkconfig.defaults
sdkconfig.wifi_nag.defaults
sdkconfig.wifi_nag_ESP32_S3_CAN
partitions_16mb_ota_4096k_nvs64_boot.csv
CMakeLists.txt
README.md
README.zh-CN.md
LICENSE
THIRD_PARTY_LICENSES
VERSION
```

Do not include generated/local folders such as `.pio/`, `dist/`, `managed_components/`, `dependencies.lock`, or local `platformio_profile.h` in a clean distributable source folder.
