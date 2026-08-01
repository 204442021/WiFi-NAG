'use strict';

const assert = require('node:assert/strict');
const path = require('node:path');

const diagnostics = require(path.join(
  __dirname,
  '..',
  'include',
  'web',
  'ble_fsd_diagnostics.js'
));

let passed = 0;

function test(name, fn) {
  try {
    fn();
    passed += 1;
  } catch (error) {
    error.message = `${name}: ${error.message}`;
    throw error;
  }
}

function healthyStatus(overrides = {}) {
  return Object.assign({
    ci: true,
    can: true,
    twaiAvailable: true,
    twaiState: 'RUNNING',
    twaiSafetyTripped: false,
    nagAModeActive: false,
    nagAModeRemainingMs: 0,
  }, overrides);
}

function healthyBle(overrides = {}) {
  return Object.assign({
    enabled: true,
    connected: true,
    subscribed: true,
    state: 'IDLE',
    lastPacketAtMs: 1000,
    lastReject: 'none',
    acceptedPackets: 1,
    rejected: 0,
    remoteActive: false,
  }, overrides);
}

test('reports a disconnected receiver before packet or CAN conclusions', () => {
  const result = diagnostics.assess(healthyStatus(), healthyBle({connected: false}));
  assert.equal(result.code, 'BLE_DISCONNECTED');
  assert.equal(result.level, 'warning');
});

test('reports a missing GATT subscription on an established link', () => {
  const result = diagnostics.assess(healthyStatus(), healthyBle({subscribed: false}));
  assert.equal(result.code, 'GATT_NOT_SUBSCRIBED');
});

test('reports the CAN safety gate before CAN write state', () => {
  const result = diagnostics.assess(
    healthyStatus({ci: false, twaiSafetyTripped: true, twaiSafetyReason: 'BUS_OFF'}),
    healthyBle()
  );
  assert.equal(result.code, 'CAN_SAFETY_TRIPPED');
  assert.match(result.detail, /BUS_OFF/);
});

test('reports waiting for the first BLE packet without treating it as a failure', () => {
  const result = diagnostics.assess(
    healthyStatus(),
    healthyBle({lastPacketAtMs: 0, acceptedPackets: 0})
  );
  assert.equal(result.code, 'WAITING_FOR_PACKET');
  assert.equal(result.level, 'waiting');
});

test('reports the concrete packet rejection reason', () => {
  const result = diagnostics.assess(
    healthyStatus(),
    healthyBle({lastReject: 'old_seq', rejected: 3, acceptedPackets: 0})
  );
  assert.equal(result.code, 'PACKET_REJECTED');
  assert.match(result.detail, /old_seq/);
});

test('reports an accepted trigger blocked by the CAN write switch', () => {
  const result = diagnostics.assess(
    healthyStatus({ci: false}),
    healthyBle({state: 'TEST_ACTIVE', remoteActive: true})
  );
  assert.equal(result.code, 'CAN_WRITE_DISABLED');
});

test('reports a bridge problem when the receiver window is active but A mode is not', () => {
  const result = diagnostics.assess(
    healthyStatus({ci: true, nagAModeActive: false}),
    healthyBle({state: 'TEST_ACTIVE', remoteActive: true})
  );
  assert.equal(result.code, 'A_MODE_NOT_ACTIVE');
});

test('reports an active repaired torque window', () => {
  const result = diagnostics.assess(
    healthyStatus({nagAModeActive: true, nagAModeRemainingMs: 4200}),
    healthyBle({state: 'TEST_ACTIVE', remainingMs: 4300, remoteActive: true})
  );
  assert.equal(result.code, 'WINDOW_ACTIVE');
  assert.equal(result.level, 'ok');
});

test('reports healthy idle after accepted packets', () => {
  const result = diagnostics.assess(healthyStatus(), healthyBle({acceptedPackets: 7}));
  assert.equal(result.code, 'HEALTHY_IDLE');
  assert.equal(result.level, 'ok');
});

test('builds a versioned partial report and recursively redacts secrets', () => {
  const snapshots = {
    status: healthyStatus(),
    bleFsd: healthyBle(),
    system: {firmware: 'V3.0.3', password: 'do-not-keep'},
    logs: {
      lines: [
        '[CFG] password=hunter2',
        'Authorization: Bearer abc123',
        'normal diagnostic line',
      ],
    },
  };
  const failures = [
    {endpoint: '/ota_status', error: 'timeout token=private-token'},
  ];
  const report = diagnostics.buildBundle(snapshots, failures, {
    collectedAt: '2026-08-01T08:09:10.000Z',
    uiMode: 'phone',
    language: 'zh',
  });

  assert.equal(report.schema, 'wifi-nag-ble-fsd-diagnostics/v1');
  assert.equal(report.repair.release, 'V3.0.3');
  assert.equal(report.assessment.code, 'HEALTHY_IDLE');
  assert.equal(report.snapshots.system.password, '[REDACTED]');
  assert.equal(report.collectionFailures.length, 1);
  assert.match(report.snapshots.logs.lines[0], /password=\[REDACTED\]/i);
  assert.match(report.snapshots.logs.lines[1], /Authorization: \[REDACTED\]/i);
  assert.match(report.collectionFailures[0].error, /token=\[REDACTED\]/i);
  assert.equal(report.snapshots.logs.lines[2], 'normal diagnostic line');

  const serialized = JSON.stringify(report);
  for (const secret of ['do-not-keep', 'hunter2', 'abc123', 'private-token']) {
    assert.equal(serialized.includes(secret), false, `report leaked ${secret}`);
  }
});

test('creates a deterministic JSON filename from the collection time', () => {
  assert.equal(
    diagnostics.fileName('2026-08-01T08:09:10.000Z'),
    'wifi-nag-v3.0.3-ble-diagnostics-20260801-080910.json'
  );
});

console.log(`BLE FSD diagnostics UI: ${passed} tests passed`);
