#pragma once

#include <cstdint>

#include "nag_das_feedback.h"

struct NagAdaptiveConfig
{
    bool maintenanceEnabled = true;
    int16_t preventiveNegativeMinCentiNm = 150;
    int16_t preventiveNegativeMaxCentiNm = 180;
    int16_t preventivePositiveMinCentiNm = 150;
    int16_t preventivePositiveMaxCentiNm = 180;
    int16_t correctiveNegativeMinCentiNm = 180;
    int16_t correctiveNegativeMaxCentiNm = 240;
    int16_t correctivePositiveMinCentiNm = 180;
    int16_t correctivePositiveMaxCentiNm = 240;
    uint16_t correctivePositiveFrames = 100;
    uint16_t correctiveNegativeFrames = 100;
    uint32_t activityMinMs = 2000;
    uint32_t activityMaxMs = 3000;
    uint32_t releaseMinMs = 200;
    uint32_t releaseMaxMs = 400;
    uint32_t restMinMs = 0;
    uint32_t restMaxMs = 0;
    uint32_t h2PersistenceMs = 3000;
    uint32_t preCorrectionPauseMs = 1000;
    uint32_t correctiveSendMinMs = 3000;
    uint32_t correctiveSendMaxMs = 3000;
    uint32_t correctivePauseMinMs = 1000;
    uint32_t correctivePauseMaxMs = 2000;
    uint32_t stabilityVerifyMs = 5000;
    uint32_t dasFreshTimeoutMs = 750;
};

struct NagAdaptiveDecision
{
    bool shouldSend = false;
    int16_t targetTorqueCentiNm = 0;
    int8_t injectionSign = 0;
    bool corrective = false;
    uint8_t attempt = 0;
    uint32_t burstFrame = 0;
};

struct NagAdaptiveSnapshot
{
    uint8_t phase = 0;
    uint8_t blockReason = 0;
    uint8_t directionSource = 0;
    bool dasSeen = false;
    bool dasValid = false;
    bool dasFresh = false;
    uint8_t dasHos = 15;
    uint32_t dasAgeMs = 0xFFFFFFFFu;
    uint32_t lastDasFrameMs = 0;
    uint32_t dasFreshnessLimitMs = 0;
    int16_t steeringAngleDeciDeg = 0;
    int16_t observedTorqueCentiNm = 0;
    int16_t targetTorqueCentiNm = 0;
    int8_t injectionSign = 0;
    bool outputActive = false;
    int16_t lastSuccessfullyTransmittedTorqueCentiNm = 0;
    uint8_t correctiveAttempt = 0;
    uint32_t correctiveBurstFrame = 0;
    uint32_t correctiveBurstFrameTarget = 0;
    int8_t correctiveSweepSign = 0;
    uint16_t correctiveSweepFrame = 0;
    uint16_t correctiveSweepFrameTarget = 0;
    int16_t correctiveSweepPeakCentiNm = 0;
    uint32_t phaseRemainingMs = 0;
    uint32_t hosEscalationCount = 0;
    uint32_t acknowledgementCount = 0;
    uint32_t lastAcknowledgementLatencyMs = 0;
    uint32_t maxAcknowledgementLatencyMs = 0;
    bool h2Tracking = false;
    uint32_t h2ElapsedMs = 0;
    uint32_t h2ThresholdMs = 0;
    uint8_t correctiveMode = 0;
    bool correctiveTransition = false;
    uint8_t correctiveTransitionFrame = 0;
    uint8_t correctiveTransitionFrameTarget = 0;
};

struct NagAdaptiveDasFreshness
{
    bool seen = false;
    bool fresh = false;
    uint32_t ageMs = 0xFFFFFFFFu;
};

inline NagAdaptiveDasFreshness nagAdaptiveDasFreshnessAt(
    const NagAdaptiveSnapshot &snapshot, uint32_t nowMs)
{
    NagAdaptiveDasFreshness result;
    result.seen = snapshot.dasSeen;
    if (!snapshot.dasSeen)
        return result;

    result.ageMs = static_cast<uint32_t>(nowMs - snapshot.lastDasFrameMs);
    result.fresh = snapshot.dasValid &&
                   result.ageMs <= snapshot.dasFreshnessLimitMs;
    return result;
}

class NagAdaptiveController
{
public:
    enum Phase : uint8_t
    {
        PHASE_DISABLED = 0,
        PHASE_WAIT_DAS = 1,
        PHASE_ARMING = 2,
        PHASE_MAINTENANCE = 3,
        PHASE_RELEASE = 4,
        PHASE_REST = 5,
        PHASE_CORRECTIVE = 6,
        PHASE_VERIFY = 7,
        PHASE_FAULT_HOLD = 8,
        PHASE_MONITOR_ONLY = 9,
        PHASE_PRE_CORRECTIVE_PAUSE = 11,
        PHASE_STABILITY_VERIFY = 12,
    };

    enum BlockReason : uint8_t
    {
        BLOCK_NONE = 0,
        BLOCK_DISABLED = 1,
        BLOCK_DAS_MISSING = 2,
        BLOCK_DAS_STALE = 3,
        BLOCK_ARMING = 4,
        BLOCK_REST = 5,
        BLOCK_NO_DIRECTION = 6,
        BLOCK_VERIFY = 7,
        BLOCK_DAS_STATE = 8,
        BLOCK_MAINTENANCE_DISABLED = 11,
        BLOCK_STEERING_ANGLE_LIMIT = 13,
        BLOCK_CORRECTIVE_DISABLED = 14,
    };

    enum DirectionSource : uint8_t
    {
        DIRECTION_NONE = 0,
        DIRECTION_ANGLE = 1,
        DIRECTION_HOLD = 2,
        DIRECTION_CORRECTIVE_SWEEP = 3,
        DIRECTION_CORRECTIVE_FIXED = 4,
        DIRECTION_CORRECTIVE_TRANSITION = 5,
    };

    enum CorrectiveMode : uint8_t
    {
        CORRECTIVE_MODE_DISABLED = 0,
        CORRECTIVE_MODE_BIPOLAR = 1,
        CORRECTIVE_MODE_POSITIVE_ONLY = 2,
        CORRECTIVE_MODE_NEGATIVE_ONLY = 3,
    };

    static NagAdaptiveConfig normalizeConfig(NagAdaptiveConfig value)
    {
        normalizeI16Range(value.preventiveNegativeMinCentiNm,
                          value.preventiveNegativeMaxCentiNm, 150, 180);
        normalizeI16Range(value.preventivePositiveMinCentiNm,
                          value.preventivePositiveMaxCentiNm, 150, 180);
        normalizeI16Range(value.correctiveNegativeMinCentiNm,
                          value.correctiveNegativeMaxCentiNm, 180, 250);
        normalizeI16Range(value.correctivePositiveMinCentiNm,
                          value.correctivePositiveMaxCentiNm, 180, 250);
        value.correctivePositiveFrames =
            normalizeCorrectiveFrames(value.correctivePositiveFrames);
        value.correctiveNegativeFrames =
            normalizeCorrectiveFrames(value.correctiveNegativeFrames);
        normalizeU32Range(value.activityMinMs, value.activityMaxMs, 100, UINT32_MAX);
        normalizeU32Range(value.releaseMinMs, value.releaseMaxMs, 100, 1000);
        normalizeOptionalU32Range(value.restMinMs, value.restMaxMs, 100, UINT32_MAX);
        normalizeU32Range(value.correctiveSendMinMs, value.correctiveSendMaxMs,
                          100, UINT32_MAX);
        normalizeOptionalU32Range(value.correctivePauseMinMs,
                                  value.correctivePauseMaxMs,
                                  100, UINT32_MAX);
        value.dasFreshTimeoutMs = clampU32(value.dasFreshTimeoutMs, 100, 2000);
        return value;
    }

    void setConfig(const NagAdaptiveConfig &requested)
    {
        config_ = normalizeConfig(requested);
        resetRequested_ = true;
    }

    NagAdaptiveConfig config() const
    {
        return config_;
    }

    void requestReset()
    {
        resetRequested_ = true;
    }

    void disable(uint32_t nowMs)
    {
        (void)nowMs;
        resetRequested_ = false;
        enabled_ = false;
        phase_ = PHASE_DISABLED;
        blockReason_ = BLOCK_DISABLED;
        directionSource_ = DIRECTION_NONE;
        targetTorqueCentiNm_ = 0;
        injectionSign_ = 0;
        outputActive_ = false;
        armingFrames_ = 0;
        correctiveActive_ = false;
        acknowledgementStarted_ = false;
        normalHosConsecutive_ = 0;
        clearH2Tracking();
        stabilityVerifyActive_ = false;
        clearSweepState();
    }

    bool observeDas(const CanFrame &frame, uint32_t nowMs)
    {
        applyReset(nowMs);
        const bool continuesNormalRecovery = faultRecoveryActive_ && das_.seen() &&
                                             das_.raw() <= 2 &&
                                             das_.fresh(nowMs, config_.dasFreshTimeoutMs);
        const bool accepted = das_.observe(frame, nowMs);
        if (!accepted && phase_ == PHASE_FAULT_HOLD)
            faultRecoveryActive_ = false;
        if (frame.id != NagDasFeedbackTracker::kDasCanId || frame.dlc < 8)
            return false;

        const uint8_t hos = das_.raw();
        if (hos >= 6)
        {
            enterFault(BLOCK_DAS_STATE);
            return accepted;
        }
        if (!enabled_)
            return accepted;

        if (phase_ == PHASE_FAULT_HOLD)
        {
            if (accepted && hos <= 2)
            {
                if (!continuesNormalRecovery)
                {
                    faultRecoveryActive_ = true;
                    faultRecoveryStartedAtMs_ = nowMs;
                }
                if (static_cast<uint32_t>(nowMs - faultRecoveryStartedAtMs_) >= 2000U)
                {
                    faultRecoveryActive_ = false;
                    beginMaintenance(nowMs, rngState_);
                }
            }
            else
            {
                faultRecoveryActive_ = false;
            }
            return accepted;
        }

        if (hos >= 3 && hos <= 5)
        {
            normalHosConsecutive_ = 0;
            if (!correctiveActive_)
                beginRecovery(nowMs);
            return accepted;
        }

        if (correctiveActive_)
        {
            if (phase_ == PHASE_PRE_CORRECTIVE_PAUSE)
            {
                normalHosConsecutive_ = 0;
                return accepted;
            }
            if (hos == 2)
            {
                normalHosConsecutive_ = 0;
                return accepted;
            }
            if (normalHosConsecutive_ < 2U)
                normalHosConsecutive_++;
            if (normalHosConsecutive_ >= 2U)
                finishCorrection(nowMs);
            return accepted;
        }

        if (phase_ == PHASE_WAIT_DAS)
            beginArming();

        if (!config_.maintenanceEnabled)
        {
            clearH2Tracking();
            return accepted;
        }

        if (hos == 2)
        {
            if (!h2Tracking_)
            {
                h2Tracking_ = true;
                h2StartedAtMs_ = nowMs;
            }
            if (config_.h2PersistenceMs == 0U || h2PersistenceExpired(nowMs))
                beginRecovery(nowMs);
            return accepted;
        }

        clearH2Tracking();

        if (phase_ == PHASE_STABILITY_VERIFY && phaseExpired(nowMs))
            beginMaintenance(nowMs, rngState_);
        return accepted;
    }

    NagAdaptiveDecision observeEpas(uint32_t nowMs,
                                    int16_t steeringAngleDeciDeg,
                                    int16_t observedTorqueCentiNm,
                                    uint32_t entropy)
    {
        applyReset(nowMs);
        observedTorqueCentiNm_ = observedTorqueCentiNm;
        steeringAngleDeciDeg_ = steeringAngleDeciDeg;

        if (!enabled_)
            return blockedDecision(BLOCK_DISABLED);
        if (phase_ == PHASE_FAULT_HOLD)
        {
            if (faultRecoveryActive_ && !das_.fresh(nowMs, config_.dasFreshTimeoutMs))
                faultRecoveryActive_ = false;
            return blockedDecision(static_cast<BlockReason>(blockReason_));
        }
        if (!das_.seen())
        {
            enterWait(BLOCK_DAS_MISSING);
            return blockedDecision(BLOCK_DAS_MISSING);
        }
        if (!das_.fresh(nowMs, config_.dasFreshTimeoutMs))
        {
            enterWait(BLOCK_DAS_STALE);
            return blockedDecision(BLOCK_DAS_STALE);
        }

        if (epasSeen_ && static_cast<uint32_t>(nowMs - lastEpasAtMs_) > 200U)
        {
            armingFrames_ = 0;
            if (!correctiveActive_)
                beginArming();
        }
        if (phase_ == PHASE_WAIT_DAS)
            beginArming();
        epasSeen_ = true;
        lastEpasAtMs_ = nowMs;
        if (!steeringAngleOutOfRange(steeringAngleDeciDeg) &&
            (phase_ != PHASE_CORRECTIVE || correctiveSweepSign_ == 0))
            updateDirection(steeringAngleDeciDeg);

        if (armingFrames_ < 3)
            armingFrames_++;
        if (armingFrames_ < 3)
            return blockedDecision(BLOCK_ARMING);

        if (phase_ == PHASE_ARMING)
        {
            if (das_.raw() >= 3 && das_.raw() <= 5)
                beginRecovery(nowMs);
            else
                beginMaintenance(nowMs, entropy);
        }

        if (phase_ == PHASE_MONITOR_ONLY)
            return blockedDecision(BLOCK_MAINTENANCE_DISABLED);

        if (h2Tracking_ && !correctiveActive_ && h2PersistenceExpired(nowMs))
            beginRecovery(nowMs);

        if (phase_ == PHASE_PRE_CORRECTIVE_PAUSE)
        {
            if (!phaseExpired(nowMs))
                return blockedDecision(BLOCK_VERIFY);
            beginCorrective(nowMs);
        }

        if (phase_ == PHASE_CORRECTIVE && phaseExpired(nowMs))
        {
            beginCorrectivePause(nowMs);
            if (phase_ == PHASE_VERIFY)
                return blockedDecision(BLOCK_VERIFY);
        }

        if (phase_ == PHASE_VERIFY)
        {
            if (!phaseExpired(nowMs))
                return blockedDecision(BLOCK_VERIFY);
            beginCorrective(nowMs);
        }

        if (phase_ == PHASE_STABILITY_VERIFY && phaseExpired(nowMs))
            beginMaintenance(nowMs, entropy);

        if (phase_ == PHASE_MAINTENANCE && phaseExpired(nowMs))
            beginRest(nowMs, entropy);

        if (phase_ == PHASE_REST)
        {
            if (!phaseExpired(nowMs))
                return blockedDecision(BLOCK_REST);
            beginMaintenance(nowMs, entropy);
        }

        if (phase_ == PHASE_RELEASE)
        {
            const NagAdaptiveDecision decision = releaseDecision(nowMs, entropy);
            if (steeringAngleOutOfRange(steeringAngleDeciDeg))
                return blockedDecision(BLOCK_STEERING_ANGLE_LIMIT);
            return decision;
        }

        if (phase_ == PHASE_CORRECTIVE && correctiveSweepSign_ == 0)
            initializeCorrectiveSweep();

        if (steeringAngleOutOfRange(steeringAngleDeciDeg))
            return blockedDecision(BLOCK_STEERING_ANGLE_LIMIT);

        if (phase_ == PHASE_CORRECTIVE && correctiveMode() == CORRECTIVE_MODE_DISABLED)
            return blockedDecision(BLOCK_CORRECTIVE_DISABLED);

        if (phase_ != PHASE_CORRECTIVE && injectionSign_ == 0)
            return blockedDecision(BLOCK_NO_DIRECTION);

        if (phase_ == PHASE_CORRECTIVE && correctiveTransitionActive_)
            return prepareCorrectiveTransitionDecision();

        if (phase_ == PHASE_CORRECTIVE)
            return prepareCorrectiveSweepDecision(entropy);

        if (phase_ == PHASE_MAINTENANCE ||
            phase_ == PHASE_STABILITY_VERIFY)
        {
            preparePreventive(entropy);
            return sendDecision(false);
        }

        return blockedDecision(static_cast<BlockReason>(blockReason_));
    }

    void onTransmitResult(uint32_t nowMs,
                          const NagAdaptiveDecision &decision,
                          bool success)
    {
        if (!success || !decision.shouldSend)
            return;

        lastSuccessfullyTransmittedTorqueCentiNm_ = decision.targetTorqueCentiNm;
        outputActive_ = decision.targetTorqueCentiNm != 0;

        if (phase_ != PHASE_CORRECTIVE || !decision.corrective)
        {
            if (phase_ == PHASE_MAINTENANCE ||
                phase_ == PHASE_STABILITY_VERIFY)
                advancePreventiveSweep();
            return;
        }

        if (!acknowledgementStarted_)
        {
            acknowledgementStarted_ = true;
            acknowledgementStartedAtMs_ = nowMs;
        }
        correctiveBurstFrame_++;
        if (correctiveTransitionActive_)
        {
            if (correctiveTransitionFrame_ < kCorrectiveTransitionFrames)
                correctiveTransitionFrame_++;
            if (correctiveTransitionFrame_ >= kCorrectiveTransitionFrames)
                finishCorrectiveTransition();
            return;
        }
        if (correctiveSweepFrame_ < correctiveSweepFrameTarget_)
            correctiveSweepFrame_++;
        if (correctiveSweepFrame_ >= correctiveSweepFrameTarget_)
        {
            const int8_t nextSign = nextCorrectiveSign(correctiveSweepSign_);
            if (nextSign != 0 && nextSign != correctiveSweepSign_)
                beginCorrectiveTransition(correctiveSweepSign_, nextSign);
            else
                restartCorrectiveSide(nextSign);
        }
    }

    NagAdaptiveSnapshot snapshot(uint32_t nowMs) const
    {
        NagAdaptiveSnapshot value;
        value.phase = phase_;
        value.blockReason = blockReason_;
        value.directionSource = directionSource_;
        value.dasSeen = das_.seen();
        value.dasValid = das_.valid();
        value.dasFresh = das_.fresh(nowMs, config_.dasFreshTimeoutMs);
        value.dasHos = das_.raw();
        value.dasAgeMs = das_.ageMs(nowMs);
        value.lastDasFrameMs = value.dasSeen
                                   ? static_cast<uint32_t>(nowMs - value.dasAgeMs)
                                   : 0U;
        value.dasFreshnessLimitMs = config_.dasFreshTimeoutMs;
        value.steeringAngleDeciDeg = steeringAngleDeciDeg_;
        value.observedTorqueCentiNm = observedTorqueCentiNm_;
        value.targetTorqueCentiNm = targetTorqueCentiNm_;
        value.injectionSign = outputActive_ && lastSuccessfullyTransmittedTorqueCentiNm_ != 0
                                  ? (lastSuccessfullyTransmittedTorqueCentiNm_ < 0 ? -1 : 1)
                                  : (phase_ == PHASE_CORRECTIVE && correctiveSweepSign_ != 0
                                         ? correctiveSweepSign_
                                         : injectionSign_);
        value.outputActive = outputActive_;
        value.lastSuccessfullyTransmittedTorqueCentiNm =
            lastSuccessfullyTransmittedTorqueCentiNm_;
        value.correctiveAttempt = correctiveAttempt_;
        value.correctiveBurstFrame = correctiveBurstFrame_;
        value.correctiveBurstFrameTarget = correctiveBurstFrameTarget_;
        value.correctiveSweepSign = correctiveSweepSign_;
        value.correctiveSweepFrame = correctiveSweepFrame_;
        value.correctiveSweepFrameTarget = correctiveSweepFrameTarget_;
        value.correctiveSweepPeakCentiNm = correctiveSweepPeakCentiNm_;
        value.phaseRemainingMs = phaseRemainingMs(nowMs);
        value.hosEscalationCount = hosEscalationCount_;
        value.acknowledgementCount = acknowledgementCount_;
        value.lastAcknowledgementLatencyMs = lastAcknowledgementLatencyMs_;
        value.maxAcknowledgementLatencyMs = maxAcknowledgementLatencyMs_;
        value.h2Tracking = h2Tracking_;
        value.h2ElapsedMs = h2Tracking_
                                ? static_cast<uint32_t>(nowMs - h2StartedAtMs_)
                                : 0U;
        value.h2ThresholdMs = config_.h2PersistenceMs;
        value.correctiveMode = static_cast<uint8_t>(correctiveMode());
        value.correctiveTransition = correctiveTransitionActive_;
        value.correctiveTransitionFrame = correctiveTransitionFrame_;
        value.correctiveTransitionFrameTarget = kCorrectiveTransitionFrames;
        return value;
    }

private:
    static int16_t clampI16(int32_t value, int16_t minValue, int16_t maxValue)
    {
        if (value < minValue)
            return minValue;
        if (value > maxValue)
            return maxValue;
        return static_cast<int16_t>(value);
    }

    static uint32_t clampU32(uint32_t value, uint32_t minValue, uint32_t maxValue)
    {
        if (value < minValue)
            return minValue;
        if (value > maxValue)
            return maxValue;
        return value;
    }

    static uint16_t clampU16(uint16_t value, uint16_t minValue, uint16_t maxValue)
    {
        if (value < minValue)
            return minValue;
        if (value > maxValue)
            return maxValue;
        return value;
    }

    static uint16_t normalizeCorrectiveFrames(uint16_t value)
    {
        return value == 0U ? 0U : clampU16(value, 10U, 255U);
    }

    static void normalizeI16Range(int16_t &minValue, int16_t &maxValue,
                                  int16_t floor, int16_t ceiling)
    {
        if (minValue > maxValue)
        {
            const int16_t temporary = minValue;
            minValue = maxValue;
            maxValue = temporary;
        }
        minValue = clampI16(minValue, floor, ceiling);
        maxValue = clampI16(maxValue, floor, ceiling);
    }

    static void normalizeU32Range(uint32_t &minValue, uint32_t &maxValue,
                                  uint32_t floor, uint32_t ceiling)
    {
        if (minValue > maxValue)
        {
            const uint32_t temporary = minValue;
            minValue = maxValue;
            maxValue = temporary;
        }
        minValue = clampU32(minValue, floor, ceiling);
        maxValue = clampU32(maxValue, floor, ceiling);
    }

    static void normalizeOptionalU32Range(uint32_t &minValue, uint32_t &maxValue,
                                          uint32_t floor, uint32_t ceiling)
    {
        if (minValue == 0 && maxValue == 0)
            return;
        normalizeU32Range(minValue, maxValue, floor, ceiling);
    }

    void applyReset(uint32_t nowMs)
    {
        if (!resetRequested_)
            return;
        resetRequested_ = false;
        enabled_ = true;
        das_ = NagDasFeedbackTracker();
        phase_ = PHASE_WAIT_DAS;
        blockReason_ = BLOCK_DAS_MISSING;
        directionSource_ = DIRECTION_NONE;
        targetTorqueCentiNm_ = 0;
        observedTorqueCentiNm_ = 0;
        steeringAngleDeciDeg_ = 0;
        injectionSign_ = 0;
        armingFrames_ = 0;
        epasSeen_ = false;
        lastEpasAtMs_ = nowMs;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = 0;
        releaseStartTorqueCentiNm_ = 0;
        lastSuccessfullyTransmittedTorqueCentiNm_ = 0;
        outputActive_ = false;
        currentMagnitudeCentiNm_ = 0;
        preventiveMagnitudeStep_ = 1;
        correctiveSweepSign_ = 0;
        correctiveSweepFrame_ = 0;
        correctiveSweepFrameTarget_ = 0;
        correctiveSweepPeakCentiNm_ = 0;
        correctiveTransitionActive_ = false;
        correctiveTransitionFrame_ = 0;
        correctiveTransitionFromSign_ = 0;
        correctiveTransitionToSign_ = 0;
        correctiveActive_ = false;
        correctiveAttempt_ = 0;
        correctiveBurstFrame_ = 0;
        correctiveBurstFrameTarget_ = 0;
        acknowledgementStarted_ = false;
        normalHosConsecutive_ = 0;
        clearH2Tracking();
        stabilityVerifyActive_ = false;
        rngState_ = 0;
        hosEscalationCount_ = 0;
        acknowledgementCount_ = 0;
        lastAcknowledgementLatencyMs_ = 0;
        maxAcknowledgementLatencyMs_ = 0;
    }

    void enterWait(BlockReason reason)
    {
        phase_ = PHASE_WAIT_DAS;
        blockReason_ = reason;
        targetTorqueCentiNm_ = 0;
        armingFrames_ = 0;
        epasSeen_ = false;
        correctiveActive_ = false;
        acknowledgementStarted_ = false;
        normalHosConsecutive_ = 0;
        clearH2Tracking();
        stabilityVerifyActive_ = false;
        faultRecoveryActive_ = false;
        outputActive_ = false;
        clearSweepState();
    }

    void beginArming()
    {
        phase_ = PHASE_ARMING;
        blockReason_ = BLOCK_ARMING;
        targetTorqueCentiNm_ = 0;
        armingFrames_ = 0;
    }

    void beginMaintenance(uint32_t nowMs, uint32_t entropy)
    {
        if (!config_.maintenanceEnabled)
        {
            beginMonitorOnly();
            return;
        }
        phase_ = PHASE_MAINTENANCE;
        blockReason_ = BLOCK_NONE;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = preventiveRestDisabled()
                               ? 0U
                               : triangularDuration(config_.activityMinMs,
                                                    config_.activityMaxMs,
                                                    entropy);
        clearSweepState();
        stabilityVerifyActive_ = false;
        normalHosConsecutive_ = 0;
        correctiveAttempt_ = 0;
        correctiveBurstFrame_ = 0;
        correctiveBurstFrameTarget_ = 0;
        preparePreventive(entropy);
    }

    void beginMonitorOnly()
    {
        phase_ = PHASE_MONITOR_ONLY;
        blockReason_ = BLOCK_MAINTENANCE_DISABLED;
        phaseDurationMs_ = 0;
        targetTorqueCentiNm_ = 0;
        clearSweepState();
        outputActive_ = false;
        correctiveAttempt_ = 0;
        correctiveBurstFrame_ = 0;
        correctiveBurstFrameTarget_ = 0;
        stabilityVerifyActive_ = false;
        clearH2Tracking();
        normalHosConsecutive_ = 0;
    }

    void beginRelease(uint32_t nowMs)
    {
        if (!outputActive_ || lastSuccessfullyTransmittedTorqueCentiNm_ == 0)
        {
            beginRest(nowMs, rngState_);
            return;
        }
        phase_ = PHASE_RELEASE;
        blockReason_ = BLOCK_NONE;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = 0;
        releaseStartTorqueCentiNm_ = lastSuccessfullyTransmittedTorqueCentiNm_;
    }

    void beginRest(uint32_t nowMs, uint32_t entropy)
    {
        if (preventiveRestDisabled())
        {
            beginMaintenance(nowMs, entropy);
            return;
        }
        phase_ = PHASE_REST;
        blockReason_ = BLOCK_REST;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = triangularDuration(config_.restMinMs, config_.restMaxMs, entropy);
        targetTorqueCentiNm_ = 0;
        currentMagnitudeCentiNm_ = 0;
        preventiveMagnitudeStep_ = 1;
        outputActive_ = false;
    }

    void beginRecovery(uint32_t nowMs)
    {
        if (correctiveActive_)
            return;
        correctiveActive_ = true;
        stabilityVerifyActive_ = false;
        clearH2Tracking();
        normalHosConsecutive_ = 0;
        hosEscalationCount_++;
        targetTorqueCentiNm_ = 0;
        currentMagnitudeCentiNm_ = 0;
        clearSweepState();
        correctiveBurstFrame_ = 0;
        correctiveBurstFrameTarget_ = 0;
        outputActive_ = false;
        if (config_.preCorrectionPauseMs == 0U)
        {
            beginCorrective(nowMs);
            return;
        }
        phase_ = PHASE_PRE_CORRECTIVE_PAUSE;
        blockReason_ = BLOCK_VERIFY;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = config_.preCorrectionPauseMs;
    }

    void finishCorrection(uint32_t nowMs)
    {
        if (acknowledgementStarted_)
        {
            const uint32_t latency = static_cast<uint32_t>(nowMs - acknowledgementStartedAtMs_);
            acknowledgementCount_++;
            lastAcknowledgementLatencyMs_ = latency;
            if (latency > maxAcknowledgementLatencyMs_)
                maxAcknowledgementLatencyMs_ = latency;
        }
        correctiveActive_ = false;
        acknowledgementStarted_ = false;
        normalHosConsecutive_ = 0;
        clearH2Tracking();
        stabilityVerifyActive_ = false;
        if (!config_.maintenanceEnabled)
        {
            beginMonitorOnly();
            return;
        }
        beginStabilityVerify(nowMs);
    }

    void beginStabilityVerify(uint32_t nowMs)
    {
        if (config_.stabilityVerifyMs == 0U)
        {
            beginMaintenance(nowMs, rngState_);
            return;
        }
        phase_ = PHASE_STABILITY_VERIFY;
        blockReason_ = BLOCK_NONE;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = config_.stabilityVerifyMs;
        stabilityVerifyActive_ = true;
        clearSweepState();
        targetTorqueCentiNm_ = 0;
        outputActive_ = false;
        preparePreventive(rngState_);
    }

    void beginCorrective(uint32_t nowMs)
    {
        correctiveActive_ = true;
        phase_ = PHASE_CORRECTIVE;
        blockReason_ = BLOCK_NONE;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = triangularDuration(config_.correctiveSendMinMs,
                                              config_.correctiveSendMaxMs,
                                              rngState_);
        // This diagnostic counter belongs to one time-based send window. A
        // zero target explicitly means that the window has no frame target.
        correctiveBurstFrame_ = 0;
        correctiveBurstFrameTarget_ = 0;
        if (correctiveAttempt_ < 0xFFU)
            correctiveAttempt_++;
        if (correctiveSweepSign_ == 0)
            initializeCorrectiveSweep();
        if (correctiveTransitionActive_)
            directionSource_ = DIRECTION_CORRECTIVE_TRANSITION;
        else if (correctiveSweepSign_ != 0)
            directionSource_ = correctiveMode() == CORRECTIVE_MODE_BIPOLAR
                                   ? DIRECTION_CORRECTIVE_SWEEP
                                   : DIRECTION_CORRECTIVE_FIXED;
        targetTorqueCentiNm_ = 0;
        lastSuccessfullyTransmittedTorqueCentiNm_ = 0;
        outputActive_ = false;
    }

    void beginCorrectivePause(uint32_t nowMs)
    {
        if (correctivePauseDisabled())
        {
            beginCorrective(nowMs);
            return;
        }
        phase_ = PHASE_VERIFY;
        blockReason_ = BLOCK_VERIFY;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = triangularDuration(config_.correctivePauseMinMs,
                                              config_.correctivePauseMaxMs,
                                              rngState_);
        targetTorqueCentiNm_ = 0;
        outputActive_ = false;
    }

    bool preventiveRestDisabled() const
    {
        return config_.restMinMs == 0U && config_.restMaxMs == 0U;
    }

    bool correctivePauseDisabled() const
    {
        return config_.correctivePauseMinMs == 0U &&
               config_.correctivePauseMaxMs == 0U;
    }

    void enterFault(BlockReason reason)
    {
        phase_ = PHASE_FAULT_HOLD;
        blockReason_ = reason;
        targetTorqueCentiNm_ = 0;
        correctiveActive_ = false;
        acknowledgementStarted_ = false;
        normalHosConsecutive_ = 0;
        clearH2Tracking();
        stabilityVerifyActive_ = false;
        faultRecoveryActive_ = false;
        outputActive_ = false;
        clearSweepState();
    }

    static bool steeringAngleOutOfRange(int16_t angleDeciDeg)
    {
        return angleDeciDeg > 500 || angleDeciDeg < -500;
    }

    void updateDirection(int16_t angleDeciDeg)
    {
        if (angleDeciDeg > 10)
        {
            injectionSign_ = -1;
            directionSource_ = DIRECTION_ANGLE;
            return;
        }
        if (angleDeciDeg < -10)
        {
            injectionSign_ = 1;
            directionSource_ = DIRECTION_ANGLE;
            return;
        }
        directionSource_ = injectionSign_ == 0 ? DIRECTION_NONE : DIRECTION_HOLD;
    }

    NagAdaptiveDecision releaseDecision(uint32_t nowMs, uint32_t entropy)
    {
        if (phaseDurationMs_ == 0)
            phaseDurationMs_ = triangularDuration(config_.releaseMinMs, config_.releaseMaxMs, entropy);
        const uint32_t elapsed = static_cast<uint32_t>(nowMs - phaseStartedAtMs_);
        if (elapsed >= phaseDurationMs_)
        {
            beginRest(nowMs, entropy);
            return blockedDecision(BLOCK_REST);
        }

        const double x = static_cast<double>(elapsed) / static_cast<double>(phaseDurationMs_);
        const double weight = 1.0 - (3.0 * x * x - 2.0 * x * x * x);
        const double scaled = static_cast<double>(releaseStartTorqueCentiNm_) * weight;
        const int32_t rounded = static_cast<int32_t>(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
        targetTorqueCentiNm_ = clampI16(rounded, -180, 180);
        if (targetTorqueCentiNm_ == 0)
        {
            beginRest(nowMs, entropy);
            return blockedDecision(BLOCK_REST);
        }
        NagAdaptiveDecision decision;
        decision.shouldSend = true;
        decision.targetTorqueCentiNm = targetTorqueCentiNm_;
        decision.injectionSign = targetTorqueCentiNm_ < 0 ? -1 : 1;
        blockReason_ = BLOCK_NONE;
        return decision;
    }

    void preparePreventive(uint32_t entropy)
    {
        if (injectionSign_ == 0)
            return;
        int16_t minValue;
        int16_t maxValue;
        preventiveRange(minValue, maxValue);
        if (currentMagnitudeCentiNm_ == 0)
        {
            currentMagnitudeCentiNm_ = triangularMagnitude(minValue, maxValue, entropy);
            if (currentMagnitudeCentiNm_ <= minValue)
                preventiveMagnitudeStep_ = 1;
            else if (currentMagnitudeCentiNm_ >= maxValue)
                preventiveMagnitudeStep_ = -1;
            else
                preventiveMagnitudeStep_ = (nextRandom(entropy) & 1U) != 0U ? 1 : -1;
        }
        else
        {
            currentMagnitudeCentiNm_ =
                clampI16(currentMagnitudeCentiNm_, minValue, maxValue);
            if (currentMagnitudeCentiNm_ <= minValue)
                preventiveMagnitudeStep_ = 1;
            else if (currentMagnitudeCentiNm_ >= maxValue)
                preventiveMagnitudeStep_ = -1;
        }
    }

    void advancePreventiveSweep()
    {
        if (injectionSign_ == 0 || currentMagnitudeCentiNm_ == 0)
            return;
        int16_t minValue;
        int16_t maxValue;
        preventiveRange(minValue, maxValue);
        if (minValue == maxValue)
            return;
        if (currentMagnitudeCentiNm_ >= maxValue)
            preventiveMagnitudeStep_ = -1;
        else if (currentMagnitudeCentiNm_ <= minValue)
            preventiveMagnitudeStep_ = 1;
        currentMagnitudeCentiNm_ = clampI16(
            static_cast<int32_t>(currentMagnitudeCentiNm_) + preventiveMagnitudeStep_,
            minValue, maxValue);
    }

    void preventiveRange(int16_t &minValue, int16_t &maxValue) const
    {
        if (injectionSign_ < 0)
        {
            minValue = config_.preventiveNegativeMinCentiNm;
            maxValue = config_.preventiveNegativeMaxCentiNm;
        }
        else
        {
            minValue = config_.preventivePositiveMinCentiNm;
            maxValue = config_.preventivePositiveMaxCentiNm;
        }
    }

    void correctiveRangeForSign(int8_t sign, int16_t &minValue, int16_t &maxValue) const
    {
        if (sign < 0)
        {
            minValue = config_.correctiveNegativeMinCentiNm;
            maxValue = config_.correctiveNegativeMaxCentiNm;
        }
        else
        {
            minValue = config_.correctivePositiveMinCentiNm;
            maxValue = config_.correctivePositiveMaxCentiNm;
        }
    }

    uint16_t correctiveFramesForSign(int8_t sign) const
    {
        if (sign < 0)
            return config_.correctiveNegativeFrames;
        if (sign > 0)
            return config_.correctivePositiveFrames;
        return 0;
    }

    CorrectiveMode correctiveMode() const
    {
        const bool negativeEnabled = config_.correctiveNegativeFrames != 0U;
        const bool positiveEnabled = config_.correctivePositiveFrames != 0U;
        if (negativeEnabled && positiveEnabled)
            return CORRECTIVE_MODE_BIPOLAR;
        if (positiveEnabled)
            return CORRECTIVE_MODE_POSITIVE_ONLY;
        if (negativeEnabled)
            return CORRECTIVE_MODE_NEGATIVE_ONLY;
        return CORRECTIVE_MODE_DISABLED;
    }

    bool correctiveSignEnabled(int8_t sign) const
    {
        return correctiveFramesForSign(sign) != 0U;
    }

    int8_t selectEnabledCorrectiveSign(int8_t preferredSign) const
    {
        if (preferredSign == 0)
        {
            if (correctiveMode() == CORRECTIVE_MODE_POSITIVE_ONLY)
                return 1;
            if (correctiveMode() == CORRECTIVE_MODE_NEGATIVE_ONLY)
                return -1;
            return 0;
        }
        if (preferredSign != 0 && correctiveSignEnabled(preferredSign))
            return preferredSign;
        if (preferredSign != 0 &&
            correctiveSignEnabled(static_cast<int8_t>(-preferredSign)))
            return static_cast<int8_t>(-preferredSign);
        if (correctiveSignEnabled(1))
            return 1;
        if (correctiveSignEnabled(-1))
            return -1;
        return 0;
    }

    int8_t nextCorrectiveSign(int8_t currentSign) const
    {
        if (correctiveMode() == CORRECTIVE_MODE_BIPOLAR)
            return static_cast<int8_t>(-currentSign);
        return selectEnabledCorrectiveSign(currentSign);
    }

    void initializeCorrectiveSweep()
    {
        correctiveSweepSign_ = selectEnabledCorrectiveSign(injectionSign_);
        correctiveSweepFrame_ = 0;
        correctiveSweepFrameTarget_ = correctiveFramesForSign(correctiveSweepSign_);
        correctiveSweepPeakCentiNm_ = 0;
        currentMagnitudeCentiNm_ = 0;
        correctiveTransitionActive_ = false;
        correctiveTransitionFrame_ = 0;
        if (correctiveSweepSign_ != 0)
            directionSource_ = correctiveMode() == CORRECTIVE_MODE_BIPOLAR
                                   ? DIRECTION_CORRECTIVE_SWEEP
                                   : DIRECTION_CORRECTIVE_FIXED;
    }

    void restartCorrectiveSide(int8_t sign)
    {
        correctiveSweepSign_ = sign;
        correctiveSweepFrame_ = 0;
        correctiveSweepFrameTarget_ = correctiveFramesForSign(sign);
        correctiveSweepPeakCentiNm_ = 0;
        currentMagnitudeCentiNm_ = 0;
        correctiveTransitionActive_ = false;
        correctiveTransitionFrame_ = 0;
        correctiveTransitionFromSign_ = 0;
        correctiveTransitionToSign_ = 0;
        if (sign != 0)
            directionSource_ = correctiveMode() == CORRECTIVE_MODE_BIPOLAR
                                   ? DIRECTION_CORRECTIVE_SWEEP
                                   : DIRECTION_CORRECTIVE_FIXED;
    }

    void beginCorrectiveTransition(int8_t fromSign, int8_t toSign)
    {
        correctiveTransitionActive_ = true;
        correctiveTransitionFrame_ = 0;
        correctiveTransitionFromSign_ = fromSign;
        correctiveTransitionToSign_ = toSign;
        correctiveSweepFrame_ = 0;
        correctiveSweepFrameTarget_ = 0;
        correctiveSweepPeakCentiNm_ = 0;
        currentMagnitudeCentiNm_ = 0;
        directionSource_ = DIRECTION_CORRECTIVE_TRANSITION;
    }

    void finishCorrectiveTransition()
    {
        const int8_t toSign = correctiveTransitionToSign_;
        restartCorrectiveSide(toSign);
    }

    NagAdaptiveDecision prepareCorrectiveTransitionDecision()
    {
        int16_t fromMin;
        int16_t fromMax;
        int16_t toMin;
        int16_t toMax;
        correctiveRangeForSign(correctiveTransitionFromSign_, fromMin, fromMax);
        correctiveRangeForSign(correctiveTransitionToSign_, toMin, toMax);
        (void)fromMax;
        (void)toMax;

        const int32_t step = static_cast<int32_t>(correctiveTransitionFrame_) + 1;
        const int32_t remaining = static_cast<int32_t>(kCorrectiveTransitionFrames) - step;
        const int32_t fromTorque =
            static_cast<int32_t>(correctiveTransitionFromSign_) * fromMin;
        const int32_t toTorque =
            static_cast<int32_t>(correctiveTransitionToSign_) * toMin;
        targetTorqueCentiNm_ = clampI16(
            (fromTorque * remaining + toTorque * step) /
                static_cast<int32_t>(kCorrectiveTransitionFrames),
            -250, 250);

        NagAdaptiveDecision decision;
        decision.shouldSend = true;
        decision.targetTorqueCentiNm = targetTorqueCentiNm_;
        decision.injectionSign = targetTorqueCentiNm_ < 0
                                     ? -1
                                     : (targetTorqueCentiNm_ > 0 ? 1 : 0);
        decision.corrective = true;
        decision.attempt = correctiveAttempt_;
        decision.burstFrame = correctiveBurstFrame_;
        blockReason_ = BLOCK_NONE;
        return decision;
    }

    NagAdaptiveDecision prepareCorrectiveSweepDecision(uint32_t entropy)
    {
        if (correctiveSweepSign_ == 0)
            return blockedDecision(BLOCK_NO_DIRECTION);
        if (correctiveSweepFrameTarget_ == 0)
            correctiveSweepFrameTarget_ = correctiveFramesForSign(correctiveSweepSign_);
        if (correctiveSweepPeakCentiNm_ == 0)
        {
            int16_t minValue;
            int16_t maxValue;
            correctiveRangeForSign(correctiveSweepSign_, minValue, maxValue);
            correctiveSweepPeakCentiNm_ = triangularMagnitude(minValue, maxValue, entropy);
        }

        const uint16_t lastFrame = static_cast<uint16_t>(correctiveSweepFrameTarget_ - 1U);
        const uint16_t mirroredFrame = correctiveSweepFrame_ <= lastFrame - correctiveSweepFrame_
                                           ? correctiveSweepFrame_
                                           : static_cast<uint16_t>(lastFrame - correctiveSweepFrame_);
        const uint16_t rampMaximum = static_cast<uint16_t>(lastFrame / 2U);
        int16_t minValue;
        int16_t maxValue;
        correctiveRangeForSign(correctiveSweepSign_, minValue, maxValue);
        (void)maxValue;
        int16_t magnitude = correctiveSweepPeakCentiNm_;
        if (rampMaximum != 0U)
        {
            const uint32_t scaled =
                static_cast<uint32_t>(correctiveSweepPeakCentiNm_ - minValue) * mirroredFrame;
            magnitude = static_cast<int16_t>(minValue + scaled / rampMaximum);
        }
        currentMagnitudeCentiNm_ = magnitude;
        targetTorqueCentiNm_ = clampI16(
            static_cast<int32_t>(correctiveSweepSign_) * magnitude, -250, 250);

        NagAdaptiveDecision decision;
        decision.shouldSend = true;
        decision.targetTorqueCentiNm = targetTorqueCentiNm_;
        decision.injectionSign = correctiveSweepSign_;
        decision.corrective = true;
        decision.attempt = correctiveAttempt_;
        decision.burstFrame = correctiveBurstFrame_;
        blockReason_ = BLOCK_NONE;
        return decision;
    }

    void clearSweepState()
    {
        currentMagnitudeCentiNm_ = 0;
        preventiveMagnitudeStep_ = 1;
        correctiveSweepSign_ = 0;
        correctiveSweepFrame_ = 0;
        correctiveSweepFrameTarget_ = 0;
        correctiveSweepPeakCentiNm_ = 0;
        correctiveTransitionActive_ = false;
        correctiveTransitionFrame_ = 0;
        correctiveTransitionFromSign_ = 0;
        correctiveTransitionToSign_ = 0;
    }

    bool h2PersistenceExpired(uint32_t nowMs) const
    {
        return h2Tracking_ &&
               static_cast<uint32_t>(nowMs - h2StartedAtMs_) >=
                   config_.h2PersistenceMs;
    }

    void clearH2Tracking()
    {
        h2Tracking_ = false;
        h2StartedAtMs_ = 0;
    }

    NagAdaptiveDecision sendDecision(bool corrective)
    {
        targetTorqueCentiNm_ = clampI16(
            static_cast<int32_t>(injectionSign_) * currentMagnitudeCentiNm_, -180, 180);
        NagAdaptiveDecision decision;
        decision.shouldSend = true;
        decision.targetTorqueCentiNm = targetTorqueCentiNm_;
        decision.injectionSign =
            phase_ == PHASE_CORRECTIVE && correctiveSweepSign_ != 0
                ? correctiveSweepSign_
                : injectionSign_;
        decision.corrective = corrective;
        decision.attempt = corrective ? correctiveAttempt_ : 0;
        decision.burstFrame = corrective ? correctiveBurstFrame_ : 0;
        blockReason_ = BLOCK_NONE;
        return decision;
    }

    NagAdaptiveDecision blockedDecision(BlockReason reason)
    {
        blockReason_ = reason;
        NagAdaptiveDecision decision;
        decision.injectionSign =
            phase_ == PHASE_CORRECTIVE && correctiveSweepSign_ != 0
                ? correctiveSweepSign_
                : injectionSign_;
        decision.corrective = phase_ == PHASE_CORRECTIVE;
        decision.attempt = correctiveAttempt_;
        decision.burstFrame = correctiveBurstFrame_;
        return decision;
    }

    bool phaseExpired(uint32_t nowMs) const
    {
        return phaseDurationMs_ != 0 &&
               static_cast<uint32_t>(nowMs - phaseStartedAtMs_) >= phaseDurationMs_;
    }

    uint32_t phaseRemainingMs(uint32_t nowMs) const
    {
        if (phaseDurationMs_ == 0)
            return 0;
        const uint32_t elapsed = static_cast<uint32_t>(nowMs - phaseStartedAtMs_);
        return elapsed >= phaseDurationMs_ ? 0 : phaseDurationMs_ - elapsed;
    }

    uint32_t nextRandom(uint32_t entropy)
    {
        if (rngState_ == 0)
        {
            rngState_ = entropy ^ 0x9E3779B9u;
            if (rngState_ == 0)
                rngState_ = 0xA341316Cu;
        }
        uint32_t value = rngState_;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        rngState_ = value;
        return value;
    }

    uint32_t triangularDuration(uint32_t minValue, uint32_t maxValue, uint32_t entropy)
    {
        const uint32_t first = static_cast<uint16_t>(nextRandom(entropy));
        const uint32_t second = static_cast<uint16_t>(nextRandom(entropy));
        const uint32_t triangular = (first + second) / 2U;
        const uint64_t scaled = static_cast<uint64_t>(triangular) * (maxValue - minValue);
        return minValue + static_cast<uint32_t>(scaled / 65535U);
    }

    int16_t triangularMagnitude(int16_t minValue, int16_t maxValue, uint32_t entropy)
    {
        const uint32_t first = static_cast<uint16_t>(nextRandom(entropy));
        const uint32_t second = static_cast<uint16_t>(nextRandom(entropy));
        const uint32_t triangular = (first + second) / 2U;
        const uint32_t scaled = triangular * static_cast<uint32_t>(maxValue - minValue);
        return static_cast<int16_t>(minValue + scaled / 65535U);
    }

    NagAdaptiveConfig config_{};
    NagDasFeedbackTracker das_{};
    bool resetRequested_ = false;
    bool enabled_ = false;
    uint8_t phase_ = PHASE_DISABLED;
    uint8_t blockReason_ = BLOCK_DISABLED;
    uint8_t directionSource_ = DIRECTION_NONE;
    int16_t steeringAngleDeciDeg_ = 0;
    int16_t observedTorqueCentiNm_ = 0;
    int16_t targetTorqueCentiNm_ = 0;
    int16_t releaseStartTorqueCentiNm_ = 0;
    int16_t lastSuccessfullyTransmittedTorqueCentiNm_ = 0;
    int16_t currentMagnitudeCentiNm_ = 0;
    int8_t preventiveMagnitudeStep_ = 1;
    bool outputActive_ = false;
    int8_t injectionSign_ = 0;
    uint8_t armingFrames_ = 0;
    bool epasSeen_ = false;
    uint32_t lastEpasAtMs_ = 0;
    uint32_t phaseStartedAtMs_ = 0;
    uint32_t phaseDurationMs_ = 0;
    uint32_t rngState_ = 0;
    bool correctiveActive_ = false;
    uint8_t correctiveAttempt_ = 0;
    uint32_t correctiveBurstFrame_ = 0;
    uint32_t correctiveBurstFrameTarget_ = 0;
    int8_t correctiveSweepSign_ = 0;
    uint16_t correctiveSweepFrame_ = 0;
    uint16_t correctiveSweepFrameTarget_ = 0;
    int16_t correctiveSweepPeakCentiNm_ = 0;
    bool acknowledgementStarted_ = false;
    uint32_t acknowledgementStartedAtMs_ = 0;
    uint32_t hosEscalationCount_ = 0;
    uint32_t acknowledgementCount_ = 0;
    uint32_t lastAcknowledgementLatencyMs_ = 0;
    uint32_t maxAcknowledgementLatencyMs_ = 0;
    bool faultRecoveryActive_ = false;
    uint32_t faultRecoveryStartedAtMs_ = 0;
    uint8_t normalHosConsecutive_ = 0;
    bool stabilityVerifyActive_ = false;
    bool h2Tracking_ = false;
    uint32_t h2StartedAtMs_ = 0;
    static constexpr uint8_t kCorrectiveTransitionFrames = 10U;
    bool correctiveTransitionActive_ = false;
    uint8_t correctiveTransitionFrame_ = 0;
    int8_t correctiveTransitionFromSign_ = 0;
    int8_t correctiveTransitionToSign_ = 0;
};
