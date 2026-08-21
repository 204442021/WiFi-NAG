#include <unity.h>

#include "can_helpers.h"
#include "drivers/mock_driver.h"
#include "handlers.h"
#include "nag_adaptive_controller.h"

static constexpr uint32_t kEntropy = 0x13579BDFu;

static CanFrame makeDasFrame(uint8_t hos)
{
    CanFrame frame = {.id = NagDasFeedbackTracker::kDasCanId, .dlc = 8};
    frame.data[5] = static_cast<uint8_t>((hos & 0x0F) << 2);
    return frame;
}

static void updateHandlerChecksum(CanFrame &frame)
{
    uint16_t sum = 0;
    for (uint8_t index = 0; index < 7; ++index)
        sum += frame.data[index];
    frame.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
}

static CanFrame makeHandlerEpasFrame(uint8_t counter,
                                     int16_t torqueCentiNm = 20,
                                     int16_t angleDeciDeg = 0)
{
    CanFrame frame = {.id = 0x370, .dlc = 8};
    frame.data[0] = 0x12;
    frame.data[1] = 0x00;
    frame.data[2] = 0x80;
    NagHandler::writeTorqueRaw(frame, NagHandler::centiNmToRaw(torqueCentiNm));
    NagHandler::writeSteeringAngleDeciDeg(frame, angleDeciDeg);
    frame.data[6] = static_cast<uint8_t>(0x40 | (counter & 0x0F));
    updateHandlerChecksum(frame);
    return frame;
}

static CanFrame makeExpectedEcho(const CanFrame &oem, int16_t torqueCentiNm)
{
    CanFrame echo = oem;
    NagHandler::writeTorqueRaw(echo, NagHandler::centiNmToRaw(torqueCentiNm));
    echo.data[4] = static_cast<uint8_t>((oem.data[4] & 0x3F) | 0x40);
    echo.data[6] = static_cast<uint8_t>((oem.data[6] & 0xF0) |
                                       (((oem.data[6] & 0x0F) + 1U) & 0x0F));
    updateHandlerChecksum(echo);
    return echo;
}

static void handleAt(NagHandler &handler, MockDriver &driver,
                     CanFrame frame, uint32_t nowMs)
{
    handler.setTestNowMs(nowMs);
    handler.handleMessage(frame, driver);
}

static void resetWithDas(NagAdaptiveController &controller, uint8_t hos = 0, uint32_t nowMs = 0)
{
    controller.requestReset();
    TEST_ASSERT_EQUAL(hos <= 8, controller.observeDas(makeDasFrame(hos), nowMs));
}

static NagAdaptiveDecision epas(NagAdaptiveController &controller,
                                uint32_t nowMs,
                                int16_t torqueCentiNm = 20,
                                int16_t angleDeciDeg = 0,
                                uint32_t entropy = kEntropy)
{
    return controller.observeEpas(nowMs, angleDeciDeg, torqueCentiNm, entropy);
}

static NagAdaptiveDecision arm(NagAdaptiveController &controller,
                               int16_t torqueCentiNm = 20,
                               int16_t angleDeciDeg = 0)
{
    TEST_ASSERT_FALSE(epas(controller, 0, torqueCentiNm, angleDeciDeg).shouldSend);
    TEST_ASSERT_FALSE(epas(controller, 10, torqueCentiNm, angleDeciDeg).shouldSend);
    NagAdaptiveDecision decision = epas(controller, 20, torqueCentiNm, angleDeciDeg);
    TEST_ASSERT_TRUE(decision.shouldSend);
    return decision;
}

static void enterCorrective(NagAdaptiveController &controller, uint32_t nowMs, uint8_t hos = 2)
{
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(hos), nowMs));
    const NagAdaptiveDecision decision = epas(controller, nowMs);
    TEST_ASSERT_TRUE(decision.shouldSend);
    TEST_ASSERT_TRUE(decision.corrective);
}

static uint32_t completeCurrentBurst(NagAdaptiveController &controller, uint32_t nowMs)
{
    while (controller.snapshot(nowMs).correctiveBurstFrame <
           controller.snapshot(nowMs).correctiveBurstFrameTarget)
    {
        const NagAdaptiveDecision decision = epas(controller, nowMs);
        TEST_ASSERT_TRUE(decision.shouldSend);
        TEST_ASSERT_TRUE(decision.corrective);
        controller.onTransmitResult(nowMs, decision, true);
        nowMs += 10;
    }
    return nowMs;
}

void setUp() {}
void tearDown() {}

void test_adaptive_never_sends_without_fresh_das()
{
    NagAdaptiveController controller;
    controller.requestReset();
    for (uint32_t nowMs = 0; nowMs < 40; nowMs += 10)
        TEST_ASSERT_FALSE(epas(controller, nowMs).shouldSend);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_WAIT_DAS,
                            controller.snapshot(40).phase);

    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 100));
    TEST_ASSERT_FALSE(epas(controller, 100).shouldSend);
    TEST_ASSERT_FALSE(epas(controller, 110).shouldSend);
    TEST_ASSERT_TRUE(epas(controller, 120).shouldSend);
    TEST_ASSERT_FALSE(epas(controller, 601).shouldSend);
    const NagAdaptiveSnapshot stale = controller.snapshot(601);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_WAIT_DAS, stale.phase);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::BLOCK_DAS_STALE, stale.blockReason);
}

void test_adaptive_requires_three_valid_oem_epas_frames()
{
    NagAdaptiveController controller;
    resetWithDas(controller);
    TEST_ASSERT_FALSE(epas(controller, 0).shouldSend);
    TEST_ASSERT_FALSE(epas(controller, 10).shouldSend);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_ARMING,
                            controller.snapshot(10).phase);
    TEST_ASSERT_TRUE(epas(controller, 20).shouldSend);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_MAINTENANCE,
                            controller.snapshot(20).phase);
}

void test_hos_0_runs_preventive_window_release_and_no_tx_rest()
{
    NagAdaptiveController controller;
    NagAdaptiveConfig config;
    config.activityMinMs = config.activityMaxMs = 400;
    config.releaseMinMs = config.releaseMaxMs = 100;
    config.restMinMs = config.restMaxMs = 500;
    config.dasFreshTimeoutMs = 2000;
    controller.setConfig(config);
    resetWithDas(controller);
    arm(controller);

    TEST_ASSERT_TRUE(epas(controller, 200).shouldSend);
    TEST_ASSERT_TRUE(epas(controller, 400).shouldSend);
    TEST_ASSERT_TRUE(epas(controller, 419).shouldSend);
    const NagAdaptiveDecision releaseStart = epas(controller, 420);
    TEST_ASSERT_TRUE(releaseStart.shouldSend);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_RELEASE,
                            controller.snapshot(420).phase);
    TEST_ASSERT_TRUE(epas(controller, 470).shouldSend);
    TEST_ASSERT_FALSE(epas(controller, 520).shouldSend);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_REST,
                            controller.snapshot(520).phase);
    for (uint32_t nowMs = 620; nowMs < 1020; nowMs += 100)
        TEST_ASSERT_FALSE(epas(controller, nowMs).shouldSend);
    TEST_ASSERT_TRUE(epas(controller, 1020).shouldSend);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_MAINTENANCE,
                            controller.snapshot(1020).phase);
}

void test_hos_1_interrupts_preventive_window_into_release()
{
    NagAdaptiveController controller;
    resetWithDas(controller);
    const NagAdaptiveDecision active = arm(controller);
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(1), 30));
    const NagAdaptiveDecision release = epas(controller, 30);
    TEST_ASSERT_TRUE(release.shouldSend);
    TEST_ASSERT_FALSE(release.corrective);
    TEST_ASSERT_TRUE((release.targetTorqueCentiNm < 0) == (active.targetTorqueCentiNm < 0));
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_RELEASE,
                            controller.snapshot(30).phase);
}

void test_rest_duration_uses_1500_to_2500ms_triangular_bounds()
{
    NagAdaptiveController controller;
    NagAdaptiveConfig config;
    config.activityMinMs = config.activityMaxMs = 400;
    config.releaseMinMs = config.releaseMaxMs = 100;
    config.dasFreshTimeoutMs = 2000;
    controller.setConfig(config);
    resetWithDas(controller);
    arm(controller);
    epas(controller, 200);
    epas(controller, 400);
    epas(controller, 420);
    TEST_ASSERT_FALSE(epas(controller, 520).shouldSend);
    const uint32_t remaining = controller.snapshot(520).phaseRemainingMs;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1500, remaining);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(2500, remaining);
}

void test_preventive_walk_stays_between_15_and_18_centi_nm()
{
    NagAdaptiveController controller;
    resetWithDas(controller);
    arm(controller);
    for (uint32_t nowMs = 30; nowMs < 500; nowMs += 10)
    {
        const NagAdaptiveDecision decision = epas(controller, nowMs, 20, 0, nowMs + kEntropy);
        TEST_ASSERT_TRUE(decision.shouldSend);
        TEST_ASSERT_GREATER_OR_EQUAL_INT16(15, static_cast<int16_t>(-decision.targetTorqueCentiNm));
        TEST_ASSERT_LESS_OR_EQUAL_INT16(18, static_cast<int16_t>(-decision.targetTorqueCentiNm));
    }
}

void test_preventive_walk_changes_by_at_most_one_centi_nm_per_frame()
{
    NagAdaptiveController controller;
    resetWithDas(controller);
    int16_t previous = arm(controller).targetTorqueCentiNm;
    for (uint32_t nowMs = 30; nowMs < 500; nowMs += 10)
    {
        const int16_t current = epas(controller, nowMs, 20, 0, nowMs + kEntropy).targetTorqueCentiNm;
        const int16_t delta = static_cast<int16_t>(current - previous);
        TEST_ASSERT_TRUE(delta >= -1 && delta <= 1);
        previous = current;
    }
}

void test_measured_torque_selects_opposite_injection_direction()
{
    NagAdaptiveController positive;
    resetWithDas(positive);
    TEST_ASSERT_EQUAL_INT8(-1, arm(positive, 20).injectionSign);

    NagAdaptiveController negative;
    resetWithDas(negative);
    TEST_ASSERT_EQUAL_INT8(1, arm(negative, -20).injectionSign);
}

void test_direction_flip_requires_100ms_stability()
{
    NagAdaptiveController controller;
    resetWithDas(controller);
    arm(controller, 20);
    TEST_ASSERT_EQUAL_INT8(-1, epas(controller, 30, -20).injectionSign);
    TEST_ASSERT_EQUAL_INT8(-1, epas(controller, 129, -20).injectionSign);
    TEST_ASSERT_EQUAL_INT8(1, epas(controller, 130, -20).injectionSign);
}

void test_deadband_holds_last_trusted_direction()
{
    NagAdaptiveController controller;
    resetWithDas(controller);
    arm(controller, 20);
    epas(controller, 30, -20);
    const NagAdaptiveDecision held = epas(controller, 80, 0);
    TEST_ASSERT_EQUAL_INT8(-1, held.injectionSign);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::DIRECTION_HOLD,
                            controller.snapshot(80).directionSource);
    TEST_ASSERT_EQUAL_INT8(-1, epas(controller, 130, -20).injectionSign);
    TEST_ASSERT_EQUAL_INT8(-1, epas(controller, 229, -20).injectionSign);
    TEST_ASSERT_EQUAL_INT8(1, epas(controller, 230, -20).injectionSign);
}

void test_no_torque_and_no_angle_direction_blocks_send()
{
    NagAdaptiveController controller;
    resetWithDas(controller);
    TEST_ASSERT_FALSE(epas(controller, 0, 0, 0).shouldSend);
    TEST_ASSERT_FALSE(epas(controller, 10, 0, 10).shouldSend);
    const NagAdaptiveDecision blocked = epas(controller, 20, 0, -10);
    TEST_ASSERT_FALSE(blocked.shouldSend);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::BLOCK_NO_DIRECTION,
                            controller.snapshot(20).blockReason);
}

void test_hos_2_through_7_interrupt_every_nonfault_phase()
{
    for (uint8_t hos = 2; hos <= 7; ++hos)
    {
        NagAdaptiveController controller;
        resetWithDas(controller);
        arm(controller);
        TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(hos), 30));
        const NagAdaptiveDecision decision = epas(controller, 30);
        TEST_ASSERT_TRUE(decision.shouldSend);
        TEST_ASSERT_TRUE(decision.corrective);
        TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_CORRECTIVE,
                                controller.snapshot(30).phase);
    }

    NagAdaptiveController resting;
    NagAdaptiveConfig config;
    config.activityMinMs = config.activityMaxMs = 400;
    config.releaseMinMs = config.releaseMaxMs = 100;
    config.dasFreshTimeoutMs = 2000;
    resting.setConfig(config);
    resetWithDas(resting);
    arm(resting);
    epas(resting, 200);
    epas(resting, 400);
    epas(resting, 420);
    epas(resting, 520);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_REST, resting.snapshot(520).phase);
    TEST_ASSERT_TRUE(resting.observeDas(makeDasFrame(2), 530));
    TEST_ASSERT_TRUE(epas(resting, 530).corrective);
}

void test_corrective_burst_has_three_to_five_successful_echoes()
{
    NagAdaptiveController controller;
    resetWithDas(controller);
    arm(controller);
    enterCorrective(controller, 30);
    const NagAdaptiveSnapshot snapshot = controller.snapshot(30);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(3, snapshot.correctiveBurstFrameTarget);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(5, snapshot.correctiveBurstFrameTarget);

    NagAdaptiveDecision decision = epas(controller, 40);
    controller.onTransmitResult(40, decision, false);
    TEST_ASSERT_EQUAL_UINT8(0, controller.snapshot(40).correctiveBurstFrame);

    uint8_t successes = 0;
    uint32_t nowMs = 50;
    while (controller.snapshot(nowMs).phase == NagAdaptiveController::PHASE_CORRECTIVE)
    {
        decision = epas(controller, nowMs);
        controller.onTransmitResult(nowMs, decision, true);
        ++successes;
        nowMs += 10;
    }
    TEST_ASSERT_EQUAL_UINT8(snapshot.correctiveBurstFrameTarget, successes);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_VERIFY,
                            controller.snapshot(nowMs).phase);
}

void test_corrective_walk_stays_between_150_and_180_centi_nm()
{
    NagAdaptiveController controller;
    resetWithDas(controller);
    arm(controller);
    enterCorrective(controller, 30);
    for (uint32_t nowMs = 40; nowMs < 140; nowMs += 10)
    {
        const NagAdaptiveDecision decision = epas(controller, nowMs, 20, 0, kEntropy + nowMs);
        TEST_ASSERT_TRUE(decision.corrective);
        TEST_ASSERT_GREATER_OR_EQUAL_INT16(150, static_cast<int16_t>(-decision.targetTorqueCentiNm));
        TEST_ASSERT_LESS_OR_EQUAL_INT16(180, static_cast<int16_t>(-decision.targetTorqueCentiNm));
    }
}

void test_hos_return_to_0_or_1_records_ack_latency()
{
    NagAdaptiveController controller;
    resetWithDas(controller);
    arm(controller);
    enterCorrective(controller, 30);
    const NagAdaptiveDecision decision = epas(controller, 40);
    controller.onTransmitResult(40, decision, true);
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(1), 165));
    const NagAdaptiveSnapshot snapshot = controller.snapshot(165);
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.acknowledgementCount);
    TEST_ASSERT_EQUAL_UINT32(125, snapshot.lastAcknowledgementLatencyMs);
    TEST_ASSERT_EQUAL_UINT32(125, snapshot.maxAcknowledgementLatencyMs);
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 170));
    TEST_ASSERT_EQUAL_UINT32(1, controller.snapshot(170).acknowledgementCount);
}

void test_second_failed_corrective_attempt_enters_fault_hold()
{
    NagAdaptiveController controller;
    NagAdaptiveConfig config;
    config.dasFreshTimeoutMs = 2000;
    controller.setConfig(config);
    resetWithDas(controller);
    arm(controller);
    enterCorrective(controller, 30);
    uint32_t nowMs = completeCurrentBurst(controller, 40);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_VERIFY,
                            controller.snapshot(nowMs).phase);
    const uint32_t firstVerifyEnd = nowMs + controller.snapshot(nowMs).phaseRemainingMs;
    for (uint32_t tick = nowMs + 100; tick < firstVerifyEnd; tick += 100)
        TEST_ASSERT_FALSE(epas(controller, tick).shouldSend);
    nowMs = firstVerifyEnd;
    TEST_ASSERT_TRUE(epas(controller, nowMs).corrective);
    TEST_ASSERT_EQUAL_UINT8(2, controller.snapshot(nowMs).correctiveAttempt);
    nowMs = completeCurrentBurst(controller, nowMs + 10);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_VERIFY,
                            controller.snapshot(nowMs).phase);
    const uint32_t secondVerifyEnd = nowMs + controller.snapshot(nowMs).phaseRemainingMs;
    for (uint32_t tick = nowMs + 100; tick < secondVerifyEnd; tick += 100)
        TEST_ASSERT_FALSE(epas(controller, tick).shouldSend);
    TEST_ASSERT_FALSE(epas(controller, secondVerifyEnd).shouldSend);
    const NagAdaptiveSnapshot fault = controller.snapshot(secondVerifyEnd);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_FAULT_HOLD, fault.phase);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::BLOCK_ACK_TIMEOUT, fault.blockReason);
    TEST_ASSERT_EQUAL_UINT32(1, fault.acknowledgementTimeoutCount);
}

void test_hos_8_9_and_15_enter_fault_hold_without_send()
{
    const uint8_t blockedHos[] = {8, 9, 15};
    for (uint8_t hos : blockedHos)
    {
        NagAdaptiveController controller;
        resetWithDas(controller);
        arm(controller);
        const bool accepted = controller.observeDas(makeDasFrame(hos), 30);
        TEST_ASSERT_EQUAL(hos == 8, accepted);
        TEST_ASSERT_FALSE(epas(controller, 30).shouldSend);
        const NagAdaptiveSnapshot snapshot = controller.snapshot(30);
        TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_FAULT_HOLD, snapshot.phase);
        TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::BLOCK_DAS_STATE, snapshot.blockReason);
        TEST_ASSERT_EQUAL_UINT8(hos, snapshot.dasHos);
    }
}

void test_das_stale_during_send_returns_wait_das()
{
    NagAdaptiveController controller;
    resetWithDas(controller, 0, 100);
    TEST_ASSERT_FALSE(epas(controller, 100).shouldSend);
    TEST_ASSERT_FALSE(epas(controller, 110).shouldSend);
    TEST_ASSERT_TRUE(epas(controller, 120).shouldSend);
    TEST_ASSERT_FALSE(epas(controller, 601).shouldSend);
    const NagAdaptiveSnapshot snapshot = controller.snapshot(601);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_WAIT_DAS, snapshot.phase);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::BLOCK_DAS_STALE, snapshot.blockReason);
}

void test_epas_gap_over_200ms_rearms_three_frame_guard()
{
    NagAdaptiveController controller;
    resetWithDas(controller);
    arm(controller);
    TEST_ASSERT_FALSE(epas(controller, 221).shouldSend);
    TEST_ASSERT_FALSE(epas(controller, 231).shouldSend);
    TEST_ASSERT_TRUE(epas(controller, 241).shouldSend);
}

void test_every_decision_is_clamped_to_plus_minus_180_centi_nm()
{
    NagAdaptiveController controller;
    NagAdaptiveConfig config;
    config.preventiveNegativeMinCentiNm = -300;
    config.preventiveNegativeMaxCentiNm = 400;
    config.preventivePositiveMinCentiNm = 400;
    config.preventivePositiveMaxCentiNm = -300;
    config.correctiveNegativeMinCentiNm = -500;
    config.correctiveNegativeMaxCentiNm = 500;
    config.correctivePositiveMinCentiNm = 500;
    config.correctivePositiveMaxCentiNm = -500;
    controller.setConfig(config);
    resetWithDas(controller);
    NagAdaptiveDecision decision = arm(controller);
    TEST_ASSERT_TRUE(decision.targetTorqueCentiNm >= -180 && decision.targetTorqueCentiNm <= 180);
    enterCorrective(controller, 30);
    for (uint32_t nowMs = 40; nowMs < 200; nowMs += 10)
    {
        decision = epas(controller, nowMs, (nowMs & 0x20) ? 100 : -100, 0, nowMs);
        TEST_ASSERT_TRUE(decision.targetTorqueCentiNm >= -180 && decision.targetTorqueCentiNm <= 180);
    }
}

void test_100000_deterministic_370_sequences_cover_adaptive_safety_matrix()
{
    static constexpr uint32_t kSequenceCount = 100000;
    static constexpr uint32_t kCombinationCount = 16U * 16U * 3U * 2U * 2U;
    uint8_t coverage[16][16][3][2][2] = {};
    MockDriver driver;

    const auto makeLiteralEpasFrame = [](uint8_t counter, int16_t torqueCentiNm) {
        CanFrame frame = {.id = 0x370, .dlc = 8};
        const uint16_t torqueRaw = static_cast<uint16_t>(2050 + torqueCentiNm);
        frame.data[0] = 0x12;
        frame.data[1] = 0x00;
        frame.data[2] = static_cast<uint8_t>(0x80 | ((torqueRaw >> 8) & 0x0F));
        frame.data[3] = static_cast<uint8_t>(torqueRaw & 0xFF);
        frame.data[4] = 0x20;
        frame.data[5] = 0x00;
        frame.data[6] = static_cast<uint8_t>(0x40 | (counter & 0x0F));
        uint16_t sum = 0;
        for (uint8_t index = 0; index < 7; ++index)
            sum += frame.data[index];
        frame.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);
        return frame;
    };
    const auto readLiteralTorqueCentiNm = [](const CanFrame &frame) {
        const uint16_t raw = static_cast<uint16_t>(((frame.data[2] & 0x0F) << 8) |
                                                   frame.data[3]);
        return static_cast<int16_t>(raw) - 2050;
    };

    uint32_t allowedCases = 0;
    uint32_t prohibitedCases = 0;
    uint32_t attemptedAllowedCases = 0;
    uint32_t successfulAllowedCases = 0;
    uint32_t failedAllowedCases = 0;
    bool sawCounterWrap = false;
    bool sawPreventiveSend = false;
    bool sawCorrectiveSend = false;
    bool sawStaleBlock = false;
    bool sawHosOneBlock = false;
    bool sawBlockedHos = false;
    bool sawNoDirectionBlock = false;
    bool sawDriverFailure = false;
    bool sawVerifyNoSend = false;
    bool sawDasAcknowledgement = false;
    bool representativeAcknowledged[4] = {};

    nagKillerRuntime = true;
    for (uint32_t sequence = 0; sequence < kSequenceCount; ++sequence)
    {
        uint32_t combination = sequence % kCombinationCount;
        const uint8_t counter0 = static_cast<uint8_t>(combination % 16U);
        combination /= 16U;
        const uint8_t hos = static_cast<uint8_t>(combination % 16U);
        combination /= 16U;
        const uint8_t directionCase = static_cast<uint8_t>(combination % 3U);
        combination /= 3U;
        const uint8_t freshnessCase = static_cast<uint8_t>(combination % 2U);
        combination /= 2U;
        const uint8_t localSendCase = static_cast<uint8_t>(combination % 2U);

        NagHandler handler;
        handler.setMode(NagHandler::MODE_ADAPTIVE);
        driver.reset();
        driver.writeEnabled = localSendCase == 0;

        const uint8_t counter1 = static_cast<uint8_t>((counter0 + 1U) & 0x0FU);
        const uint8_t counter2 = static_cast<uint8_t>((counter0 + 2U) & 0x0FU);
        const uint8_t expectedEchoCounter = static_cast<uint8_t>((counter2 + 1U) & 0x0FU);
        const int16_t observedTorqueCentiNm = directionCase == 0 ? 20 :
                                              directionCase == 1 ? -20 : 0;
        const bool dasFresh = freshnessCase == 0;
        const bool localSendSucceeds = localSendCase == 0;
        const bool legalHos = hos == 0 || (hos >= 2 && hos <= 7);
        const bool hasDirection = directionCase < 2;
        const bool shouldAttempt = dasFresh && legalHos && hasDirection;
        const uint32_t epasStartMs = dasFresh ? 0U : 501U;

        coverage[counter0][hos][directionCase][freshnessCase][localSendCase]++;
        sawCounterWrap = sawCounterWrap || counter1 < counter0 || counter2 < counter1 ||
                         expectedEchoCounter < counter2;

        handleAt(handler, driver, makeDasFrame(hos), 0);
        handleAt(handler, driver,
                 makeLiteralEpasFrame(counter0, observedTorqueCentiNm), epasStartMs);
        handleAt(handler, driver,
                 makeLiteralEpasFrame(counter1, observedTorqueCentiNm), epasStartMs + 10U);
        handleAt(handler, driver,
                 makeLiteralEpasFrame(counter2, observedTorqueCentiNm), epasStartMs + 20U);

        const NagAdaptiveSnapshot initialSnapshot =
            handler.adaptiveController.snapshot(epasStartMs + 20U);
        if (!shouldAttempt)
        {
            prohibitedCases++;
            TEST_ASSERT_EQUAL(0, driver.sent.size());
            TEST_ASSERT_EQUAL_UINT32(0, handler.nagSendAttemptCount);
            TEST_ASSERT_EQUAL_UINT32(0, handler.nagSendFailureCount);
            TEST_ASSERT_EQUAL_UINT32(0, handler.nagEchoCount);
            TEST_ASSERT_EQUAL_UINT32(0, initialSnapshot.acknowledgementCount);
            sawStaleBlock = sawStaleBlock || !dasFresh;
            sawHosOneBlock = sawHosOneBlock || (dasFresh && hos == 1 && hasDirection);
            sawBlockedHos = sawBlockedHos || (dasFresh && hos >= 8 && hasDirection);
            sawNoDirectionBlock = sawNoDirectionBlock || (dasFresh && legalHos && !hasDirection);
            continue;
        }

        allowedCases++;
        attemptedAllowedCases++;
        TEST_ASSERT_EQUAL_UINT32(1, handler.nagSendAttemptCount);
        TEST_ASSERT_EQUAL_UINT32(0, initialSnapshot.acknowledgementCount);

        if (!localSendSucceeds)
        {
            failedAllowedCases++;
            sawDriverFailure = true;
            TEST_ASSERT_EQUAL(0, driver.sent.size());
            TEST_ASSERT_EQUAL_UINT32(1, handler.nagSendFailureCount);
            TEST_ASSERT_EQUAL_UINT32(0, handler.nagEchoCount);
            TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
            TEST_ASSERT_EQUAL_UINT32(0, initialSnapshot.acknowledgementCount);
            if (hos >= 2)
                TEST_ASSERT_EQUAL_UINT8(0, initialSnapshot.correctiveBurstFrame);
            continue;
        }

        successfulAllowedCases++;
        TEST_ASSERT_EQUAL_UINT32(0, handler.nagSendFailureCount);
        TEST_ASSERT_EQUAL_UINT32(1, handler.nagEchoCount);
        TEST_ASSERT_EQUAL_UINT32(1, handler.framesSent);
        TEST_ASSERT_EQUAL(1, driver.sent.size());

        const CanFrame &initialEcho = driver.sent[0];
        const int16_t sentTorqueCentiNm = readLiteralTorqueCentiNm(initialEcho);
        const int16_t sentMagnitudeCentiNm = sentTorqueCentiNm < 0 ?
                                             static_cast<int16_t>(-sentTorqueCentiNm) :
                                             sentTorqueCentiNm;
        TEST_ASSERT_TRUE(sentTorqueCentiNm >= -180 && sentTorqueCentiNm <= 180);
        TEST_ASSERT_EQUAL_UINT8(expectedEchoCounter, initialEcho.data[6] & 0x0F);
        if (observedTorqueCentiNm > 0)
            TEST_ASSERT_TRUE(sentTorqueCentiNm < 0);
        else
            TEST_ASSERT_TRUE(sentTorqueCentiNm > 0);

        if (hos == 0)
        {
            sawPreventiveSend = true;
            TEST_ASSERT_TRUE(sentMagnitudeCentiNm >= 15 && sentMagnitudeCentiNm <= 18);
            TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_MAINTENANCE,
                                    initialSnapshot.phase);
        }
        else
        {
            sawCorrectiveSend = true;
            TEST_ASSERT_TRUE(sentMagnitudeCentiNm >= 150 && sentMagnitudeCentiNm <= 180);
            TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_CORRECTIVE,
                                    initialSnapshot.phase);
            TEST_ASSERT_EQUAL_UINT8(1, initialSnapshot.correctiveBurstFrame);
        }

        int8_t representativeIndex = -1;
        if (hos == 2 && counter0 == 0)
            representativeIndex = static_cast<int8_t>(directionCase);
        else if (hos == 7 && counter0 == 15)
            representativeIndex = static_cast<int8_t>(2 + directionCase);

        if (representativeIndex >= 0 && !representativeAcknowledged[representativeIndex])
        {
            uint32_t nowMs = epasStartMs + 30U;
            uint8_t nextCounter = static_cast<uint8_t>((counter2 + 1U) & 0x0FU);
            while (handler.adaptiveController.snapshot(nowMs).phase ==
                   NagAdaptiveController::PHASE_CORRECTIVE)
            {
                const size_t sentBefore = driver.sent.size();
                handleAt(handler, driver,
                         makeLiteralEpasFrame(nextCounter, observedTorqueCentiNm), nowMs);
                TEST_ASSERT_EQUAL(sentBefore + 1U, driver.sent.size());
                const CanFrame &burstEcho = driver.sent.back();
                const int16_t burstTorqueCentiNm = readLiteralTorqueCentiNm(burstEcho);
                TEST_ASSERT_TRUE(burstTorqueCentiNm >= -180 && burstTorqueCentiNm <= 180);
                TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>((nextCounter + 1U) & 0x0FU),
                                        burstEcho.data[6] & 0x0F);
                if (observedTorqueCentiNm > 0)
                    TEST_ASSERT_TRUE(burstTorqueCentiNm < 0);
                else
                    TEST_ASSERT_TRUE(burstTorqueCentiNm > 0);
                nextCounter = static_cast<uint8_t>((nextCounter + 1U) & 0x0FU);
                nowMs += 10U;
            }

            TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_VERIFY,
                                    handler.adaptiveController.snapshot(nowMs).phase);
            const size_t sentBeforeVerify = driver.sent.size();
            const uint32_t attemptsBeforeVerify = handler.nagSendAttemptCount;
            handleAt(handler, driver,
                     makeLiteralEpasFrame(nextCounter, observedTorqueCentiNm), nowMs);
            TEST_ASSERT_EQUAL(sentBeforeVerify, driver.sent.size());
            TEST_ASSERT_EQUAL_UINT32(attemptsBeforeVerify, handler.nagSendAttemptCount);
            TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_VERIFY,
                                    handler.adaptiveController.snapshot(nowMs).phase);
            sawVerifyNoSend = true;

            const uint32_t acknowledgementAtMs = nowMs + 50U;
            handleAt(handler, driver, makeDasFrame(1), acknowledgementAtMs);
            const NagAdaptiveSnapshot acknowledged =
                handler.adaptiveController.snapshot(acknowledgementAtMs);
            TEST_ASSERT_EQUAL(sentBeforeVerify, driver.sent.size());
            TEST_ASSERT_EQUAL_UINT32(1, acknowledged.acknowledgementCount);
            TEST_ASSERT_EQUAL_UINT32(
                acknowledgementAtMs - (epasStartMs + 20U),
                acknowledged.lastAcknowledgementLatencyMs);
            TEST_ASSERT_EQUAL_UINT32(0, acknowledged.acknowledgementTimeoutCount);
            representativeAcknowledged[representativeIndex] = true;
            sawDasAcknowledgement = true;
        }
    }

    for (uint8_t counter = 0; counter < 16; ++counter)
        for (uint8_t hos = 0; hos < 16; ++hos)
            for (uint8_t direction = 0; direction < 3; ++direction)
                for (uint8_t freshness = 0; freshness < 2; ++freshness)
                    for (uint8_t sendResult = 0; sendResult < 2; ++sendResult)
                        TEST_ASSERT_GREATER_THAN_UINT8(
                            0, coverage[counter][hos][direction][freshness][sendResult]);

    TEST_ASSERT_EQUAL_UINT32(kSequenceCount, allowedCases + prohibitedCases);
    TEST_ASSERT_EQUAL_UINT32(allowedCases, attemptedAllowedCases);
    TEST_ASSERT_EQUAL_UINT32(allowedCases, successfulAllowedCases + failedAllowedCases);
    TEST_ASSERT_GREATER_THAN_UINT32(0, successfulAllowedCases);
    TEST_ASSERT_GREATER_THAN_UINT32(0, failedAllowedCases);
    TEST_ASSERT_TRUE(sawCounterWrap);
    TEST_ASSERT_TRUE(sawPreventiveSend);
    TEST_ASSERT_TRUE(sawCorrectiveSend);
    TEST_ASSERT_TRUE(sawStaleBlock);
    TEST_ASSERT_TRUE(sawHosOneBlock);
    TEST_ASSERT_TRUE(sawBlockedHos);
    TEST_ASSERT_TRUE(sawNoDirectionBlock);
    TEST_ASSERT_TRUE(sawDriverFailure);
    TEST_ASSERT_TRUE(sawVerifyNoSend);
    TEST_ASSERT_TRUE(sawDasAcknowledgement);
    for (uint8_t representative = 0; representative < 4; ++representative)
        TEST_ASSERT_TRUE(representativeAcknowledged[representative]);
}

void test_release_targets_decay_monotonically_and_zero_enters_no_send_rest()
{
    NagAdaptiveController controller;
    NagAdaptiveConfig config;
    config.activityMinMs = config.activityMaxMs = 400;
    config.releaseMinMs = config.releaseMaxMs = 100;
    config.restMinMs = config.restMaxMs = 500;
    config.dasFreshTimeoutMs = 2000;
    controller.setConfig(config);
    resetWithDas(controller);
    arm(controller);
    epas(controller, 200);
    epas(controller, 400);

    const NagAdaptiveDecision start = epas(controller, 420);
    const NagAdaptiveDecision quarter = epas(controller, 445);
    const NagAdaptiveDecision half = epas(controller, 470);
    const NagAdaptiveDecision threeQuarter = epas(controller, 495);
    TEST_ASSERT_TRUE(start.shouldSend);
    TEST_ASSERT_TRUE(quarter.shouldSend);
    TEST_ASSERT_TRUE(half.shouldSend);
    TEST_ASSERT_TRUE(threeQuarter.shouldSend);
    TEST_ASSERT_TRUE(-quarter.targetTorqueCentiNm < -start.targetTorqueCentiNm);
    TEST_ASSERT_TRUE(-half.targetTorqueCentiNm < -quarter.targetTorqueCentiNm);
    TEST_ASSERT_TRUE(-threeQuarter.targetTorqueCentiNm < -half.targetTorqueCentiNm);

    const NagAdaptiveDecision zero = epas(controller, 520);
    TEST_ASSERT_FALSE(zero.shouldSend);
    TEST_ASSERT_EQUAL_INT16(0, zero.targetTorqueCentiNm);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_REST,
                            controller.snapshot(520).phase);
}

void test_corrective_hos_requires_three_valid_oem_epas_frames_before_send()
{
    for (uint8_t hos = 2; hos <= 7; ++hos)
    {
        NagAdaptiveController controller;
        controller.requestReset();
        TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(hos), 0));
        TEST_ASSERT_FALSE(epas(controller, 0).shouldSend);
        TEST_ASSERT_FALSE(epas(controller, 10).shouldSend);
        const NagAdaptiveDecision third = epas(controller, 20);
        TEST_ASSERT_TRUE(third.shouldSend);
        TEST_ASSERT_TRUE(third.corrective);
    }
}

void test_epas_gap_over_200ms_rearms_during_corrective()
{
    NagAdaptiveController controller;
    resetWithDas(controller);
    arm(controller);
    enterCorrective(controller, 30);

    TEST_ASSERT_FALSE(epas(controller, 231).shouldSend);
    TEST_ASSERT_FALSE(epas(controller, 241).shouldSend);
    const NagAdaptiveDecision third = epas(controller, 251);
    TEST_ASSERT_TRUE(third.shouldSend);
    TEST_ASSERT_TRUE(third.corrective);
}

void test_fault_hold_recovers_after_2000ms_continuously_fresh_normal_hos()
{
    NagAdaptiveController controller;
    controller.requestReset();
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(8), 0));
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_FAULT_HOLD,
                            controller.snapshot(0).phase);

    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 100));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(1), 600));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(2), 700));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 800));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(1), 1300));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(8), 1400));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 1500));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(1), 2000));
    TEST_ASSERT_FALSE(epas(controller, 2501).shouldSend);
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 2600));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(1), 3100));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 3600));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(1), 4100));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 4599));
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_FAULT_HOLD,
                            controller.snapshot(4599).phase);
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(1), 4600));
    const NagAdaptiveSnapshot recovered = controller.snapshot(4600);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_REST, recovered.phase);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::BLOCK_REST, recovered.blockReason);
    TEST_ASSERT_FALSE(epas(controller, 4600).shouldSend);
}

void test_request_reset_recovers_fault_hold_immediately()
{
    NagAdaptiveController controller;
    controller.requestReset();
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(15), 0) == false);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_FAULT_HOLD,
                            controller.snapshot(0).phase);
    controller.requestReset();
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 10));
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_ARMING,
                            controller.snapshot(10).phase);
}

void test_epas_gap_clears_direction_reversal_candidate_timer()
{
    NagAdaptiveController controller;
    resetWithDas(controller);
    arm(controller, 20);
    TEST_ASSERT_EQUAL_INT8(-1, epas(controller, 30, -20).injectionSign);

    TEST_ASSERT_FALSE(epas(controller, 231, -20).shouldSend);
    TEST_ASSERT_FALSE(epas(controller, 241, -20).shouldSend);
    const NagAdaptiveDecision third = epas(controller, 251, -20);
    TEST_ASSERT_TRUE(third.shouldSend);
    TEST_ASSERT_EQUAL_INT8(-1, third.injectionSign);
    TEST_ASSERT_EQUAL_INT8(-1, epas(controller, 330, -20).injectionSign);
    TEST_ASSERT_EQUAL_INT8(1, epas(controller, 331, -20).injectionSign);
}

void test_normalize_config_swaps_and_clamps_every_range()
{
    NagAdaptiveConfig requested;
    requested.preventiveNegativeMinCentiNm = 60;
    requested.preventiveNegativeMaxCentiNm = 5;
    requested.preventivePositiveMinCentiNm = 49;
    requested.preventivePositiveMaxCentiNm = 11;
    requested.correctiveNegativeMinCentiNm = 200;
    requested.correctiveNegativeMaxCentiNm = 40;
    requested.correctivePositiveMinCentiNm = 179;
    requested.correctivePositiveMaxCentiNm = 51;
    requested.torqueDeadbandCentiNm = -1;
    requested.activityMinMs = 4000;
    requested.activityMaxMs = 100;
    requested.releaseMinMs = 2000;
    requested.releaseMaxMs = 50;
    requested.restMinMs = 6000;
    requested.restMaxMs = 100;
    requested.dasFreshTimeoutMs = 99;

    const NagAdaptiveConfig normalized = NagAdaptiveController::normalizeConfig(requested);
    TEST_ASSERT_EQUAL_INT16(10, normalized.preventiveNegativeMinCentiNm);
    TEST_ASSERT_EQUAL_INT16(50, normalized.preventiveNegativeMaxCentiNm);
    TEST_ASSERT_EQUAL_INT16(11, normalized.preventivePositiveMinCentiNm);
    TEST_ASSERT_EQUAL_INT16(49, normalized.preventivePositiveMaxCentiNm);
    TEST_ASSERT_EQUAL_INT16(50, normalized.correctiveNegativeMinCentiNm);
    TEST_ASSERT_EQUAL_INT16(180, normalized.correctiveNegativeMaxCentiNm);
    TEST_ASSERT_EQUAL_INT16(51, normalized.correctivePositiveMinCentiNm);
    TEST_ASSERT_EQUAL_INT16(179, normalized.correctivePositiveMaxCentiNm);
    TEST_ASSERT_EQUAL_INT16(0, normalized.torqueDeadbandCentiNm);
    TEST_ASSERT_EQUAL_UINT32(400, normalized.activityMinMs);
    TEST_ASSERT_EQUAL_UINT32(3000, normalized.activityMaxMs);
    TEST_ASSERT_EQUAL_UINT32(100, normalized.releaseMinMs);
    TEST_ASSERT_EQUAL_UINT32(1000, normalized.releaseMaxMs);
    TEST_ASSERT_EQUAL_UINT32(500, normalized.restMinMs);
    TEST_ASSERT_EQUAL_UINT32(5000, normalized.restMaxMs);
    TEST_ASSERT_EQUAL_UINT32(100, normalized.dasFreshTimeoutMs);

    requested.torqueDeadbandCentiNm = 51;
    requested.dasFreshTimeoutMs = 2001;
    const NagAdaptiveConfig upper = NagAdaptiveController::normalizeConfig(requested);
    TEST_ASSERT_EQUAL_INT16(50, upper.torqueDeadbandCentiNm);
    TEST_ASSERT_EQUAL_UINT32(2000, upper.dasFreshTimeoutMs);
}

void test_rejected_das_frames_reset_fault_recovery_interval()
{
    NagAdaptiveController controller;
    controller.requestReset();
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(8), 0));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 100));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(1), 600));

    CanFrame wrongId = makeDasFrame(0);
    wrongId.id = 0x123;
    TEST_ASSERT_FALSE(controller.observeDas(wrongId, 700));
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_FAULT_HOLD,
                            controller.snapshot(700).phase);

    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 800));
    CanFrame shortFrame = makeDasFrame(1);
    shortFrame.dlc = 7;
    TEST_ASSERT_FALSE(controller.observeDas(shortFrame, 900));
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_FAULT_HOLD,
                            controller.snapshot(900).phase);

    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 1000));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(1), 1500));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 2000));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(1), 2500));
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(0), 2999));
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_FAULT_HOLD,
                            controller.snapshot(2999).phase);
    TEST_ASSERT_TRUE(controller.observeDas(makeDasFrame(1), 3000));
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_REST,
                            controller.snapshot(3000).phase);
}

void test_39b_is_observed_but_never_echoed()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    handler.setMode(NagHandler::MODE_ADAPTIVE);

    handleAt(handler, driver, makeDasFrame(0), 100);

    TEST_ASSERT_EQUAL(0, driver.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagDasFrameCount);
    const NagAdaptiveSnapshot snapshot = handler.adaptiveController.snapshot(100);
    TEST_ASSERT_TRUE(snapshot.dasSeen);
    TEST_ASSERT_TRUE(snapshot.dasFresh);
    TEST_ASSERT_EQUAL_UINT8(0, snapshot.dasHos);
}

void test_adaptive_370_does_not_send_before_fresh_39b()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    handler.setMode(NagHandler::MODE_ADAPTIVE);

    handleAt(handler, driver, makeHandlerEpasFrame(0), 0);
    handleAt(handler, driver, makeHandlerEpasFrame(1), 10);
    handleAt(handler, driver, makeHandlerEpasFrame(2), 20);

    TEST_ASSERT_EQUAL(0, driver.sent.size());
    TEST_ASSERT_EQUAL_UINT32(3, handler.nagOemEpasFrameCount);
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::BLOCK_DAS_MISSING,
                            handler.adaptiveController.snapshot(20).blockReason);
}

void test_adaptive_uses_only_non_own_370_for_direction()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    handler.setMode(NagHandler::MODE_ADAPTIVE);
    handleAt(handler, driver, makeDasFrame(0), 0);
    handleAt(handler, driver, makeHandlerEpasFrame(0, 20), 0);
    handleAt(handler, driver, makeHandlerEpasFrame(1, 20), 10);
    handleAt(handler, driver, makeHandlerEpasFrame(2, 20), 20);
    TEST_ASSERT_EQUAL(1, driver.sent.size());

    CanFrame ownEcho = driver.sent.back();
    handleAt(handler, driver, ownEcho, 30);

    TEST_ASSERT_EQUAL(1, driver.sent.size());
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagOwnEchoSkipCount);
    TEST_ASSERT_EQUAL_UINT32(3, handler.nagOemEpasFrameCount);
    const NagAdaptiveSnapshot snapshot = handler.adaptiveController.snapshot(30);
    TEST_ASSERT_EQUAL_INT16(20, snapshot.observedTorqueCentiNm);
    TEST_ASSERT_EQUAL_INT8(-1, snapshot.injectionSign);
}

void test_adaptive_uses_decision_target_without_legacy_h1_h2_ranges()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    NagAdaptiveConfig config;
    config.preventiveNegativeMinCentiNm = 17;
    config.preventiveNegativeMaxCentiNm = 17;
    config.preventivePositiveMinCentiNm = 17;
    config.preventivePositiveMaxCentiNm = 17;
    handler.setAdaptiveConfig(config);
    handler.setMode(NagHandler::MODE_ADAPTIVE);
    handleAt(handler, driver, makeDasFrame(0), 0);
    handleAt(handler, driver, makeHandlerEpasFrame(0, 20), 0);
    handleAt(handler, driver, makeHandlerEpasFrame(1, 20), 10);
    handleAt(handler, driver, makeHandlerEpasFrame(2, 20), 20);

    TEST_ASSERT_EQUAL(1, driver.sent.size());
    TEST_ASSERT_EQUAL_INT16(-17, NagHandler::rawToCentiNm(
                                     NagHandler::readTorqueRaw(driver.sent[0])));
}

void test_local_send_success_does_not_increment_das_ack_count()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    handler.setMode(NagHandler::MODE_ADAPTIVE);
    handleAt(handler, driver, makeDasFrame(3), 0);
    handleAt(handler, driver, makeHandlerEpasFrame(0), 0);
    handleAt(handler, driver, makeHandlerEpasFrame(1), 10);
    handleAt(handler, driver, makeHandlerEpasFrame(2), 20);

    TEST_ASSERT_EQUAL(1, driver.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, handler.adaptiveController.snapshot(20).acknowledgementCount);
}

void test_das_transition_from_3_to_1_records_ack_after_tx()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    handler.setMode(NagHandler::MODE_ADAPTIVE);
    handleAt(handler, driver, makeDasFrame(3), 0);
    handleAt(handler, driver, makeHandlerEpasFrame(0), 0);
    handleAt(handler, driver, makeHandlerEpasFrame(1), 10);
    handleAt(handler, driver, makeHandlerEpasFrame(2), 20);
    TEST_ASSERT_EQUAL(1, driver.sent.size());

    handleAt(handler, driver, makeDasFrame(1), 70);

    const NagAdaptiveSnapshot snapshot = handler.adaptiveController.snapshot(70);
    TEST_ASSERT_EQUAL_UINT32(1, snapshot.acknowledgementCount);
    TEST_ASSERT_EQUAL_UINT32(50, snapshot.lastAcknowledgementLatencyMs);
}

void test_failed_send_does_not_advance_corrective_burst()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    handler.setMode(NagHandler::MODE_ADAPTIVE);
    handleAt(handler, driver, makeDasFrame(3), 0);
    driver.writeEnabled = false;
    handleAt(handler, driver, makeHandlerEpasFrame(0), 0);
    handleAt(handler, driver, makeHandlerEpasFrame(1), 10);
    CanFrame failedInput = makeHandlerEpasFrame(2);
    handleAt(handler, driver, failedInput, 20);

    TEST_ASSERT_EQUAL_UINT32(1, handler.nagSendAttemptCount);
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagSendFailureCount);
    TEST_ASSERT_EQUAL_UINT32(0, handler.framesSent);
    TEST_ASSERT_EQUAL_UINT32(0, handler.nagEchoCount);
    TEST_ASSERT_FALSE(handler.lastInjectedValid);
    TEST_ASSERT_EQUAL_INT16(0, handler.lastInjectedCenti());
    TEST_ASSERT_EQUAL_UINT32(0, handler.lastInjectedAtMs);
    TEST_ASSERT_EQUAL_UINT8(0, handler.adaptiveController.snapshot(20).correctiveBurstFrame);
    TEST_ASSERT_EQUAL_UINT32(
        0, handler.adaptiveController.snapshot(20).acknowledgementCount);
    const CanFrame failedEcho = makeExpectedEcho(
        failedInput, handler.adaptiveController.snapshot(20).targetTorqueCentiNm);
    TEST_ASSERT_FALSE(handler.isOwnEcho(failedEcho));
    TEST_ASSERT_EQUAL_UINT32(0, handler.nagCounterCollisionCount);

    driver.writeEnabled = true;
    handleAt(handler, driver, makeHandlerEpasFrame(3), 30);
    TEST_ASSERT_EQUAL(1, driver.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, handler.nagCounterCollisionCount);
    TEST_ASSERT_EQUAL_UINT8(1, handler.adaptiveController.snapshot(30).correctiveBurstFrame);
    TEST_ASSERT_EQUAL_UINT32(
        0, handler.adaptiveController.snapshot(30).acknowledgementCount);
}

void test_adaptive_runtime_reenable_rearms_without_mode_change()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    handler.setMode(NagHandler::MODE_ADAPTIVE);

    nagKillerRuntime = false;
    handleAt(handler, driver, makeDasFrame(0), 0);
    handleAt(handler, driver, makeHandlerEpasFrame(0), 0);
    handleAt(handler, driver, makeHandlerEpasFrame(1), 10);
    handleAt(handler, driver, makeHandlerEpasFrame(2), 20);
    TEST_ASSERT_EQUAL(0, driver.sent.size());
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_DISABLED,
                            handler.adaptiveController.snapshot(20).phase);

    nagKillerRuntime = true;
    handleAt(handler, driver, makeDasFrame(0), 100);
    handleAt(handler, driver, makeHandlerEpasFrame(3), 100);
    handleAt(handler, driver, makeHandlerEpasFrame(4), 110);
    handleAt(handler, driver, makeHandlerEpasFrame(5), 120);

    TEST_ASSERT_EQUAL_UINT8(NagHandler::MODE_ADAPTIVE, (uint8_t)handler.nagMode);
    TEST_ASSERT_EQUAL(1, driver.sent.size());
    TEST_ASSERT_EQUAL_UINT8(NagAdaptiveController::PHASE_MAINTENANCE,
                            handler.adaptiveController.snapshot(120).phase);
}

void test_adaptive_angle_tracks_only_accepted_non_own_oem_epas()
{
    NagHandler handler;
    MockDriver driver;
    nagKillerRuntime = true;
    handler.setMode(NagHandler::MODE_ADAPTIVE);
    handleAt(handler, driver, makeDasFrame(0), 0);

    handleAt(handler, driver, makeHandlerEpasFrame(0, 20, 123), 0);
    TEST_ASSERT_EQUAL_INT16(123, handler.adaptiveAngleDeciDeg());

    CanFrame badChecksum = makeHandlerEpasFrame(1, 20, 456);
    badChecksum.data[7] ^= 0x01;
    handleAt(handler, driver, badChecksum, 1);
    TEST_ASSERT_EQUAL_INT16(123, handler.adaptiveAngleDeciDeg());

    CanFrame reserved = makeHandlerEpasFrame(1, 20, 789);
    NagHandler::writeTorqueRaw(reserved, 0);
    updateHandlerChecksum(reserved);
    handleAt(handler, driver, reserved, 2);
    TEST_ASSERT_EQUAL_INT16(123, handler.adaptiveAngleDeciDeg());

    handleAt(handler, driver, makeHandlerEpasFrame(1, 20, 123), 10);
    handleAt(handler, driver, makeHandlerEpasFrame(2, 20, 123), 20);
    TEST_ASSERT_EQUAL(1, driver.sent.size());
    CanFrame earlierOwnEcho = driver.sent[0];

    handleAt(handler, driver, makeHandlerEpasFrame(3, 20, 222), 30);
    TEST_ASSERT_EQUAL_INT16(222, handler.adaptiveAngleDeciDeg());
    handleAt(handler, driver, earlierOwnEcho, 40);
    TEST_ASSERT_EQUAL_INT16(222, handler.adaptiveAngleDeciDeg());
    TEST_ASSERT_EQUAL_UINT32(1, handler.nagOwnEchoSkipCount);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_adaptive_never_sends_without_fresh_das);
    RUN_TEST(test_adaptive_requires_three_valid_oem_epas_frames);
    RUN_TEST(test_hos_0_runs_preventive_window_release_and_no_tx_rest);
    RUN_TEST(test_hos_1_interrupts_preventive_window_into_release);
    RUN_TEST(test_rest_duration_uses_1500_to_2500ms_triangular_bounds);
    RUN_TEST(test_preventive_walk_stays_between_15_and_18_centi_nm);
    RUN_TEST(test_preventive_walk_changes_by_at_most_one_centi_nm_per_frame);
    RUN_TEST(test_measured_torque_selects_opposite_injection_direction);
    RUN_TEST(test_direction_flip_requires_100ms_stability);
    RUN_TEST(test_deadband_holds_last_trusted_direction);
    RUN_TEST(test_no_torque_and_no_angle_direction_blocks_send);
    RUN_TEST(test_hos_2_through_7_interrupt_every_nonfault_phase);
    RUN_TEST(test_corrective_burst_has_three_to_five_successful_echoes);
    RUN_TEST(test_corrective_walk_stays_between_150_and_180_centi_nm);
    RUN_TEST(test_hos_return_to_0_or_1_records_ack_latency);
    RUN_TEST(test_second_failed_corrective_attempt_enters_fault_hold);
    RUN_TEST(test_hos_8_9_and_15_enter_fault_hold_without_send);
    RUN_TEST(test_das_stale_during_send_returns_wait_das);
    RUN_TEST(test_epas_gap_over_200ms_rearms_three_frame_guard);
    RUN_TEST(test_every_decision_is_clamped_to_plus_minus_180_centi_nm);
    RUN_TEST(test_100000_deterministic_370_sequences_cover_adaptive_safety_matrix);
    RUN_TEST(test_release_targets_decay_monotonically_and_zero_enters_no_send_rest);
    RUN_TEST(test_corrective_hos_requires_three_valid_oem_epas_frames_before_send);
    RUN_TEST(test_epas_gap_over_200ms_rearms_during_corrective);
    RUN_TEST(test_fault_hold_recovers_after_2000ms_continuously_fresh_normal_hos);
    RUN_TEST(test_request_reset_recovers_fault_hold_immediately);
    RUN_TEST(test_epas_gap_clears_direction_reversal_candidate_timer);
    RUN_TEST(test_normalize_config_swaps_and_clamps_every_range);
    RUN_TEST(test_rejected_das_frames_reset_fault_recovery_interval);
    RUN_TEST(test_39b_is_observed_but_never_echoed);
    RUN_TEST(test_adaptive_370_does_not_send_before_fresh_39b);
    RUN_TEST(test_adaptive_uses_only_non_own_370_for_direction);
    RUN_TEST(test_adaptive_uses_decision_target_without_legacy_h1_h2_ranges);
    RUN_TEST(test_local_send_success_does_not_increment_das_ack_count);
    RUN_TEST(test_das_transition_from_3_to_1_records_ack_after_tx);
    RUN_TEST(test_failed_send_does_not_advance_corrective_burst);
    RUN_TEST(test_adaptive_runtime_reenable_rearms_without_mode_change);
    RUN_TEST(test_adaptive_angle_tracks_only_accepted_non_own_oem_epas);
    return UNITY_END();
}
