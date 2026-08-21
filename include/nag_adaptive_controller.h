#pragma once

#include <cstdint>

#include "nag_das_feedback.h"

struct NagAdaptiveConfig
{
    int16_t preventiveNegativeMinCentiNm = 15;
    int16_t preventiveNegativeMaxCentiNm = 18;
    int16_t preventivePositiveMinCentiNm = 15;
    int16_t preventivePositiveMaxCentiNm = 18;
    int16_t correctiveNegativeMinCentiNm = 150;
    int16_t correctiveNegativeMaxCentiNm = 180;
    int16_t correctivePositiveMinCentiNm = 150;
    int16_t correctivePositiveMaxCentiNm = 180;
    int16_t torqueDeadbandCentiNm = 5;
    uint32_t activityMinMs = 800;
    uint32_t activityMaxMs = 1400;
    uint32_t releaseMinMs = 200;
    uint32_t releaseMaxMs = 400;
    uint32_t restMinMs = 1500;
    uint32_t restMaxMs = 2500;
    uint32_t dasFreshTimeoutMs = 500;
};

struct NagAdaptiveDecision
{
    bool shouldSend = false;
    int16_t targetTorqueCentiNm = 0;
    int8_t injectionSign = 0;
    bool corrective = false;
    uint8_t attempt = 0;
    uint8_t burstFrame = 0;
};

struct NagAdaptiveSnapshot
{
    uint8_t phase = 0;
    uint8_t blockReason = 0;
    uint8_t directionSource = 0;
    bool dasSeen = false;
    bool dasFresh = false;
    uint8_t dasHos = 15;
    uint32_t dasAgeMs = 0xFFFFFFFFu;
    int16_t observedTorqueCentiNm = 0;
    int16_t targetTorqueCentiNm = 0;
    int8_t injectionSign = 0;
    bool outputActive = false;
    int16_t lastSuccessfullyTransmittedTorqueCentiNm = 0;
    int8_t candidateInjectionSign = 0;
    uint8_t correctiveAttempt = 0;
    uint8_t correctiveBurstFrame = 0;
    uint8_t correctiveBurstFrameTarget = 0;
    uint32_t phaseRemainingMs = 0;
    uint32_t hosEscalationCount = 0;
    uint32_t acknowledgementCount = 0;
    uint32_t acknowledgementTimeoutCount = 0;
    uint32_t lastAcknowledgementLatencyMs = 0;
    uint32_t maxAcknowledgementLatencyMs = 0;
};

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
        BLOCK_ACK_TIMEOUT = 9,
    };

    enum DirectionSource : uint8_t
    {
        DIRECTION_NONE = 0,
        DIRECTION_TORQUE = 1,
        DIRECTION_ANGLE = 2,
        DIRECTION_HOLD = 3,
    };

    static NagAdaptiveConfig normalizeConfig(NagAdaptiveConfig value)
    {
        normalizeI16Range(value.preventiveNegativeMinCentiNm,
                          value.preventiveNegativeMaxCentiNm, 10, 50);
        normalizeI16Range(value.preventivePositiveMinCentiNm,
                          value.preventivePositiveMaxCentiNm, 10, 50);
        normalizeI16Range(value.correctiveNegativeMinCentiNm,
                          value.correctiveNegativeMaxCentiNm, 50, 180);
        normalizeI16Range(value.correctivePositiveMinCentiNm,
                          value.correctivePositiveMaxCentiNm, 50, 180);
        value.torqueDeadbandCentiNm = clampI16(value.torqueDeadbandCentiNm, 0, 50);
        normalizeU32Range(value.activityMinMs, value.activityMaxMs, 400, 3000);
        normalizeU32Range(value.releaseMinMs, value.releaseMaxMs, 100, 1000);
        normalizeU32Range(value.restMinMs, value.restMaxMs, 500, 5000);
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
        candidateSign_ = 0;
        armingFrames_ = 0;
        correctiveActive_ = false;
        acknowledgementStarted_ = false;
    }

    bool observeDas(const CanFrame &frame, uint32_t nowMs)
    {
        applyReset(nowMs);
        const bool continuesNormalRecovery = faultRecoveryActive_ && das_.seen() &&
                                             das_.raw() <= 1 &&
                                             das_.fresh(nowMs, config_.dasFreshTimeoutMs);
        const bool accepted = das_.observe(frame, nowMs);
        if (!accepted && phase_ == PHASE_FAULT_HOLD)
            faultRecoveryActive_ = false;
        if (frame.id != NagDasFeedbackTracker::kDasCanId || frame.dlc < 8)
            return false;

        const uint8_t hos = das_.raw();
        if (hos >= 8)
        {
            enterFault(BLOCK_DAS_STATE);
            return accepted;
        }
        if (!enabled_)
            return accepted;

        if (phase_ == PHASE_FAULT_HOLD)
        {
            if (accepted && hos <= 1)
            {
                if (!continuesNormalRecovery)
                {
                    faultRecoveryActive_ = true;
                    faultRecoveryStartedAtMs_ = nowMs;
                }
                if (static_cast<uint32_t>(nowMs - faultRecoveryStartedAtMs_) >= 2000U)
                {
                    faultRecoveryActive_ = false;
                    beginRest(nowMs, rngState_);
                }
            }
            else
            {
                faultRecoveryActive_ = false;
            }
            return accepted;
        }

        if (hos >= 2)
        {
            if (!correctiveActive_)
            {
                correctiveActive_ = true;
                hosEscalationCount_++;
                beginCorrective(nowMs, 1);
            }
            else if (phase_ != PHASE_CORRECTIVE && phase_ != PHASE_VERIFY)
            {
                beginCorrective(nowMs, correctiveAttempt_ == 0 ? 1 : correctiveAttempt_);
            }
            return accepted;
        }

        if (correctiveActive_)
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
            beginRelease(nowMs);
        }
        else if (phase_ == PHASE_WAIT_DAS)
        {
            beginArming();
        }
        else if (hos == 1 && phase_ == PHASE_MAINTENANCE)
        {
            beginRelease(nowMs);
        }
        return accepted;
    }

    NagAdaptiveDecision observeEpas(uint32_t nowMs,
                                    int16_t steeringAngleDeciDeg,
                                    int16_t observedTorqueCentiNm,
                                    uint32_t entropy)
    {
        applyReset(nowMs);
        observedTorqueCentiNm_ = observedTorqueCentiNm;

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
            candidateSign_ = 0;
            candidateStartedAtMs_ = 0;
            if (!correctiveActive_)
                beginArming();
        }
        if (phase_ == PHASE_WAIT_DAS)
            beginArming();
        epasSeen_ = true;
        lastEpasAtMs_ = nowMs;
        updateDirection(nowMs, steeringAngleDeciDeg, observedTorqueCentiNm);

        if (armingFrames_ < 3)
            armingFrames_++;
        if (armingFrames_ < 3)
            return blockedDecision(BLOCK_ARMING);

        if (phase_ == PHASE_ARMING)
        {
            if (das_.raw() >= 2 && das_.raw() <= 7)
                beginCorrective(nowMs, correctiveAttempt_ == 0 ? 1 : correctiveAttempt_);
            else
                beginMaintenance(nowMs, entropy);
        }

        if (phase_ == PHASE_MAINTENANCE && das_.raw() == 1)
            beginRelease(nowMs);
        if (phase_ == PHASE_MAINTENANCE && phaseExpired(nowMs))
            beginRelease(nowMs);

        if (phase_ == PHASE_REST)
        {
            if (!phaseExpired(nowMs))
                return blockedDecision(BLOCK_REST);
            beginMaintenance(nowMs, entropy);
        }

        if (phase_ == PHASE_VERIFY)
        {
            if (!phaseExpired(nowMs))
                return blockedDecision(BLOCK_VERIFY);
            if (correctiveAttempt_ == 1 && das_.raw() >= 2 && das_.raw() <= 7)
                beginCorrective(nowMs, 2);
            else if (correctiveAttempt_ >= 2)
            {
                acknowledgementTimeoutCount_++;
                enterFault(BLOCK_ACK_TIMEOUT);
                return blockedDecision(BLOCK_ACK_TIMEOUT);
            }
            else
                beginRelease(nowMs);
        }

        if (phase_ == PHASE_RELEASE)
            return releaseDecision(nowMs, entropy);

        if (injectionSign_ == 0)
            return blockedDecision(BLOCK_NO_DIRECTION);

        if (phase_ == PHASE_CORRECTIVE)
        {
            prepareCorrective(entropy);
            walkMagnitude(5, true, entropy);
            return sendDecision(true);
        }

        if (phase_ == PHASE_MAINTENANCE)
        {
            preparePreventive(entropy);
            walkMagnitude(1, false, entropy);
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

        if (phase_ != PHASE_CORRECTIVE ||
            !decision.corrective || decision.attempt != correctiveAttempt_ ||
            decision.burstFrame != correctiveBurstFrame_)
            return;

        if (!acknowledgementStarted_)
        {
            acknowledgementStarted_ = true;
            acknowledgementStartedAtMs_ = nowMs;
        }
        correctiveBurstFrame_++;
        if (correctiveBurstFrame_ >= correctiveBurstFrameTarget_)
        {
            phase_ = PHASE_VERIFY;
            blockReason_ = BLOCK_VERIFY;
            phaseStartedAtMs_ = nowMs;
            phaseDurationMs_ = correctiveAttempt_ == 1 ? 500U : 1000U;
            targetTorqueCentiNm_ = 0;
        }
    }

    NagAdaptiveSnapshot snapshot(uint32_t nowMs) const
    {
        NagAdaptiveSnapshot value;
        value.phase = phase_;
        value.blockReason = blockReason_;
        value.directionSource = directionSource_;
        value.dasSeen = das_.seen();
        value.dasFresh = das_.fresh(nowMs, config_.dasFreshTimeoutMs);
        value.dasHos = das_.raw();
        value.dasAgeMs = das_.ageMs(nowMs);
        value.observedTorqueCentiNm = observedTorqueCentiNm_;
        value.targetTorqueCentiNm = targetTorqueCentiNm_;
        value.injectionSign = outputActive_ && lastSuccessfullyTransmittedTorqueCentiNm_ != 0
                                  ? (lastSuccessfullyTransmittedTorqueCentiNm_ < 0 ? -1 : 1)
                                  : injectionSign_;
        value.outputActive = outputActive_;
        value.lastSuccessfullyTransmittedTorqueCentiNm =
            lastSuccessfullyTransmittedTorqueCentiNm_;
        value.candidateInjectionSign = injectionSign_;
        value.correctiveAttempt = correctiveAttempt_;
        value.correctiveBurstFrame = correctiveBurstFrame_;
        value.correctiveBurstFrameTarget = correctiveBurstFrameTarget_;
        value.phaseRemainingMs = phaseRemainingMs(nowMs);
        value.hosEscalationCount = hosEscalationCount_;
        value.acknowledgementCount = acknowledgementCount_;
        value.acknowledgementTimeoutCount = acknowledgementTimeoutCount_;
        value.lastAcknowledgementLatencyMs = lastAcknowledgementLatencyMs_;
        value.maxAcknowledgementLatencyMs = maxAcknowledgementLatencyMs_;
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
        injectionSign_ = 0;
        candidateSign_ = 0;
        candidateStartedAtMs_ = 0;
        armingFrames_ = 0;
        epasSeen_ = false;
        lastEpasAtMs_ = nowMs;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = 0;
        releaseStartTorqueCentiNm_ = 0;
        lastSuccessfullyTransmittedTorqueCentiNm_ = 0;
        outputActive_ = false;
        currentMagnitudeCentiNm_ = 0;
        correctiveActive_ = false;
        correctiveAttempt_ = 0;
        correctiveBurstFrame_ = 0;
        correctiveBurstFrameTarget_ = 0;
        acknowledgementStarted_ = false;
        rngState_ = 0;
        hosEscalationCount_ = 0;
        acknowledgementCount_ = 0;
        acknowledgementTimeoutCount_ = 0;
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
        faultRecoveryActive_ = false;
        outputActive_ = false;
    }

    void beginArming()
    {
        phase_ = PHASE_ARMING;
        blockReason_ = BLOCK_ARMING;
        targetTorqueCentiNm_ = 0;
        armingFrames_ = 0;
        candidateSign_ = 0;
        candidateStartedAtMs_ = 0;
    }

    void beginMaintenance(uint32_t nowMs, uint32_t entropy)
    {
        phase_ = PHASE_MAINTENANCE;
        blockReason_ = BLOCK_NONE;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = triangularDuration(config_.activityMinMs, config_.activityMaxMs, entropy);
        currentMagnitudeCentiNm_ = 0;
        preparePreventive(entropy);
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
        phase_ = PHASE_REST;
        blockReason_ = BLOCK_REST;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = triangularDuration(config_.restMinMs, config_.restMaxMs, entropy);
        targetTorqueCentiNm_ = 0;
        currentMagnitudeCentiNm_ = 0;
        outputActive_ = false;
    }

    void beginCorrective(uint32_t nowMs, uint8_t attempt)
    {
        phase_ = PHASE_CORRECTIVE;
        blockReason_ = BLOCK_NONE;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = 0;
        correctiveAttempt_ = attempt;
        correctiveBurstFrame_ = 0;
        correctiveBurstFrameTarget_ = 0;
        currentMagnitudeCentiNm_ = 0;
        targetTorqueCentiNm_ = 0;
    }

    void enterFault(BlockReason reason)
    {
        phase_ = PHASE_FAULT_HOLD;
        blockReason_ = reason;
        targetTorqueCentiNm_ = 0;
        correctiveActive_ = false;
        acknowledgementStarted_ = false;
        faultRecoveryActive_ = false;
        outputActive_ = false;
    }

    void updateDirection(uint32_t nowMs, int16_t angleDeciDeg, int16_t torqueCentiNm)
    {
        int8_t desiredSign = 0;
        if (torqueCentiNm > config_.torqueDeadbandCentiNm)
            desiredSign = -1;
        else if (torqueCentiNm < -config_.torqueDeadbandCentiNm)
            desiredSign = 1;

        if (injectionSign_ == 0)
        {
            if (desiredSign != 0)
            {
                injectionSign_ = desiredSign;
                directionSource_ = DIRECTION_TORQUE;
            }
            else if (angleDeciDeg > 10)
            {
                injectionSign_ = -1;
                directionSource_ = DIRECTION_ANGLE;
            }
            else if (angleDeciDeg < -10)
            {
                injectionSign_ = 1;
                directionSource_ = DIRECTION_ANGLE;
            }
            else
            {
                directionSource_ = DIRECTION_NONE;
            }
            return;
        }

        if (desiredSign == 0)
        {
            candidateSign_ = 0;
            directionSource_ = DIRECTION_HOLD;
        }
        else if (desiredSign == injectionSign_)
        {
            candidateSign_ = 0;
            directionSource_ = DIRECTION_TORQUE;
        }
        else if (candidateSign_ != desiredSign)
        {
            candidateSign_ = desiredSign;
            candidateStartedAtMs_ = nowMs;
            directionSource_ = DIRECTION_HOLD;
        }
        else if (static_cast<uint32_t>(nowMs - candidateStartedAtMs_) >= 100U)
        {
            injectionSign_ = desiredSign;
            candidateSign_ = 0;
            directionSource_ = DIRECTION_TORQUE;
        }
        else
        {
            directionSource_ = DIRECTION_HOLD;
        }
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
        if (injectionSign_ == 0 || currentMagnitudeCentiNm_ != 0)
            return;
        int16_t minValue;
        int16_t maxValue;
        preventiveRange(minValue, maxValue);
        currentMagnitudeCentiNm_ = triangularMagnitude(minValue, maxValue, entropy);
    }

    void prepareCorrective(uint32_t entropy)
    {
        if (correctiveBurstFrameTarget_ == 0)
            correctiveBurstFrameTarget_ = static_cast<uint8_t>(3U + nextRandom(entropy) % 3U);
        if (injectionSign_ == 0 || currentMagnitudeCentiNm_ != 0)
            return;
        int16_t minValue;
        int16_t maxValue;
        correctiveRange(minValue, maxValue);
        currentMagnitudeCentiNm_ = triangularMagnitude(minValue, maxValue, entropy);
    }

    void walkMagnitude(int16_t maxStep, bool corrective, uint32_t entropy)
    {
        int16_t minValue;
        int16_t maxValue;
        if (corrective)
            correctiveRange(minValue, maxValue);
        else
            preventiveRange(minValue, maxValue);
        const uint32_t width = static_cast<uint32_t>(maxStep * 2 + 1);
        const int16_t step = static_cast<int16_t>(nextRandom(entropy) % width) - maxStep;
        currentMagnitudeCentiNm_ = clampI16(
            static_cast<int32_t>(currentMagnitudeCentiNm_) + step, minValue, maxValue);
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

    void correctiveRange(int16_t &minValue, int16_t &maxValue) const
    {
        if (injectionSign_ < 0)
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

    NagAdaptiveDecision sendDecision(bool corrective)
    {
        targetTorqueCentiNm_ = clampI16(
            static_cast<int32_t>(injectionSign_) * currentMagnitudeCentiNm_, -180, 180);
        NagAdaptiveDecision decision;
        decision.shouldSend = true;
        decision.targetTorqueCentiNm = targetTorqueCentiNm_;
        decision.injectionSign = injectionSign_;
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
        decision.injectionSign = injectionSign_;
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
    int16_t observedTorqueCentiNm_ = 0;
    int16_t targetTorqueCentiNm_ = 0;
    int16_t releaseStartTorqueCentiNm_ = 0;
    int16_t lastSuccessfullyTransmittedTorqueCentiNm_ = 0;
    int16_t currentMagnitudeCentiNm_ = 0;
    bool outputActive_ = false;
    int8_t injectionSign_ = 0;
    int8_t candidateSign_ = 0;
    uint8_t armingFrames_ = 0;
    bool epasSeen_ = false;
    uint32_t lastEpasAtMs_ = 0;
    uint32_t candidateStartedAtMs_ = 0;
    uint32_t phaseStartedAtMs_ = 0;
    uint32_t phaseDurationMs_ = 0;
    uint32_t rngState_ = 0;
    bool correctiveActive_ = false;
    uint8_t correctiveAttempt_ = 0;
    uint8_t correctiveBurstFrame_ = 0;
    uint8_t correctiveBurstFrameTarget_ = 0;
    bool acknowledgementStarted_ = false;
    uint32_t acknowledgementStartedAtMs_ = 0;
    uint32_t hosEscalationCount_ = 0;
    uint32_t acknowledgementCount_ = 0;
    uint32_t acknowledgementTimeoutCount_ = 0;
    uint32_t lastAcknowledgementLatencyMs_ = 0;
    uint32_t maxAcknowledgementLatencyMs_ = 0;
    bool faultRecoveryActive_ = false;
    uint32_t faultRecoveryStartedAtMs_ = 0;
};
