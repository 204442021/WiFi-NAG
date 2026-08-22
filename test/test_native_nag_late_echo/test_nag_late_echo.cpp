#include <unity.h>

#include "drivers/mock_driver.h"
#include "handlers.h"
#include "nag_late_echo_scheduler.h"

void setUp() {}
void tearDown() {}

static void makeReady(NagLateEchoScheduler &scheduler, uint64_t firstUs = 100000U,
                      uint32_t periodUs = 9900U)
{
    scheduler.observeOem(firstUs);
    for (uint8_t sample = 0; sample < 8; ++sample)
        scheduler.observeOem(firstUs + static_cast<uint64_t>(sample + 1U) * periodUs);
}

static CanFrame makeHandlerEpasFrame(uint8_t counter, int16_t torqueCentiNm = 20)
{
    CanFrame frame = {.id = 0x370, .dlc = 8};
    frame.data[0] = 0x12;
    frame.data[1] = 0x00;
    frame.data[2] = 0x80;
    NagHandler::writeTorqueRaw(frame, NagHandler::centiNmToRaw(torqueCentiNm));
    NagHandler::writeSteeringAngleDeciDeg(frame, 0);
    frame.data[6] = static_cast<uint8_t>(0x40 | (counter & 0x0F));
    uint16_t sum = 0;
    for (uint8_t index = 0; index < 7; ++index)
        sum += frame.data[index];
    frame.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
    return frame;
}

static void handleAtUs(NagHandler &handler, MockDriver &driver,
                       CanFrame frame, uint64_t nowUs)
{
    handler.setTestNowUs(nowUs);
    handler.setTestNowMs(static_cast<uint32_t>(nowUs / 1000U));
    handler.handleMessage(frame, driver);
}

static void enableLateEcho(NagHandler &handler)
{
    NagAdaptiveConfig config = handler.adaptiveConfig();
    config.lateEchoEnabled = true;
    handler.setAdaptiveConfig(config);
}

static uint64_t warmHandlerUntilPending(NagHandler &handler, MockDriver &driver,
                                        uint64_t firstUs = 100000U,
                                        uint32_t periodUs = 9900U)
{
    for (uint8_t index = 0; index < 9; ++index)
        handleAtUs(handler, driver, makeHandlerEpasFrame(index),
                   firstUs + static_cast<uint64_t>(index) * periodUs);
    return firstUs + 8ULL * periodUs;
}

void test_late_echo_defaults_off_and_cannot_arm()
{
    NagLateEchoScheduler scheduler;
    TEST_ASSERT_FALSE(scheduler.enabled());
    TEST_ASSERT_FALSE(scheduler.ready());
    TEST_ASSERT_FALSE(scheduler.pending());
    scheduler.observeOem(100000U);
    TEST_ASSERT_FALSE(scheduler.arm(100000U));
    TEST_ASSERT_EQUAL_UINT8(NagLateEchoScheduler::POLL_IDLE,
                            scheduler.poll(110000U));
}

void test_late_echo_becomes_ready_after_eight_valid_smoothed_samples()
{
    NagLateEchoScheduler scheduler;
    scheduler.setEnabled(true);
    scheduler.observeOem(100000U);
    for (uint8_t sample = 0; sample < 7; ++sample)
    {
        scheduler.observeOem(100000U + static_cast<uint64_t>(sample + 1U) * 9900U);
        TEST_ASSERT_FALSE(scheduler.ready());
    }
    scheduler.observeOem(179200U);
    TEST_ASSERT_TRUE(scheduler.ready());
    TEST_ASSERT_EQUAL_UINT32(9900U, scheduler.estimatedPeriodUs());

    scheduler.observeOem(189300U);
    TEST_ASSERT_EQUAL_UINT32(9925U, scheduler.estimatedPeriodUs());
}

void test_late_echo_rejects_period_outliers_without_polluting_estimate()
{
    NagLateEchoScheduler scheduler;
    scheduler.setEnabled(true);
    makeReady(scheduler);
    const uint32_t estimate = scheduler.estimatedPeriodUs();

    scheduler.observeOem(183900U); // 4.7 ms: too short.
    TEST_ASSERT_EQUAL_UINT32(estimate, scheduler.estimatedPeriodUs());
    scheduler.observeOem(208900U); // 25 ms: too long.
    TEST_ASSERT_EQUAL_UINT32(estimate, scheduler.estimatedPeriodUs());
}

void test_late_echo_due_and_expiry_windows_are_exact()
{
    NagLateEchoScheduler due;
    due.setEnabled(true);
    makeReady(due);
    const uint64_t lastOemUs = due.lastOemUs();
    const uint64_t scheduledUs = lastOemUs + due.estimatedPeriodUs() - 1000U;
    TEST_ASSERT_TRUE(due.arm(lastOemUs));
    TEST_ASSERT_EQUAL_UINT8(NagLateEchoScheduler::POLL_WAITING,
                            due.poll(scheduledUs - 1U));
    TEST_ASSERT_EQUAL_UINT8(NagLateEchoScheduler::POLL_DUE,
                            due.poll(scheduledUs));
    TEST_ASSERT_FALSE(due.pending());
    TEST_ASSERT_EQUAL_UINT32(1000U, due.lastLeadUs());

    NagLateEchoScheduler expired;
    expired.setEnabled(true);
    makeReady(expired);
    const uint64_t expiryUs = expired.lastOemUs() +
                              expired.estimatedPeriodUs() + 500U;
    TEST_ASSERT_TRUE(expired.arm(expired.lastOemUs()));
    TEST_ASSERT_EQUAL_UINT8(NagLateEchoScheduler::POLL_EXPIRED,
                            expired.poll(expiryUs + 1U));
    TEST_ASSERT_EQUAL_UINT32(1U, expired.expiredCount());
    TEST_ASSERT_EQUAL_UINT8(NagLateEchoScheduler::POLL_IDLE,
                            expired.poll(expiryUs + 2U));
}

void test_late_echo_next_oem_cancels_single_pending_job()
{
    NagLateEchoScheduler scheduler;
    scheduler.setEnabled(true);
    makeReady(scheduler);
    const uint64_t previousOemUs = scheduler.lastOemUs();
    TEST_ASSERT_TRUE(scheduler.arm(previousOemUs));
    TEST_ASSERT_FALSE(scheduler.arm(previousOemUs));
    TEST_ASSERT_EQUAL_UINT32(1U, scheduler.scheduledCount());

    scheduler.observeOem(previousOemUs + 8000U);
    TEST_ASSERT_FALSE(scheduler.pending());
    TEST_ASSERT_EQUAL_UINT32(1U, scheduler.earlyCancelCount());
    TEST_ASSERT_TRUE(scheduler.arm(scheduler.lastOemUs()));
    TEST_ASSERT_TRUE(scheduler.pending());
    TEST_ASSERT_EQUAL_UINT32(2U, scheduler.scheduledCount());

    scheduler.setEnabled(false);
    TEST_ASSERT_FALSE(scheduler.pending());
    TEST_ASSERT_FALSE(scheduler.ready());
}

void test_handler_late_echo_warms_immediately_then_sends_once_at_due_time()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    enableLateEcho(handler);

    const uint64_t lastOemUs = warmHandlerUntilPending(handler, driver);
    TEST_ASSERT_EQUAL(8, driver.sent.size());
    TEST_ASSERT_TRUE(handler.lateEchoReady());
    TEST_ASSERT_TRUE(handler.lateEchoPending());
    TEST_ASSERT_EQUAL_UINT32(1U, handler.nagLateEchoScheduledCount);

    const uint64_t dueUs = lastOemUs + handler.lateEchoEstimatedPeriodUs() - 1000U;
    handler.setTestNowUs(dueUs - 1U);
    TEST_ASSERT_FALSE(handler.serviceTimedSend(driver, true));
    TEST_ASSERT_EQUAL(8, driver.sent.size());

    handler.setTestNowUs(dueUs);
    TEST_ASSERT_TRUE(handler.serviceTimedSend(driver, true));
    TEST_ASSERT_EQUAL(9, driver.sent.size());
    TEST_ASSERT_FALSE(handler.lateEchoPending());
    TEST_ASSERT_EQUAL_UINT32(1U, handler.nagLateEchoSentCount);
    TEST_ASSERT_EQUAL_UINT8(9U, driver.sent.back().data[6] & 0x0F);
    TEST_ASSERT_TRUE(NagHandler::verifyChecksum(driver.sent.back()));
}

void test_handler_next_oem_cancels_old_pending_and_replaces_it()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    enableLateEcho(handler);
    const uint64_t previousOemUs = warmHandlerUntilPending(handler, driver);
    const uint64_t oldDueUs = previousOemUs +
                              handler.lateEchoEstimatedPeriodUs() - 1000U;

    handleAtUs(handler, driver, makeHandlerEpasFrame(9), previousOemUs + 5000U);
    TEST_ASSERT_EQUAL(8, driver.sent.size());
    TEST_ASSERT_TRUE(handler.lateEchoPending());
    TEST_ASSERT_EQUAL_UINT32(1U, handler.nagLateEchoEarlyCancelCount);
    TEST_ASSERT_EQUAL_UINT32(2U, handler.nagLateEchoScheduledCount);

    handler.setTestNowUs(oldDueUs);
    TEST_ASSERT_FALSE(handler.serviceTimedSend(driver, true));
    TEST_ASSERT_EQUAL(8, driver.sent.size());
}

void test_handler_late_echo_expires_and_runtime_off_clears_pending()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    enableLateEcho(handler);
    uint64_t lastOemUs = warmHandlerUntilPending(handler, driver);

    handler.setTestNowUs(lastOemUs + handler.lateEchoEstimatedPeriodUs() + 501U);
    TEST_ASSERT_FALSE(handler.serviceTimedSend(driver, true));
    TEST_ASSERT_FALSE(handler.lateEchoPending());
    TEST_ASSERT_EQUAL_UINT32(1U, handler.nagLateEchoExpiredCount);
    TEST_ASSERT_EQUAL(8, driver.sent.size());

    handleAtUs(handler, driver, makeHandlerEpasFrame(9), lastOemUs + 9900U);
    TEST_ASSERT_TRUE(handler.lateEchoPending());
    nagKillerRuntime = false;
    TEST_ASSERT_FALSE(handler.serviceTimedSend(driver, true));
    TEST_ASSERT_FALSE(handler.lateEchoPending());
    nagKillerRuntime = true;
}

void test_handler_h6_feedback_clears_pending_late_echo()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    enableLateEcho(handler);
    const uint64_t lastOemUs = warmHandlerUntilPending(handler, driver);
    TEST_ASSERT_TRUE(handler.lateEchoPending());

    CanFrame das = {.id = NagDasFeedbackTracker::kDasCanId, .dlc = 8};
    das.data[5] = static_cast<uint8_t>(6U << 2);
    handleAtUs(handler, driver, das, lastOemUs + 100U);
    TEST_ASSERT_FALSE(handler.lateEchoPending());
}

void test_handler_adaptive_das_stale_clears_learned_late_echo_timing()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    NagAdaptiveConfig config = handler.adaptiveConfig();
    config.lateEchoEnabled = true;
    handler.setAdaptiveConfig(config);
    handler.setMode(NagHandler::MODE_ADAPTIVE);

    CanFrame das = {.id = NagDasFeedbackTracker::kDasCanId, .dlc = 8};
    das.data[5] = 0;
    handleAtUs(handler, driver, das, 100000U);
    const uint64_t lastOemUs = warmHandlerUntilPending(handler, driver);
    TEST_ASSERT_TRUE(handler.lateEchoReady());
    const uint64_t dueUs = lastOemUs + handler.lateEchoEstimatedPeriodUs() - 1000U;
    handler.setTestNowUs(dueUs);
    handler.setTestNowMs(static_cast<uint32_t>(dueUs / 1000U));
    TEST_ASSERT_TRUE(handler.serviceTimedSend(driver, true));
    TEST_ASSERT_TRUE(handler.lateEchoReady());

    handler.setTestNowUs(1000000U);
    handler.setTestNowMs(1000U);
    TEST_ASSERT_FALSE(handler.serviceTimedSend(driver, true));
    TEST_ASSERT_FALSE(handler.lateEchoReady());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_late_echo_defaults_off_and_cannot_arm);
    RUN_TEST(test_late_echo_becomes_ready_after_eight_valid_smoothed_samples);
    RUN_TEST(test_late_echo_rejects_period_outliers_without_polluting_estimate);
    RUN_TEST(test_late_echo_due_and_expiry_windows_are_exact);
    RUN_TEST(test_late_echo_next_oem_cancels_single_pending_job);
    RUN_TEST(test_handler_late_echo_warms_immediately_then_sends_once_at_due_time);
    RUN_TEST(test_handler_next_oem_cancels_old_pending_and_replaces_it);
    RUN_TEST(test_handler_late_echo_expires_and_runtime_off_clears_pending);
    RUN_TEST(test_handler_h6_feedback_clears_pending_late_echo);
    RUN_TEST(test_handler_adaptive_das_stale_clears_learned_late_echo_timing);
    return UNITY_END();
}
