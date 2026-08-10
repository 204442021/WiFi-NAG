#include <cstring>
#include <unity.h>

#include "ble/brake_state.h"
#include "can_frame_types.h"
#include "drivers/mock_driver.h"
#include "obstacle_shift_controller.h"

static MockDriver mock;
static ObstacleShiftController controller;
static const ObstacleShiftRuntimeInputs kReadyRuntime{true, true};

static uint8_t checksum118(const CanFrame &frame)
{
    uint16_t sum = 0x19U;
    for (uint8_t index = 1; index < 8; ++index)
        sum = static_cast<uint16_t>(sum + frame.data[index]);
    return static_cast<uint8_t>(sum & 0xFFU);
}

static CanFrame make118(uint8_t gear, uint8_t counter = 1)
{
    CanFrame frame{.id = 0x118, .dlc = 8};
    frame.data[1] = static_cast<uint8_t>(0x60U | (counter & 0x0FU));
    frame.data[2] = gear;
    frame.data[3] = 0x30;
    frame.data[4] = 0x00;
    frame.data[5] = 0xC8;
    frame.data[6] = gear == 0x32 ? 0x00 : 0x08;
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
    view.data.systemKnown = true;
    view.data.systemFresh = true;
    view.data.sourcesAgree = true;
    view.data.realGear = BRAKE_GEAR_D;
    view.data.gearFresh = true;
    view.data.drConfirmed = true;
    view.data.speedFresh = true;
    view.data.stationaryConfirmed = true;
    view.data.senderEnabled = true;
    view.data.normalRuntime = true;
    view.data.reason = BRAKE_REASON_RELEASE_CONFIRMED;
    return view;
}

static BrakeStateView pressed(uint32_t session, uint32_t rxMs)
{
    BrakeStateView view = released(session, rxMs);
    view.data.releaseConfirmed = false;
    view.data.brakePressed = true;
    view.data.physicalPressed = true;
    view.data.systemPressed = true;
    view.data.brakeHoldReady = true;
    view.data.activeEligible = true;
    view.data.brakeHoldMs = 150;
    view.data.reason = BRAKE_REASON_ACTIVE;
    return view;
}

static BrakeStateView unconfirmedZero(uint32_t session, uint32_t rxMs)
{
    BrakeStateView view = released(session, rxMs);
    view.data.releaseConfirmed = false;
    view.data.reason = BRAKE_REASON_STALE;
    return view;
}

static void arm(uint32_t session = 1, uint32_t nowMs = 10)
{
    const BrakeStateView brake = released(session, nowMs);
    controller.tick(brake, kReadyRuntime, nowMs);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ARMED,
                            controller.view(nowMs).state);
}

static void startActive(uint32_t session = 1,
                        uint32_t pressMs = 20,
                        uint32_t frameMs = 25)
{
    arm(session, 10);
    const BrakeStateView brake = pressed(session, pressMs);
    CanFrame frame = make118(0x95, 1);
    controller.observeFrame(frame, brake, kReadyRuntime, frameMs, mock);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ACTIVE_P,
                            controller.view(frameMs).state);
}

static void keepActiveThrough(uint32_t finalMs)
{
    uint8_t counter = 2;
    uint32_t frameMs = 65;
    while (frameMs < finalMs)
    {
        CanFrame frame = make118(0x95, counter++);
        controller.observeFrame(frame,
                                pressed(1, frameMs),
                                kReadyRuntime,
                                frameMs,
                                mock);
        frameMs += 40;
    }
    CanFrame finalFrame = make118(0x95, counter);
    controller.observeFrame(finalFrame,
                            pressed(1, finalMs),
                            kReadyRuntime,
                            finalMs,
                            mock);
}

void setUp()
{
    mock = MockDriver();
    controller.reset();
}

void tearDown() {}

void test_d_frame_becomes_expected_virtual_p()
{
    const uint8_t raw[8] = {0x0F,0x61,0x95,0x30,0x00,0xC8,0x08,0x00};
    CanFrame source{.id = 0x118, .dlc = 8};
    std::memcpy(source.data, raw, 8);
    CanFrame out;
    TEST_ASSERT_TRUE(ObstacleShiftController::makeVirtualParkFrame(source, out));
    const uint8_t expected[8] = {0xA4,0x61,0x32,0x30,0x00,0xC8,0x00,0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data, 8);
}

void test_r_frame_becomes_expected_virtual_p()
{
    const uint8_t raw[8] = {0xDC,0x6E,0x55,0x30,0x00,0xC8,0x08,0x00};
    CanFrame source{.id = 0x118, .dlc = 8};
    std::memcpy(source.data, raw, 8);
    CanFrame out;
    TEST_ASSERT_TRUE(ObstacleShiftController::makeVirtualParkFrame(source, out));
    const uint8_t expected[8] = {0xB1,0x6E,0x32,0x30,0x00,0xC8,0x00,0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out.data, 8);
}

void test_non_dr_wrong_dlc_and_bad_checksum_do_not_build_virtual_p()
{
    CanFrame out;
    CanFrame park = make118(0x32);
    TEST_ASSERT_FALSE(ObstacleShiftController::makeVirtualParkFrame(park, out));
    CanFrame shortFrame = make118(0x95);
    shortFrame.dlc = 7;
    TEST_ASSERT_FALSE(ObstacleShiftController::makeVirtualParkFrame(shortFrame, out));
    CanFrame corrupt = make118(0x95);
    corrupt.data[0] ^= 0x01;
    TEST_ASSERT_FALSE(ObstacleShiftController::makeVirtualParkFrame(corrupt, out));
}

void test_startup_ignores_pressed_until_confirmed_release()
{
    BrakeStateView brake = pressed(1, 10);
    controller.tick(brake, kReadyRuntime, 10);
    CanFrame frame = make118(0x95);
    controller.observeFrame(frame, brake, kReadyRuntime, 11, mock);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_WAIT_RELEASE,
                            controller.view(11).state);
    TEST_ASSERT_EQUAL_UINT32(0, mock.sent.size());
}

void test_confirmed_release_arms_next_press()
{
    arm();
    TEST_ASSERT_FALSE(controller.view(10).latched);
}

void test_next_live_dr_frame_starts_window_and_sends_once()
{
    startActive();
    const ObstacleShiftView view = controller.view(25);
    TEST_ASSERT_TRUE(view.virtualParkActive);
    TEST_ASSERT_TRUE(view.latched);
    TEST_ASSERT_EQUAL_UINT32(500, view.virtualParkRemainingMs);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x32, mock.sent[0].data[2]);
}

void test_active_sends_one_virtual_p_per_new_real_118()
{
    startActive();
    BrakeStateView brake = pressed(1, 40);
    CanFrame second = make118(0x95, 2);
    controller.observeFrame(second, brake, kReadyRuntime, 40, mock);
    CanFrame third = make118(0x55, 3);
    controller.observeFrame(third, brake, kReadyRuntime, 50, mock);
    TEST_ASSERT_EQUAL_UINT32(3, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x62, mock.sent[1].data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x63, mock.sent[2].data[1]);
}

void test_window_expires_at_500ms_without_restore_frame()
{
    startActive();
    keepActiveThrough(524);
    const size_t sentBeforeExpiry = mock.sent.size();
    BrakeStateView brake = pressed(1, 524);
    controller.tick(brake, kReadyRuntime, 525);
    const ObstacleShiftView view = controller.view(525);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_LATCHED, view.state);
    TEST_ASSERT_FALSE(view.virtualParkActive);
    TEST_ASSERT_EQUAL_UINT32(sentBeforeExpiry, mock.sent.size());
}

void test_same_press_cannot_retrigger_after_window()
{
    startActive();
    keepActiveThrough(524);
    BrakeStateView brake = pressed(1, 524);
    controller.tick(brake, kReadyRuntime, 525);
    const size_t sent = mock.sent.size();
    CanFrame later = make118(0x95, 5);
    controller.observeFrame(later, pressed(1, 535), kReadyRuntime, 535, mock);
    TEST_ASSERT_EQUAL_UINT32(sent, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_LATCHED,
                            controller.view(535).state);
}

void test_confirmed_release_unlocks_latch()
{
    startActive();
    controller.tick(unconfirmedZero(1, 30), kReadyRuntime, 30);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_LATCHED,
                            controller.view(30).state);
    controller.tick(released(1, 40), kReadyRuntime, 40);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ARMED,
                            controller.view(40).state);
}

void test_unconfirmed_zero_stops_but_does_not_unlock()
{
    startActive();
    controller.tick(unconfirmedZero(1, 30), kReadyRuntime, 30);
    const ObstacleShiftView view = controller.view(30);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_LATCHED, view.state);
    TEST_ASSERT_FALSE(view.virtualParkActive);
    TEST_ASSERT_TRUE(view.latched);
}

void test_brake_state_stale_at_201ms_enters_wait_release()
{
    startActive();
    BrakeStateView brake = pressed(1, 20);
    controller.tick(brake, kReadyRuntime, 221);
    const ObstacleShiftView view = controller.view(221);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_WAIT_RELEASE, view.state);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_BRAKE_STALE, view.reason);
}

void test_real_118_gap_over_50ms_requires_release_rearm()
{
    startActive();
    controller.tick(pressed(1, 70), kReadyRuntime, 76);
    const ObstacleShiftView view = controller.view(76);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_WAIT_RELEASE, view.state);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_118_STALE, view.reason);
}

void test_session_change_enters_wait_release()
{
    startActive();
    controller.tick(pressed(2, 30), kReadyRuntime, 30);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_WAIT_RELEASE,
                            controller.view(30).state);
}

void test_moving_or_not_stationary_never_sends()
{
    arm();
    BrakeStateView brake = pressed(1, 20);
    brake.data.stationaryConfirmed = false;
    CanFrame frame = make118(0x95);
    controller.observeFrame(frame, brake, kReadyRuntime, 25, mock);
    TEST_ASSERT_EQUAL_UINT32(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ARMED,
                            controller.view(25).state);
}

void test_feature_or_can_write_disabled_never_sends()
{
    arm();
    BrakeStateView brake = pressed(1, 20);
    CanFrame frame = make118(0x95);
    controller.observeFrame(frame, brake, {false, true}, 25, mock);
    TEST_ASSERT_EQUAL_UINT32(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_WAIT_RELEASE,
                            controller.view(25).state);

    controller.reset();
    arm();
    controller.observeFrame(frame, brake, {true, false}, 25, mock);
    TEST_ASSERT_EQUAL_UINT32(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_WAIT_RELEASE,
                            controller.view(25).state);
}

void test_send_failure_latches_and_never_replays()
{
    arm();
    mock.writeEnabled = false;
    BrakeStateView brake = pressed(1, 20);
    CanFrame frame = make118(0x95);
    controller.observeFrame(frame, brake, kReadyRuntime, 25, mock);
    ObstacleShiftView view = controller.view(25);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_LATCHED, view.state);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_TX_FAILED, view.reason);
    TEST_ASSERT_EQUAL_UINT32(1, view.virtualParkTxFailCount);

    mock.writeEnabled = true;
    controller.observeFrame(frame, pressed(1, 30), kReadyRuntime, 30, mock);
    TEST_ASSERT_EQUAL_UINT32(0, mock.sent.size());
}

void test_real_gear_leaving_dr_stops_active_and_requires_release()
{
    startActive();
    CanFrame park = make118(0x32, 2);
    controller.observeFrame(park, pressed(1, 30), kReadyRuntime, 30, mock);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_WAIT_RELEASE,
                            controller.view(30).state);
}

void test_virtual_p_fingerprint_is_not_accepted_as_real_118()
{
    startActive();
    const ObstacleShiftView before = controller.view(25);
    CanFrame echo = mock.sent.back();
    controller.observeFrame(echo, pressed(1, 26), kReadyRuntime, 26, mock);
    const ObstacleShiftView after = controller.view(26);
    TEST_ASSERT_EQUAL_UINT32(before.real118RxCount, after.real118RxCount);
    TEST_ASSERT_EQUAL_UINT8(BRAKE_GEAR_D, after.realGear);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());
}

void test_frame_is_not_used_when_press_was_not_established_before_read()
{
    arm();
    BrakeStateView brake = pressed(1, 20);
    CanFrame first = make118(0x95, 1);
    controller.observeFrame(first, brake, kReadyRuntime, 25, mock, false);
    TEST_ASSERT_EQUAL_UINT32(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ARMED,
                            controller.view(25).state);

    CanFrame next = make118(0x95, 2);
    controller.observeFrame(next, brake, kReadyRuntime, 35, mock, true);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ACTIVE_P,
                            controller.view(35).state);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_d_frame_becomes_expected_virtual_p);
    RUN_TEST(test_r_frame_becomes_expected_virtual_p);
    RUN_TEST(test_non_dr_wrong_dlc_and_bad_checksum_do_not_build_virtual_p);
    RUN_TEST(test_startup_ignores_pressed_until_confirmed_release);
    RUN_TEST(test_confirmed_release_arms_next_press);
    RUN_TEST(test_next_live_dr_frame_starts_window_and_sends_once);
    RUN_TEST(test_active_sends_one_virtual_p_per_new_real_118);
    RUN_TEST(test_window_expires_at_500ms_without_restore_frame);
    RUN_TEST(test_same_press_cannot_retrigger_after_window);
    RUN_TEST(test_confirmed_release_unlocks_latch);
    RUN_TEST(test_unconfirmed_zero_stops_but_does_not_unlock);
    RUN_TEST(test_brake_state_stale_at_201ms_enters_wait_release);
    RUN_TEST(test_real_118_gap_over_50ms_requires_release_rearm);
    RUN_TEST(test_session_change_enters_wait_release);
    RUN_TEST(test_moving_or_not_stationary_never_sends);
    RUN_TEST(test_feature_or_can_write_disabled_never_sends);
    RUN_TEST(test_send_failure_latches_and_never_replays);
    RUN_TEST(test_real_gear_leaving_dr_stops_active_and_requires_release);
    RUN_TEST(test_virtual_p_fingerprint_is_not_accepted_as_real_118);
    RUN_TEST(test_frame_is_not_used_when_press_was_not_established_before_read);
    return UNITY_END();
}
