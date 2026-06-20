# Dashboard

Open the WebUI from a client connected to the ESP32-S3 AP:

```text
http://100.100.1.1/
```

## Current Controls

- CAN status, RX/TX counters, errors, FPS, uptime.
- CAN Write toggle for Nag echo transmission.
- Nag mode `A` / `A_V2`.
- A_V2 min/max Nm range.
- AP hotspot settings.
- WiFi scan, save, connect, delete.
- DNS gateway blacklist/whitelist and upstream DNS.
- DNS diagnostics, blocked-domain list, and stats reset.
- System status and debug log.
- Manual firmware upload OTA.

## CAN Write

- OFF: monitors CAN ID `880 / 0x370` but does not transmit echoes.
- ON: allows Nag echo writes when the Nag runtime conditions match.

Keep CAN Write off during first installation and after firmware updates until CAN RX/status looks normal.
