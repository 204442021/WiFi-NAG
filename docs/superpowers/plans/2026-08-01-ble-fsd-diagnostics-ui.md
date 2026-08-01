# BLE FSD Diagnostics UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Force all dashboard feature panels closed on every page load and add a read-only V3.0.3 BLE repair assessment with one-click, redacted JSON download.

**Architecture:** Keep device endpoints unchanged and collect their existing read-only JSON responses in the browser. Put assessment, redaction, report assembly, and filename logic in a small browser/CommonJS JavaScript module so Node tests exercise the same production logic embedded into the dashboard. Keep panel collapse initialization in the existing dashboard script, but remove persistence so each new page starts closed.

**Tech Stack:** Embedded HTML/CSS/JavaScript, Python dashboard generator, Node.js behavior tests, Python unittest integration tests.

## Global Constraints

- Work only on WiFi-NAG branch `V3.0.3`.
- Do not compile ESP32 firmware, create BIN files, tag a release, or publish a release.
- Preserve all BLE packet, UUID, torque, and CAN safety behavior.
- Diagnostic collection is read-only and must not request WiFi credential endpoints.
- A failed optional endpoint must not block report download.

---

### Task 1: Host-tested diagnostics core

**Files:**
- Create: `include/web/ble_fsd_diagnostics.js`
- Create: `test/test_ble_fsd_diagnostics_ui.js`
- Create: `test/test_ble_fsd_diagnostics_runtime.py`

**Interfaces:**
- Produces: global/CommonJS `BleFsdDiagnostics` with `assess(status, ble)`, `buildBundle(snapshots, failures, context)`, `redact(value)`, and `fileName(isoTime)`.
- Consumes: raw JSON snapshots from `/status`, `/ble_fsd`, `/system_status`, `/ota_status`, and `/log`.

- [ ] **Step 1: Write failing Node behavior tests**

Cover disconnected, unsubscribed, CAN-safety, CAN-write-off, active-window, healthy-idle, partial collection, recursive secret redaction, and a deterministic `.json` filename.

- [ ] **Step 2: Verify the tests fail because the module is absent**

Run: `node test/test_ble_fsd_diagnostics_ui.js`

Expected: non-zero exit with module-not-found for `include/web/ble_fsd_diagnostics.js`.

- [ ] **Step 3: Implement the minimal diagnostics module**

Return assessment objects shaped as `{level, code, headline, detail}`. Return report objects with schema `wifi-nag-ble-fsd-diagnostics/v1`, repair release `V3.0.3`, assessment, snapshots, collection failures, and UI context. Redact keys and log assignments containing password, pass, token, authorization, or secret.

- [ ] **Step 4: Verify Node and Python wrapper tests pass**

Run:

```powershell
node test/test_ble_fsd_diagnostics_ui.js
python -m unittest test.test_ble_fsd_diagnostics_runtime -v
```

Expected: all diagnostics behavior tests pass.

### Task 2: Forced collapse and dashboard integration

**Files:**
- Modify: `include/web/mcp2515_dashboard_ui.src.h`
- Modify: `include/web/mcp2515_dashboard.h`
- Modify: `scripts/minify_dashboard.py`
- Modify: `test/test_ble_connected_device_status.py`

**Interfaces:**
- Consumes: `BleFsdDiagnostics` injected at `/*__BLE_FSD_DIAGNOSTICS_CORE__*/` by the dashboard generator.
- Produces: `downloadBleFsdDiagnostics()`, live repair summary elements, and extra `/status` BLE counters.

- [ ] **Step 1: Write failing integration checks**

Require card/subsection initialization to ignore stored expansion state, BLE controls to start collapsed, repair summary/download element IDs, five read-only diagnostic endpoint paths, the diagnostics injection marker, and `/status` fields for accepted packets, rejected packets, disconnects, remote-active state, and last-packet time.

- [ ] **Step 2: Verify the focused Python test fails for missing UI behavior**

Run: `python -m unittest test.test_ble_connected_device_status -v`

Expected: failures naming forced-collapse and repair-download contracts.

- [ ] **Step 3: Implement forced collapse**

Initialize every `.card` and `.subsec` with `collapsed=true` on every load and stop reading/writing their collapse state in `localStorage`. Initialize the inner BLE controls closed on every load while preserving manual expand/collapse for the current page session.

- [ ] **Step 4: Implement live assessment and download**

Add repair summary, detail, counters, and download controls below the BLE receiver status. On click, collect the five read-only endpoints independently, build a redacted report, create a JSON Blob, download it, and report partial collection failures next to the button.

- [ ] **Step 5: Extend `/status` diagnostics**

Expose `bleRxLastPacketAtMs`, `bleRxRejected`, `bleRxAcceptedPackets`, `bleRxDisconnects`, and `bleRxRemoteActive` so the live assessment updates through the existing status poll.

- [ ] **Step 6: Verify focused tests pass**

Run:

```powershell
node test/test_ble_fsd_diagnostics_ui.js
python -m unittest test.test_ble_connected_device_status -v
```

Expected: all focused tests pass.

### Task 3: Generate embedded UI and complete host verification

**Files:**
- Modify: `include/web/mcp2515_dashboard_ui.h` (generated)
- Modify: `CHANGELOG.md`

**Interfaces:**
- Consumes: source dashboard and diagnostics module.
- Produces: embedded minified/gzipped dashboard used by a later PlatformIO firmware build.

- [ ] **Step 1: Document the UI diagnostics addition**

Add V3.0.3 changelog bullets for forced collapse, live repair assessment, one-click JSON download, partial collection, and redaction.

- [ ] **Step 2: Regenerate the embedded dashboard**

Run: `python scripts/minify_dashboard.py`

Expected: successful minified and gzip size output; no PlatformIO firmware command is run.

- [ ] **Step 3: Run all allowed host-side verification**

Run the four native PlatformIO test environments, Python unittest discovery, Node diagnostics tests, release metadata check, generated-dashboard freshness comparison, and `git diff --check`.

- [ ] **Step 4: Commit and push the verified source**

Commit only source, generated dashboard, tests, and documentation to `V3.0.3`, push without force, and confirm draft PR #7 points at the new head. Do not merge or publish.
