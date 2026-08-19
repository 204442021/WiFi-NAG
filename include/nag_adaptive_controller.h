#pragma once

#include <cstdint>

#include "shared_types.h"

struct NagAdaptiveConfig
{
    int16_t torqueMagnitudeCentiNm = 180;
    int16_t torqueDeadbandCentiNm = 5;
    int16_t angleLimitDeciDeg = 500;
    uint32_t sendWindowMs = 10000;
    uint32_t pauseMinMs = 1000;
    uint32_t pauseMaxMs = 3000;
};

struct NagAdaptiveDecision
{
    bool shouldSend = false;
    int16_t targetTorqueCentiNm = 0;
};

class NagAdaptiveController
{
public:
    enum Phase : uint8_t
    {
        PHASE_DISABLED = 0,
        PHASE_ARMING = 1,
        PHASE_SEND = 2,
        PHASE_PAUSE = 3,
    };

    enum BlockReason : uint8_t
    {
        BLOCK_NONE = 0,
        BLOCK_DISABLED = 1,
        BLOCK_ARMING = 2,
        BLOCK_PAUSE = 3,
        BLOCK_ANGLE = 4,
        BLOCK_TORQUE_DEADBAND = 5,
    };

    static constexpr uint8_t kArmingFrameCount = 3;
    static constexpr uint8_t kAngleResumeFrameCount = 3;
    static constexpr int16_t kAngleHysteresisDeciDeg = 50;

    static NagAdaptiveConfig normalizeConfig(NagAdaptiveConfig value)
    {
        value.torqueMagnitudeCentiNm = clampI16(value.torqueMagnitudeCentiNm, 10, 180);
        value.torqueDeadbandCentiNm = clampI16(value.torqueDeadbandCentiNm, 0, 50);
        value.angleLimitDeciDeg = clampI16(value.angleLimitDeciDeg, 100, 1800);
        value.sendWindowMs = clampU32(value.sendWindowMs, 1000, 60000);
        value.pauseMinMs = clampU32(value.pauseMinMs, 1000, 30000);
        value.pauseMaxMs = clampU32(value.pauseMaxMs, 1000, 30000);
        if (value.pauseMinMs > value.pauseMaxMs)
        {
            const uint32_t tmp = value.pauseMinMs;
            value.pauseMinMs = value.pauseMaxMs;
            value.pauseMaxMs = tmp;
        }
        return value;
    }

    void setConfig(const NagAdaptiveConfig &requested)
    {
        const NagAdaptiveConfig value = normalizeConfig(requested);
        torqueMagnitudeCentiNm_ = value.torqueMagnitudeCentiNm;
        torqueDeadbandCentiNm_ = value.torqueDeadbandCentiNm;
        angleLimitDeciDeg_ = value.angleLimitDeciDeg;
        sendWindowMs_ = value.sendWindowMs;
        pauseMinMs_ = value.pauseMinMs;
        pauseMaxMs_ = value.pauseMaxMs;
        configGeneration_ = static_cast<uint32_t>(configGeneration_) + 1U;
    }

    NagAdaptiveConfig config() const
    {
        NagAdaptiveConfig value;
        value.torqueMagnitudeCentiNm = static_cast<int16_t>(torqueMagnitudeCentiNm_);
        value.torqueDeadbandCentiNm = static_cast<int16_t>(torqueDeadbandCentiNm_);
        value.angleLimitDeciDeg = static_cast<int16_t>(angleLimitDeciDeg_);
        value.sendWindowMs = static_cast<uint32_t>(sendWindowMs_);
        value.pauseMinMs = static_cast<uint32_t>(pauseMinMs_);
        value.pauseMaxMs = static_cast<uint32_t>(pauseMaxMs_);
        return normalizeConfig(value);
    }

    void requestReset()
    {
        resetGeneration_ = static_cast<uint32_t>(resetGeneration_) + 1U;
    }

    void disable(uint32_t now)
    {
        (void)now;
        if (!enabled_ && phaseValue() == PHASE_DISABLED)
            return;
        enabled_ = false;
        angleBlocked_ = false;
        angleSafeFrames_ = 0;
        armingFrames_ = 0;
        phase_ = PHASE_DISABLED;
        blockReason_ = BLOCK_DISABLED;
        targetTorqueCentiNm_ = 0;
        phaseDeadlineMs_ = 0;
        phaseStartedMs_ = 0;
    }

    NagAdaptiveDecision observe(uint32_t now,
                                int16_t steeringAngleDeciDeg,
                                int16_t observedTorqueCentiNm,
                                uint32_t entropy)
    {
        syncRequestedState(now);
        if (!enabled_)
            beginArming(now);

        lastAngleDeciDeg_ = steeringAngleDeciDeg;
        lastObservedTorqueCentiNm_ = observedTorqueCentiNm;
        updateAngleGate(steeringAngleDeciDeg);

        if (phaseValue() == PHASE_ARMING)
        {
            if (armingFrames_ < kArmingFrameCount)
                armingFrames_++;
            if (armingFrames_ < kArmingFrameCount)
            {
                blockReason_ = BLOCK_ARMING;
                targetTorqueCentiNm_ = 0;
                return {};
            }
            beginSend(now);
        }
        else if (phaseValue() == PHASE_SEND && deadlineReached(now, phaseDeadlineValue()))
        {
            beginPause(now, entropy);
        }
        else if (phaseValue() == PHASE_PAUSE)
        {
            if (!deadlineReached(now, phaseDeadlineValue()))
            {
                pauseSkipCount_++;
                blockReason_ = BLOCK_PAUSE;
                targetTorqueCentiNm_ = 0;
                return {};
            }
            beginSend(now);
        }

        if (phaseValue() == PHASE_PAUSE)
        {
            pauseSkipCount_++;
            blockReason_ = BLOCK_PAUSE;
            targetTorqueCentiNm_ = 0;
            return {};
        }

        if (angleBlocked_)
        {
            angleBlockedFrameCount_++;
            blockReason_ = BLOCK_ANGLE;
            targetTorqueCentiNm_ = 0;
            return {};
        }

        const NagAdaptiveConfig value = config();
        int16_t target = 0;
        if (observedTorqueCentiNm > value.torqueDeadbandCentiNm)
            target = static_cast<int16_t>(-value.torqueMagnitudeCentiNm);
        else if (observedTorqueCentiNm < -value.torqueDeadbandCentiNm)
            target = value.torqueMagnitudeCentiNm;
        else
        {
            torqueDeadbandSkipCount_++;
            blockReason_ = BLOCK_TORQUE_DEADBAND;
            targetTorqueCentiNm_ = 0;
            return {};
        }

        blockReason_ = BLOCK_NONE;
        targetTorqueCentiNm_ = target;
        return {true, target};
    }

    uint32_t phaseRemainingMs(uint32_t now) const
    {
        const Phase current = phaseValue();
        if (current != PHASE_SEND && current != PHASE_PAUSE)
            return 0;
        const uint32_t deadline = phaseDeadlineValue();
        if (deadlineReached(now, deadline))
            return 0;
        return deadline - now;
    }

    Phase phaseValue() const
    {
        return static_cast<Phase>(static_cast<uint8_t>(phase_));
    }

    BlockReason blockReasonValue() const
    {
        return static_cast<BlockReason>(static_cast<uint8_t>(blockReason_));
    }

    int16_t lastAngleDeciDeg() const { return static_cast<int16_t>(lastAngleDeciDeg_); }
    int16_t lastObservedTorqueCentiNm() const { return static_cast<int16_t>(lastObservedTorqueCentiNm_); }
    int16_t targetTorqueCentiNm() const { return static_cast<int16_t>(targetTorqueCentiNm_); }
    uint32_t currentPauseMs() const { return static_cast<uint32_t>(currentPauseMs_); }
    uint32_t angleBlockEventCount() const { return static_cast<uint32_t>(angleBlockEventCount_); }
    uint32_t angleBlockedFrameCount() const { return static_cast<uint32_t>(angleBlockedFrameCount_); }
    uint32_t torqueDeadbandSkipCount() const { return static_cast<uint32_t>(torqueDeadbandSkipCount_); }
    uint32_t pauseSkipCount() const { return static_cast<uint32_t>(pauseSkipCount_); }

private:
    static int16_t clampI16(int16_t value, int16_t minValue, int16_t maxValue)
    {
        if (value < minValue)
            return minValue;
        if (value > maxValue)
            return maxValue;
        return value;
    }

    static uint32_t clampU32(uint32_t value, uint32_t minValue, uint32_t maxValue)
    {
        if (value < minValue)
            return minValue;
        if (value > maxValue)
            return maxValue;
        return value;
    }

    static uint32_t randomWord(uint32_t value)
    {
        uint32_t x = value + 0x9E3779B9u;
        x ^= x >> 16;
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        return x;
    }

    static bool deadlineReached(uint32_t now, uint32_t deadline)
    {
        return static_cast<int32_t>(now - deadline) >= 0;
    }

    static int32_t absolute(int16_t value)
    {
        const int32_t widened = value;
        return widened < 0 ? -widened : widened;
    }

    uint32_t phaseDeadlineValue() const
    {
        return static_cast<uint32_t>(phaseDeadlineMs_);
    }

    int16_t resumeAngleDeciDeg() const
    {
        const NagAdaptiveConfig value = config();
        const int16_t resume = static_cast<int16_t>(value.angleLimitDeciDeg - kAngleHysteresisDeciDeg);
        return resume > 0 ? resume : 0;
    }

    void syncRequestedState(uint32_t now)
    {
        const uint32_t configGeneration = static_cast<uint32_t>(configGeneration_);
        const uint32_t resetGeneration = static_cast<uint32_t>(resetGeneration_);
        if (configGeneration == appliedConfigGeneration_ &&
            resetGeneration == appliedResetGeneration_)
            return;

        appliedConfigGeneration_ = configGeneration;
        appliedResetGeneration_ = resetGeneration;
        enabled_ = false;
        beginArming(now);
    }

    void beginArming(uint32_t now)
    {
        enabled_ = true;
        angleBlocked_ = false;
        angleSafeFrames_ = 0;
        armingFrames_ = 0;
        phase_ = PHASE_ARMING;
        blockReason_ = BLOCK_ARMING;
        targetTorqueCentiNm_ = 0;
        phaseStartedMs_ = now;
        phaseDeadlineMs_ = 0;
        currentPauseMs_ = 0;
    }

    void beginSend(uint32_t now)
    {
        const NagAdaptiveConfig value = config();
        phase_ = PHASE_SEND;
        blockReason_ = BLOCK_NONE;
        phaseStartedMs_ = now;
        phaseDeadlineMs_ = now + value.sendWindowMs;
        currentPauseMs_ = 0;
    }

    void beginPause(uint32_t now, uint32_t entropy)
    {
        const NagAdaptiveConfig value = config();
        randomSequence_++;
        const uint32_t span = value.pauseMaxMs - value.pauseMinMs;
        const uint32_t word = randomWord(now ^ entropy ^ (randomSequence_ * 0x85EBCA6Bu));
        const uint32_t pauseMs = value.pauseMinMs + (span == 0 ? 0 : word % (span + 1U));
        currentPauseMs_ = pauseMs;
        phase_ = PHASE_PAUSE;
        blockReason_ = BLOCK_PAUSE;
        phaseStartedMs_ = now;
        phaseDeadlineMs_ = now + pauseMs;
    }

    void updateAngleGate(int16_t steeringAngleDeciDeg)
    {
        const NagAdaptiveConfig value = config();
        const int32_t magnitude = absolute(steeringAngleDeciDeg);
        if (magnitude >= value.angleLimitDeciDeg)
        {
            if (!angleBlocked_)
                angleBlockEventCount_++;
            angleBlocked_ = true;
            angleSafeFrames_ = 0;
            return;
        }

        if (!angleBlocked_)
            return;

        if (magnitude <= resumeAngleDeciDeg())
        {
            if (angleSafeFrames_ < kAngleResumeFrameCount)
                angleSafeFrames_++;
            if (angleSafeFrames_ >= kAngleResumeFrameCount)
            {
                angleBlocked_ = false;
                angleSafeFrames_ = 0;
            }
        }
        else
        {
            angleSafeFrames_ = 0;
        }
    }

    Shared<int16_t> torqueMagnitudeCentiNm_{180};
    Shared<int16_t> torqueDeadbandCentiNm_{5};
    Shared<int16_t> angleLimitDeciDeg_{500};
    Shared<uint32_t> sendWindowMs_{10000};
    Shared<uint32_t> pauseMinMs_{1000};
    Shared<uint32_t> pauseMaxMs_{3000};
    Shared<uint32_t> configGeneration_{0};
    Shared<uint32_t> resetGeneration_{0};

    Shared<uint8_t> phase_{PHASE_DISABLED};
    Shared<uint8_t> blockReason_{BLOCK_DISABLED};
    Shared<int16_t> lastAngleDeciDeg_{0};
    Shared<int16_t> lastObservedTorqueCentiNm_{0};
    Shared<int16_t> targetTorqueCentiNm_{0};
    Shared<uint32_t> phaseStartedMs_{0};
    Shared<uint32_t> phaseDeadlineMs_{0};
    Shared<uint32_t> currentPauseMs_{0};
    Shared<uint32_t> angleBlockEventCount_{0};
    Shared<uint32_t> angleBlockedFrameCount_{0};
    Shared<uint32_t> torqueDeadbandSkipCount_{0};
    Shared<uint32_t> pauseSkipCount_{0};

    bool enabled_ = false;
    bool angleBlocked_ = false;
    uint8_t angleSafeFrames_ = 0;
    uint8_t armingFrames_ = 0;
    uint32_t randomSequence_ = 0;
    uint32_t appliedConfigGeneration_ = 0;
    uint32_t appliedResetGeneration_ = 0;
};
