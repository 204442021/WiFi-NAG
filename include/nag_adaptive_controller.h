#pragma once

#include <cstdint>

#include "shared_types.h"

struct NagAdaptiveConfig
{
    // Retained for settings compatibility. Hands-On ranges select the actual magnitude.
    int16_t torqueMagnitudeCentiNm = 180;
    // Direction-noise threshold; it never creates a transmit dead zone.
    int16_t torqueDeadbandCentiNm = 5;
    // Legacy persisted fields. V4.2 does not use them as send gates.
    int16_t angleLimitDeciDeg = 500;
    uint32_t sendWindowMs = 10000;
    uint32_t pauseMinMs = 1000;
    uint32_t pauseMaxMs = 3000;
};

struct NagAdaptiveDecision
{
    bool shouldSend = false;
    int16_t targetTorqueCentiNm = 0;
    int8_t injectionSign = 0;
    bool directionChanged = false;
};

class NagAdaptiveController
{
public:
    enum Phase : uint8_t
    {
        PHASE_DISABLED = 0,
        PHASE_ARMING = 1,
        PHASE_SEND = 2,
        PHASE_PAUSE = 3, // Legacy telemetry value; V4.2 never enters it.
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

    enum DirectionSource : uint8_t
    {
        DIRECTION_DEFAULT = 0,
        DIRECTION_ANGLE = 1,
        DIRECTION_TORQUE = 2,
        DIRECTION_HOLD = 3,
    };

    static constexpr uint8_t kArmingFrameCount = 3;
    static constexpr uint32_t kDirectionStableMs = 100;
    static constexpr int16_t kAngleDirectionNoiseDeciDeg = 10;
    static constexpr int16_t kAngleHysteresisDeciDeg = 50; // API compatibility

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
        enabled_ = false;
        armingFrames_ = 0;
        injectionSign_ = 0;
        candidateSign_ = 0;
        candidateStartedMs_ = 0;
        phase_ = PHASE_DISABLED;
        blockReason_ = BLOCK_DISABLED;
        directionSource_ = DIRECTION_DEFAULT;
        targetTorqueCentiNm_ = 0;
    }

    NagAdaptiveDecision observe(uint32_t now,
                                int16_t steeringAngleDeciDeg,
                                int16_t observedTorqueCentiNm,
                                uint32_t entropy)
    {
        (void)entropy;
        syncRequestedState(now);
        if (!enabled_)
            beginArming();

        lastAngleDeciDeg_ = steeringAngleDeciDeg;
        lastObservedTorqueCentiNm_ = observedTorqueCentiNm;

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
            phase_ = PHASE_SEND;
        }

        const int8_t desiredSign = torqueDirectionSign(observedTorqueCentiNm);
        bool changed = false;
        if (injectionSign_ == 0)
        {
            if (desiredSign != 0)
            {
                injectionSign_ = desiredSign;
                directionSource_ = DIRECTION_TORQUE;
            }
            else if (steeringAngleDeciDeg > kAngleDirectionNoiseDeciDeg)
            {
                injectionSign_ = -1;
                directionSource_ = DIRECTION_ANGLE;
            }
            else if (steeringAngleDeciDeg < -kAngleDirectionNoiseDeciDeg)
            {
                injectionSign_ = 1;
                directionSource_ = DIRECTION_ANGLE;
            }
            else
            {
                injectionSign_ = 1;
                directionSource_ = DIRECTION_DEFAULT;
            }
        }
        else if (desiredSign == 0)
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
            candidateStartedMs_ = now;
            directionSource_ = DIRECTION_HOLD;
        }
        else if (static_cast<uint32_t>(now - candidateStartedMs_) >= kDirectionStableMs)
        {
            injectionSign_ = desiredSign;
            candidateSign_ = 0;
            directionSource_ = DIRECTION_TORQUE;
            changed = true;
        }
        else
        {
            directionSource_ = DIRECTION_HOLD;
        }

        blockReason_ = BLOCK_NONE;
        const int16_t magnitude = config().torqueMagnitudeCentiNm;
        const int16_t target = static_cast<int16_t>(injectionSign_ * magnitude);
        targetTorqueCentiNm_ = target;
        return {true, target, static_cast<int8_t>(injectionSign_), changed};
    }

    uint32_t phaseRemainingMs(uint32_t now) const
    {
        (void)now;
        return 0;
    }

    Phase phaseValue() const { return static_cast<Phase>(static_cast<uint8_t>(phase_)); }
    BlockReason blockReasonValue() const { return static_cast<BlockReason>(static_cast<uint8_t>(blockReason_)); }
    DirectionSource directionSourceValue() const { return static_cast<DirectionSource>(static_cast<uint8_t>(directionSource_)); }
    int8_t injectionSign() const { return static_cast<int8_t>(injectionSign_); }
    int16_t lastAngleDeciDeg() const { return static_cast<int16_t>(lastAngleDeciDeg_); }
    int16_t lastObservedTorqueCentiNm() const { return static_cast<int16_t>(lastObservedTorqueCentiNm_); }
    int16_t targetTorqueCentiNm() const { return static_cast<int16_t>(targetTorqueCentiNm_); }
    uint32_t currentPauseMs() const { return 0; }
    uint32_t angleBlockEventCount() const { return 0; }
    uint32_t angleBlockedFrameCount() const { return 0; }
    uint32_t torqueDeadbandSkipCount() const { return 0; }
    uint32_t pauseSkipCount() const { return 0; }

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

    int8_t torqueDirectionSign(int16_t observedTorqueCentiNm) const
    {
        const int16_t noise = config().torqueDeadbandCentiNm;
        if (observedTorqueCentiNm > noise)
            return -1;
        if (observedTorqueCentiNm < -noise)
            return 1;
        return 0;
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
        disable(now);
        beginArming();
    }

    void beginArming()
    {
        enabled_ = true;
        armingFrames_ = 0;
        injectionSign_ = 0;
        candidateSign_ = 0;
        phase_ = PHASE_ARMING;
        blockReason_ = BLOCK_ARMING;
        directionSource_ = DIRECTION_DEFAULT;
        targetTorqueCentiNm_ = 0;
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
    Shared<uint8_t> directionSource_{DIRECTION_DEFAULT};
    Shared<int16_t> lastAngleDeciDeg_{0};
    Shared<int16_t> lastObservedTorqueCentiNm_{0};
    Shared<int16_t> targetTorqueCentiNm_{0};

    bool enabled_ = false;
    uint8_t armingFrames_ = 0;
    int8_t injectionSign_ = 0;
    int8_t candidateSign_ = 0;
    uint32_t candidateStartedMs_ = 0;
    uint32_t appliedConfigGeneration_ = 0;
    uint32_t appliedResetGeneration_ = 0;
};
