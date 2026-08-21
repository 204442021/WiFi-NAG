#include <unity.h>

#include "can_frame_types.h"
#include "can_helpers.h"
#include "drivers/mock_driver.h"
#include "handlers.h"

static MockDriver mock;
static NagHandler handler;

// V1.0.3 exposed the production-only write scheduler to native tests. Keep
// this compatibility probe so the regression test exercises that scheduler
// before the rollback, while becoming a no-op once the V1.0.2 handler is back.
template <typename Handler>
static auto enableProductionTimingIfPresent(Handler &value, int)
    -> decltype(value.setTestSweepTimingEnabled(true), void())
{
    value.setTestSweepTimingEnabled(true);
}

template <typename Handler>
static void enableProductionTimingIfPresent(Handler &, long)
{
}

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
    for (int index = 0; index < 7; ++index)
        sum += frame.data[index];
    frame.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
    return frame;
}

static void updateChecksum(CanFrame &frame)
{
    uint16_t sum = 0;
    for (uint8_t index = 0; index < 7; ++index)
        sum += frame.data[index];
    frame.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
}

static CanFrame makeDasFrame(uint8_t hos)
{
    CanFrame frame = {.id = NagDasFeedbackTracker::kDasCanId, .dlc = 8};
    frame.data[5] = static_cast<uint8_t>((hos & 0x0F) << 2);
    return frame;
}

static void primeAdaptiveSuccessfulEcho(uint8_t counter = 0x0C)
{
    handler.setMode(NagHandler::MODE_ADAPTIVE);
    handler.setTestNowMs(80);
    handler.setTestNowUs(80000);
    CanFrame das = makeDasFrame(0);
    handler.handleMessage(das, mock);
    for (uint8_t index = 0; index < 3; ++index)
    {
        handler.setTestNowMs(80U + static_cast<uint32_t>(index) * 10U);
        handler.setTestNowUs(80000ULL + static_cast<uint64_t>(index) * 10000ULL);
        CanFrame epas = makeEpasFrame(static_cast<uint8_t>(counter - 2U + index));
        handler.handleMessage(epas, mock);
    }
    TEST_ASSERT_EQUAL(1, mock.sent.size());
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
    handler.setTestNowMs(0);
    handler.setTestNowUs(0);
    enableProductionTimingIfPresent(handler, 0);
}

void tearDown() {}

void test_each_new_real_370_frame_is_echoed_without_a_send_interval()
{
    for (uint8_t counter = 0; counter < 4; ++counter)
    {
        handler.setTestNowMs(static_cast<uint32_t>(counter) * 10U);
        CanFrame frame = makeEpasFrame(counter);
        handler.handleMessage(frame, mock);
    }

    TEST_ASSERT_EQUAL(4, mock.sent.size());
    for (uint8_t index = 0; index < 4; ++index)
        TEST_ASSERT_EQUAL_UINT8((index + 1U) & 0x0FU,
                                mock.sent[index].data[6] & 0x0FU);
}

void test_a_mode_keeps_positive_one_point_eight_nm_on_every_echo()
{
    handler.setMode(NagHandler::MODE_A);
    for (uint8_t counter = 0; counter < 3; ++counter)
    {
        handler.setTestNowMs(static_cast<uint32_t>(counter) * 10U);
        CanFrame frame = makeEpasFrame(counter, -80);
        handler.handleMessage(frame, mock);
    }

    TEST_ASSERT_EQUAL(3, mock.sent.size());
    for (const CanFrame &frame : mock.sent)
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.80f, decodeTorqueNm(frame));
}

void test_a_v2_compatibility_value_falls_back_to_continuous_mode()
{
    handler.setMode(NagHandler::MODE_A_V2);
    TEST_ASSERT_EQUAL_UINT8(NagHandler::MODE_A, (uint8_t)handler.nagMode);
    for (uint8_t index = 0; index < 4; ++index)
    {
        handler.setTestNowMs(static_cast<uint32_t>(index) * 500U);
        CanFrame frame = makeEpasFrame(index);
        handler.handleMessage(frame, mock);
    }

    TEST_ASSERT_EQUAL(4, mock.sent.size());
    for (const CanFrame &frame : mock.sent)
        TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.80f, decodeTorqueNm(frame));
}

void test_counter_collision_is_recorded_on_next_oem_counter_match()
{
    handler.setTestNowMs(100);
    handler.setTestNowUs(100000);
    CanFrame first = makeEpasFrame(0x0C);
    handler.handleMessage(first, mock);

    handler.setTestNowMs(120);
    handler.setTestNowUs(120000);
    CanFrame collision = makeEpasFrame(0x0D);
    handler.handleMessage(collision, mock);

    TEST_ASSERT_EQUAL_UINT32(1, handler.nagCounterCollisionCount);
    TEST_ASSERT_EQUAL_UINT32(20000, handler.nagLastCounterCollisionGapUs);

    handler.setTestNowMs(221);
    handler.setTestNowUs(220001);
    CanFrame outsideWindow = makeEpasFrame(0x0E);
    handler.handleMessage(outsideWindow, mock);
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagCounterCollisionCount);
    TEST_ASSERT_EQUAL_UINT32(20000, handler.nagLastCounterCollisionGapUs);
}

void test_counter_wrap_15_to_0_is_recorded_correctly()
{
    handler.setTestNowMs(100);
    handler.setTestNowUs(100000);
    CanFrame first = makeEpasFrame(0x0F);
    handler.handleMessage(first, mock);

    handler.setTestNowMs(120);
    handler.setTestNowUs(120000);
    CanFrame collision = makeEpasFrame(0x00);
    handler.handleMessage(collision, mock);

    TEST_ASSERT_EQUAL_UINT32(1, handler.nagCounterCollisionCount);
    TEST_ASSERT_EQUAL_UINT32(20000, handler.nagLastCounterCollisionGapUs);
}

void test_counter_collision_window_includes_one_us_and_zero_tx_timestamp()
{
    handler.setTestNowMs(0);
    handler.setTestNowUs(0);
    CanFrame first = makeEpasFrame(0x0C);
    handler.handleMessage(first, mock);

    handler.setTestNowUs(1);
    CanFrame collision = makeEpasFrame(0x0D);
    handler.handleMessage(collision, mock);

    TEST_ASSERT_EQUAL_UINT32(1, handler.nagCounterCollisionCount);
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagLastCounterCollisionGapUs);
}

void test_counter_collision_window_includes_100000_us()
{
    handler.setTestNowMs(100);
    handler.setTestNowUs(100000);
    CanFrame first = makeEpasFrame(0x0C);
    handler.handleMessage(first, mock);

    handler.setTestNowMs(200);
    handler.setTestNowUs(200000);
    CanFrame collision = makeEpasFrame(0x0D);
    handler.handleMessage(collision, mock);

    TEST_ASSERT_EQUAL_UINT32(1, handler.nagCounterCollisionCount);
    TEST_ASSERT_EQUAL_UINT32(100000, handler.nagLastCounterCollisionGapUs);
}

void test_adaptive_bad_checksum_does_not_qualify_or_consume_collision_match()
{
    primeAdaptiveSuccessfulEcho();

    handler.setTestNowMs(110);
    handler.setTestNowUs(110000);
    CanFrame rejected = makeEpasFrame(0x0D);
    rejected.data[7] ^= 0x01;
    handler.handleMessage(rejected, mock);
    TEST_ASSERT_EQUAL_UINT32(0, handler.nagCounterCollisionCount);

    handler.setTestNowMs(120);
    handler.setTestNowUs(120000);
    CanFrame accepted = makeEpasFrame(0x0D);
    handler.handleMessage(accepted, mock);
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagCounterCollisionCount);
    TEST_ASSERT_EQUAL_UINT32(20000, handler.nagLastCounterCollisionGapUs);
}

void test_adaptive_reserved_torque_does_not_qualify_or_consume_collision_match()
{
    primeAdaptiveSuccessfulEcho();

    handler.setTestNowMs(110);
    handler.setTestNowUs(110000);
    CanFrame rejected = makeEpasFrame(0x0D);
    NagHandler::writeTorqueRaw(rejected, 0);
    updateChecksum(rejected);
    handler.handleMessage(rejected, mock);
    TEST_ASSERT_EQUAL_UINT32(0, handler.nagCounterCollisionCount);

    handler.setTestNowMs(120);
    handler.setTestNowUs(120000);
    CanFrame accepted = makeEpasFrame(0x0D);
    handler.handleMessage(accepted, mock);
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagCounterCollisionCount);
    TEST_ASSERT_EQUAL_UINT32(20000, handler.nagLastCounterCollisionGapUs);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_each_new_real_370_frame_is_echoed_without_a_send_interval);
    RUN_TEST(test_a_mode_keeps_positive_one_point_eight_nm_on_every_echo);
    RUN_TEST(test_a_v2_compatibility_value_falls_back_to_continuous_mode);
    RUN_TEST(test_counter_collision_is_recorded_on_next_oem_counter_match);
    RUN_TEST(test_counter_wrap_15_to_0_is_recorded_correctly);
    RUN_TEST(test_counter_collision_window_includes_one_us_and_zero_tx_timestamp);
    RUN_TEST(test_counter_collision_window_includes_100000_us);
    RUN_TEST(test_adaptive_bad_checksum_does_not_qualify_or_consume_collision_match);
    RUN_TEST(test_adaptive_reserved_torque_does_not_qualify_or_consume_collision_match);
    return UNITY_END();
}
