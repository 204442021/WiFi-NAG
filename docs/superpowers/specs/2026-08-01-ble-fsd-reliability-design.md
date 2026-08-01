# BLE FSD Receiver Reliability Design

## Goal

Make WiFi-NAG V3.0.3 react reliably and promptly to BLE FSD activation packets from the existing T2CAN-FSD V1.5.3 sender, including after a BLE reconnect, sender reboot, temporary CAN-health failure, or a rapid second FSD activation.

## Scope

This release changes only the WiFi-NAG repository. It keeps the existing 16-byte BLE packet, UUIDs, peer-MAC whitelist, torque encoding, and CAN safety gates compatible with T2CAN-FSD V1.5.3.

The work covers:

- receiver session reset and sequence handling;
- retry after temporary CAN unavailability;
- authoritative handling of the sender hold duration and clear packet;
- reliable propagation of each accepted activation to the Nag A-mode window;
- API diagnostics and native regression tests;
- dashboard panels that always start collapsed after every page load;
- a V3.0.3 BLE repair assessment with one-click JSON diagnostics download;
- V3.0.3 release metadata.

It does not attempt to recover an activation that starts and finishes while the two devices remain completely disconnected. That requires a later sender-side snapshot-on-connect improvement.

This task does not build ESP32 firmware, generate BIN files, create a version tag, or publish a GitHub Release. It ends with reviewed source changes on the V3.0.3 branch and a draft pull request.

## Considered Approaches

### 1. Minimal condition patches

Patch the existing global-state branches in `src/ble_fsd_receiver.cpp` without separating the state machine. This is the smallest diff, but the important reconnect and retry cases would remain difficult to exercise with real native tests.

### 2. Transport-independent receiver core (selected)

Move packet/session/window decisions into a small C++ core that does not depend on NimBLE or ESP-IDF. Keep BLE discovery and GATT operations in `src/ble_fsd_receiver.cpp`. Native tests can then drive the same production state machine used by the firmware.

This adds one focused component but gives deterministic regression coverage and prevents the BLE callback code from accumulating more intertwined conditions.

### 3. New BLE protocol with a boot/session identifier

Add a sender boot ID and explicit event timestamps to the packet. This provides the strongest long-term session identity, but it requires coordinated FSD and WiFi-NAG upgrades. It is deferred so V3.0.3 remains compatible with the current sender.

## Receiver Core

Introduce a transport-independent `BleFsdReceiverCore` responsible for:

- accepted sequence and source-timestamp tracking;
- remote-active state;
- retry eligibility after CAN rejection or interruption;
- active-window start, restart, clear, timeout, and cancellation;
- counters and reject reasons needed by the dashboard.

NimBLE callbacks will parse packets and pass them to the core. The periodic receiver tick will pass current CAN health and time to the core. The existing public receiver status remains the integration boundary for the dashboard.

## Session Rules

A BLE disconnect, receiver disable, or peer-MAC change starts a new receiver session. A new session clears sequence, source-timestamp, remote-active, retry, and active-window state. If an A-mode window is active, it is cancelled immediately.

This allows a sender that rebooted and restarted its sequence at 1 to reconnect without being rejected forever as `old_seq`. It also lets an active heartbeat resume output after a short reconnect during the sender's hold window.

## Packet Acceptance Rules

Packet framing, CRC, command, timestamp, and ordering validation remain mandatory.

For an active packet:

1. Check CAN health before committing the activation as consumed.
2. If CAN is unhealthy, record `can_unhealthy` and retain a retry marker for that sequence.
3. A later heartbeat with the retry-marked sequence may start the window once CAN is healthy.
4. A newer sequence always represents a new activation and starts or restarts the window, even if a clear packet for the previous sequence was lost.
5. A same-sequence active heartbeat does not extend a healthy running window.

For a clear packet:

1. A clear matching the current sequence immediately ends the receiver window.
2. The dashboard bridge immediately cancels the Nag A-mode window.
3. A clear also removes any retry marker for that sequence.

Old sequences and unrelated same-sequence packets remain rejected.

## Window Duration and Bridge

For a valid active packet, use the packet `holdMs` as the window duration, clamped to 1,000-60,000 ms. If `holdMs` is zero, use the configured `testWindowMs` as a compatibility fallback with the same clamp.

The dashboard bridge must not rely only on an `Idle -> TestActive` state edge. It will track the receiver's monotonically increasing accepted-window counter. Each increment triggers or retriggers `NagHandler::triggerAModeWindow()` with the receiver's current remaining duration. Leaving `TestActive` cancels the A-mode window.

This makes a rapid new FSD sequence observable even when the receiver was already in `TestActive`.

## Safety Behavior

Torque output remains gated by all existing conditions:

- BLE packet and peer validation;
- healthy TWAI state and error counters;
- dashboard CAN master switch;
- Nag runtime enable;
- time-bounded MODE_A activation;
- real 0x370 input frames and existing non-blocking transmit policy.

Disconnect, CAN-health loss, matching clear, receiver disable, or timeout closes the BLE-driven A-mode window. MODE_A torque encoding remains fixed at +1.80 Nm; MODE_A_V2 behavior is unchanged.

## Diagnostics

Extend `/ble_fsd` output with the receiver fields that already exist but are not currently exposed: accepted packet count, disconnect count, and remote-active state. Preserve `lastReject`, `lastSequence`, duplicate/rejected counts, window count, and connection/subscription fields.

These fields allow field diagnosis to distinguish BLE delivery failure, sequence rejection, CAN gating, and downstream CAN transmit drops.

### Dashboard Repair Assessment

Every dashboard card, configuration subsection, and the inner BLE settings panel starts collapsed on every page load. A user may expand panels for the current page session, but the dashboard does not restore an expanded state after refresh. The existing BLE auto-collapse-after-connection option remains useful within the current session.

The BLE receiver area shows a concise V3.0.3 repair assessment derived from live `/status` and `/ble_fsd` fields. It distinguishes at least: receiver disabled, BLE disconnected, GATT not subscribed, waiting for the first packet, packet rejection, unhealthy or safety-tripped CAN, CAN Write disabled, trigger received without an A-mode window, active torque window, and healthy idle operation.

The assessment is advisory and read-only. It must not change BLE, CAN, Nag, or safety state.

### One-click Diagnostic Download

The browser collects existing read-only endpoints instead of adding a large firmware-side report builder. One click downloads a UTF-8 JSON file containing:

- schema and V3.0.3 repair identifiers;
- browser collection time and UI mode;
- the human-readable assessment and machine-readable reason code;
- raw `/status`, `/ble_fsd`, `/system_status`, and `/ota_status` snapshots;
- recent `/log` lines when available;
- a list of endpoints that could not be collected.

Collection is best-effort: failure of one endpoint does not prevent download of the remaining snapshots. Common password, token, and authorization values in log text are redacted before the file is created. The report does not request WiFi configuration endpoints and therefore does not collect saved WiFi passwords.

## Tests

Add native tests against the production receiver core for:

- disconnect followed by a sender sequence restart at 1;
- reconnect during the same active sequence;
- first packet rejected by unhealthy CAN, followed by a successful same-sequence heartbeat;
- CAN becoming unhealthy during an active window and later retrying safely;
- matching clear immediately stopping an active window;
- packet `holdMs` controlling the window duration;
- a newer sequence retriggering while already active;
- duplicate heartbeat not extending a healthy window;
- older sequence rejection.

Keep the existing Nag handler, TWAI, dashboard, OTA, and Wi-Fi regression suites. Add source-contract checks only where they verify API wiring; behavioral assertions belong in the native core tests.

Add host-side JavaScript behavior tests for the repair assessment, partial snapshot collection, secret redaction, deterministic JSON schema, and filename generation. Add dashboard integration checks for the forced-collapse controls, visible assessment fields, download action, and injected diagnostics core. Regenerate the embedded minified/gzipped dashboard, but do not compile the ESP32 firmware.

## Release and Compatibility

Update `VERSION` and `CHANGELOG.md` so the source tree identifies itself as V3.0.3. No BLE wire-format change is made, so WiFi-NAG V3.0.3 remains compatible with T2CAN-FSD V1.5.3. Firmware compilation, binary packaging, tagging, and release publication are explicitly outside this task.

## Acceptance Criteria

- A subscribed, CAN-healthy receiver opens MODE_A on the first valid active packet without waiting for a 500 ms heartbeat.
- BLE reconnect and sender reboot do not require a WiFi-NAG reboot or BLE toggle.
- Temporary CAN unavailability does not permanently consume the activation.
- A matching clear stops torque output immediately.
- The default T2CAN-FSD 5,000 ms hold produces a 5,000 ms receiver/Nag window rather than 10,000 ms.
- A rapid second activation retriggers the Nag window.
- Every dashboard card, subsection, and BLE settings panel is collapsed again after a page refresh, regardless of prior expansion.
- The live repair assessment identifies the most likely BLE/CAN gate state without changing device state.
- One click downloads a parseable, redacted JSON report even if one or more optional endpoints fail.
- All relevant native and Python regression tests pass and source metadata reports V3.0.3 before the branch is proposed for merge. ESP32 firmware compilation is intentionally not performed in this task.

