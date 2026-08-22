#pragma once

#include <cstdint>

#include "nag_das_feedback.h"

struct NagAdaptiveConfig
{
    bool maintenanceEnabled = true;
    int16_t preventiveNegativeMinCentiNm = 170;
    int16_t preventiveNegativeMaxCentiNm = 180;
    int16_t preventivePositiveMinCentiNm = 170;
    int16_t preventivePositiveMaxCentiNm = 180;
    int16_t correctiveNegativeMinCentiNm = 180;
    int16_t correctiveNegativeMaxCentiNm = 200;
    int16_t correctivePositiveMinCentiNm = 180;
    int16_t correctivePositiveMaxCentiNm = 200;
    uint32_t activityMinMs = 2000;
    uint32_t activityMaxMs = 3000;
    uint32_t releaseMinMs = 200;
    uint32_t releaseMaxMs = 400;
    uint32_t restMinMs = 4000;
    uint32_t restMaxMs = 5000;
    uint32_t correctiveSendMinMs = 3000;
    uint32_t correctiveSendMaxMs = 3000;
    uint32_t correctivePauseMinMs = 1000;
    uint32_t correctivePauseMaxMs = 2000;
    uint32_t correctiveFrameIntervalMs = 1;
    uint32_t dasFreshTimeoutMs = 750;
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
    bool dasValid = false;
    bool dasFresh = false;
    uint8_t dasHos = 15;
    uint32_t dasAgeMs = 0xFFFFFFFFu;
    uint32_t lastDasFrameMs = 0;
    uint32_t dasFreshnessLimitMs = 0;
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
        BLOCK_DIRECTION_CHANGE = 10,
        BLOCK_MAINTENANCE_DISABLED = 11,
        BLOCK_CORRECTIVE_INTERVAL = 12,
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
                          value.preventiveNegativeMaxCentiNm, 150, 180);
        normalizeI16Range(value.preventivePositiveMinCentiNm,
                          value.preventivePositiveMaxCentiNm, 150, 180);
        normalizeI16Range(value.correctiveNegativeMinCentiNm,
                          value.correctiveNegativeMaxCentiNm, 180, 200);
        normalizeI16Range(value.correctivePositiveMinCentiNm,
                          value.correctivePositiveMaxCentiNm, 180, 200);
        normalizeU32Range(value.activityMinMs, value.activityMaxMs, 100, UINT32_MAX);
        normalizeU32Range(value.releaseMinMs, value.releaseMaxMs, 100, 1000);
        normalizeU32Range(value.restMinMs, value.restMaxMs, 100, UINT32_MAX);
        normalizeU32Range(value.correctiveSendMinMs, value.correctiveSendMaxMs,
                          100, UINT32_MAX);
        normalizeU32Range(value.correctivePauseMinMs, value.correctivePauseMaxMs,
                          100, UINT32_MAX);
        value.correctiveFrameIntervalMs =
            clampU32(value.correctiveFrameIntervalMs, 1, UINT32_MAX);
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
            if (!correctiveActive_)
            {
                correctiveActive_ = true;
                hosEscalationCount_++;
                beginCorrective(nowMs);
            }
            else if (phase_ != PHASE_CORRECTIVE && phase_ != PHASE_VERIFY)
            {
                beginCorrective(nowMs);
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
            beginPostCorrectionRest(nowMs);
        }
        else if (phase_ == PHASE_WAIT_DAS)
        {
            beginArming();
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
            if (das_.raw() >= 3 && das_.raw() <= 5)
                beginCorrective(nowMs);
            else
                beginMaintenance(nowMs, entropy);
        }

        if (phase_ == PHASE_MONITOR_ONLY)
            return blockedDecision(BLOCK_MAINTENANCE_DISABLED);

        if (phase_ == PHASE_CORRECTIVE && phaseExpired(nowMs))
        {
            beginCorrectivePause(nowMs);
            return blockedDecision(BLOCK_VERIFY);
        }

        if (phase_ == PHASE_VERIFY)
        {
            if (!phaseExpired(nowMs))
                return blockedDecision(BLOCK_VERIFY);
            beginCorrective(nowMs);
        }

        if (candidateSign_ != 0)
            return blockedDecision(BLOCK_DIRECTION_CHANGE);

        if (phase_ == PHASE_MAINTENANCE && phaseExpired(nowMs))
            beginRest(nowMs, entropy);

        if (phase_ == PHASE_REST)
        {
            if (!phaseExpired(nowMs))
                return blockedDecision(BLOCK_REST);
            beginMaintenance(nowMs, entropy);
        }

        if (phase_ == PHASE_RELEASE)
            return releaseDecision(nowMs, entropy);

        if (injectionSign_ == 0)
            return blockedDecision(BLOCK_NO_DIRECTION);

        if (phase_ == PHASE_CORRECTIVE)
        {
            if (correctiveSendSeen_ &&
                static_cast<uint32_t>(nowMs - correctiveLastSendAtMs_) <
                    config_.correctiveFrameIntervalMs)
                return blockedDecision(BLOCK_CORRECTIVE_INTERVAL);
            prepareCorrective(entropy);
            return sendDecision(true);
        }

        if (phase_ == PHASE_MAINTENANCE)
        {
            selectRandomMagnitude(false, entropy);
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
            return;

        if (!acknowledgementStarted_)
        {
            acknowledgementStarted_ = true;
            acknowledgementStartedAtMs_ = nowMs;
        }
        if (correctiveBurstFrame_ < 0xFFU)
            correctiveBurstFrame_++;
        correctiveSendSeen_ = true;
        correctiveLastSendAtMs_ = nowMs;
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
        correctiveSendSeen_ = false;
        correctiveLastSendAtMs_ = 0;
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
        if (!config_.maintenanceEnabled)
        {
            beginMonitorOnly();
            return;
        }
        phase_ = PHASE_MAINTENANCE;
        blockReason_ = BLOCK_NONE;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = triangularDuration(config_.activityMinMs, config_.activityMaxMs, entropy);
        currentMagnitudeCentiNm_ = 0;
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
        currentMagnitudeCentiNm_ = 0;
        outputActive_ = false;
        correctiveAttempt_ = 0;
        correctiveBurstFrame_ = 0;
        correctiveBurstFrameTarget_ = 0;
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

    void beginPostCorrectionRest(uint32_t nowMs)
    {
        if (!config_.maintenanceEnabled)
        {
            beginMonitorOnly();
            return;
        }
        beginRest(nowMs, rngState_);
    }

    void beginCorrective(uint32_t nowMs)
    {
        phase_ = PHASE_CORRECTIVE;
        blockReason_ = BLOCK_NONE;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = triangularDuration(config_.correctiveSendMinMs,
                                              config_.correctiveSendMaxMs,
                                              rngState_);
        if (correctiveAttempt_ < 0xFFU)
            correctiveAttempt_++;
        correctiveBurstFrame_ = 0;
        correctiveBurstFrameTarget_ = 0;
        currentMagnitudeCentiNm_ = 0;
        targetTorqueCentiNm_ = 0;
        lastSuccessfullyTransmittedTorqueCentiNm_ = 0;
        outputActive_ = false;
        correctiveSendSeen_ = false;
        correctiveLastSendAtMs_ = 0;
    }

    void beginCorrectivePause(uint32_t nowMs)
    {
        phase_ = PHASE_VERIFY;
        blockReason_ = BLOCK_VERIFY;
        phaseStartedAtMs_ = nowMs;
        phaseDurationMs_ = triangularDuration(config_.correctivePauseMinMs,
                                              config_.correctivePauseMaxMs,
                                              rngState_);
        targetTorqueCentiNm_ = 0;
        currentMagnitudeCentiNm_ = 0;
        outputActive_ = false;
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
        if (torqueCentiNm > 0)
            desiredSign = -1;
        else if (torqueCentiNm < 0)
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
        targetTorqueCentiNm_ = clampI16(rounded, -200, 200);
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
        if (injectionSign_ == 0 || currentMagnitudeCentiNm_ != 0)
            return;
        int16_t minValue;
        int16_t maxValue;
        correctiveRange(minValue, maxValue);
        currentMagnitudeCentiNm_ = triangularMagnitude(minValue, maxValue, entropy);
    }

    void selectRandomMagnitude(bool corrective, uint32_t entropy)
    {
        int16_t minValue;
        int16_t maxValue;
        if (corrective)
            correctiveRange(minValue, maxValue);
        else
            preventiveRange(minValue, maxValue);
        currentMagnitudeCentiNm_ = triangularMagnitude(minValue, maxValue, entropy);
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
            static_cast<int32_t>(injectionSign_) * currentMagnitudeCentiNm_, -200, 200);
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
    bool correctiveSendSeen_ = false;
    uint32_t correctiveLastSendAtMs_ = 0;
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
