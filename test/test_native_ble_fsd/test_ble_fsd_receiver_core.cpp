#include <unity.h>

#include <cstdio>

#include "ble_fsd_receiver_core.h"

namespace
{
BleFsdPacket packet(bool active, uint32_t sequence, uint32_t timestamp,
                    uint16_t holdMs = 5000)
{
    BleFsdPacket value{};
    value.command = kBleFsdCommandActive;
    value.active = active;
    value.sequence = sequence;
    value.sourceTimestampMs = timestamp;
    value.holdMs = holdMs;
    return value;
}

BleFsdReceiverCore readyCore(uint32_t fallbackWindowMs = 10000)
{
    BleFsdReceiverCore core;
    core.setFallbackWindowMs(fallbackWindowMs);
    core.resetSession(true, 0);
    return core;
}
} // namespace

void setUp() {}
void tearDown() {}

void test_disconnect_resets_sequence_for_sender_restart()
{
    BleFsdReceiverCore core = readyCore();
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(BleFsdCoreEvent::WindowStarted),
        static_cast<uint8_t>(core.onPacket(packet(true, 42, 1000), true, 100).event));

    core.resetSession(true, 200);
    const BleFsdCoreResult restarted =
        core.onPacket(packet(true, 1, 100), true, 300);
    const BleFsdCoreSnapshot status = core.snapshot(300);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(BleFsdCoreEvent::WindowStarted),
        static_cast<uint8_t>(restarted.event));
    TEST_ASSERT_EQUAL_UINT32(1, status.lastSequence);
    TEST_ASSERT_EQUAL_UINT32(2, status.testWindows);
}

void test_reconnect_accepts_same_active_sequence_in_new_session()
{
    BleFsdReceiverCore core = readyCore();
    core.onPacket(packet(true, 7, 1000), true, 100);
    core.resetSession(true, 200);

    const BleFsdCoreResult result =
        core.onPacket(packet(true, 7, 1500), true, 300);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(BleFsdCoreEvent::WindowStarted),
        static_cast<uint8_t>(result.event));
    TEST_ASSERT_EQUAL_UINT32(2, core.snapshot(300).testWindows);
}

void test_can_unhealthy_packet_retries_on_same_sequence_heartbeat()
{
    BleFsdReceiverCore core = readyCore();

    const BleFsdCoreResult rejected =
        core.onPacket(packet(true, 7, 1000), false, 100);
    const BleFsdCoreSnapshot rejectedStatus = core.snapshot(100);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(BleFsdRejectReason::CanUnhealthy),
        static_cast<uint8_t>(rejected.reject));
    TEST_ASSERT_EQUAL_UINT32(0, rejectedStatus.acceptedPackets);
    TEST_ASSERT_EQUAL_UINT32(0, rejectedStatus.testWindows);
    TEST_ASSERT_FALSE(rejectedStatus.remoteActive);

    const BleFsdCoreResult recovered =
        core.onPacket(packet(true, 7, 1500), true, 600);
    const BleFsdCoreSnapshot recoveredStatus = core.snapshot(600);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(BleFsdCoreEvent::WindowStarted),
        static_cast<uint8_t>(recovered.event));
    TEST_ASSERT_EQUAL_UINT32(1, recoveredStatus.acceptedPackets);
    TEST_ASSERT_EQUAL_UINT32(1, recoveredStatus.testWindows);
    TEST_ASSERT_TRUE(recoveredStatus.remoteActive);
}

void test_can_loss_stops_window_and_allows_same_sequence_retry()
{
    BleFsdReceiverCore core = readyCore();
    core.onPacket(packet(true, 7, 1000), true, 100);

    const BleFsdCoreResult stopped = core.tick(false, 200);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(BleFsdCoreEvent::WindowStopped),
        static_cast<uint8_t>(stopped.event));
    TEST_ASSERT_FALSE(core.snapshot(200).remoteActive);

    const BleFsdCoreResult recovered =
        core.onPacket(packet(true, 7, 1500), true, 600);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(BleFsdCoreEvent::WindowStarted),
        static_cast<uint8_t>(recovered.event));
    TEST_ASSERT_EQUAL_UINT32(2, core.snapshot(600).testWindows);
}

void test_matching_clear_stops_window_immediately()
{
    BleFsdReceiverCore core = readyCore();
    core.onPacket(packet(true, 7, 1000), true, 100);

    const BleFsdCoreResult cleared =
        core.onPacket(packet(false, 7, 1500), true, 200);
    const BleFsdCoreSnapshot status = core.snapshot(200);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(BleFsdCoreEvent::WindowStopped),
        static_cast<uint8_t>(cleared.event));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(BleFsdReceiverState::Idle),
        static_cast<uint8_t>(status.state));
    TEST_ASSERT_FALSE(status.remoteActive);
    TEST_ASSERT_EQUAL_UINT32(0, status.testRemainingMs);
}

void test_sender_hold_ms_controls_window_duration()
{
    BleFsdReceiverCore core = readyCore(10000);

    const BleFsdCoreResult result =
        core.onPacket(packet(true, 7, 1000, 5000), true, 2000);

    TEST_ASSERT_EQUAL_UINT32(5000, result.windowMs);
    TEST_ASSERT_EQUAL_UINT32(5000, core.snapshot(2000).testRemainingMs);
    TEST_ASSERT_EQUAL_UINT32(2500, core.snapshot(4500).testRemainingMs);
}

void test_zero_hold_uses_configured_fallback()
{
    BleFsdReceiverCore core = readyCore(7000);

    const BleFsdCoreResult result =
        core.onPacket(packet(true, 7, 1000, 0), true, 2000);

    TEST_ASSERT_EQUAL_UINT32(7000, result.windowMs);
    TEST_ASSERT_EQUAL_UINT32(7000, core.snapshot(2000).testRemainingMs);
}

void test_newer_sequence_retriggers_while_already_active()
{
    BleFsdReceiverCore core = readyCore();
    core.onPacket(packet(true, 7, 1000, 5000), true, 100);

    const BleFsdCoreResult second =
        core.onPacket(packet(true, 8, 1500, 4000), true, 600);
    const BleFsdCoreSnapshot status = core.snapshot(600);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(BleFsdCoreEvent::WindowStarted),
        static_cast<uint8_t>(second.event));
    TEST_ASSERT_EQUAL_UINT32(4000, second.windowMs);
    TEST_ASSERT_EQUAL_UINT32(2, status.testWindows);
    TEST_ASSERT_EQUAL_UINT32(8, status.lastSequence);
}

void test_duplicate_heartbeat_does_not_extend_window()
{
    BleFsdReceiverCore core = readyCore();
    core.onPacket(packet(true, 7, 1000, 5000), true, 100);

    const BleFsdCoreResult heartbeat =
        core.onPacket(packet(true, 7, 1500, 5000), true, 600);
    const BleFsdCoreSnapshot status = core.snapshot(600);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(BleFsdCoreEvent::None),
        static_cast<uint8_t>(heartbeat.event));
    TEST_ASSERT_EQUAL_UINT32(4500, status.testRemainingMs);
    TEST_ASSERT_EQUAL_UINT32(1, status.testWindows);
    TEST_ASSERT_EQUAL_UINT32(1, status.duplicateCount);
}

void test_older_sequence_is_rejected()
{
    BleFsdReceiverCore core = readyCore();
    core.onPacket(packet(true, 7, 1000), true, 100);

    const BleFsdCoreResult result =
        core.onPacket(packet(true, 6, 1500), true, 200);

    TEST_ASSERT_EQUAL_UINT8(
        static_cast<uint8_t>(BleFsdRejectReason::OldSequence),
        static_cast<uint8_t>(result.reject));
    TEST_ASSERT_EQUAL_UINT32(1, core.snapshot(200).testWindows);
}

void test_bridge_retriggers_on_window_generation_change()
{
    BleFsdWindowBridgeCore bridge;
    BleFsdReceiverStatus status{};
    status.state = BleFsdReceiverState::TestActive;
    status.testRemainingMs = 5000;
    status.testWindows = 1;

    const BleFsdBridgeDecision first = bridge.update(status, true);
    const BleFsdBridgeDecision duplicate = bridge.update(status, true);
    status.testWindows = 2;
    status.testRemainingMs = 4000;
    const BleFsdBridgeDecision retrigger = bridge.update(status, true);

    TEST_ASSERT_TRUE(first.trigger);
    TEST_ASSERT_FALSE(duplicate.trigger);
    TEST_ASSERT_TRUE(retrigger.trigger);
    TEST_ASSERT_EQUAL_UINT32(4000, retrigger.windowMs);
}

void test_bridge_waits_for_output_gate_then_triggers_remaining_window()
{
    BleFsdWindowBridgeCore bridge;
    BleFsdReceiverStatus status{};
    status.state = BleFsdReceiverState::TestActive;
    status.testRemainingMs = 5000;
    status.testWindows = 1;

    const BleFsdBridgeDecision gated = bridge.update(status, false);
    status.testRemainingMs = 4500;
    const BleFsdBridgeDecision enabled = bridge.update(status, true);

    TEST_ASSERT_FALSE(gated.trigger);
    TEST_ASSERT_TRUE(enabled.trigger);
    TEST_ASSERT_EQUAL_UINT32(4500, enabled.windowMs);
}

void test_bridge_cancels_when_receiver_leaves_active_state()
{
    BleFsdWindowBridgeCore bridge;
    BleFsdReceiverStatus status{};
    status.state = BleFsdReceiverState::TestActive;
    status.testRemainingMs = 5000;
    status.testWindows = 1;
    bridge.update(status, true);

    status.state = BleFsdReceiverState::Idle;
    status.testRemainingMs = 0;
    const BleFsdBridgeDecision stopped = bridge.update(status, true);

    TEST_ASSERT_FALSE(stopped.trigger);
    TEST_ASSERT_TRUE(stopped.cancel);
}

void test_core_snapshot_copy_preserves_link_fields()
{
    BleFsdReceiverCore core = readyCore();
    core.onPacket(packet(true, 9, 1200, 5000), true, 100);

    BleFsdReceiverStatus status{};
    status.initialized = true;
    status.connected = true;
    status.subscribed = true;
    status.rssi = -42;
    status.peerAddressType = 1;
    std::snprintf(status.peerMac, sizeof(status.peerMac), "%s",
                  "AA:BB:CC:DD:EE:FF");
    std::snprintf(status.peerName, sizeof(status.peerName), "%s", "FSD sender");

    bleFsdApplyCoreSnapshot(core.snapshot(100), status);

    TEST_ASSERT_TRUE(status.initialized);
    TEST_ASSERT_TRUE(status.connected);
    TEST_ASSERT_TRUE(status.subscribed);
    TEST_ASSERT_EQUAL_INT8(-42, status.rssi);
    TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", status.peerMac);
    TEST_ASSERT_EQUAL_STRING("FSD sender", status.peerName);
    TEST_ASSERT_EQUAL_UINT32(9, status.lastSequence);
    TEST_ASSERT_EQUAL_UINT32(5000, status.testRemainingMs);
    TEST_ASSERT_EQUAL_UINT32(1, status.testWindows);
    TEST_ASSERT_TRUE(status.remoteActive);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_disconnect_resets_sequence_for_sender_restart);
    RUN_TEST(test_reconnect_accepts_same_active_sequence_in_new_session);
    RUN_TEST(test_can_unhealthy_packet_retries_on_same_sequence_heartbeat);
    RUN_TEST(test_can_loss_stops_window_and_allows_same_sequence_retry);
    RUN_TEST(test_matching_clear_stops_window_immediately);
    RUN_TEST(test_sender_hold_ms_controls_window_duration);
    RUN_TEST(test_zero_hold_uses_configured_fallback);
    RUN_TEST(test_newer_sequence_retriggers_while_already_active);
    RUN_TEST(test_duplicate_heartbeat_does_not_extend_window);
    RUN_TEST(test_older_sequence_is_rejected);
    RUN_TEST(test_bridge_retriggers_on_window_generation_change);
    RUN_TEST(test_bridge_waits_for_output_gate_then_triggers_remaining_window);
    RUN_TEST(test_bridge_cancels_when_receiver_leaves_active_state);
    RUN_TEST(test_core_snapshot_copy_preserves_link_fields);
    return UNITY_END();
}
