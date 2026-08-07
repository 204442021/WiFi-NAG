#include <unity.h>

#include "ble/bridge_protocol.h"
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
    RUN_TEST(test_obstacle_snapshot_keeps_latest_255_and_12b);
    RUN_TEST(test_obstacle_snapshot_marks_wrong_dlc_invalid_without_stale_payload);
    RUN_TEST(test_nag_controller_increments_revision_once_for_real_change);
    RUN_TEST(test_nag_controller_rejects_stale_conflicting_revision);
    RUN_TEST(test_nag_controller_duplicate_command_is_idempotent);
    RUN_TEST(test_nag_controller_accepts_stale_revision_when_state_is_already_equal);
    return UNITY_END();
}
