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
                          int16_t angleDeciDeg)
{
    CanFrame frame = {.id = 880, .dlc = 8};
    frame.data[0] = 0x12;
    frame.data[1] = 0x00;
    frame.data[2] = 0x80;
    NagHandler::writeTorqueRaw(
        frame,
        static_cast<uint16_t>(2050 + torqueCentiNm));
    frame.data[4] = 0x00;
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
                      int16_t angleDeciDeg)
{
    handler.setTestNowMs(now);
    CanFrame frame = makeFrame(counter, torqueCentiNm, angleDeciDeg);
    handler.handleMessage(frame, mock);
}

static void armAdaptive(int16_t torqueCentiNm = 20)
{
    observeAt(0, 0, torqueCentiNm, 0);
    observeAt(10, 1, torqueCentiNm, 0);
    observeAt(20, 2, torqueCentiNm, 0);
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

void test_positive_vehicle_torque_sends_negative_and_negative_sends_positive()
{
    armAdaptive(20);
    TEST_ASSERT_EQUAL_INT16(-180, outputTorqueCentiNm(mock.sent.back()));

    observeAt(30, 3, -20, 0);
    TEST_ASSERT_EQUAL(2, mock.sent.size());
    TEST_ASSERT_EQUAL_INT16(180, outputTorqueCentiNm(mock.sent.back()));
}

void test_torque_deadband_does_not_send()
{
    armAdaptive(5);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    observeAt(30, 3, -5, 0);
    TEST_ASSERT_EQUAL(0, mock.sent.size());
    observeAt(40, 4, 6, 0);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_INT16(-180, outputTorqueCentiNm(mock.sent.back()));
}

void test_positive_fifty_degrees_blocks_immediately()
{
    armAdaptive();
    observeAt(30, 3, 20, 500);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::BLOCK_ANGLE,
                            handler.adaptiveController.blockReasonValue());
}

void test_negative_fifty_degrees_blocks_immediately()
{
    armAdaptive();
    observeAt(30, 3, 20, -500);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::BLOCK_ANGLE,
                            handler.adaptiveController.blockReasonValue());
}

void test_angle_block_requires_three_frames_at_or_below_forty_five_degrees()
{
    armAdaptive();
    observeAt(30, 3, 20, 500);
    observeAt(40, 4, 20, 450);
    observeAt(50, 5, 20, -450);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    observeAt(60, 6, 20, 450);
    TEST_ASSERT_EQUAL(2, mock.sent.size());
}

void test_angle_fields_are_preserved_in_echo()
{
    armAdaptive();
    observeAt(30, 3, -20, 321);
    TEST_ASSERT_EQUAL(2, mock.sent.size());
    TEST_ASSERT_EQUAL_INT16(321,
                            NagHandler::readSteeringAngleDeciDeg(mock.sent.back()));
    TEST_ASSERT_EQUAL_UINT8(1, (mock.sent.back().data[4] >> 6) & 0x03);
}

void test_send_window_then_pause_then_resume()
{
    NagAdaptiveConfig config = handler.adaptiveConfig();
    config.sendWindowMs = 1000;
    config.pauseMinMs = 1000;
    config.pauseMaxMs = 1000;
    handler.setAdaptiveConfig(config);

    armAdaptive();
    observeAt(1019, 3, 20, 0);
    TEST_ASSERT_EQUAL(2, mock.sent.size());
    observeAt(1020, 4, 20, 0);
    observeAt(2019, 5, 20, 0);
    TEST_ASSERT_EQUAL(2, mock.sent.size());
    observeAt(2020, 6, 20, 0);
    TEST_ASSERT_EQUAL(3, mock.sent.size());
}

void test_random_pause_stays_inside_configured_range()
{
    NagAdaptiveController controller;
    NagAdaptiveConfig config;
    config.sendWindowMs = 1000;
    config.pauseMinMs = 1000;
    config.pauseMaxMs = 3000;
    controller.setConfig(config);

    controller.observe(0, 0, 20, 1);
    controller.observe(1, 0, 20, 2);
    controller.observe(2, 0, 20, 3);

    uint32_t now = 1002;
    uint32_t firstPause = 0;
    bool sawDifferentPause = false;
    for (uint32_t cycle = 0; cycle < 8; ++cycle)
    {
        controller.observe(now, 0, 20, 0x12340000u + cycle);
        const uint32_t pauseMs = controller.currentPauseMs();
        TEST_ASSERT_TRUE(pauseMs >= 1000);
        TEST_ASSERT_TRUE(pauseMs <= 3000);
        if (cycle == 0)
            firstPause = pauseMs;
        else if (pauseMs != firstPause)
            sawDifferentPause = true;

        now += pauseMs;
        TEST_ASSERT_TRUE(controller.observe(now, 0, 20, cycle).shouldSend);
        now += 1000;
    }
    TEST_ASSERT_TRUE(sawDifferentPause);
}

void test_pause_bounds_are_normalized_and_swapped()
{
    NagAdaptiveConfig config;
    config.pauseMinMs = 9000;
    config.pauseMaxMs = 500;
    const NagAdaptiveConfig normalized = NagAdaptiveController::normalizeConfig(config);
    TEST_ASSERT_EQUAL_UINT32(1000, normalized.pauseMinMs);
    TEST_ASSERT_EQUAL_UINT32(9000, normalized.pauseMaxMs);
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

void test_failed_send_is_not_counted_as_success()
{
    mock.writeEnabled = false;
    armAdaptive();
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagSendAttemptCount);
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagSendFailureCount);
    TEST_ASSERT_EQUAL_UINT32(0, handler.nagEchoCount);
    TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
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
    RUN_TEST(test_positive_vehicle_torque_sends_negative_and_negative_sends_positive);
    RUN_TEST(test_torque_deadband_does_not_send);
    RUN_TEST(test_positive_fifty_degrees_blocks_immediately);
    RUN_TEST(test_negative_fifty_degrees_blocks_immediately);
    RUN_TEST(test_angle_block_requires_three_frames_at_or_below_forty_five_degrees);
    RUN_TEST(test_angle_fields_are_preserved_in_echo);
    RUN_TEST(test_send_window_then_pause_then_resume);
    RUN_TEST(test_random_pause_stays_inside_configured_range);
    RUN_TEST(test_pause_bounds_are_normalized_and_swapped);
    RUN_TEST(test_bad_checksum_is_rejected_and_does_not_arm);
    RUN_TEST(test_failed_send_is_not_counted_as_success);
    RUN_TEST(test_successful_echo_is_skipped_as_own_echo);
    return UNITY_END();
}
