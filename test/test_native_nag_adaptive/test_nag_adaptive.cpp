#include <unity.h>

#include "nag_adaptive_controller.h"

static constexpr uint32_t kEntropy = 0x13579BDFu;

static CanFrame makeDasFrame(uint8_t hos)
{
    CanFrame frame = {.id = NagDasFeedbackTracker::kDasCanId, .dlc = 8};
    frame.data[5] = static_cast<uint8_t>((hos & 0x0F) << 2);
    return frame;
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
    RUN_TEST(test_release_targets_decay_monotonically_and_zero_enters_no_send_rest);
    RUN_TEST(test_corrective_hos_requires_three_valid_oem_epas_frames_before_send);
    RUN_TEST(test_epas_gap_over_200ms_rearms_during_corrective);
    RUN_TEST(test_fault_hold_recovers_after_2000ms_continuously_fresh_normal_hos);
    RUN_TEST(test_request_reset_recovers_fault_hold_immediately);
    RUN_TEST(test_epas_gap_clears_direction_reversal_candidate_timer);
    RUN_TEST(test_normalize_config_swaps_and_clamps_every_range);
    return UNITY_END();
}
