(function (root, factory) {
  'use strict';
  var api = factory();
  if (typeof module === 'object' && module.exports) {
    module.exports = api;
  }
  if (root) {
    root.BleFsdDiagnostics = api;
  }
}(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  var SCHEMA = 'wifi-nag-ble-fsd-diagnostics/v1';
  var RELEASE = 'V3.0.3';
  var SENSITIVE_KEY = /(?:password|passwd|passphrase|wifi[_-]?pass|token|authorization|secret|api[_-]?key|apikey|(?:^|[_-])pass(?:$|[_-])|(?:^|[_-])pwd(?:$|[_-]))/i;

  function firstDefined(primary, primaryName, fallback, fallbackName, defaultValue) {
    if (primary && primary[primaryName] !== undefined) {
      return primary[primaryName];
    }
    if (fallback && fallback[fallbackName] !== undefined) {
      return fallback[fallbackName];
    }
    return defaultValue;
  }

  function result(level, code, headline, detail) {
    return {
      level: level,
      code: code,
      headline: headline,
      detail: detail,
    };
  }

  function assess(status, ble) {
    status = status || {};
    ble = ble || {};

    var enabled = !!firstDefined(ble, 'enabled', status, 'bleRxEnabled', false);
    var connected = !!firstDefined(ble, 'connected', status, 'bleRxConnected', false);
    var subscribed = !!firstDefined(ble, 'subscribed', status, 'bleRxSubscribed', false);
    var state = String(firstDefined(ble, 'state', status, 'bleRxState', 'DISABLED') || 'DISABLED');
    var lastPacketAtMs = Number(firstDefined(ble, 'lastPacketAtMs', status, 'bleRxLastPacketAtMs', 0) || 0);
    var acceptedPackets = Number(firstDefined(ble, 'acceptedPackets', status, 'bleRxAcceptedPackets', 0) || 0);
    var rejectedPackets = Number(firstDefined(ble, 'rejected', status, 'bleRxRejected', 0) || 0);
    var lastReject = String(firstDefined(ble, 'lastReject', status, 'bleRxReject', 'none') || 'none');
    var canWrite = status.ci;
    var canOnline = status.can;
    var twaiAvailable = status.twaiAvailable;
    var twaiState = String(status.twaiState || '');

    if (!enabled) {
      return result('warning', 'RECEIVER_DISABLED', 'BLE FSD receiver is disabled', 'Enable the configured BLE receiver before checking trigger delivery.');
    }
    if (!connected) {
      return result('warning', 'BLE_DISCONNECTED', 'BLE device is not connected', 'Check peer MAC, distance, RSSI threshold, and whether the FSD sender is advertising.');
    }
    if (!subscribed) {
      return result('warning', 'GATT_NOT_SUBSCRIBED', 'BLE connected but notifications are not subscribed', 'The link exists, but FSD_ACTIVE notifications cannot reach WiFi-NAG yet.');
    }
    if (status.twaiSafetyTripped === true) {
      return result('error', 'CAN_SAFETY_TRIPPED', 'CAN safety protection is active', 'Safety reason: ' + String(status.twaiSafetyReason || 'unknown') + '.');
    }
    if (twaiAvailable === false || (twaiState && twaiState !== 'RUNNING')) {
      return result('error', 'CAN_UNHEALTHY', 'CAN driver is not healthy', 'TWAI state: ' + (twaiState || 'UNAVAILABLE') + '.');
    }
    if (canOnline === false && lastPacketAtMs > 0) {
      return result('warning', 'CAN_BUS_OFFLINE', 'BLE data arrived but the CAN bus is offline', 'Check CAN wiring, vehicle wake state, and live 0x370 traffic.');
    }
    if (lastReject !== 'none') {
      if (lastReject === 'can_unhealthy') {
        return result('warning', 'CAN_UNHEALTHY', 'The latest BLE trigger was held for CAN recovery', 'The same active sequence may retry when CAN becomes healthy.');
      }
      return result('warning', 'PACKET_REJECTED', 'The latest BLE packet was rejected', 'Reject reason: ' + lastReject + '; rejected packets: ' + rejectedPackets + '.');
    }
    if (lastPacketAtMs === 0 && acceptedPackets === 0) {
      return result('waiting', 'WAITING_FOR_PACKET', 'BLE link is ready and waiting for FSD data', 'Activate FSD once, then download diagnostics if the counters do not change.');
    }
    if (state === 'TEST_ACTIVE') {
      if (canWrite === false) {
        return result('warning', 'CAN_WRITE_DISABLED', 'BLE trigger accepted but CAN Write is off', 'Turn on CAN Write only when it is safe to allow the time-limited A-mode output.');
      }
      if (status.nagAModeActive === false) {
        return result('error', 'A_MODE_NOT_ACTIVE', 'Receiver window is active but A mode did not start', 'This points to the BLE-to-Nag bridge or a downstream runtime gate.');
      }
      if (status.nagAModeActive === true) {
        return result('ok', 'WINDOW_ACTIVE', 'BLE trigger and A-mode torque window are active', 'Remaining A-mode time: ' + Number(status.nagAModeRemainingMs || 0) + ' ms.');
      }
      return result('ok', 'TRIGGER_ACCEPTED', 'BLE trigger window is active', 'The receiver accepted the current FSD activation.');
    }
    return result('ok', 'HEALTHY_IDLE', 'BLE repair path is healthy and idle', 'Accepted packets: ' + acceptedPackets + '; waiting for the next activation.');
  }

  function redactText(value) {
    return String(value)
      .replace(/\b(authorization)\s*:\s*(?:bearer\s+)?[^\s,;]+/gi, '$1: [REDACTED]')
      .replace(/\b(password|passwd|passphrase|wifi[_-]?pass|token|secret|api[_-]?key|apikey|pwd)\s*([=:])\s*(?:"[^"]*"|'[^']*'|[^\s,;]+)/gi, '$1$2[REDACTED]');
  }

  function redact(value, key) {
    if (key && SENSITIVE_KEY.test(String(key))) {
      return '[REDACTED]';
    }
    if (Array.isArray(value)) {
      return value.map(function (item) { return redact(item, ''); });
    }
    if (value && typeof value === 'object') {
      var output = {};
      Object.keys(value).forEach(function (name) {
        output[name] = redact(value[name], name);
      });
      return output;
    }
    if (typeof value === 'string') {
      return redactText(value);
    }
    return value;
  }

  function buildBundle(snapshots, failures, context) {
    snapshots = snapshots || {};
    failures = failures || [];
    context = context || {};
    var collectedAt = String(context.collectedAt || new Date().toISOString());

    return {
      schema: SCHEMA,
      repair: {
        release: RELEASE,
        focus: 'BLE FSD reconnect, CAN retry, clear, hold duration, and rapid retrigger',
        expectedBehaviors: [
          'session_reset_on_disconnect',
          'same_sequence_retry_after_can_recovery',
          'sender_hold_duration',
          'matching_clear_stops_window',
          'new_sequence_retriggers_active_window',
        ],
      },
      collectedAt: collectedAt,
      context: redact({
        uiMode: String(context.uiMode || 'unknown'),
        language: String(context.language || 'unknown'),
      }),
      assessment: assess(snapshots.status || {}, snapshots.bleFsd || {}),
      snapshots: redact(snapshots),
      collectionFailures: redact(failures),
    };
  }

  function fileName(isoTime) {
    var value = String(isoTime || new Date().toISOString());
    var match = value.match(/^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})/);
    var stamp = match
      ? match.slice(1).join('')
      : String(Date.now());
    if (match) {
      stamp = match[1] + match[2] + match[3] + '-' + match[4] + match[5] + match[6];
    }
    return 'wifi-nag-v3.0.3-ble-diagnostics-' + stamp + '.json';
  }

  return {
    schema: SCHEMA,
    release: RELEASE,
    assess: assess,
    buildBundle: buildBundle,
    redact: redact,
    fileName: fileName,
  };
}));
