#pragma once

#include <algorithm>
#include <cstdint>

#include "ble_fsd_receiver.h"

enum class BleFsdCoreEvent : uint8_t
{
    None = 0,
    WindowStarted,
    WindowStopped,
};

struct BleFsdCoreResult
{
    BleFsdCoreEvent event = BleFsdCoreEvent::None;
    BleFsdRejectReason reject = BleFsdRejectReason::None;
    uint32_t windowMs = 0;
};

struct BleFsdCoreSnapshot
{
    bool remoteActive = false;
    BleFsdReceiverState state = BleFsdReceiverState::Disabled;
    BleFsdRejectReason lastReject = BleFsdRejectReason::None;
    uint32_t lastSequence = 0;
    uint32_t lastSourceTimestampMs = 0;
    uint32_t lastPacketAtMs = 0;
    uint32_t testRemainingMs = 0;
    uint32_t acceptedPackets = 0;
    uint32_t testWindows = 0;
    uint32_t crcErrors = 0;
    uint32_t timeoutCount = 0;
    uint32_t duplicateCount = 0;
    uint32_t rejectedCount = 0;
};

class BleFsdReceiverCore
{
  public:
    void setFallbackWindowMs(uint32_t windowMs)
    {
        fallbackWindowMs_ = clampWindow(windowMs);
    }

    BleFsdCoreResult resetSession(bool enabled, uint32_t now)
    {
        (void)now;
        const bool wasActive = state_ == BleFsdReceiverState::TestActive;
        enabled_ = enabled;
        haveSequence_ = false;
        haveSourceTimestamp_ = false;
        retryPending_ = false;
        retrySequence_ = 0;
        remoteActive_ = false;
        state_ = enabled ? BleFsdReceiverState::Idle
                         : BleFsdReceiverState::Disabled;
        lastReject_ = BleFsdRejectReason::None;
        lastSequence_ = 0;
        lastSourceTimestampMs_ = 0;
        lastPacketAtMs_ = 0;
        testEndsAtMs_ = 0;

        BleFsdCoreResult result{};
        if (wasActive)
            result.event = BleFsdCoreEvent::WindowStopped;
        return result;
    }

    void recordReject(BleFsdRejectReason reason, uint32_t now)
    {
        (void)now;
        reject(reason);
    }

    BleFsdCoreResult onPacket(const BleFsdPacket &packet, bool canHealthy,
                              uint32_t now)
    {
        if (!enabled_)
            return rejected(BleFsdRejectReason::Disabled);
        if (packet.sourceTimestampMs == 0)
            return rejected(BleFsdRejectReason::InvalidTimestamp);

        if (haveSequence_)
        {
            if (packet.sequence == lastSequence_)
                return handleSameSequence(packet, canHealthy, now);
            if (isOlder(packet.sequence, lastSequence_))
                return rejected(BleFsdRejectReason::OldSequence);
        }

        if (haveSourceTimestamp_ &&
            isOlder(packet.sourceTimestampMs, lastSourceTimestampMs_))
            return rejected(BleFsdRejectReason::InvalidTimestamp);

        const bool wasActive = state_ == BleFsdReceiverState::TestActive;
        rememberPacket(packet, now);

        if (!packet.active)
        {
            acceptedPackets_++;
            remoteActive_ = false;
            retryPending_ = false;
            stopWindow();
            lastReject_ = BleFsdRejectReason::None;
            return eventResult(wasActive ? BleFsdCoreEvent::WindowStopped
                                         : BleFsdCoreEvent::None);
        }

        if (!canHealthy)
        {
            retryPending_ = true;
            retrySequence_ = packet.sequence;
            remoteActive_ = false;
            stopWindow();
            BleFsdCoreResult result = rejected(BleFsdRejectReason::CanUnhealthy);
            if (wasActive)
                result.event = BleFsdCoreEvent::WindowStopped;
            return result;
        }

        return startWindow(packet, now);
    }

    BleFsdCoreResult tick(bool canHealthy, uint32_t now)
    {
        if (!enabled_ || state_ != BleFsdReceiverState::TestActive)
            return {};

        if (!canHealthy)
        {
            retryPending_ = haveSequence_;
            retrySequence_ = lastSequence_;
            remoteActive_ = false;
            stopWindow();
            BleFsdCoreResult result = rejected(BleFsdRejectReason::CanUnhealthy);
            result.event = BleFsdCoreEvent::WindowStopped;
            return result;
        }

        if (static_cast<int32_t>(now - testEndsAtMs_) >= 0)
        {
            testEndsAtMs_ = 0;
            state_ = remoteActive_ ? BleFsdReceiverState::AwaitingClear
                                   : BleFsdReceiverState::Idle;
            timeoutCount_++;
            return eventResult(BleFsdCoreEvent::WindowStopped);
        }

        return {};
    }

    BleFsdCoreSnapshot snapshot(uint32_t now) const
    {
        BleFsdCoreSnapshot result{};
        result.remoteActive = remoteActive_;
        result.state = state_;
        result.lastReject = lastReject_;
        result.lastSequence = lastSequence_;
        result.lastSourceTimestampMs = lastSourceTimestampMs_;
        result.lastPacketAtMs = lastPacketAtMs_;
        if (state_ == BleFsdReceiverState::TestActive &&
            static_cast<int32_t>(testEndsAtMs_ - now) > 0)
            result.testRemainingMs = testEndsAtMs_ - now;
        result.acceptedPackets = acceptedPackets_;
        result.testWindows = testWindows_;
        result.crcErrors = crcErrors_;
        result.timeoutCount = timeoutCount_;
        result.duplicateCount = duplicateCount_;
        result.rejectedCount = rejectedCount_;
        return result;
    }

  private:
    static bool isOlder(uint32_t candidate, uint32_t reference)
    {
        return static_cast<int32_t>(candidate - reference) < 0;
    }

    static uint32_t clampWindow(uint32_t windowMs)
    {
        return std::clamp<uint32_t>(windowMs, 1000U, 60000U);
    }

    void reject(BleFsdRejectReason reason)
    {
        lastReject_ = reason;
        rejectedCount_++;
        if (reason == BleFsdRejectReason::InvalidCrc)
            crcErrors_++;
        if (reason == BleFsdRejectReason::DuplicateSequence)
            duplicateCount_++;
    }

    BleFsdCoreResult rejected(BleFsdRejectReason reason)
    {
        reject(reason);
        BleFsdCoreResult result{};
        result.reject = reason;
        return result;
    }

    static BleFsdCoreResult eventResult(BleFsdCoreEvent event)
    {
        BleFsdCoreResult result{};
        result.event = event;
        return result;
    }

    void rememberPacket(const BleFsdPacket &packet, uint32_t now)
    {
        haveSequence_ = true;
        haveSourceTimestamp_ = true;
        lastSequence_ = packet.sequence;
        lastSourceTimestampMs_ = packet.sourceTimestampMs;
        lastPacketAtMs_ = now;
    }

    void stopWindow()
    {
        testEndsAtMs_ = 0;
        state_ = enabled_ ? BleFsdReceiverState::Idle
                          : BleFsdReceiverState::Disabled;
    }

    BleFsdCoreResult startWindow(const BleFsdPacket &packet, uint32_t now)
    {
        const uint32_t requestedMs =
            packet.holdMs == 0 ? fallbackWindowMs_
                               : static_cast<uint32_t>(packet.holdMs);
        const uint32_t windowMs = clampWindow(requestedMs);

        rememberPacket(packet, now);
        acceptedPackets_++;
        remoteActive_ = true;
        retryPending_ = false;
        retrySequence_ = 0;
        state_ = BleFsdReceiverState::TestActive;
        testEndsAtMs_ = now + windowMs;
        testWindows_++;
        lastReject_ = BleFsdRejectReason::None;

        BleFsdCoreResult result{};
        result.event = BleFsdCoreEvent::WindowStarted;
        result.windowMs = windowMs;
        return result;
    }

    BleFsdCoreResult handleSameSequence(const BleFsdPacket &packet,
                                        bool canHealthy, uint32_t now)
    {
        if (haveSourceTimestamp_ &&
            isOlder(packet.sourceTimestampMs, lastSourceTimestampMs_))
            return rejected(BleFsdRejectReason::InvalidTimestamp);

        if (packet.active)
        {
            if (retryPending_ && retrySequence_ == packet.sequence)
            {
                lastPacketAtMs_ = now;
                lastSourceTimestampMs_ = packet.sourceTimestampMs;
                if (!canHealthy)
                    return rejected(BleFsdRejectReason::CanUnhealthy);
                return startWindow(packet, now);
            }

            if (remoteActive_)
            {
                duplicateCount_++;
                lastPacketAtMs_ = now;
                lastSourceTimestampMs_ = packet.sourceTimestampMs;
                lastReject_ = BleFsdRejectReason::None;
                return {};
            }

            return rejected(BleFsdRejectReason::DuplicateSequence);
        }

        if (!remoteActive_ && !retryPending_ &&
            state_ != BleFsdReceiverState::AwaitingClear &&
            state_ != BleFsdReceiverState::TestActive)
            return rejected(BleFsdRejectReason::DuplicateSequence);

        const bool wasActive = state_ == BleFsdReceiverState::TestActive;
        rememberPacket(packet, now);
        acceptedPackets_++;
        remoteActive_ = false;
        retryPending_ = false;
        retrySequence_ = 0;
        stopWindow();
        lastReject_ = BleFsdRejectReason::None;
        return eventResult(wasActive ? BleFsdCoreEvent::WindowStopped
                                     : BleFsdCoreEvent::None);
    }

    bool enabled_ = false;
    bool haveSequence_ = false;
    bool haveSourceTimestamp_ = false;
    bool retryPending_ = false;
    bool remoteActive_ = false;
    BleFsdReceiverState state_ = BleFsdReceiverState::Disabled;
    BleFsdRejectReason lastReject_ = BleFsdRejectReason::None;
    uint32_t fallbackWindowMs_ = 10000;
    uint32_t retrySequence_ = 0;
    uint32_t lastSequence_ = 0;
    uint32_t lastSourceTimestampMs_ = 0;
    uint32_t lastPacketAtMs_ = 0;
    uint32_t testEndsAtMs_ = 0;
    uint32_t acceptedPackets_ = 0;
    uint32_t testWindows_ = 0;
    uint32_t crcErrors_ = 0;
    uint32_t timeoutCount_ = 0;
    uint32_t duplicateCount_ = 0;
    uint32_t rejectedCount_ = 0;
};

inline void bleFsdApplyCoreSnapshot(const BleFsdCoreSnapshot &core,
                                    BleFsdReceiverStatus &status)
{
    status.remoteActive = core.remoteActive;
    status.state = core.state;
    status.lastReject = core.lastReject;
    status.lastSequence = core.lastSequence;
    status.lastSourceTimestampMs = core.lastSourceTimestampMs;
    status.lastPacketAtMs = core.lastPacketAtMs;
    status.testRemainingMs = core.testRemainingMs;
    status.acceptedPackets = core.acceptedPackets;
    status.testWindows = core.testWindows;
    status.crcErrors = core.crcErrors;
    status.timeoutCount = core.timeoutCount;
    status.duplicateCount = core.duplicateCount;
    status.rejectedCount = core.rejectedCount;
}

struct BleFsdBridgeDecision
{
    bool trigger = false;
    bool cancel = false;
    uint32_t windowMs = 0;
};

class BleFsdWindowBridgeCore
{
  public:
    BleFsdBridgeDecision update(const BleFsdReceiverStatus &status,
                                bool outputEnabled)
    {
        BleFsdBridgeDecision result{};
        const bool active =
            status.state == BleFsdReceiverState::TestActive &&
            status.testRemainingMs > 0;

        if (!active)
        {
            if (armed_)
                result.cancel = true;
            armed_ = false;
            handledWindows_ = status.testWindows;
            return result;
        }

        if (!outputEnabled)
        {
            if (armed_)
                result.cancel = true;
            armed_ = false;
            return result;
        }

        if (!armed_ || handledWindows_ != status.testWindows)
        {
            result.trigger = true;
            result.windowMs = status.testRemainingMs;
            handledWindows_ = status.testWindows;
            armed_ = true;
        }
        return result;
    }

  private:
    uint32_t handledWindows_ = 0;
    bool armed_ = false;
};
