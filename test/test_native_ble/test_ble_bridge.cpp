#include <unity.h>

#include "ble/bridge_client.h"
#include "ble/bridge_protocol.h"
#include "ble/obstacle_transport.h"
#include "ble/brake_state.h"
#include "can_frame_types.h"
#include "nag_state_controller.h"
#include "obstacle_can_snapshot.h"

static bool appliedEnabled = false;
static bool runtimeEnabled = false;
static uint32_t applyCount = 0;

static NagResultCode testApply(bool desired, bool, NagChangeSource)
{
    appliedEnabled = desired;
    runtimeEnabled = desired;
    ++applyCount;
    return NAG_RESULT_OK;
}

static bool testRuntime()
{
    return runtimeEnabled;
}

void setUp()
{
    appliedEnabled = false;
    runtimeEnabled = false;
    applyCount = 0;
    obstacleCanSnapshot.reset();
}

void tearDown() {}

void test_ble_packet_crc_round_trip()
{
    BleBridgeProtocol::Packet packet = BleBridgeProtocol::makePacket(
        BleBridgeProtocol::MSG_OBSTACLE_STATE, 0x5A, 0x11223344);
    for (uint8_t i = 0; i < BleBridgeProtocol::kPayloadSize; ++i)
        BleBridgeProtocol::payload(packet)[i] = static_cast<uint8_t>(i * 7U);
    BleBridgeProtocol::finalize(packet);

    TEST_ASSERT_EQUAL_UINT8(BleBridgeProtocol::DECODE_OK,
                            BleBridgeProtocol::validate(packet.bytes,
                                                        BleBridgeProtocol::kPacketSize));
    TEST_ASSERT_EQUAL_HEX32(0x11223344,
                            BleBridgeProtocol::readLe32(packet.bytes + 4));
}

void test_ble_packet_rejects_crc_corruption()
{
    BleBridgeProtocol::Packet packet = BleBridgeProtocol::makePacket(
        BleBridgeProtocol::MSG_NAG_STATE, 0, 3);
    BleBridgeProtocol::finalize(packet);
    packet.bytes[9] ^= 0x80;

    TEST_ASSERT_EQUAL_UINT8(BleBridgeProtocol::DECODE_BAD_CRC,
                            BleBridgeProtocol::validate(packet.bytes,
                                                        BleBridgeProtocol::kPacketSize));
}

void test_unbind_packets_preserve_transaction_and_identity()
{
    BleBridgeProtocol::Packet request = BleBridgeProtocol::makePacket(
        BleBridgeProtocol::MSG_UNBIND_REQUEST, 0, 7);
    BleBridgeProtocol::encodeUnbindPayload(request, 0x11223344, 0x55667788, 0);
    BleBridgeProtocol::finalize(request);

    BleBridgeProtocol::Packet decodedRequest;
    TEST_ASSERT_TRUE(BleBridgeProtocol::copyPacket(
        decodedRequest, request.bytes, BleBridgeProtocol::kPacketSize));
    BleBridgeProtocol::UnbindPayload requestPayload;
    TEST_ASSERT_TRUE(BleBridgeProtocol::decodeUnbindPayload(
        decodedRequest, requestPayload));
    TEST_ASSERT_EQUAL_HEX32(0x11223344, requestPayload.transactionId);
    TEST_ASSERT_EQUAL_HEX32(0x55667788, requestPayload.deviceId);
    TEST_ASSERT_EQUAL_UINT8(0, requestPayload.result);

    BleBridgeProtocol::Packet ack = BleBridgeProtocol::makePacket(
        BleBridgeProtocol::MSG_UNBIND_ACK, 0, 8);
    BleBridgeProtocol::encodeUnbindPayload(ack, 0x11223344, 0x99AABBCC, 1);
    BleBridgeProtocol::finalize(ack);
    BleBridgeProtocol::UnbindPayload ackPayload;
    TEST_ASSERT_TRUE(BleBridgeProtocol::decodeUnbindPayload(ack, ackPayload));
    TEST_ASSERT_EQUAL_HEX32(0x11223344, ackPayload.transactionId);
    TEST_ASSERT_EQUAL_HEX32(0x99AABBCC, ackPayload.deviceId);
    TEST_ASSERT_EQUAL_UINT8(1, ackPayload.result);
    TEST_ASSERT_TRUE(BleBridgeProtocol::isKnownMessageType(
        BleBridgeProtocol::MSG_UNBIND_REQUEST));
    TEST_ASSERT_TRUE(BleBridgeProtocol::isKnownMessageType(
        BleBridgeProtocol::MSG_UNBIND_ACK));
}

void test_unbind_packet_rejects_zero_identity_and_wrong_result()
{
    BleBridgeProtocol::Packet packet = BleBridgeProtocol::makePacket(
        BleBridgeProtocol::MSG_UNBIND_REQUEST, 0, 9);
    BleBridgeProtocol::UnbindPayload payload;

    BleBridgeProtocol::encodeUnbindPayload(packet, 0, 0x1234, 0);
    TEST_ASSERT_FALSE(BleBridgeProtocol::decodeUnbindPayload(packet, payload));
    BleBridgeProtocol::encodeUnbindPayload(packet, 1, 0, 0);
    TEST_ASSERT_FALSE(BleBridgeProtocol::decodeUnbindPayload(packet, payload));
    BleBridgeProtocol::encodeUnbindPayload(packet, 1, 0x1234, 1);
    TEST_ASSERT_FALSE(BleBridgeProtocol::decodeUnbindPayload(packet, payload));
}

void test_brake_state_contract_and_capabilities()
{
    TEST_ASSERT_TRUE(BleBridgeProtocol::isKnownMessageType(0x12));
    TEST_ASSERT_EQUAL_HEX8(0x0F, BleBridgeProtocol::kRequiredCapabilities);
    TEST_ASSERT_EQUAL_HEX8(0x3F, BleBridgeProtocol::kAdvertisedCapabilities);
    TEST_ASSERT_EQUAL_HEX8(0x10, BleBridgeProtocol::CAPABILITY_BRAKE_STATE);
    TEST_ASSERT_EQUAL_HEX8(0x20,
                           BleBridgeProtocol::CAPABILITY_OBSTACLE_TRANSPORT_CONTROL);
    TEST_ASSERT_TRUE(BleBridgeProtocol::isKnownMessageType(0x13));
}

void test_obstacle_transport_payload_and_watchdog()
{
    const uint8_t paused[10] = {1, 3, 4, 1, 7, 0, 0, 0, 0, 0};
    ObstacleTransportController controller;
    controller.beginSession(true, 100);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_TRANSPORT_CHANGED,
                            controller.ingest(paused, 200));
    ObstacleTransportView view = controller.view(200);
    TEST_ASSERT_TRUE(view.supported);
    TEST_ASSERT_TRUE(view.valid);
    TEST_ASSERT_TRUE(view.paused);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_TRANSPORT_GEAR_D, view.realGear);
    TEST_ASSERT_EQUAL_UINT32(7, view.stateGeneration);

    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_TRANSPORT_NO_CHANGE,
                            controller.service(3199));
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_TRANSPORT_TIMED_OUT,
                            controller.service(3200));
    view = controller.view(3200);
    TEST_ASSERT_FALSE(view.valid);
    TEST_ASSERT_FALSE(view.paused);
    TEST_ASSERT_EQUAL_UINT32(1, view.timeoutCount);
}

void test_obstacle_transport_resume_requires_strict_payload()
{
    ObstacleTransportController controller;
    controller.beginSession(true, 10);
    uint8_t paused[10] = {1, 3, 2, 2, 1, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_TRANSPORT_CHANGED,
                            controller.ingest(paused, 20));

    uint8_t resumed[10] = {1, 2, 1, 0, 2, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_TRANSPORT_CHANGED,
                            controller.ingest(resumed, 30));
    TEST_ASSERT_FALSE(controller.view(30).paused);

    resumed[3] = 1;
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_TRANSPORT_REJECTED,
                            controller.ingest(resumed, 40));
    TEST_ASSERT_EQUAL_UINT32(1, controller.view(40).badPayloadCount);
}

void test_brake_state_active_payload_decodes()
{
    const uint8_t payload[10] = {0x01, 0xFD, 0xDC, 0x37, 0x01,
                                 0x00, 0x96, 0x00, 0x00, 0x00};
    BrakeStateData state;
    TEST_ASSERT_TRUE(decodeBrakeStatePayload(payload, state));
    TEST_ASSERT_TRUE(state.brakePressed);
    TEST_ASSERT_FALSE(state.releaseConfirmed);
    TEST_ASSERT_EQUAL_UINT8(BRAKE_GEAR_D, state.realGear);
    TEST_ASSERT_TRUE(state.stationaryConfirmed);
    TEST_ASSERT_EQUAL_UINT16(150, state.brakeHoldMs);
}

void test_brake_state_release_payload_decodes()
{
    const uint8_t payload[10] = {0x01, 0x6E, 0xDC, 0x39, 0x01,
                                 0x01, 0x00, 0x00, 0x00, 0x00};
    BrakeStateData state;
    TEST_ASSERT_TRUE(decodeBrakeStatePayload(payload, state));
    TEST_ASSERT_FALSE(state.brakePressed);
    TEST_ASSERT_TRUE(state.releaseConfirmed);
}

void test_brake_state_session_resync_release_payload_decodes()
{
    const uint8_t payload[10] = {0x01, 0x6E, 0xDC, 0x39, 0x01,
                                 0x0B, 0x00, 0x00, 0x00, 0x00};
    BrakeStateData state;
    TEST_ASSERT_TRUE(decodeBrakeStatePayload(payload, state));
    TEST_ASSERT_FALSE(state.brakePressed);
    TEST_ASSERT_TRUE(state.releaseConfirmed);
    TEST_ASSERT_EQUAL_UINT8(BRAKE_REASON_SESSION_RESYNC, state.reason);
}

void test_brake_state_stationary_negative_tolerance_decodes_as_signed()
{
    const uint8_t payload[10] = {0x01, 0xFD, 0xDC, 0x37, 0x01,
                                 0x00, 0x96, 0x00, 0xFF, 0xFF};
    BrakeStateData state;
    TEST_ASSERT_TRUE(decodeBrakeStatePayload(payload, state));
    TEST_ASSERT_EQUAL_INT16(-1, state.speedDeciKph);
}

void test_brake_state_rejects_contradictory_release()
{
    const uint8_t payload[10] = {0x01, 0xFF, 0xDC, 0x3F, 0x01,
                                 0x01, 0x96, 0x00, 0x00, 0x00};
    BrakeStateData state;
    TEST_ASSERT_FALSE(decodeBrakeStatePayload(payload, state));
}

void test_brake_state_rejects_active_with_short_hold_or_moving_speed()
{
    uint8_t payload[10] = {0x01, 0xFD, 0xDC, 0x37, 0x01,
                           0x00, 0x95, 0x00, 0x00, 0x00};
    BrakeStateData state;
    TEST_ASSERT_FALSE(decodeBrakeStatePayload(payload, state));

    payload[6] = 0x96;
    payload[8] = 0x03;
    TEST_ASSERT_FALSE(decodeBrakeStatePayload(payload, state));
}

void test_brake_state_rejects_reason_tail_and_gear_source_conflicts()
{
    uint8_t active[10] = {0x01, 0xFD, 0xDC, 0x37, 0x01,
                          0x00, 0x96, 0x00, 0x00, 0x00};
    BrakeStateData state;

    active[5] = BRAKE_REASON_MOVING;
    TEST_ASSERT_FALSE(decodeBrakeStatePayload(active, state));
    active[5] = BRAKE_REASON_ACTIVE;
    active[3] |= 0x08U;
    TEST_ASSERT_FALSE(decodeBrakeStatePayload(active, state));
    active[3] &= static_cast<uint8_t>(~0x08U);
    active[4] = 0;
    TEST_ASSERT_FALSE(decodeBrakeStatePayload(active, state));

    uint8_t released[10] = {0x01, 0x6E, 0xDC, 0x39, 0x01,
                            0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(decodeBrakeStatePayload(released, state));
    released[5] = BRAKE_REASON_RELEASE_CONFIRMED;
    released[3] &= static_cast<uint8_t>(~0x08U);
    TEST_ASSERT_FALSE(decodeBrakeStatePayload(released, state));
}

void test_hello_ack_classification_protects_ready_session_sequence()
{
    TEST_ASSERT_TRUE(BleBridgeProtocol::isHelloSessionBoundary(
        BleBridgeProtocol::MSG_HELLO_ACK));
    TEST_ASSERT_FALSE(BleBridgeProtocol::isHelloSessionBoundary(
        BleBridgeProtocol::MSG_BRAKE_STATE));

    TEST_ASSERT_EQUAL_UINT8(
        BleBridgeProtocol::HELLO_DUPLICATE_OR_OLD,
        BleBridgeProtocol::classifyHelloAck(0x1234, 0x1234, true,
                                            100, 100, true));
    TEST_ASSERT_EQUAL_UINT8(
        BleBridgeProtocol::HELLO_DUPLICATE_OR_OLD,
        BleBridgeProtocol::classifyHelloAck(0x1234, 0x1234, true,
                                            99, 100, true));
    TEST_ASSERT_EQUAL_UINT8(
        BleBridgeProtocol::HELLO_NEW_SESSION,
        BleBridgeProtocol::classifyHelloAck(0x1234, 0x1234, true,
                                            101, 100, true));
    TEST_ASSERT_EQUAL_UINT8(
        BleBridgeProtocol::HELLO_NEW_SESSION,
        BleBridgeProtocol::classifyHelloAck(0x5678, 0x1234, true,
                                            1, 100, true));
    TEST_ASSERT_EQUAL_UINT8(
        BleBridgeProtocol::HELLO_NEW_SESSION,
        BleBridgeProtocol::classifyHelloAck(0x1234, 0, false,
                                            1, 100, true));
}

void test_mailbox_rejects_publish_after_session_end()
{
    BrakeStateMailbox mailbox;
    mailbox.beginSession(0x1234, true, 10);
    mailbox.endSession();
    const uint8_t payload[10] = {0x01, 0xFD, 0xDC, 0x37, 0x01,
                                 0x00, 0x96, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(mailbox.publish(payload, 1, 20));
    BrakeStateView view;
    TEST_ASSERT_TRUE(mailbox.read(view));
    TEST_ASSERT_FALSE(view.linkReady);
    TEST_ASSERT_FALSE(view.hasState);
}

void test_sequence_comparison_is_global_and_wrap_safe()
{
    TEST_ASSERT_TRUE(BleBridgeProtocol::isSequenceNewer(11, 10));
    TEST_ASSERT_FALSE(BleBridgeProtocol::isSequenceNewer(10, 10));
    TEST_ASSERT_FALSE(BleBridgeProtocol::isSequenceNewer(9, 10));
    TEST_ASSERT_TRUE(BleBridgeProtocol::isSequenceNewer(0, UINT32_MAX));
}

void test_required_capabilities_keep_brake_state_optional()
{
    TEST_ASSERT_TRUE(BleBridgeProtocol::supportsRequiredCapabilities(0x0F));
    TEST_ASSERT_TRUE(BleBridgeProtocol::supportsRequiredCapabilities(0x1F));
    TEST_ASSERT_FALSE(BleBridgeProtocol::supportsRequiredCapabilities(0x10));
    TEST_ASSERT_FALSE(BleBridgeProtocol::supportsBrakeState(0x0F));
    TEST_ASSERT_TRUE(BleBridgeProtocol::supportsBrakeState(0x1F));
}

void test_sequence_classification_accepts_gaps_and_rejects_old_packets()
{
    TEST_ASSERT_EQUAL_UINT8(BleBridgeProtocol::SEQUENCE_FIRST,
                            BleBridgeProtocol::classifySequence(100, 0, false));
    TEST_ASSERT_EQUAL_UINT8(BleBridgeProtocol::SEQUENCE_NEXT,
                            BleBridgeProtocol::classifySequence(101, 100, true));
    TEST_ASSERT_EQUAL_UINT8(BleBridgeProtocol::SEQUENCE_GAP,
                            BleBridgeProtocol::classifySequence(104, 101, true));
    TEST_ASSERT_EQUAL_UINT8(BleBridgeProtocol::SEQUENCE_DUPLICATE_OR_OLD,
                            BleBridgeProtocol::classifySequence(104, 104, true));
    TEST_ASSERT_EQUAL_UINT8(BleBridgeProtocol::SEQUENCE_DUPLICATE_OR_OLD,
                            BleBridgeProtocol::classifySequence(103, 104, true));
    TEST_ASSERT_EQUAL_UINT8(BleBridgeProtocol::SEQUENCE_NEXT,
                            BleBridgeProtocol::classifySequence(0, UINT32_MAX, true));
}

void test_obstacle_shift_feature_defaults_disabled()
{
    TEST_ASSERT_FALSE(static_cast<bool>(obstacleShiftFeatureEnabled));
}

void test_brake_mailbox_starts_new_session_without_reusing_old_state()
{
    BrakeStateMailbox mailbox;
    mailbox.beginSession(0x1234, true, 100);
    const uint8_t payload[10] = {0x01, 0xFD, 0xDC, 0x37, 0x01,
                                 0x00, 0x96, 0x00, 0x00, 0x00};
    TEST_ASSERT_TRUE(mailbox.publish(payload, 44, 120));

    BrakeStateView before;
    TEST_ASSERT_TRUE(mailbox.read(before));
    TEST_ASSERT_TRUE(before.hasState);
    TEST_ASSERT_TRUE(before.data.brakePressed);

    mailbox.beginSession(0x5678, true, 200);
    BrakeStateView after;
    TEST_ASSERT_TRUE(mailbox.read(after));
    TEST_ASSERT_FALSE(after.hasState);
    TEST_ASSERT_TRUE(after.linkReady);
    TEST_ASSERT_EQUAL_HEX32(0x5678, after.peerBootId);
    TEST_ASSERT_TRUE(after.sessionGeneration > before.sessionGeneration);
}

void test_periodic_obstacle_contract_is_50ms_and_not_query()
{
    TEST_ASSERT_EQUAL_UINT32(50, BleBridgeTiming::kObstacleStatePeriodMs);
    TEST_ASSERT_EQUAL_UINT32(20, BleBridgeTiming::kBridgeServicePollMs);
    TEST_ASSERT_TRUE(BleBridgeTiming::kObstacleStatePeriodMs !=
                     BleBridgeTiming::kBridgeServicePollMs);

    uint32_t nowMs = BleBridgeTiming::nextObstacleServiceDelayMs(0, 0, true);
    TEST_ASSERT_EQUAL_UINT32(20, nowMs);
    nowMs += BleBridgeTiming::nextObstacleServiceDelayMs(nowMs, 0, true);
    TEST_ASSERT_EQUAL_UINT32(40, nowMs);
    nowMs += BleBridgeTiming::nextObstacleServiceDelayMs(nowMs, 0, true);
    TEST_ASSERT_EQUAL_UINT32(50, nowMs);
    TEST_ASSERT_EQUAL_UINT32(
        10, BleBridgeTiming::nextObstacleServiceDelayMs(40, 0, true));
    TEST_ASSERT_FALSE(BleBridgeTiming::obstacleStateDue(49, 0));
    TEST_ASSERT_TRUE(BleBridgeTiming::obstacleStateDue(50, 0));

    const BleBridgeProtocol::Packet packet = BleBridgeProtocol::makePacket(
        BleBridgeProtocol::MSG_OBSTACLE_STATE, 0, 9);
    TEST_ASSERT_EQUAL_HEX8(BleBridgeProtocol::MSG_OBSTACLE_STATE,
                           packet.bytes[2]);
    TEST_ASSERT_EQUAL_UINT8(0, packet.bytes[3]);
}

void test_obstacle_snapshot_keeps_latest_255_and_12b()
{
    CanFrame frame255 = {.id = 0x255, .dlc = 4};
    frame255.data[0] = 0x10;
    frame255.data[1] = 0x20;
    frame255.data[2] = 0x30;
    frame255.data[3] = 0x02;
    obstacleCanSnapshot.observe(frame255, 100);

    CanFrame frame12B = {.id = 0x12B, .dlc = 4};
    frame12B.data[0] = 0x40;
    frame12B.data[1] = 0x30;
    frame12B.data[2] = 0x50;
    frame12B.data[3] = 0x60;
    obstacleCanSnapshot.observe(frame12B, 125);

    ObstacleCanView view;
    TEST_ASSERT_TRUE(obstacleCanSnapshot.read(view));
    TEST_ASSERT_TRUE(view.dlc255Valid);
    TEST_ASSERT_TRUE(view.dlc12BValid);
    TEST_ASSERT_EQUAL_UINT8(0x02, view.raw255[3]);
    TEST_ASSERT_EQUAL_UINT8(0x30, view.raw12B[1]);
    TEST_ASSERT_EQUAL_UINT32(100, view.last255RxMs);
    TEST_ASSERT_EQUAL_UINT32(125, view.last12BRxMs);
    TEST_ASSERT_EQUAL_UINT32(1, view.generation255);
    TEST_ASSERT_EQUAL_UINT32(1, view.generation12B);
}

void test_obstacle_snapshot_marks_wrong_dlc_invalid_without_stale_payload()
{
    CanFrame valid = {.id = 0x255, .dlc = 4};
    valid.data[3] = 0x01;
    obstacleCanSnapshot.observe(valid, 10);

    CanFrame invalid = {.id = 0x255, .dlc = 3};
    invalid.data[3] = 0x02;
    obstacleCanSnapshot.observe(invalid, 20);

    ObstacleCanView view;
    TEST_ASSERT_TRUE(obstacleCanSnapshot.read(view));
    TEST_ASSERT_TRUE(view.has255);
    TEST_ASSERT_FALSE(view.dlc255Valid);
    TEST_ASSERT_EQUAL_UINT8(0, view.raw255[3]);
    TEST_ASSERT_EQUAL_UINT32(2, view.generation255);
}

void test_obstacle_snapshot_invalidate_clears_data_without_reusing_it()
{
    CanFrame frame255 = {.id = 0x255, .dlc = 4};
    frame255.data[3] = 0x01;
    obstacleCanSnapshot.observe(frame255, 10);
    CanFrame frame12B = {.id = 0x12B, .dlc = 4};
    frame12B.data[1] = 0x30;
    obstacleCanSnapshot.observe(frame12B, 11);

    ObstacleCanView before;
    TEST_ASSERT_TRUE(obstacleCanSnapshot.read(before));
    obstacleCanSnapshot.invalidate();
    ObstacleCanView after;
    TEST_ASSERT_TRUE(obstacleCanSnapshot.read(after));
    TEST_ASSERT_FALSE(after.has255);
    TEST_ASSERT_FALSE(after.has12B);
    TEST_ASSERT_FALSE(after.dlc255Valid);
    TEST_ASSERT_FALSE(after.dlc12BValid);
    TEST_ASSERT_EQUAL_UINT8(0, after.raw255[3]);
    TEST_ASSERT_EQUAL_UINT8(0, after.raw12B[1]);
    TEST_ASSERT_TRUE(after.invalidationGeneration >
                     before.invalidationGeneration);
}

void test_obstacle_pause_diagnostics_default_to_zero()
{
    BleBridgeDiagnostics diagnostics;
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.obstaclePausedCanFrameSkipCount);
    TEST_ASSERT_EQUAL_UINT32(0, diagnostics.obstaclePausedTxSlotCount);
}

void test_nag_controller_increments_revision_once_for_real_change()
{
    NagStateController controller;
    controller.configureCallbacks(testApply, testRuntime);
    controller.begin(false);

    const NagStateView first = controller.applyLocal(true);
    const NagStateView second = controller.applyLocal(true);

    TEST_ASSERT_TRUE(first.configuredEnabled);
    TEST_ASSERT_EQUAL_UINT32(1, first.revision);
    TEST_ASSERT_EQUAL_UINT32(1, second.revision);
    TEST_ASSERT_EQUAL_UINT32(1, applyCount);
}

void test_nag_controller_rejects_stale_conflicting_revision()
{
    NagStateController controller;
    controller.configureCallbacks(testApply, testRuntime);
    controller.begin(false);
    controller.applyLocal(true);

    NagRemoteCommand command;
    command.desiredEnabled = false;
    command.commandId = 7;
    command.expectedRevision = 0;
    const NagStateView state = controller.applyRemote(command);

    TEST_ASSERT_TRUE(state.configuredEnabled);
    TEST_ASSERT_EQUAL_UINT8(NAG_RESULT_REVISION_CONFLICT, state.result);
    TEST_ASSERT_EQUAL_UINT32(1, state.revision);
    TEST_ASSERT_EQUAL_UINT32(1, state.revisionConflictCount);
    TEST_ASSERT_EQUAL_UINT32(1, applyCount);
}

void test_nag_controller_duplicate_command_is_idempotent()
{
    NagStateController controller;
    controller.configureCallbacks(testApply, testRuntime);
    controller.begin(false);

    NagRemoteCommand command;
    command.desiredEnabled = true;
    command.commandId = 21;
    command.expectedRevision = 0;
    const NagStateView first = controller.applyRemote(command);
    const NagStateView duplicate = controller.applyRemote(command);

    TEST_ASSERT_EQUAL_UINT8(NAG_RESULT_OK, first.result);
    TEST_ASSERT_EQUAL_UINT8(NAG_RESULT_OK, duplicate.result);
    TEST_ASSERT_EQUAL_UINT32(1, duplicate.revision);
    TEST_ASSERT_EQUAL_UINT32(1, duplicate.duplicateCommandCount);
    TEST_ASSERT_EQUAL_UINT32(1, applyCount);
}

void test_nag_controller_accepts_stale_revision_when_state_is_already_equal()
{
    NagStateController controller;
    controller.configureCallbacks(testApply, testRuntime);
    controller.begin(false);
    controller.applyLocal(true);

    NagRemoteCommand command;
    command.desiredEnabled = true;
    command.commandId = 33;
    command.expectedRevision = 0;
    const NagStateView state = controller.applyRemote(command);

    TEST_ASSERT_EQUAL_UINT8(NAG_RESULT_OK, state.result);
    TEST_ASSERT_TRUE(state.configuredEnabled);
    TEST_ASSERT_EQUAL_UINT32(1, state.revision);
    TEST_ASSERT_EQUAL_UINT32(1, applyCount);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_ble_packet_crc_round_trip);
    RUN_TEST(test_ble_packet_rejects_crc_corruption);
    RUN_TEST(test_unbind_packets_preserve_transaction_and_identity);
    RUN_TEST(test_unbind_packet_rejects_zero_identity_and_wrong_result);
    RUN_TEST(test_brake_state_contract_and_capabilities);
    RUN_TEST(test_obstacle_transport_payload_and_watchdog);
    RUN_TEST(test_obstacle_transport_resume_requires_strict_payload);
    RUN_TEST(test_brake_state_active_payload_decodes);
    RUN_TEST(test_brake_state_release_payload_decodes);
    RUN_TEST(test_brake_state_session_resync_release_payload_decodes);
    RUN_TEST(test_brake_state_stationary_negative_tolerance_decodes_as_signed);
    RUN_TEST(test_brake_state_rejects_contradictory_release);
    RUN_TEST(test_brake_state_rejects_active_with_short_hold_or_moving_speed);
    RUN_TEST(test_brake_state_rejects_reason_tail_and_gear_source_conflicts);
    RUN_TEST(test_hello_ack_classification_protects_ready_session_sequence);
    RUN_TEST(test_mailbox_rejects_publish_after_session_end);
    RUN_TEST(test_sequence_comparison_is_global_and_wrap_safe);
    RUN_TEST(test_required_capabilities_keep_brake_state_optional);
    RUN_TEST(test_sequence_classification_accepts_gaps_and_rejects_old_packets);
    RUN_TEST(test_obstacle_shift_feature_defaults_disabled);
    RUN_TEST(test_brake_mailbox_starts_new_session_without_reusing_old_state);
    RUN_TEST(test_periodic_obstacle_contract_is_50ms_and_not_query);
    RUN_TEST(test_obstacle_snapshot_keeps_latest_255_and_12b);
    RUN_TEST(test_obstacle_snapshot_marks_wrong_dlc_invalid_without_stale_payload);
    RUN_TEST(test_obstacle_snapshot_invalidate_clears_data_without_reusing_it);
    RUN_TEST(test_obstacle_pause_diagnostics_default_to_zero);
    RUN_TEST(test_nag_controller_increments_revision_once_for_real_change);
    RUN_TEST(test_nag_controller_rejects_stale_conflicting_revision);
    RUN_TEST(test_nag_controller_duplicate_command_is_idempotent);
    RUN_TEST(test_nag_controller_accepts_stale_revision_when_state_is_already_equal);
    return UNITY_END();
}
