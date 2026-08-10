#pragma once

#include <cstdint>
#include <cstring>

#include "ble/brake_state.h"
#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "shared_types.h"

enum ObstacleShiftState : uint8_t
{
    OBSTACLE_SHIFT_WAIT_RELEASE = 0,
    OBSTACLE_SHIFT_ARMED,
    OBSTACLE_SHIFT_ACTIVE_P,
    OBSTACLE_SHIFT_LATCHED,
};

enum ObstacleShiftReason : uint8_t
{
    SHIFT_REASON_NONE = 0,
    SHIFT_REASON_WAIT_RELEASE,
    SHIFT_REASON_ACTIVE,
    SHIFT_REASON_WINDOW_COMPLETE,
    SHIFT_REASON_BRAKE_STALE,
    SHIFT_REASON_RELEASE_UNCONFIRMED,
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
    ObstacleShiftState state = OBSTACLE_SHIFT_WAIT_RELEASE;
    ObstacleShiftReason reason = SHIFT_REASON_WAIT_RELEASE;
    BrakeRealGear realGear = BRAKE_GEAR_UNKNOWN;
    uint32_t sessionGeneration = 0;
    uint32_t brakeStateAgeMs = UINT32_MAX;
    uint32_t real118AgeMs = UINT32_MAX;
    uint32_t virtualParkRemainingMs = 0;
    uint32_t real118RxCount = 0;
    uint32_t virtualParkTxCount = 0;
    uint32_t virtualParkTxFailCount = 0;
    bool featureEnabled = false;
    bool linkReady = false;
    bool capabilitySupported = false;
    bool brakeStateFresh = false;
    bool brakePressed = false;
    bool releaseConfirmed = false;
    bool stationaryConfirmed = false;
    bool hasReal118 = false;
    bool virtualParkActive = false;
    bool latched = false;
};

class ObstacleShiftController
{
public:
    static constexpr uint32_t kBrakeFreshMs = 200;
    static constexpr uint32_t kReal118FreshMs = 50;
    static constexpr uint32_t kVirtualParkWindowMs = 500;
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
        const BrakeRealGear gear = parseRealGear(realFrame);
        if (gear != BRAKE_GEAR_D && gear != BRAKE_GEAR_R)
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
        if (!brake.linkReady)
        {
            haveSession_ = false;
            requireRelease(SHIFT_REASON_LINK_UNAVAILABLE);
            publish(brake, runtime, nowMs);
            return;
        }
        if (!brake.capabilitySupported)
        {
            requireRelease(SHIFT_REASON_CAPABILITY_MISSING);
            publish(brake, runtime, nowMs);
            return;
        }
        if (!runtime.featureEnabled)
        {
            requireRelease(SHIFT_REASON_FEATURE_DISABLED);
            publish(brake, runtime, nowMs);
            return;
        }
        if (!runtime.canWriteReady)
        {
            requireRelease(SHIFT_REASON_CAN_WRITE_DISABLED);
            publish(brake, runtime, nowMs);
            return;
        }
        if (!brake.hasState || nowMs - brake.lastRxMs > kBrakeFreshMs)
        {
            requireRelease(SHIFT_REASON_BRAKE_STALE);
            publish(brake, runtime, nowMs);
            return;
        }

        if (!haveSession_ || brake.sessionGeneration != sessionGeneration_)
        {
            haveSession_ = true;
            sessionGeneration_ = brake.sessionGeneration;
            requireRelease(SHIFT_REASON_SESSION_CHANGED);
        }

        if (state_ == OBSTACLE_SHIFT_ACTIVE_P && hasReal118_ &&
            nowMs - lastReal118Ms_ > kReal118FreshMs)
        {
            requireRelease(SHIFT_REASON_118_STALE);
            publish(brake, runtime, nowMs);
            return;
        }

        if (state_ == OBSTACLE_SHIFT_ACTIVE_P &&
            nowMs - activeStartedMs_ >= kVirtualParkWindowMs)
        {
            state_ = OBSTACLE_SHIFT_LATCHED;
            reason_ = SHIFT_REASON_WINDOW_COMPLETE;
            virtualParkActive_ = false;
        }

        if (brake.data.brakePressed)
        {
            if (state_ == OBSTACLE_SHIFT_ACTIVE_P && !triggerEligible(brake.data))
            {
                requireRelease(brake.data.stationaryConfirmed
                                   ? SHIFT_REASON_WAIT_RELEASE
                                   : SHIFT_REASON_MOVING);
            }
            publish(brake, runtime, nowMs);
            return;
        }

        if (brake.data.releaseConfirmed)
        {
            state_ = OBSTACLE_SHIFT_ARMED;
            reason_ = SHIFT_REASON_NONE;
            virtualParkActive_ = false;
            latched_ = false;
        }
        else if (state_ == OBSTACLE_SHIFT_ACTIVE_P || latched_)
        {
            state_ = OBSTACLE_SHIFT_LATCHED;
            reason_ = SHIFT_REASON_RELEASE_UNCONFIRMED;
            virtualParkActive_ = false;
            latched_ = true;
        }
        else
        {
            state_ = OBSTACLE_SHIFT_WAIT_RELEASE;
            reason_ = SHIFT_REASON_RELEASE_UNCONFIRMED;
        }
        publish(brake, runtime, nowMs);
    }

    void observeFrame(const CanFrame &frame,
                      const BrakeStateView &brake,
                      const ObstacleShiftRuntimeInputs &runtime,
                      uint32_t nowMs,
                      CanDriver &driver,
                      bool allowStartFromFrame = true)
    {
        tick(brake, runtime, nowMs);
        if (isOwnEcho(frame, nowMs) || !validReal118(frame))
            return;

        const BrakeRealGear gear = parseRealGear(frame);
        hasReal118_ = true;
        lastReal118Ms_ = nowMs;
        realGear_ = gear;
        real118RxCount_++;

        if (gear != BRAKE_GEAR_D && gear != BRAKE_GEAR_R)
        {
            if (state_ == OBSTACLE_SHIFT_ACTIVE_P)
                requireRelease(SHIFT_REASON_GEAR_NOT_DR);
            else if (state_ == OBSTACLE_SHIFT_ARMED)
                reason_ = SHIFT_REASON_GEAR_NOT_DR;
            publish(brake, runtime, nowMs);
            return;
        }

        if (state_ != OBSTACLE_SHIFT_ARMED &&
            state_ != OBSTACLE_SHIFT_ACTIVE_P)
        {
            publish(brake, runtime, nowMs);
            return;
        }
        if (!triggerEligible(brake.data))
        {
            if (!brake.data.stationaryConfirmed)
                reason_ = SHIFT_REASON_MOVING;
            publish(brake, runtime, nowMs);
            return;
        }

        const bool starting = state_ == OBSTACLE_SHIFT_ARMED;
        if (starting && !allowStartFromFrame)
        {
            publish(brake, runtime, nowMs);
            return;
        }

        CanFrame virtualPark;
        if (!makeVirtualParkFrame(frame, virtualPark))
        {
            publish(brake, runtime, nowMs);
            return;
        }

        if (!driver.send(virtualPark))
        {
            virtualParkTxFailCount_++;
            state_ = OBSTACLE_SHIFT_LATCHED;
            reason_ = SHIFT_REASON_TX_FAILED;
            virtualParkActive_ = false;
            latched_ = true;
            publish(brake, runtime, nowMs);
            return;
        }

        virtualParkTxCount_++;
        lastVirtualPark_ = virtualPark;
        lastVirtualParkMs_ = nowMs;
        hasVirtualFingerprint_ = true;
        if (starting)
        {
            state_ = OBSTACLE_SHIFT_ACTIVE_P;
            activeStartedMs_ = nowMs;
            virtualParkActive_ = true;
            latched_ = true;
        }
        reason_ = SHIFT_REASON_ACTIVE;
        publish(brake, runtime, nowMs);
    }

    ObstacleShiftView view(uint32_t nowMs) const
    {
        for (uint8_t attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t before = static_cast<uint32_t>(publishedVersion_);
            if (before & 1U)
                continue;

            ObstacleShiftView out;
            out.state = static_cast<ObstacleShiftState>(static_cast<uint8_t>(publishedState_));
            out.reason = static_cast<ObstacleShiftReason>(static_cast<uint8_t>(publishedReason_));
            out.realGear = static_cast<BrakeRealGear>(static_cast<uint8_t>(publishedRealGear_));
            out.sessionGeneration = static_cast<uint32_t>(publishedSessionGeneration_);
            const uint32_t brakeRx = static_cast<uint32_t>(publishedBrakeRxMs_);
            const uint32_t realRx = static_cast<uint32_t>(publishedReal118Ms_);
            const uint32_t activeStart = static_cast<uint32_t>(publishedActiveStartedMs_);
            out.real118RxCount = static_cast<uint32_t>(publishedReal118RxCount_);
            out.virtualParkTxCount = static_cast<uint32_t>(publishedTxCount_);
            out.virtualParkTxFailCount = static_cast<uint32_t>(publishedTxFailCount_);
            out.featureEnabled = static_cast<bool>(publishedFeatureEnabled_);
            out.linkReady = static_cast<bool>(publishedLinkReady_);
            out.capabilitySupported = static_cast<bool>(publishedCapabilitySupported_);
            const bool hasBrake = static_cast<bool>(publishedHasBrakeState_);
            out.brakePressed = static_cast<bool>(publishedBrakePressed_);
            out.releaseConfirmed = static_cast<bool>(publishedReleaseConfirmed_);
            out.stationaryConfirmed = static_cast<bool>(publishedStationaryConfirmed_);
            out.hasReal118 = static_cast<bool>(publishedHasReal118_);
            out.virtualParkActive = static_cast<bool>(publishedVirtualParkActive_);
            out.latched = static_cast<bool>(publishedLatched_);

            const uint32_t after = static_cast<uint32_t>(publishedVersion_);
            if (before != after || (after & 1U))
                continue;

            if (hasBrake)
            {
                out.brakeStateAgeMs = nowMs - brakeRx;
                out.brakeStateFresh = out.brakeStateAgeMs <= kBrakeFreshMs;
            }
            if (out.hasReal118)
                out.real118AgeMs = nowMs - realRx;
            if (out.virtualParkActive)
            {
                const uint32_t elapsed = nowMs - activeStart;
                out.virtualParkRemainingMs =
                    elapsed < kVirtualParkWindowMs
                        ? kVirtualParkWindowMs - elapsed
                        : 0U;
            }
            return out;
        }
        return ObstacleShiftView{};
    }

    void reset()
    {
        state_ = OBSTACLE_SHIFT_WAIT_RELEASE;
        reason_ = SHIFT_REASON_WAIT_RELEASE;
        realGear_ = BRAKE_GEAR_UNKNOWN;
        haveSession_ = false;
        sessionGeneration_ = 0;
        hasReal118_ = false;
        lastReal118Ms_ = 0;
        activeStartedMs_ = 0;
        lastVirtualParkMs_ = 0;
        hasVirtualFingerprint_ = false;
        virtualParkActive_ = false;
        latched_ = false;
        real118RxCount_ = 0;
        virtualParkTxCount_ = 0;
        virtualParkTxFailCount_ = 0;
        lastVirtualPark_ = CanFrame{};
        publish(BrakeStateView{}, ObstacleShiftRuntimeInputs{}, 0);
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

    static bool triggerEligible(const BrakeStateData &brake)
    {
        return brake.brakePressed && brake.physicalKnown &&
               brake.physicalFresh && brake.physicalPressed &&
               brake.systemKnown && brake.systemFresh &&
               brake.systemPressed && brake.sourcesAgree &&
               !brake.fallbackActive && brake.gearFresh &&
               (brake.realGear == BRAKE_GEAR_D ||
                brake.realGear == BRAKE_GEAR_R) &&
               brake.drConfirmed && brake.brakeHoldReady &&
               brake.activeEligible && brake.speedFresh &&
               brake.stationaryConfirmed && brake.senderEnabled &&
               brake.normalRuntime;
    }

    bool isOwnEcho(const CanFrame &frame, uint32_t nowMs) const
    {
        return hasVirtualFingerprint_ &&
               nowMs - lastVirtualParkMs_ <= kOwnEchoWindowMs &&
               frame.id == lastVirtualPark_.id &&
               frame.dlc == lastVirtualPark_.dlc &&
               std::memcmp(frame.data, lastVirtualPark_.data, 8) == 0;
    }

    void requireRelease(ObstacleShiftReason reason)
    {
        if (state_ == OBSTACLE_SHIFT_ACTIVE_P ||
            state_ == OBSTACLE_SHIFT_LATCHED || latched_)
            latched_ = true;
        state_ = OBSTACLE_SHIFT_WAIT_RELEASE;
        reason_ = reason;
        virtualParkActive_ = false;
    }

    void publish(const BrakeStateView &brake,
                 const ObstacleShiftRuntimeInputs &runtime,
                 uint32_t nowMs)
    {
        (void)nowMs;
        const uint32_t start = static_cast<uint32_t>(publishedVersion_);
        publishedVersion_ = start + 1U;
        publishedState_ = static_cast<uint8_t>(state_);
        publishedReason_ = static_cast<uint8_t>(reason_);
        publishedRealGear_ = static_cast<uint8_t>(realGear_);
        publishedSessionGeneration_ = brake.sessionGeneration;
        publishedBrakeRxMs_ = brake.lastRxMs;
        publishedReal118Ms_ = lastReal118Ms_;
        publishedActiveStartedMs_ = activeStartedMs_;
        publishedReal118RxCount_ = real118RxCount_;
        publishedTxCount_ = virtualParkTxCount_;
        publishedTxFailCount_ = virtualParkTxFailCount_;
        publishedFeatureEnabled_ = runtime.featureEnabled;
        publishedLinkReady_ = brake.linkReady;
        publishedCapabilitySupported_ = brake.capabilitySupported;
        publishedHasBrakeState_ = brake.hasState;
        publishedBrakePressed_ = brake.data.brakePressed;
        publishedReleaseConfirmed_ = brake.data.releaseConfirmed;
        publishedStationaryConfirmed_ = brake.data.stationaryConfirmed;
        publishedHasReal118_ = hasReal118_;
        publishedVirtualParkActive_ = virtualParkActive_;
        publishedLatched_ = latched_;
        publishedVersion_ = start + 2U;
    }

    ObstacleShiftState state_ = OBSTACLE_SHIFT_WAIT_RELEASE;
    ObstacleShiftReason reason_ = SHIFT_REASON_WAIT_RELEASE;
    BrakeRealGear realGear_ = BRAKE_GEAR_UNKNOWN;
    bool haveSession_ = false;
    uint32_t sessionGeneration_ = 0;
    bool hasReal118_ = false;
    uint32_t lastReal118Ms_ = 0;
    uint32_t activeStartedMs_ = 0;
    CanFrame lastVirtualPark_{};
    uint32_t lastVirtualParkMs_ = 0;
    bool hasVirtualFingerprint_ = false;
    bool virtualParkActive_ = false;
    bool latched_ = false;
    uint32_t real118RxCount_ = 0;
    uint32_t virtualParkTxCount_ = 0;
    uint32_t virtualParkTxFailCount_ = 0;

    Shared<uint32_t> publishedVersion_{0};
    Shared<uint8_t> publishedState_{OBSTACLE_SHIFT_WAIT_RELEASE};
    Shared<uint8_t> publishedReason_{SHIFT_REASON_WAIT_RELEASE};
    Shared<uint8_t> publishedRealGear_{BRAKE_GEAR_UNKNOWN};
    Shared<uint32_t> publishedSessionGeneration_{0};
    Shared<uint32_t> publishedBrakeRxMs_{0};
    Shared<uint32_t> publishedReal118Ms_{0};
    Shared<uint32_t> publishedActiveStartedMs_{0};
    Shared<uint32_t> publishedReal118RxCount_{0};
    Shared<uint32_t> publishedTxCount_{0};
    Shared<uint32_t> publishedTxFailCount_{0};
    Shared<bool> publishedFeatureEnabled_{false};
    Shared<bool> publishedLinkReady_{false};
    Shared<bool> publishedCapabilitySupported_{false};
    Shared<bool> publishedHasBrakeState_{false};
    Shared<bool> publishedBrakePressed_{false};
    Shared<bool> publishedReleaseConfirmed_{false};
    Shared<bool> publishedStationaryConfirmed_{false};
    Shared<bool> publishedHasReal118_{false};
    Shared<bool> publishedVirtualParkActive_{false};
    Shared<bool> publishedLatched_{false};
};

inline ObstacleShiftController obstacleShiftController;
