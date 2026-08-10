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

enum ObstacleShiftTriggerSource : uint8_t
{
    SHIFT_TRIGGER_NONE = 0,
    SHIFT_TRIGGER_AUTOMATIC_BRAKE,
    SHIFT_TRIGGER_MANUAL_BUTTON,
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
    SHIFT_REASON_BUSY,
};

struct ObstacleShiftRuntimeInputs
{
    bool featureEnabled = false;
    bool canWriteReady = false;
    uint32_t manualRequestGeneration = 0;
};

struct ObstacleShiftView
{
    ObstacleShiftState state = OBSTACLE_SHIFT_WAIT_RELEASE;
    ObstacleShiftReason reason = SHIFT_REASON_WAIT_RELEASE;
    ObstacleShiftReason manualRequestReason = SHIFT_REASON_WAIT_RELEASE;
    ObstacleShiftTriggerSource triggerSource = SHIFT_TRIGGER_NONE;
    BrakeRealGear realGear = BRAKE_GEAR_UNKNOWN;
    uint32_t sessionGeneration = 0;
    uint32_t brakeStateAgeMs = UINT32_MAX;
    uint32_t real118AgeMs = UINT32_MAX;
    uint32_t virtualParkRemainingMs = 0;
    uint32_t real118RxCount = 0;
    uint32_t virtualParkTxCount = 0;
    uint32_t virtualParkTxFailCount = 0;
    uint32_t automaticWindowCount = 0;
    uint32_t manualWindowCount = 0;
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
    bool manualRequestReady = false;
};

class ObstacleShiftController
{
public:
    static constexpr uint32_t kControlFreshMs = 500;
    static constexpr uint32_t kBrakeFreshMs = kControlFreshMs;
    static constexpr uint32_t kReal118FreshMs = 50;
    static constexpr uint32_t kVirtualParkWindowMs = 1000;
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
        const bool newManualRequest =
            runtime.manualRequestGeneration != lastManualRequestGeneration_;
        if (newManualRequest)
            lastManualRequestGeneration_ = runtime.manualRequestGeneration;

        if (!brake.linkReady)
        {
            haveSession_ = false;
            stopWindow(SHIFT_REASON_LINK_UNAVAILABLE, brake, runtime);
            state_ = automaticLocked_ ? OBSTACLE_SHIFT_LATCHED
                                      : OBSTACLE_SHIFT_WAIT_RELEASE;
            reason_ = SHIFT_REASON_LINK_UNAVAILABLE;
            updateManualReadiness(brake, runtime, nowMs);
            publish(brake, runtime);
            return;
        }
        if (!brake.capabilitySupported)
        {
            stopWindow(SHIFT_REASON_CAPABILITY_MISSING, brake, runtime);
            state_ = automaticLocked_ ? OBSTACLE_SHIFT_LATCHED
                                      : OBSTACLE_SHIFT_WAIT_RELEASE;
            reason_ = SHIFT_REASON_CAPABILITY_MISSING;
            updateManualReadiness(brake, runtime, nowMs);
            publish(brake, runtime);
            return;
        }

        if (!haveSession_ || brake.sessionGeneration != sessionGeneration_)
        {
            haveSession_ = true;
            sessionGeneration_ = brake.sessionGeneration;
            stopWindow(SHIFT_REASON_SESSION_CHANGED, brake, runtime);
            state_ = OBSTACLE_SHIFT_WAIT_RELEASE;
            reason_ = SHIFT_REASON_SESSION_CHANGED;
        }

        if (virtualParkActive_)
        {
            if (triggerSource_ == SHIFT_TRIGGER_MANUAL_BUTTON &&
                physicalPressedEligible(brake.data))
                automaticLocked_ = true;

            const ObstacleShiftReason sustainFailure =
                activeSustainFailure(brake, runtime, nowMs);
            if (sustainFailure != SHIFT_REASON_NONE)
            {
                stopWindow(sustainFailure, brake, runtime);
                reason_ = sustainFailure;
                updateManualReadiness(brake, runtime, nowMs);
                publish(brake, runtime);
                return;
            }

            if (nowMs - activeStartedMs_ >= kVirtualParkWindowMs)
            {
                stopWindow(SHIFT_REASON_WINDOW_COMPLETE, brake, runtime);
                reason_ = SHIFT_REASON_WINDOW_COMPLETE;
                updateManualReadiness(brake, runtime, nowMs);
                publish(brake, runtime);
                return;
            }

            updateManualReadiness(brake, runtime, nowMs);
            publish(brake, runtime);
            return;
        }

        updateManualReadiness(brake, runtime, nowMs);
        if (newManualRequest)
        {
            if (manualRequestReady_)
            {
                startWindow(SHIFT_TRIGGER_MANUAL_BUTTON, nowMs);
                manualWindowCount_++;
                updateManualReadiness(brake, runtime, nowMs);
            }
            else
            {
                reason_ = manualRequestReason_;
            }
            publish(brake, runtime);
            return;
        }

        if (!runtime.featureEnabled)
        {
            state_ = OBSTACLE_SHIFT_WAIT_RELEASE;
            reason_ = SHIFT_REASON_FEATURE_DISABLED;
            publish(brake, runtime);
            return;
        }

        if (!controlFresh(brake, nowMs))
        {
            state_ = automaticLocked_ ? OBSTACLE_SHIFT_LATCHED
                                      : OBSTACLE_SHIFT_WAIT_RELEASE;
            reason_ = SHIFT_REASON_BRAKE_STALE;
            publish(brake, runtime);
            return;
        }

        if (physicalReleasedEligible(brake.data))
        {
            automaticLocked_ = false;
            state_ = OBSTACLE_SHIFT_ARMED;
            reason_ = SHIFT_REASON_NONE;
            publish(brake, runtime);
            return;
        }

        if (automaticLocked_)
        {
            state_ = OBSTACLE_SHIFT_LATCHED;
            reason_ = SHIFT_REASON_WAIT_RELEASE;
            publish(brake, runtime);
            return;
        }

        if (state_ != OBSTACLE_SHIFT_ARMED)
        {
            state_ = OBSTACLE_SHIFT_WAIT_RELEASE;
            reason_ = SHIFT_REASON_WAIT_RELEASE;
            publish(brake, runtime);
            return;
        }

        if (!physicalPressedEligible(brake.data))
        {
            reason_ = SHIFT_REASON_RELEASE_UNCONFIRMED;
            publish(brake, runtime);
            return;
        }
        if (!stationaryEligible(brake.data))
        {
            reason_ = SHIFT_REASON_MOVING;
            publish(brake, runtime);
            return;
        }
        if (!t2canDrEligible(brake.data))
        {
            reason_ = SHIFT_REASON_GEAR_NOT_DR;
            publish(brake, runtime);
            return;
        }
        if (!runtime.canWriteReady)
        {
            reason_ = SHIFT_REASON_CAN_WRITE_DISABLED;
            publish(brake, runtime);
            return;
        }

        automaticLocked_ = true;
        startWindow(SHIFT_TRIGGER_AUTOMATIC_BRAKE, nowMs);
        automaticWindowCount_++;
        updateManualReadiness(brake, runtime, nowMs);
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
        tick(brake, runtime, nowMs);
        if (isOwnEcho(frame, nowMs) || !validReal118(frame))
            return;

        hasReal118_ = true;
        lastReal118Ms_ = nowMs;
        realGear_ = parseRealGear(frame);
        real118RxCount_++;

        if (!virtualParkActive_)
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
            stopWindow(SHIFT_REASON_TX_FAILED, brake, runtime);
            reason_ = SHIFT_REASON_TX_FAILED;
            updateManualReadiness(brake, runtime, nowMs);
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
            out.state = static_cast<ObstacleShiftState>(static_cast<uint8_t>(publishedState_));
            out.reason = static_cast<ObstacleShiftReason>(static_cast<uint8_t>(publishedReason_));
            out.manualRequestReason = static_cast<ObstacleShiftReason>(
                static_cast<uint8_t>(publishedManualRequestReason_));
            out.triggerSource = static_cast<ObstacleShiftTriggerSource>(
                static_cast<uint8_t>(publishedTriggerSource_));
            out.realGear = static_cast<BrakeRealGear>(static_cast<uint8_t>(publishedRealGear_));
            out.sessionGeneration = static_cast<uint32_t>(publishedSessionGeneration_);
            const uint32_t brakeRx = static_cast<uint32_t>(publishedBrakeRxMs_);
            const uint32_t realRx = static_cast<uint32_t>(publishedReal118Ms_);
            const uint32_t activeStart = static_cast<uint32_t>(publishedActiveStartedMs_);
            out.real118RxCount = static_cast<uint32_t>(publishedReal118RxCount_);
            out.virtualParkTxCount = static_cast<uint32_t>(publishedTxCount_);
            out.virtualParkTxFailCount = static_cast<uint32_t>(publishedTxFailCount_);
            out.automaticWindowCount = static_cast<uint32_t>(publishedAutomaticWindowCount_);
            out.manualWindowCount = static_cast<uint32_t>(publishedManualWindowCount_);
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
            out.manualRequestReady = static_cast<bool>(publishedManualRequestReady_);

            const uint32_t after = static_cast<uint32_t>(publishedVersion_);
            if (before != after || (after & 1U))
                continue;

            if (hasBrake)
            {
                out.brakeStateAgeMs = nowMs - brakeRx;
                out.brakeStateFresh = out.brakeStateAgeMs <= kControlFreshMs;
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
        manualRequestReason_ = SHIFT_REASON_WAIT_RELEASE;
        triggerSource_ = SHIFT_TRIGGER_NONE;
        realGear_ = BRAKE_GEAR_UNKNOWN;
        haveSession_ = false;
        sessionGeneration_ = 0;
        hasReal118_ = false;
        lastReal118Ms_ = 0;
        activeStartedMs_ = 0;
        lastVirtualParkMs_ = 0;
        hasVirtualFingerprint_ = false;
        virtualParkActive_ = false;
        automaticLocked_ = false;
        manualRequestReady_ = false;
        lastManualRequestGeneration_ = 0;
        real118RxCount_ = 0;
        virtualParkTxCount_ = 0;
        virtualParkTxFailCount_ = 0;
        automaticWindowCount_ = 0;
        manualWindowCount_ = 0;
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
        return brake.linkReady && brake.capabilitySupported && brake.hasState &&
               nowMs - brake.lastRxMs <= kControlFreshMs;
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

    static bool physicalReleasedEligible(const BrakeStateData &brake)
    {
        return brake.physicalKnown && brake.physicalFresh &&
               !brake.physicalPressed;
    }

    ObstacleShiftReason manualEligibilityReason(
        const BrakeStateView &brake,
        const ObstacleShiftRuntimeInputs &runtime,
        uint32_t nowMs) const
    {
        if (virtualParkActive_)
            return SHIFT_REASON_BUSY;
        if (!runtime.canWriteReady)
            return SHIFT_REASON_CAN_WRITE_DISABLED;
        if (!brake.linkReady)
            return SHIFT_REASON_LINK_UNAVAILABLE;
        if (!brake.capabilitySupported)
            return SHIFT_REASON_CAPABILITY_MISSING;
        if (!controlFresh(brake, nowMs))
            return SHIFT_REASON_BRAKE_STALE;
        if (!stationaryEligible(brake.data))
            return SHIFT_REASON_MOVING;
        if (!t2canDrEligible(brake.data))
            return SHIFT_REASON_GEAR_NOT_DR;
        return SHIFT_REASON_NONE;
    }

    ObstacleShiftReason activeSustainFailure(
        const BrakeStateView &brake,
        const ObstacleShiftRuntimeInputs &runtime,
        uint32_t nowMs) const
    {
        if (!runtime.canWriteReady)
            return SHIFT_REASON_CAN_WRITE_DISABLED;
        if (!brake.linkReady)
            return SHIFT_REASON_LINK_UNAVAILABLE;
        if (!brake.capabilitySupported)
            return SHIFT_REASON_CAPABILITY_MISSING;
        if (!controlFresh(brake, nowMs))
            return SHIFT_REASON_BRAKE_STALE;
        if (!stationaryEligible(brake.data))
            return SHIFT_REASON_MOVING;
        if (!t2canDrEligible(brake.data))
            return SHIFT_REASON_GEAR_NOT_DR;
        if (triggerSource_ == SHIFT_TRIGGER_AUTOMATIC_BRAKE)
        {
            if (!runtime.featureEnabled)
                return SHIFT_REASON_FEATURE_DISABLED;
            if (!physicalPressedEligible(brake.data))
                return SHIFT_REASON_RELEASE_UNCONFIRMED;
        }
        return SHIFT_REASON_NONE;
    }

    void updateManualReadiness(const BrakeStateView &brake,
                               const ObstacleShiftRuntimeInputs &runtime,
                               uint32_t nowMs)
    {
        manualRequestReason_ = manualEligibilityReason(brake, runtime, nowMs);
        manualRequestReady_ = manualRequestReason_ == SHIFT_REASON_NONE;
    }

    void startWindow(ObstacleShiftTriggerSource source, uint32_t nowMs)
    {
        triggerSource_ = source;
        state_ = OBSTACLE_SHIFT_ACTIVE_P;
        reason_ = SHIFT_REASON_ACTIVE;
        activeStartedMs_ = nowMs;
        virtualParkActive_ = true;
    }

    void stopWindow(ObstacleShiftReason reason,
                    const BrakeStateView &brake,
                    const ObstacleShiftRuntimeInputs &runtime)
    {
        const ObstacleShiftTriggerSource stoppedSource = triggerSource_;
        if (stoppedSource == SHIFT_TRIGGER_AUTOMATIC_BRAKE ||
            (stoppedSource == SHIFT_TRIGGER_MANUAL_BUTTON &&
             physicalPressedEligible(brake.data)))
            automaticLocked_ = true;

        triggerSource_ = SHIFT_TRIGGER_NONE;
        virtualParkActive_ = false;
        reason_ = reason;

        if (automaticLocked_)
            state_ = OBSTACLE_SHIFT_LATCHED;
        else if (runtime.featureEnabled &&
                 physicalReleasedEligible(brake.data))
            state_ = OBSTACLE_SHIFT_ARMED;
        else
            state_ = OBSTACLE_SHIFT_WAIT_RELEASE;
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
        publishedManualRequestReason_ = static_cast<uint8_t>(manualRequestReason_);
        publishedTriggerSource_ = static_cast<uint8_t>(triggerSource_);
        publishedRealGear_ = static_cast<uint8_t>(realGear_);
        publishedSessionGeneration_ = brake.sessionGeneration;
        publishedBrakeRxMs_ = brake.lastRxMs;
        publishedReal118Ms_ = lastReal118Ms_;
        publishedActiveStartedMs_ = activeStartedMs_;
        publishedReal118RxCount_ = real118RxCount_;
        publishedTxCount_ = virtualParkTxCount_;
        publishedTxFailCount_ = virtualParkTxFailCount_;
        publishedAutomaticWindowCount_ = automaticWindowCount_;
        publishedManualWindowCount_ = manualWindowCount_;
        publishedFeatureEnabled_ = runtime.featureEnabled;
        publishedLinkReady_ = brake.linkReady;
        publishedCapabilitySupported_ = brake.capabilitySupported;
        publishedHasBrakeState_ = brake.hasState;
        publishedBrakePressed_ = brake.data.brakePressed;
        publishedReleaseConfirmed_ = brake.data.releaseConfirmed;
        publishedStationaryConfirmed_ = brake.data.stationaryConfirmed;
        publishedHasReal118_ = hasReal118_;
        publishedVirtualParkActive_ = virtualParkActive_;
        publishedLatched_ = automaticLocked_;
        publishedManualRequestReady_ = manualRequestReady_;
        publishedVersion_ = start + 2U;
    }

    ObstacleShiftState state_ = OBSTACLE_SHIFT_WAIT_RELEASE;
    ObstacleShiftReason reason_ = SHIFT_REASON_WAIT_RELEASE;
    ObstacleShiftReason manualRequestReason_ = SHIFT_REASON_WAIT_RELEASE;
    ObstacleShiftTriggerSource triggerSource_ = SHIFT_TRIGGER_NONE;
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
    bool automaticLocked_ = false;
    bool manualRequestReady_ = false;
    uint32_t lastManualRequestGeneration_ = 0;
    uint32_t real118RxCount_ = 0;
    uint32_t virtualParkTxCount_ = 0;
    uint32_t virtualParkTxFailCount_ = 0;
    uint32_t automaticWindowCount_ = 0;
    uint32_t manualWindowCount_ = 0;

    Shared<uint32_t> publishedVersion_{0};
    Shared<uint8_t> publishedState_{OBSTACLE_SHIFT_WAIT_RELEASE};
    Shared<uint8_t> publishedReason_{SHIFT_REASON_WAIT_RELEASE};
    Shared<uint8_t> publishedManualRequestReason_{SHIFT_REASON_WAIT_RELEASE};
    Shared<uint8_t> publishedTriggerSource_{SHIFT_TRIGGER_NONE};
    Shared<uint8_t> publishedRealGear_{BRAKE_GEAR_UNKNOWN};
    Shared<uint32_t> publishedSessionGeneration_{0};
    Shared<uint32_t> publishedBrakeRxMs_{0};
    Shared<uint32_t> publishedReal118Ms_{0};
    Shared<uint32_t> publishedActiveStartedMs_{0};
    Shared<uint32_t> publishedReal118RxCount_{0};
    Shared<uint32_t> publishedTxCount_{0};
    Shared<uint32_t> publishedTxFailCount_{0};
    Shared<uint32_t> publishedAutomaticWindowCount_{0};
    Shared<uint32_t> publishedManualWindowCount_{0};
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
    Shared<bool> publishedManualRequestReady_{false};
};

inline ObstacleShiftController obstacleShiftController;
