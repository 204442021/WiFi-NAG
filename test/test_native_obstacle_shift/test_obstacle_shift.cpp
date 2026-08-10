#include <cstring>
#include <unity.h>

#include "ble/brake_state.h"
#include "can_frame_types.h"
#include "drivers/mock_driver.h"
#include "obstacle_shift_controller.h"

static MockDriver mock;
static ObstacleShiftController controller;
static const ObstacleShiftRuntimeInputs kAutomaticReady{true, true, 0};
static const ObstacleShiftRuntimeInputs kManualReady{false, true, 0};

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
    view.data.systemKnown = true;
    view.data.systemFresh = true;
    view.data.systemPressed = false;
    view.data.sourcesAgree = true;
    view.data.realGear = BRAKE_GEAR_D;
    view.data.gearFresh = true;
    view.data.speedFresh = true;
    view.data.stationaryConfirmed = true;
    view.data.reason = BRAKE_REASON_RELEASE_CONFIRMED;
    return view;
}

static BrakeStateView physicalPressedOnly(uint32_t session, uint32_t rxMs)
{
    BrakeStateView view = released(session, rxMs);
    view.data.releaseConfirmed = false;
    view.data.physicalPressed = true;
    view.data.systemPressed = false;
    view.data.sourcesAgree = false;
    view.data.brakePressed = false;
    view.data.brakeHoldReady = false;
    view.data.activeEligible = false;
    view.data.reason = BRAKE_REASON_CONFLICT;
    return view;
}

static void armAutomatic(uint32_t session = 1, uint32_t nowMs = 10)
{
    controller.tick(released(session, nowMs), kAutomaticReady, nowMs);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ARMED,
                            controller.view(nowMs).state);
}

static void startAutomatic(uint32_t session = 1,
                           uint32_t pressMs = 20,
                           uint32_t frameMs = 21)
{
    armAutomatic(session, 10);
    BrakeStateView brake = physicalPressedOnly(session, pressMs);
    controller.tick(brake, kAutomaticReady, pressMs);
    CanFrame frame = make118(0x41, 1);
    controller.observeFrame(frame, brake, kAutomaticReady, frameMs, mock);
    const ObstacleShiftView view = controller.view(frameMs);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ACTIVE_P, view.state);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_TRIGGER_AUTOMATIC_BRAKE,
                            view.triggerSource);
}

static void startManual(uint32_t requestGeneration = 1,
                        uint32_t startMs = 20,
                        uint32_t frameMs = 21)
{
    BrakeStateView brake = released(1, startMs);
    controller.tick(brake, kManualReady, startMs - 1);
    ObstacleShiftRuntimeInputs runtime{false, true, requestGeneration};
    controller.tick(brake, runtime, startMs);
    CanFrame frame = make118(0x41, 1);
    controller.observeFrame(frame, brake, runtime, frameMs, mock);
    const ObstacleShiftView view = controller.view(frameMs);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ACTIVE_P, view.state);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_TRIGGER_MANUAL_BUTTON,
                            view.triggerSource);
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
    TEST_ASSERT_FALSE(ObstacleShiftController::makeVirtualParkFrame(shortFrame, out));
    CanFrame corrupt = make118(0x41);
    corrupt.data[0] ^= 0x01;
    TEST_ASSERT_FALSE(ObstacleShiftController::makeVirtualParkFrame(corrupt, out));
}

void test_automatic_waits_for_release_before_first_press()
{
    BrakeStateView brake = physicalPressedOnly(1, 10);
    controller.tick(brake, kAutomaticReady, 10);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_WAIT_RELEASE,
                            controller.view(10).state);
}

void test_automatic_uses_physical_brake_without_system_or_hold_gates()
{
    startAutomatic();
    const ObstacleShiftView view = controller.view(21);
    TEST_ASSERT_TRUE(view.virtualParkActive);
    TEST_ASSERT_TRUE(view.latched);
    TEST_ASSERT_EQUAL_UINT32(999, view.virtualParkRemainingMs);
    TEST_ASSERT_EQUAL_UINT32(1, view.automaticWindowCount);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());
}

void test_active_sends_once_per_new_real_118_using_t2can_gear()
{
    startAutomatic();
    BrakeStateView brake = physicalPressedOnly(1, 30);
    CanFrame second = make118(0x73, 2);
    controller.observeFrame(second, brake, kAutomaticReady, 30, mock);
    CanFrame third = make118(0x18, 3);
    controller.observeFrame(third, brake, kAutomaticReady, 40, mock);
    TEST_ASSERT_EQUAL_UINT32(3, mock.sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x62, mock.sent[1].data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x63, mock.sent[2].data[1]);
}

void test_window_expires_at_1000ms_and_same_press_cannot_retrigger()
{
    startAutomatic();
    BrakeStateView brake = physicalPressedOnly(1, 1020);
    controller.tick(brake, kAutomaticReady, 1020);
    ObstacleShiftView view = controller.view(1020);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_LATCHED, view.state);
    TEST_ASSERT_FALSE(view.virtualParkActive);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_TRIGGER_NONE, view.triggerSource);

    CanFrame later = make118(0x41, 2);
    controller.observeFrame(later, brake, kAutomaticReady, 1021, mock);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());
}

void test_physical_release_rearms_automatic_after_latch()
{
    startAutomatic();
    controller.tick(physicalPressedOnly(1, 1020), kAutomaticReady, 1020);
    controller.tick(released(1, 1030), kAutomaticReady, 1030);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ARMED,
                            controller.view(1030).state);
}

void test_control_snapshot_stale_at_501ms_stops_window()
{
    startAutomatic();
    BrakeStateView brake = physicalPressedOnly(1, 20);
    controller.tick(brake, kAutomaticReady, 521);
    const ObstacleShiftView view = controller.view(521);
    TEST_ASSERT_FALSE(view.virtualParkActive);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_BRAKE_STALE, view.reason);
}

void test_moving_or_t2can_not_dr_never_starts_automatic()
{
    armAutomatic();
    BrakeStateView moving = physicalPressedOnly(1, 20);
    moving.data.stationaryConfirmed = false;
    controller.tick(moving, kAutomaticReady, 20);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ARMED,
                            controller.view(20).state);

    BrakeStateView park = physicalPressedOnly(1, 30);
    park.data.realGear = BRAKE_GEAR_P;
    controller.tick(park, kAutomaticReady, 30);
    TEST_ASSERT_EQUAL_UINT8(OBSTACLE_SHIFT_ARMED,
                            controller.view(30).state);
    TEST_ASSERT_EQUAL_UINT32(0, mock.sent.size());
}

void test_manual_starts_without_brake_and_runs_one_1000ms_window()
{
    startManual();
    const ObstacleShiftView view = controller.view(21);
    TEST_ASSERT_TRUE(view.virtualParkActive);
    TEST_ASSERT_EQUAL_UINT32(999, view.virtualParkRemainingMs);
    TEST_ASSERT_EQUAL_UINT32(1, view.manualWindowCount);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());
}

void test_manual_request_is_rejected_when_moving_stale_or_not_dr()
{
    BrakeStateView moving = released(1, 20);
    moving.data.stationaryConfirmed = false;
    controller.tick(moving, {false, true, 1}, 20);
    TEST_ASSERT_FALSE(controller.view(20).virtualParkActive);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_MOVING,
                            controller.view(20).reason);

    controller.reset();
    BrakeStateView stale = released(1, 20);
    controller.tick(stale, {false, true, 1}, 521);
    TEST_ASSERT_FALSE(controller.view(521).virtualParkActive);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_BRAKE_STALE,
                            controller.view(521).reason);

    controller.reset();
    BrakeStateView park = released(1, 20);
    park.data.realGear = BRAKE_GEAR_P;
    controller.tick(park, {false, true, 1}, 20);
    TEST_ASSERT_FALSE(controller.view(20).virtualParkActive);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_GEAR_NOT_DR,
                            controller.view(20).reason);
}

void test_manual_window_does_not_repeat_without_new_request()
{
    startManual();
    BrakeStateView brake = released(1, 1020);
    controller.tick(brake, {false, true, 1}, 1020);
    CanFrame after = make118(0x41, 2);
    controller.observeFrame(after, brake, {false, true, 1}, 1021, mock);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());

    controller.tick(brake, {false, true, 2}, 1030);
    controller.observeFrame(after, brake, {false, true, 2}, 1031, mock);
    TEST_ASSERT_EQUAL_UINT32(2, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(2, controller.view(1031).manualWindowCount);
}

void test_manual_and_automatic_windows_are_mutually_exclusive()
{
    startAutomatic();
    BrakeStateView brake = physicalPressedOnly(1, 30);
    controller.tick(brake, {true, true, 1}, 30);
    const ObstacleShiftView view = controller.view(30);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_TRIGGER_AUTOMATIC_BRAKE,
                            view.triggerSource);
    TEST_ASSERT_EQUAL_UINT32(0, view.manualWindowCount);
}

void test_manual_ignores_feature_switch_but_honors_can_write_gate()
{
    BrakeStateView brake = released(1, 20);
    controller.tick(brake, {false, false, 1}, 20);
    TEST_ASSERT_FALSE(controller.view(20).virtualParkActive);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_CAN_WRITE_DISABLED,
                            controller.view(20).reason);

    controller.reset();
    startManual();
    TEST_ASSERT_TRUE(controller.view(21).virtualParkActive);
}

void test_send_failure_stops_and_never_replays()
{
    BrakeStateView brake = released(1, 20);
    controller.tick(brake, {false, true, 1}, 20);
    mock.writeEnabled = false;
    CanFrame frame = make118(0x41, 1);
    controller.observeFrame(frame, brake, {false, true, 1}, 21, mock);
    ObstacleShiftView view = controller.view(21);
    TEST_ASSERT_FALSE(view.virtualParkActive);
    TEST_ASSERT_EQUAL_UINT8(SHIFT_REASON_TX_FAILED, view.reason);
    TEST_ASSERT_EQUAL_UINT32(1, view.virtualParkTxFailCount);

    mock.writeEnabled = true;
    controller.observeFrame(frame, brake, {false, true, 1}, 22, mock);
    TEST_ASSERT_EQUAL_UINT32(0, mock.sent.size());
}

void test_virtual_p_fingerprint_is_not_accepted_as_real_118()
{
    startManual();
    const ObstacleShiftView before = controller.view(21);
    CanFrame echo = mock.sent.back();
    controller.observeFrame(echo, released(1, 22), {false, true, 1}, 22, mock);
    const ObstacleShiftView after = controller.view(22);
    TEST_ASSERT_EQUAL_UINT32(before.real118RxCount, after.real118RxCount);
    TEST_ASSERT_EQUAL_UINT32(1, mock.sent.size());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_d_frame_becomes_expected_virtual_p);
    RUN_TEST(test_unknown_local_gear_still_becomes_virtual_p);
    RUN_TEST(test_wrong_dlc_and_bad_checksum_do_not_build_virtual_p);
    RUN_TEST(test_automatic_waits_for_release_before_first_press);
    RUN_TEST(test_automatic_uses_physical_brake_without_system_or_hold_gates);
    RUN_TEST(test_active_sends_once_per_new_real_118_using_t2can_gear);
    RUN_TEST(test_window_expires_at_1000ms_and_same_press_cannot_retrigger);
    RUN_TEST(test_physical_release_rearms_automatic_after_latch);
    RUN_TEST(test_control_snapshot_stale_at_501ms_stops_window);
    RUN_TEST(test_moving_or_t2can_not_dr_never_starts_automatic);
    RUN_TEST(test_manual_starts_without_brake_and_runs_one_1000ms_window);
    RUN_TEST(test_manual_request_is_rejected_when_moving_stale_or_not_dr);
    RUN_TEST(test_manual_window_does_not_repeat_without_new_request);
    RUN_TEST(test_manual_and_automatic_windows_are_mutually_exclusive);
    RUN_TEST(test_manual_ignores_feature_switch_but_honors_can_write_gate);
    RUN_TEST(test_send_failure_stops_and_never_replays);
    RUN_TEST(test_virtual_p_fingerprint_is_not_accepted_as_real_118);
    return UNITY_END();
}
