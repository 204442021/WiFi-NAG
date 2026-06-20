# EVtools WIFI-NAG

WIFI-NAG is firmware for the Waveshare ESP32-S3 RS485/CAN board. This repository now maintains one product line only:

- ESP32-S3 native TWAI CAN
- Nag suppression on CAN ID `880 / 0x370`
- WiFi AP + STA
- AP-to-STA NAPT gateway
- DNS proxy/filter/cache
- WebUI for Nag, WiFi, DNS, status, logs, and manual OTA

It is not a Legacy/HW3/HW4 FSD activation firmware. MCP2515, SAME51, plugin runtime/examples, FSD activation handlers, CAN recorder/sniffer tools, online OTA, settings backup, and task-stats pages are outside the maintained scope.

## Hardware

- Board: Waveshare ESP32-S3 RS485/CAN
- CAN driver: ESP32-S3 native TWAI
- CAN speed: `500 kbit/s`
- Default TWAI TX: `GPIO_NUM_15`
- Default TWAI RX: `GPIO_NUM_16`
- Default WebUI: `http://100.100.1.1/`

## Current Features

### Nag CAN Runtime

- Listens only to CAN ID `880 / 0x370`.
- `CAN Write OFF`: read-only CAN monitoring.
- `CAN Write ON`: allows Nag echo writes.
- Mode `A`: fixed `+1.80 Nm` echo when `handsOnLevel == 0`.
- Mode `A_V2`: warmup at `+1.80 Nm`, then sweeps inside the configured Nm range.
- Updates low-nibble counter and checksum byte.
- Skips its own echo frames.

### WiFi Gateway

- Starts AP + STA mode.
- Default AP network: `100.100.1.x`.
- Device/gateway address: `100.100.1.1`.
- Saves multiple upstream WiFi networks.
- Supports static STA IP, gateway, mask, and DNS.
- Enables NAPT when STA is connected.

### DNS Proxy

- Handles AP client DNS on UDP 53.
- Supports blacklist and whitelist rules.
- Defaults include Tesla root-domain blocking with specific allow-list exceptions.
- Caches DNS responses and coalesces duplicate pending queries.
- Exposes gateway status, DNS settings, DNS test, stats reset, and blocked-domain APIs.

### WebUI

- CAN status, RX/TX/errors, uptime.
- CAN Write toggle.
- Nag mode and A_V2 range controls.
- AP hotspot settings.
- WiFi scan/connect/delete.
- DNS gateway controls and diagnostics.
- System status and log viewer.
- Manual firmware upload OTA.

## Build

```powershell
pio run -e wifi_nag_ESP32_S3_CAN
```

Firmware output:

```text
.pio/build/wifi_nag_ESP32_S3_CAN/firmware.bin
```

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

## Safety

CAN behavior is the highest-risk area. Keep CAN timing, ID `0x370`, torque encoding, counter, checksum, echo-skip, and TWAI filter behavior stable unless a change explicitly targets them. Test with `CAN Write OFF` first, confirm RX/status behavior, then enable writes only when needed.
