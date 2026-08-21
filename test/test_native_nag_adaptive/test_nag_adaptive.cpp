#include <unity.h>

#include "can_helpers.h"
#include "drivers/mock_driver.h"
#include "handlers.h"

static MockDriver mock;
static NagHandler handler;

static void updateChecksum(CanFrame &frame)
{
    uint16_t sum = 0;
    for (uint8_t index = 0; index < 7; ++index)
        sum += frame.data[index];
    frame.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
}

static CanFrame makeFrame(uint8_t counter,
                          int16_t torqueCentiNm,
                          int16_t angleDeciDeg,
                          uint8_t handsOn = 1)
{
    CanFrame frame = {.id = 880, .dlc = 8};
    frame.data[0] = 0x12;
    frame.data[1] = 0x00;
    frame.data[2] = 0x80;
    NagHandler::writeTorqueRaw(frame, static_cast<uint16_t>(2050 + torqueCentiNm));
    frame.data[4] = static_cast<uint8_t>((handsOn & 0x03) << 6);
    NagHandler::writeSteeringAngleDeciDeg(frame, angleDeciDeg);
    frame.data[6] = static_cast<uint8_t>(0x40 | (counter & 0x0F));
    updateChecksum(frame);
    return frame;
}

static int16_t outputTorqueCentiNm(const CanFrame &frame)
{
    return NagHandler::rawToObservedCentiNm(NagHandler::readTorqueRaw(frame));
}

static void observeAt(uint32_t now,
                      uint8_t counter,
                      int16_t torqueCentiNm,
                      int16_t angleDeciDeg,
                      uint8_t handsOn = 1)
{
    handler.setTestNowMs(now);
    CanFrame frame = makeFrame(counter, torqueCentiNm, angleDeciDeg, handsOn);
    handler.handleMessage(frame, mock);
}

static void armAdaptive(int16_t torqueCentiNm = 20,
                        int16_t angleDeciDeg = 0,
                        uint8_t handsOn = 1)
{
    observeAt(0, 0, torqueCentiNm, angleDeciDeg, handsOn);
    observeAt(10, 1, torqueCentiNm, angleDeciDeg, handsOn);
    observeAt(20, 2, torqueCentiNm, angleDeciDeg, handsOn);
}

void setUp()
{
    mock = MockDriver();
    handler = NagHandler();
    nagKillerRuntime = true;
    handler.setTestNowMs(0);
    handler.setMode(NagHandler::MODE_ADAPTIVE);
}

void tearDown() {}

void test_angle_decoder_uses_both_directions()
{
    CanFrame positive = makeFrame(0, 20, 500);
    CanFrame negative = makeFrame(1, 20, -500);
    TEST_ASSERT_EQUAL_INT16(500, NagHandler::readSteeringAngleDeciDeg(positive));
    TEST_ASSERT_EQUAL_INT16(-500, NagHandler::readSteeringAngleDeciDeg(negative));
}

void test_adaptive_requires_three_valid_frames_before_first_send()
{
    observeAt(0, 0, 20, 0);
    observeAt(10, 1, 20, 0);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    observeAt(20, 2, 20, 0);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_zero_and_small_torque_still_send_nonzero_after_arming()
{
    armAdaptive(0);
    observeAt(30, 3, 1, 0);
    observeAt(40, 4, -1, 0);
    TEST_ASSERT_EQUAL(3, mock.sent.size());
    for (const CanFrame &frame : mock.sent)
        TEST_ASSERT_NOT_EQUAL(0, outputTorqueCentiNm(frame));
}

void test_measured_torque_selects_opposite_injection_direction()
{
    armAdaptive(20);
    TEST_ASSERT_TRUE(outputTorqueCentiNm(mock.sent.back()) < 0);

    setUp();
    armAdaptive(-20);
    TEST_ASSERT_TRUE(outputTorqueCentiNm(mock.sent.back()) > 0);
}

void test_opposite_candidate_must_be_stable_for_100ms_before_flip()
{
    armAdaptive(20);
    observeAt(30, 3, -20, 0);
    observeAt(129, 4, -20, 0);
    TEST_ASSERT_TRUE(outputTorqueCentiNm(mock.sent.back()) < 0);
    observeAt(130, 5, -20, 0);
    TEST_ASSERT_TRUE(outputTorqueCentiNm(mock.sent.back()) > 0);
}

void test_noise_band_holds_previous_direction_and_cancels_candidate()
{
    armAdaptive(20);
    observeAt(30, 3, -20, 0);
    observeAt(80, 4, 0, 0);
    observeAt(130, 5, -20, 0);
    TEST_ASSERT_TRUE(outputTorqueCentiNm(mock.sent.back()) < 0);
    observeAt(229, 6, -20, 0);
    TEST_ASSERT_TRUE(outputTorqueCentiNm(mock.sent.back()) < 0);
    observeAt(230, 7, -20, 0);
    TEST_ASSERT_TRUE(outputTorqueCentiNm(mock.sent.back()) > 0);
}

void test_startup_falls_back_to_opposite_steering_angle()
{
    armAdaptive(0, 100);
    TEST_ASSERT_TRUE(outputTorqueCentiNm(mock.sent.back()) < 0);

    setUp();
    armAdaptive(0, -100);
    TEST_ASSERT_TRUE(outputTorqueCentiNm(mock.sent.back()) > 0);
}

void test_startup_near_zero_uses_deterministic_positive_default()
{
    armAdaptive(0, 0);
    TEST_ASSERT_TRUE(outputTorqueCentiNm(mock.sent.back()) > 0);
    observeAt(30, 3, 0, 0);
    TEST_ASSERT_TRUE(outputTorqueCentiNm(mock.sent.back()) > 0);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::DIRECTION_HOLD,
                            handler.adaptiveController.directionSourceValue());
}

void test_large_angle_and_legacy_pause_time_do_not_stop_sending()
{
    NagAdaptiveConfig config = handler.adaptiveConfig();
    config.sendWindowMs = 1000;
    config.pauseMinMs = 1000;
    config.pauseMaxMs = 1000;
    handler.setAdaptiveConfig(config);
    armAdaptive(20, 600);
    observeAt(1020, 3, 20, -600);
    observeAt(2020, 4, 20, 600);
    TEST_ASSERT_EQUAL(3, mock.sent.size());
}

void test_handson_1_and_2_choose_their_direction_ranges()
{
    handler.setHandsOnRangeCentiNm(1, -1, 110, 110);
    handler.setHandsOnRangeCentiNm(2, -1, 140, 140);
    armAdaptive(20, 0, 1);
    TEST_ASSERT_EQUAL_INT16(-110, outputTorqueCentiNm(mock.sent.back()));

    setUp();
    handler.setHandsOnRangeCentiNm(1, -1, 110, 110);
    handler.setHandsOnRangeCentiNm(2, -1, 140, 140);
    armAdaptive(20, 0, 2);
    TEST_ASSERT_EQUAL_INT16(-140, outputTorqueCentiNm(mock.sent.back()));
}

void test_handson_0_and_3_retain_last_valid_tier_with_h1_boot_fallback()
{
    observeAt(0, 0, 20, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(1, handler.handsOnTier());
    observeAt(10, 1, 20, 0, 2);
    TEST_ASSERT_EQUAL_UINT8(2, handler.handsOnTier());
    observeAt(20, 2, 20, 0, 3);
    TEST_ASSERT_EQUAL_UINT8(2, handler.handsOnTier());
    TEST_ASSERT_EQUAL(1, mock.sent.size());
}

void test_input_handson_is_captured_before_output_is_forced_to_one()
{
    armAdaptive(20, 0, 2);
    TEST_ASSERT_EQUAL_UINT8(2, handler.handsOnRaw());
    TEST_ASSERT_EQUAL_UINT8(2, handler.handsOnTier());
    TEST_ASSERT_EQUAL_UINT8(1, (mock.sent.back().data[4] >> 6) & 0x03);
}

void test_bad_checksum_is_rejected_and_does_not_arm()
{
    observeAt(0, 0, 20, 0);
    handler.setTestNowMs(10);
    CanFrame invalid = makeFrame(1, 20, 0);
    invalid.data[7]++;
    handler.handleMessage(invalid, mock);
    observeAt(20, 2, 20, 0);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    observeAt(30, 3, 20, 0);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagChecksumRejectCount);
}

void test_failed_send_does_not_update_actual_injection_snapshot()
{
    mock.writeEnabled = false;
    armAdaptive();
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagSendAttemptCount);
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagSendFailureCount);
    TEST_ASSERT_EQUAL_UINT32(0, handler.nagEchoCount);
    TEST_ASSERT_FALSE(handler.injectedTorqueIsValid());
}

void test_successful_send_snapshot_expires_after_200ms()
{
    armAdaptive();
    TEST_ASSERT_TRUE(handler.injectedTorqueIsValid());
    TEST_ASSERT_TRUE(handler.lastInjectedCenti() < 0);
    handler.setTestNowMs(220);
    TEST_ASSERT_TRUE(handler.injectedTorqueIsValid());
    handler.setTestNowMs(221);
    TEST_ASSERT_FALSE(handler.injectedTorqueIsValid());
}

void test_reserved_torque_is_rejected()
{
    handler.setTestNowMs(0);
    CanFrame frame = makeFrame(0, 20, 0);
    NagHandler::writeTorqueRaw(frame, 0);
    updateChecksum(frame);
    handler.handleMessage(frame, mock);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagInvalidTorqueRejectCount);
}

void test_successful_echo_is_skipped_as_own_echo()
{
    armAdaptive();
    CanFrame ownEcho = mock.sent.back();
    handler.setTestNowMs(30);
    handler.handleMessage(ownEcho, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagOwnEchoSkipCount);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_angle_decoder_uses_both_directions);
    RUN_TEST(test_adaptive_requires_three_valid_frames_before_first_send);
    RUN_TEST(test_zero_and_small_torque_still_send_nonzero_after_arming);
    RUN_TEST(test_measured_torque_selects_opposite_injection_direction);
    RUN_TEST(test_opposite_candidate_must_be_stable_for_100ms_before_flip);
    RUN_TEST(test_noise_band_holds_previous_direction_and_cancels_candidate);
    RUN_TEST(test_startup_falls_back_to_opposite_steering_angle);
    RUN_TEST(test_startup_near_zero_uses_deterministic_positive_default);
    RUN_TEST(test_large_angle_and_legacy_pause_time_do_not_stop_sending);
    RUN_TEST(test_handson_1_and_2_choose_their_direction_ranges);
    RUN_TEST(test_handson_0_and_3_retain_last_valid_tier_with_h1_boot_fallback);
    RUN_TEST(test_input_handson_is_captured_before_output_is_forced_to_one);
    RUN_TEST(test_bad_checksum_is_rejected_and_does_not_arm);
    RUN_TEST(test_failed_send_does_not_update_actual_injection_snapshot);
    RUN_TEST(test_successful_send_snapshot_expires_after_200ms);
    RUN_TEST(test_reserved_torque_is_rejected);
    RUN_TEST(test_successful_echo_is_skipped_as_own_echo);
    return UNITY_END();
}
