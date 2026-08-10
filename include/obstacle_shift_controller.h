#pragma once

#include <cstdint>
#include <cstring>

#include "ble/brake_state.h"
#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "shared_types.h"

enum ObstacleShiftState : uint8_t
{
    OBSTACLE_SHIFT_IDLE = 0,
    OBSTACLE_SHIFT_ACTIVE_P,
};

enum ObstacleShiftReason : uint8_t
{
    SHIFT_REASON_NONE = 0,
    SHIFT_REASON_ACTIVE,
    SHIFT_REASON_BRAKE_NOT_PRESSED,
    SHIFT_REASON_BRAKE_STALE,
    SHIFT_REASON_SESSION_CHANGED,
    SHIFT_REASON_LINK_UNAVAILABLE,
    SHIFT_REASON_CAPABILITY_MISSING,
    SHIFT_REASON_FEATURE_DISABLED,
    SHIFT_REASON_CAN_WRITE_DISABLED,
    SHIFT_REASON_118_STALE,
    SHIFT_REASON_GEAR_NOT_DR,
    SHIFT_REASON_MOVING,
    SHIFT_REASON_TX_FAILED,
};

struct ObstacleShiftRuntimeInputs
{
    bool featureEnabled = false;
    bool canWriteReady = false;
};

struct ObstacleShiftView
{
    ObstacleShiftState state = OBSTACLE_SHIFT_IDLE;
    ObstacleShiftReason reason = SHIFT_REASON_FEATURE_DISABLED;
    BrakeRealGear realGear = BRAKE_GEAR_UNKNOWN;
    uint32_t sessionGeneration = 0;
    uint32_t brakeStateAgeMs = UINT32_MAX;
    uint32_t real118AgeMs = UINT32_MAX;
    uint32_t real118RxCount = 0;
    uint32_t virtualParkTxCount = 0;
    uint32_t virtualParkTxFailCount = 0;
    uint32_t activationCount = 0;
    bool featureEnabled = false;
    bool linkReady = false;
    bool capabilitySupported = false;
    bool brakeStateFresh = false;
    bool brakePressed = false;
    bool releaseConfirmed = false;
    bool stationaryConfirmed = false;
    bool hasReal118 = false;
    bool virtualParkActive = false;
};

class ObstacleShiftController
{
public:
    static constexpr uint32_t kControlFreshMs = 500;
    static constexpr uint32_t kBrakeFreshMs = kControlFreshMs;
    static constexpr uint32_t kReal118FreshMs = 50;
    static constexpr uint32_t kOwnEchoWindowMs = 20;

    static BrakeRealGear parseRealGear(const CanFrame &frame)
    {
        if (!validReal118(frame))
            return BRAKE_GEAR_UNKNOWN;
        if (frame.data[2] == 0x32U)
            return BRAKE_GEAR_P;
        if (frame.data[2] == 0x55U)
            return BRAKE_GEAR_R;
        if (frame.data[2] == 0x95U)
            return BRAKE_GEAR_D;
        return BRAKE_GEAR_UNKNOWN;
    }

    static bool makeVirtualParkFrame(const CanFrame &realFrame, CanFrame &out)
    {
        if (!validReal118(realFrame))
            return false;

        out = realFrame;
        out.data[2] = 0x32U;
        out.data[6] = 0x00U;
        out.data[0] = checksum118(out);
        return true;
    }

    void tick(const BrakeStateView &brake,
              const ObstacleShiftRuntimeInputs &runtime,
              uint32_t nowMs)
    {
        evaluateState(brake, runtime, nowMs);
        publish(brake, runtime);
    }

    void observeFrame(const CanFrame &frame,
                      const BrakeStateView &brake,
                      const ObstacleShiftRuntimeInputs &runtime,
                      uint32_t nowMs,
                      CanDriver &driver,
                      bool allowStartFromFrame = true)
    {
        (void)allowStartFromFrame;

        if (isOwnEcho(frame, nowMs) || !validReal118(frame))
        {
            evaluateState(brake, runtime, nowMs);
            publish(brake, runtime);
            return;
        }

        hasReal118_ = true;
        lastReal118Ms_ = nowMs;
        real118RxCount_++;
        evaluateState(brake, runtime, nowMs);

        if (state_ != OBSTACLE_SHIFT_ACTIVE_P)
        {
            publish(brake, runtime);
            return;
        }

        CanFrame virtualPark;
        if (!makeVirtualParkFrame(frame, virtualPark))
        {
            publish(brake, runtime);
            return;
        }

        if (!driver.send(virtualPark))
        {
            virtualParkTxFailCount_++;
            reason_ = SHIFT_REASON_TX_FAILED;
            publish(brake, runtime);
            return;
        }

        virtualParkTxCount_++;
        lastVirtualPark_ = virtualPark;
        lastVirtualParkMs_ = nowMs;
        hasVirtualFingerprint_ = true;
        reason_ = SHIFT_REASON_ACTIVE;
        publish(brake, runtime);
    }

    ObstacleShiftView view(uint32_t nowMs) const
    {
        for (uint8_t attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t before = static_cast<uint32_t>(publishedVersion_);
            if (before & 1U)
                continue;

            ObstacleShiftView out;
            out.state = static_cast<ObstacleShiftState>(
                static_cast<uint8_t>(publishedState_));
            out.reason = static_cast<ObstacleShiftReason>(
                static_cast<uint8_t>(publishedReason_));
            out.realGear = static_cast<BrakeRealGear>(
                static_cast<uint8_t>(publishedRealGear_));
            out.sessionGeneration =
                static_cast<uint32_t>(publishedSessionGeneration_);
            const uint32_t brakeRx =
                static_cast<uint32_t>(publishedBrakeRxMs_);
            const uint32_t realRx =
                static_cast<uint32_t>(publishedReal118Ms_);
            out.real118RxCount =
                static_cast<uint32_t>(publishedReal118RxCount_);
            out.virtualParkTxCount =
                static_cast<uint32_t>(publishedTxCount_);
            out.virtualParkTxFailCount =
                static_cast<uint32_t>(publishedTxFailCount_);
            out.activationCount =
                static_cast<uint32_t>(publishedActivationCount_);
            out.featureEnabled = static_cast<bool>(publishedFeatureEnabled_);
            out.linkReady = static_cast<bool>(publishedLinkReady_);
            out.capabilitySupported =
                static_cast<bool>(publishedCapabilitySupported_);
            const bool hasBrake =
                static_cast<bool>(publishedHasBrakeState_);
            out.brakePressed = static_cast<bool>(publishedBrakePressed_);
            out.releaseConfirmed =
                static_cast<bool>(publishedReleaseConfirmed_);
            out.stationaryConfirmed =
                static_cast<bool>(publishedStationaryConfirmed_);
            out.hasReal118 = static_cast<bool>(publishedHasReal118_);
            out.virtualParkActive =
                static_cast<bool>(publishedVirtualParkActive_);

            const uint32_t after = static_cast<uint32_t>(publishedVersion_);
            if (before != after || (after & 1U))
                continue;

            if (hasBrake)
            {
                out.brakeStateAgeMs = nowMs - brakeRx;
                out.brakeStateFresh =
                    out.brakeStateAgeMs <= kControlFreshMs;
            }
            if (out.hasReal118)
                out.real118AgeMs = nowMs - realRx;
            return out;
        }
        return ObstacleShiftView{};
    }

    void reset()
    {
        state_ = OBSTACLE_SHIFT_IDLE;
        reason_ = SHIFT_REASON_FEATURE_DISABLED;
        haveSession_ = false;
        sessionGeneration_ = 0;
        hasReal118_ = false;
        lastReal118Ms_ = 0;
        lastVirtualParkMs_ = 0;
        hasVirtualFingerprint_ = false;
        real118RxCount_ = 0;
        virtualParkTxCount_ = 0;
        virtualParkTxFailCount_ = 0;
        activationCount_ = 0;
        lastVirtualPark_ = CanFrame{};
        publish(BrakeStateView{}, ObstacleShiftRuntimeInputs{});
    }

private:
    static uint8_t checksum118(const CanFrame &frame)
    {
        uint16_t sum = 0x19U;
        for (uint8_t index = 1; index < 8; ++index)
            sum = static_cast<uint16_t>(sum + frame.data[index]);
        return static_cast<uint8_t>(sum & 0xFFU);
    }

    static bool validReal118(const CanFrame &frame)
    {
        return frame.id == 0x118U && frame.dlc == 8U &&
               frame.data[0] == checksum118(frame);
    }

    static bool controlFresh(const BrakeStateView &brake, uint32_t nowMs)
    {
        return brake.hasState && nowMs - brake.lastRxMs <= kControlFreshMs;
    }

    static bool stationaryEligible(const BrakeStateData &brake)
    {
        return brake.speedFresh && brake.stationaryConfirmed;
    }

    static bool t2canDrEligible(const BrakeStateData &brake)
    {
        return brake.gearFresh &&
               (brake.realGear == BRAKE_GEAR_D ||
                brake.realGear == BRAKE_GEAR_R);
    }

    static bool physicalPressedEligible(const BrakeStateData &brake)
    {
        return brake.physicalKnown && brake.physicalFresh &&
               brake.physicalPressed;
    }

    ObstacleShiftReason blockingReason(
        const BrakeStateView &brake,
        const ObstacleShiftRuntimeInputs &runtime,
        uint32_t nowMs) const
    {
        if (!runtime.featureEnabled)
            return SHIFT_REASON_FEATURE_DISABLED;
        if (!brake.linkReady)
            return SHIFT_REASON_LINK_UNAVAILABLE;
        if (!brake.capabilitySupported)
            return SHIFT_REASON_CAPABILITY_MISSING;
        if (!controlFresh(brake, nowMs))
            return SHIFT_REASON_BRAKE_STALE;
        if (!physicalPressedEligible(brake.data))
            return SHIFT_REASON_BRAKE_NOT_PRESSED;
        if (!stationaryEligible(brake.data))
            return SHIFT_REASON_MOVING;
        if (!t2canDrEligible(brake.data))
            return SHIFT_REASON_GEAR_NOT_DR;
        if (!runtime.canWriteReady)
            return SHIFT_REASON_CAN_WRITE_DISABLED;
        if (!hasReal118_ || nowMs - lastReal118Ms_ > kReal118FreshMs)
            return SHIFT_REASON_118_STALE;
        return SHIFT_REASON_NONE;
    }

    void evaluateState(const BrakeStateView &brake,
                       const ObstacleShiftRuntimeInputs &runtime,
                       uint32_t nowMs)
    {
        if (!haveSession_ || brake.sessionGeneration != sessionGeneration_)
        {
            haveSession_ = brake.linkReady;
            sessionGeneration_ = brake.sessionGeneration;
        }

        const ObstacleShiftReason blocked =
            blockingReason(brake, runtime, nowMs);
        if (blocked != SHIFT_REASON_NONE)
        {
            state_ = OBSTACLE_SHIFT_IDLE;
            reason_ = blocked;
            return;
        }

        if (state_ != OBSTACLE_SHIFT_ACTIVE_P)
            activationCount_++;
        state_ = OBSTACLE_SHIFT_ACTIVE_P;
        reason_ = SHIFT_REASON_ACTIVE;
    }

    bool isOwnEcho(const CanFrame &frame, uint32_t nowMs) const
    {
        return hasVirtualFingerprint_ &&
               nowMs - lastVirtualParkMs_ <= kOwnEchoWindowMs &&
               frame.id == lastVirtualPark_.id &&
               frame.dlc == lastVirtualPark_.dlc &&
               std::memcmp(frame.data, lastVirtualPark_.data, 8) == 0;
    }

    void publish(const BrakeStateView &brake,
                 const ObstacleShiftRuntimeInputs &runtime)
    {
        const uint32_t start = static_cast<uint32_t>(publishedVersion_);
        publishedVersion_ = start + 1U;
        publishedState_ = static_cast<uint8_t>(state_);
        publishedReason_ = static_cast<uint8_t>(reason_);
        publishedRealGear_ = static_cast<uint8_t>(brake.data.realGear);
        publishedSessionGeneration_ = brake.sessionGeneration;
        publishedBrakeRxMs_ = brake.lastRxMs;
        publishedReal118Ms_ = lastReal118Ms_;
        publishedReal118RxCount_ = real118RxCount_;
        publishedTxCount_ = virtualParkTxCount_;
        publishedTxFailCount_ = virtualParkTxFailCount_;
        publishedActivationCount_ = activationCount_;
        publishedFeatureEnabled_ = runtime.featureEnabled;
        publishedLinkReady_ = brake.linkReady;
        publishedCapabilitySupported_ = brake.capabilitySupported;
        publishedHasBrakeState_ = brake.hasState;
        publishedBrakePressed_ = brake.data.physicalPressed;
        publishedReleaseConfirmed_ = brake.data.releaseConfirmed;
        publishedStationaryConfirmed_ = brake.data.stationaryConfirmed;
        publishedHasReal118_ = hasReal118_;
        publishedVirtualParkActive_ =
            state_ == OBSTACLE_SHIFT_ACTIVE_P;
        publishedVersion_ = start + 2U;
    }

    ObstacleShiftState state_ = OBSTACLE_SHIFT_IDLE;
    ObstacleShiftReason reason_ = SHIFT_REASON_FEATURE_DISABLED;
    bool haveSession_ = false;
    uint32_t sessionGeneration_ = 0;
    bool hasReal118_ = false;
    uint32_t lastReal118Ms_ = 0;
    CanFrame lastVirtualPark_{};
    uint32_t lastVirtualParkMs_ = 0;
    bool hasVirtualFingerprint_ = false;
    uint32_t real118RxCount_ = 0;
    uint32_t virtualParkTxCount_ = 0;
    uint32_t virtualParkTxFailCount_ = 0;
    uint32_t activationCount_ = 0;

    Shared<uint32_t> publishedVersion_{0};
    Shared<uint8_t> publishedState_{OBSTACLE_SHIFT_IDLE};
    Shared<uint8_t> publishedReason_{SHIFT_REASON_FEATURE_DISABLED};
    Shared<uint8_t> publishedRealGear_{BRAKE_GEAR_UNKNOWN};
    Shared<uint32_t> publishedSessionGeneration_{0};
    Shared<uint32_t> publishedBrakeRxMs_{0};
    Shared<uint32_t> publishedReal118Ms_{0};
    Shared<uint32_t> publishedReal118RxCount_{0};
    Shared<uint32_t> publishedTxCount_{0};
    Shared<uint32_t> publishedTxFailCount_{0};
    Shared<uint32_t> publishedActivationCount_{0};
    Shared<bool> publishedFeatureEnabled_{false};
    Shared<bool> publishedLinkReady_{false};
    Shared<bool> publishedCapabilitySupported_{false};
    Shared<bool> publishedHasBrakeState_{false};
    Shared<bool> publishedBrakePressed_{false};
    Shared<bool> publishedReleaseConfirmed_{false};
    Shared<bool> publishedStationaryConfirmed_{false};
    Shared<bool> publishedHasReal118_{false};
    Shared<bool> publishedVirtualParkActive_{false};
};

inline ObstacleShiftController obstacleShiftController;
