#include <cstring>
#include <unity.h>

#include "ble/brake_state.h"
#include "can_frame_types.h"
#include "drivers/mock_driver.h"
#include "obstacle_shift_controller.h"

static MockDriver mock;
static ObstacleShiftController controller;
static const ObstacleShiftRuntimeInputs kReady{true, true};

static uint8_t checksum118(const CanFrame &frame)
{
    uint16_t sum = 0x19U;
    for (uint8_t index = 1; index < 8; ++index)
        sum = static_cast<uint16_t>(sum + frame.data[index]);
    return static_cast<uint8_t>(sum & 0xFFU);
}

static CanFrame make118(uint8_t byte2, uint8_t counter = 1)
{
    CanFrame frame{.id = 0x118, .dlc = 8};
    frame.data[1] = static_cast<uint8_t>(0x60U | (counter & 0x0FU));
    frame.data[2] = byte2;
    frame.data[3] = 0x30;
    frame.data[4] = 0x00;
    frame.data[5] = 0xC8;
    frame.data[6] = byte2 == 0x32 ? 0x00 : 0x08;
    frame.data[7] = 0x00;
    frame.data[0] = checksum118(frame);
    return frame;
}

static BrakeStateView released(uint32_t session, uint32_t rxMs)
{
    BrakeStateView view;
    view.linkReady = true;
    view.capabilitySupported = true;
    view.hasState = true;
    view.sessionGeneration = session;
    view.lastRxMs = rxMs;
    view.data.releaseConfirmed = true;
    view.data.physicalKnown = true;
    view.data.physicalFresh = true;
    view.data.physicalPressed = false;
    view.data.realGear = BRAKE_GEAR_D;
    view.data.gearFresh = true;
    view.data.speedFresh = true;
    view.data.stationaryConfirmed = true;
    view.data.reason = BRAKE_REASON_RELEASE_CONFIRMED;
    return view;
}

static BrakeStateView pressed(uint32_t session, uint32_t rxMs)
{
    BrakeStateView view = released(session, rxMs);
    view.data.releaseConfirmed = false;
    view.data.physicalPressed = true;
    view.data.systemKnown = false;
    view.data.systemFresh = false;
    view.data.systemPressed = false;
    view.data.sourcesAgree = false;
    view.data.brakePressed = false;
    view.data.brakeHoldReady = false;
    view.data.activeEligible = false;
    view.data.reason = BRAKE_REASON_ACTIVE;
    return view;
}

static void sendReal118(const BrakeStateView &brake,
                        uint32_t nowMs,
                        uint8_t counter = 1)
{
    const CanFrame frame = make118(0x41, counter);
    controller.observeFrame(frame, brake, kReady, nowMs, mock);
}

void setUp()
{
    mock = MockDriver();
    controller.reset();
}

void tearDown() {}

void test_d_frame_becomes_expected_virtual_p()
{
    const uint8_t raw[8] = {0x0F, 0x61, 0x95, 0x30,
                            0x00, 0xC8, 0x08, 0x00};
    CanFrame source{.id = 0x118, .dlc = 8};
    std::memcpy(source.data, raw, 8);
    CanFrame out;
    TEST_ASSERT_TRUE(ObstacleShiftController::makeVirtualParkFrame(source, out));
    const uint8_t expected[8] = {0xA4, 0x61, 0x32, 0x30,
                                 0x00, 0xC8, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data, 8);
}

void test_unknown_local_gear_still_becomes_virtual_p()
{
    CanFrame source = make118(0x41);
    CanFrame out;
    TEST_ASSERT_TRUE(ObstacleShiftController::makeVirtualParkFrame(source, out));
    TEST_ASSERT_EQUAL_HEX8(0x32, out.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.data[6]);
    TEST_ASSERT_EQUAL_HEX8(checksum118(out), out.data[0]);
}

void test_wrong_dlc_and_bad_checksum_do_not_build_virtual_p()
{
    CanFrame out;
    CanFrame shortFrame = make118(0x41);
    shortFrame.dlc = 7;
    TEST_ASSERT_FALSE(
        ObstacleShiftController::makeVirtualParkFrame(shortFrame, out));
    CanFrame corrupt = make118(0x41);
    corrupt.data[0] ^= 0x01;
    TEST_ASSERT_FALSE(ObstacleShiftController::makeVirtualParkFrame(corrupt, out));
}

void test_fresh_press_starts_without_prior_release()
{
    const BrakeStateView brake = pressed(1, 20);
    sendReal118(brake, 20);

    const ObstacleShiftView view = controller.view(20);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ACTIVE_P, view.state);
    TEST_ASSERT_TRUE(view.virtualParkActive);
    TEST_ASSERT_EQUAL_UINT32(1, view.activationCount);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());
}

void test_stale_idle_release_does_not_lock_next_press()
{
    controller.tick(released(1, 0), kReady, 501);
    ObstacleShiftView view = controller.view(501);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_IDLE, view.state);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_BRAKE_STALE, view.reason);
    TEST_ASSERT_FALSE(view.virtualParkActive);

    const BrakeStateView brake = pressed(1, 510);
    sendReal118(brake, 510);
    view = controller.view(510);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ACTIVE_P, view.state);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());
}

void test_continues_past_1000ms_while_pressed_and_fresh()
{
    sendReal118(pressed(1, 20), 20, 1);
    sendReal118(pressed(1, 1500), 1500, 2);

    const ObstacleShiftView view = controller.view(1500);
    TEST_ASSERT_TRUE(view.virtualParkActive);
    TEST_ASSERT_EQUAL_UINT32(1, view.activationCount);
    TEST_ASSERT_EQUAL_UINT32(2, mock.sent.size());
}

void test_each_real_frame_gets_at_most_one_send_attempt()
{
    const BrakeStateView brake = pressed(1, 20);
    sendReal118(brake, 20, 1);
    sendReal118(pressed(1, 30), 30, 2);
    sendReal118(pressed(1, 40), 40, 3);

    TEST_ASSERT_EQUAL_UINT32(3, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x61, mock.sent[0].data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x62, mock.sent[1].data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x63, mock.sent[2].data[1]);
}

void test_release_stops_immediately_and_next_press_restarts()
{
    sendReal118(pressed(1, 20), 20, 1);
    controller.tick(released(1, 30), kReady, 30);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_IDLE, controller.view(30).state);
    TEST_ASSERT_FALSE(controller.view(30).virtualParkActive);

    sendReal118(released(1, 31), 31, 2);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());

    sendReal118(pressed(1, 40), 40, 3);
    TEST_ASSERT_EQUAL_UINT32(2, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(2, controller.view(40).activationCount);
}

void test_stale_pressed_state_stops_injection()
{
    sendReal118(pressed(1, 20), 20, 1);
    const BrakeStateView stalePress = pressed(1, 20);
    controller.tick(stalePress, kReady, 521);
    TEST_ASSERT_FALSE(controller.view(521).virtualParkActive);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_BRAKE_STALE,
                            controller.view(521).reason);

    sendReal118(stalePress, 522, 2);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());
}

void test_moving_or_t2can_not_dr_never_sends()
{
    BrakeStateView moving = pressed(1, 20);
    moving.data.stationaryConfirmed = false;
    sendReal118(moving, 20, 1);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_MOVING, controller.view(20).reason);

    BrakeStateView park = pressed(1, 30);
    park.data.realGear = BRAKE_GEAR_P;
    sendReal118(park, 30, 2);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_GEAR_NOT_DR,
                            controller.view(30).reason);
    TEST_ASSERT_EQUAL_UINT32(0, mock.sent.size());
}

void test_feature_and_can_write_gates_remain_required()
{
    const BrakeStateView brake = pressed(1, 20);
    const CanFrame frame = make118(0x41, 1);
    controller.observeFrame(frame, brake, {false, true}, 20, mock);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_FEATURE_DISABLED,
                            controller.view(20).reason);

    controller.observeFrame(frame, brake, {true, false}, 21, mock);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_CAN_WRITE_DISABLED,
                            controller.view(21).reason);
    TEST_ASSERT_EQUAL_UINT32(0, mock.sent.size());
}

void test_send_failure_does_not_lock_and_next_frame_retries()
{
    mock.writeEnabled = false;
    sendReal118(pressed(1, 20), 20, 1);
    ObstacleShiftView view = controller.view(20);
    TEST_ASSERT_TRUE(view.virtualParkActive);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_TX_FAILED, view.reason);
    TEST_ASSERT_EQUAL_UINT32(1, view.virtualParkTxFailCount);

    mock.writeEnabled = true;
    sendReal118(pressed(1, 30), 30, 2);
    view = controller.view(30);
    TEST_ASSERT_TRUE(view.virtualParkActive);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, view.virtualParkTxFailCount);
}

void test_virtual_p_fingerprint_is_not_accepted_as_real_118()
{
    sendReal118(pressed(1, 20), 20, 1);
    const ObstacleShiftView before = controller.view(20);
    const CanFrame echo = mock.sent.back();
    controller.observeFrame(echo, pressed(1, 21), kReady, 21, mock);
    const ObstacleShiftView after = controller.view(21);
    TEST_ASSERT_EQUAL_UINT32(before.real118RxCount, after.real118RxCount);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_d_frame_becomes_expected_virtual_p);
    RUN_TEST(test_unknown_local_gear_still_becomes_virtual_p);
    RUN_TEST(test_wrong_dlc_and_bad_checksum_do_not_build_virtual_p);
    RUN_TEST(test_fresh_press_starts_without_prior_release);
    RUN_TEST(test_stale_idle_release_does_not_lock_next_press);
    RUN_TEST(test_continues_past_1000ms_while_pressed_and_fresh);
    RUN_TEST(test_each_real_frame_gets_at_most_one_send_attempt);
    RUN_TEST(test_release_stops_immediately_and_next_press_restarts);
    RUN_TEST(test_stale_pressed_state_stops_injection);
    RUN_TEST(test_moving_or_t2can_not_dr_never_sends);
    RUN_TEST(test_feature_and_can_write_gates_remain_required);
    RUN_TEST(test_send_failure_does_not_lock_and_next_frame_retries);
    RUN_TEST(test_virtual_p_fingerprint_is_not_accepted_as_real_118);
    return UNITY_END();
}
