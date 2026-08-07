#include <unity.h>
#include "can_frame_types.h"
#include "can_helpers.h"
#include "drivers/mock_driver.h"
#include "handlers.h"

static MockDriver mock;
static NagHandler handler;

static CanFrame makeEpasFrame(uint8_t counter, int16_t torqueCentiNm = 0)
{
    CanFrame frame = {.id = 880, .dlc = 8};
    frame.data[0] = 0x12;
    frame.data[1] = 0x00;
    NagHandler::writeTorqueRaw(frame, NagHandler::centiNmToRaw(torqueCentiNm));
    frame.data[4] = 0x1F;
    frame.data[5] = 0x89;
    frame.data[6] = static_cast<uint8_t>(0x40 | (counter & 0x0F));
    uint16_t sum = 0;
    for (int i = 0; i < 7; ++i)
        sum += frame.data[i];
    frame.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
    return frame;
}

static float decodeTorqueNm(const CanFrame &frame)
{
    return NagHandler::centiNmToNm(
        NagHandler::rawToCentiNm(NagHandler::readTorqueRaw(frame)));
}

void setUp()
{
    mock = MockDriver();
    handler = NagHandler();
    nagKillerRuntime = true;
    nagSetSweepRangeSeconds(5, 8);
    handler.setTestNowMs(0);
    handler.setTestSweepTimingEnabled(true);
}

void tearDown() {}

void test_sweep_defaults_are_five_to_eight_seconds()
{
    TEST_ASSERT_EQUAL_UINT8(5, nagSweepMinSecondsValue());
    TEST_ASSERT_EQUAL_UINT8(8, nagSweepMaxSecondsValue());
}

void test_sweep_range_clamps_and_swaps_to_one_through_thirty_seconds()
{
    nagSetSweepRangeSeconds(40, -3);
    TEST_ASSERT_EQUAL_UINT8(1, nagSweepMinSecondsValue());
    TEST_ASSERT_EQUAL_UINT8(30, nagSweepMaxSecondsValue());
}

void test_first_real_frame_after_enable_is_written_immediately()
{
    CanFrame frame = makeEpasFrame(0);
    handler.handleMessage(frame, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_TRUE(handler.testLastSweepDelayMs() >= 5000);
    TEST_ASSERT_TRUE(handler.testLastSweepDelayMs() <= 8000);
    TEST_ASSERT_EQUAL_UINT32(handler.testLastSweepDelayMs(), handler.testNextEchoAtMs());
}

void test_frames_before_random_deadline_are_not_written_or_queued()
{
    CanFrame first = makeEpasFrame(0);
    handler.handleMessage(first, mock);
    const uint32_t deadline = handler.testNextEchoAtMs();

    for (uint8_t i = 1; i <= 5; ++i)
    {
        handler.setTestNowMs(deadline - (6 - i));
        CanFrame waiting = makeEpasFrame(i);
        handler.handleMessage(waiting, mock);
    }

    TEST_ASSERT_EQUAL(1, mock.sent.size());

    handler.setTestNowMs(deadline);
    CanFrame due = makeEpasFrame(6);
    handler.handleMessage(due, mock);
    TEST_ASSERT_EQUAL(2, mock.sent.size());
}

void test_fixed_sweep_range_uses_exact_delay_each_cycle()
{
    nagSetSweepRangeSeconds(7, 7);
    CanFrame first = makeEpasFrame(0);
    handler.handleMessage(first, mock);

    TEST_ASSERT_EQUAL_UINT32(7000, handler.testLastSweepDelayMs());
    TEST_ASSERT_EQUAL_UINT32(7000, handler.testNextEchoAtMs());

    handler.setTestNowMs(7000);
    CanFrame second = makeEpasFrame(1);
    handler.handleMessage(second, mock);

    TEST_ASSERT_EQUAL(2, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(7000, handler.testLastSweepDelayMs());
    TEST_ASSERT_EQUAL_UINT32(14000, handler.testNextEchoAtMs());
}

void test_off_then_on_without_intermediate_frames_writes_next_frame_immediately()
{
    CanFrame first = makeEpasFrame(0);
    handler.handleMessage(first, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());

    nagKillerRuntime = false;
    nagKillerRuntime = true;

    handler.setTestNowMs(1000);
    CanFrame afterEnable = makeEpasFrame(1);
    handler.handleMessage(afterEnable, mock);

    TEST_ASSERT_EQUAL(2, mock.sent.size());
    TEST_ASSERT_TRUE(handler.testNextEchoAtMs() >= 6000);
    TEST_ASSERT_TRUE(handler.testNextEchoAtMs() <= 9000);
}

void test_range_change_reschedules_without_an_extra_immediate_write()
{
    CanFrame first = makeEpasFrame(0);
    handler.handleMessage(first, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());

    nagSetSweepRangeSeconds(10, 10);
    handler.setTestNowMs(1000);
    CanFrame changed = makeEpasFrame(1);
    handler.handleMessage(changed, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_EQUAL_UINT32(10000, handler.testLastSweepDelayMs());
    TEST_ASSERT_EQUAL_UINT32(11000, handler.testNextEchoAtMs());

    handler.setTestNowMs(10999);
    CanFrame early = makeEpasFrame(2);
    handler.handleMessage(early, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());

    handler.setTestNowMs(11000);
    CanFrame due = makeEpasFrame(3);
    handler.handleMessage(due, mock);
    TEST_ASSERT_EQUAL(2, mock.sent.size());
}

void test_a_mode_keeps_fixed_positive_one_point_eight_nm()
{
    handler.setMode(NagHandler::MODE_A);
    CanFrame frame = makeEpasFrame(0, -80);
    handler.handleMessage(frame, mock);

    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.80f, decodeTorqueNm(mock.sent[0]));
}

void test_a_v2_generates_random_torque_only_when_a_write_is_due()
{
    handler.setMode(NagHandler::MODE_A_V2);
    handler.setAv2RangeNm(-1.50f, 1.50f);

    CanFrame first = makeEpasFrame(0);
    handler.handleMessage(first, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());
    TEST_ASSERT_TRUE(decodeTorqueNm(mock.sent[0]) >= -1.50f);
    TEST_ASSERT_TRUE(decodeTorqueNm(mock.sent[0]) <= 1.50f);

    const uint32_t deadline = handler.testNextEchoAtMs();
    handler.setTestNowMs(deadline - 1);
    CanFrame early = makeEpasFrame(1);
    handler.handleMessage(early, mock);
    TEST_ASSERT_EQUAL(1, mock.sent.size());

    handler.setTestNowMs(deadline);
    CanFrame due = makeEpasFrame(2);
    handler.handleMessage(due, mock);
    TEST_ASSERT_EQUAL(2, mock.sent.size());
    TEST_ASSERT_TRUE(decodeTorqueNm(mock.sent[1]) >= -1.50f);
    TEST_ASSERT_TRUE(decodeTorqueNm(mock.sent[1]) <= 1.50f);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_sweep_defaults_are_five_to_eight_seconds);
    RUN_TEST(test_sweep_range_clamps_and_swaps_to_one_through_thirty_seconds);
    RUN_TEST(test_first_real_frame_after_enable_is_written_immediately);
    RUN_TEST(test_frames_before_random_deadline_are_not_written_or_queued);
    RUN_TEST(test_fixed_sweep_range_uses_exact_delay_each_cycle);
    RUN_TEST(test_off_then_on_without_intermediate_frames_writes_next_frame_immediately);
    RUN_TEST(test_range_change_reschedules_without_an_extra_immediate_write);
    RUN_TEST(test_a_mode_keeps_fixed_positive_one_point_eight_nm);
    RUN_TEST(test_a_v2_generates_random_torque_only_when_a_write_is_due);
    return UNITY_END();
}
