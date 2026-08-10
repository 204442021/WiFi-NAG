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

void test_a_v2_uses_the_2000ms_torque_curve_while_echoing_every_frame()
{
    handler.setMode(NagHandler::MODE_A_V2);
    handler.setAv2RangeNm(1.50f, 1.80f);

    const uint32_t times[] = {0U, 500U, 1000U, 2000U};
    float observed[4] = {};
    for (uint8_t index = 0; index < 4; ++index)
    {
        handler.setTestNowMs(times[index]);
        CanFrame frame = makeEpasFrame(index);
        handler.handleMessage(frame, mock);
        if (mock.sent.size() > index)
            observed[index] = decodeTorqueNm(mock.sent[index]);
    }

    TEST_ASSERT_EQUAL(4, mock.sent.size());
    for (float torque : observed)
    {
        TEST_ASSERT_TRUE(torque >= 1.50f);
        TEST_ASSERT_TRUE(torque <= 1.80f);
    }
    TEST_ASSERT_TRUE(observed[0] != observed[1] ||
                     observed[1] != observed[2] ||
                     observed[2] != observed[3]);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_each_new_real_370_frame_is_echoed_without_a_send_interval);
    RUN_TEST(test_a_mode_keeps_positive_one_point_eight_nm_on_every_echo);
    RUN_TEST(test_a_v2_uses_the_2000ms_torque_curve_while_echoing_every_frame);
    return UNITY_END();
}
