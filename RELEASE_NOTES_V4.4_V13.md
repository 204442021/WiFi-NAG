# WiFi-NAG V4.4 V13

V4.4 focuses only on the adaptive NAG correction path introduced in V4.3.

- Corrective HOS 3..5 targets can now reach the wire at the configured `1.80..2.00 Nm`; the legacy continuous mode remains capped at `1.80 Nm`.
- Corrective magnitude uses one shared range rather than separate positive/negative settings.
- Direction selection has no torque deadband and no 100 ms reversal confirmation: non-zero measured torque selects the opposite injection sign immediately; exact zero holds the last established sign.
- A maintenance-layer switch is available and defaults on. Turning it off suppresses HOS 0..2 maintenance output while leaving HOS 3..5 corrective output armed for isolated testing.
- Three valid OEM `0x370` frames are still required only for startup/re-arm. A fresh DAS HOS 3..5 frame still enters corrective mode immediately.
- `OEM counter + 1` echo generation is unchanged. Diagnostics now distinguish OEM counter-sequence anomalies from the normal echo-to-next-OEM counter reuse timing instead of treating every reuse as a collision.
- HOS >= 6 fail-closed, DAS freshness, checksum validation, own-echo suppression, and other CAN safety gates are unchanged.
