#pragma once

#include <algorithm>
#include <cstdint>

// Keep the proven V1.0.3 Nag codec/echo implementation intact and add only a
// thin timing layer around it. The original blob is kept as handlers_base.h.
#define NagHandler NagHandlerBase
#include "handlers_base.h"
#undef NagHandler

inline constexpr uint8_t kNagSweepMinSeconds = 1;
inline constexpr uint8_t kNagSweepMaxSeconds = 30;
inline constexpr uint8_t kNagSweepDefaultMinSeconds = 5;
inline constexpr uint8_t kNagSweepDefaultMaxSeconds = 8;

inline Shared<uint8_t> nagSweepMinSeconds{kNagSweepDefaultMinSeconds};
inline Shared<uint8_t> nagSweepMaxSeconds{kNagSweepDefaultMaxSeconds};
inline Shared<uint32_t> nagSweepConfigVersion{0};

static inline uint8_t nagClampSweepSeconds(int value)
{
    if (value < kNagSweepMinSeconds)
        return kNagSweepMinSeconds;
    if (value > kNagSweepMaxSeconds)
        return kNagSweepMaxSeconds;
    return static_cast<uint8_t>(value);
}

static inline void nagSetSweepRangeSeconds(int minSeconds, int maxSeconds)
{
    uint8_t minValue = nagClampSweepSeconds(minSeconds);
    uint8_t maxValue = nagClampSweepSeconds(maxSeconds);
    if (minValue > maxValue)
        std::swap(minValue, maxValue);

    if ((uint8_t)nagSweepMinSeconds == minValue &&
        (uint8_t)nagSweepMaxSeconds == maxValue)
        return;

    nagSweepMinSeconds = minValue;
    nagSweepMaxSeconds = maxValue;
    nagSweepConfigVersion++;
}

static inline uint8_t nagSweepMinSecondsValue()
{
    return (uint8_t)nagSweepMinSeconds;
}

static inline uint8_t nagSweepMaxSecondsValue()
{
    return (uint8_t)nagSweepMaxSeconds;
}

/**
 * Adds a configurable random write interval to the existing Nag handler.
 *
 * Production behavior:
 * - First real 0x370 after CAN Write is enabled is sent immediately.
 * - Every completed write chooses a fresh random delay in the configured
 *   1..30 second range (default 5..8 seconds).
 * - Frames received while waiting are observed but never queued or replayed.
 * - At the deadline, the current real frame is used as the echo template.
 * - A keeps the original +1.80 Nm output.
 * - A_V2 chooses a fresh pseudo-random torque endpoint on each actual write;
 *   it no longer runs an independent fixed 2000 ms sweep in production.
 */
struct NagHandler : public NagHandlerBase
{
    bool writeGateWasOpen = false;
    bool nextEchoScheduled = false;
    uint32_t nextEchoAtMs = 0;
    uint32_t lastSweepDelayMs = 0;
    uint32_t intervalRandomSequence = 0;
    uint32_t torqueRandomSequence = 0;
    uint32_t appliedSweepConfigVersion = 0;
    uint32_t appliedRuntimeGateVersion = 0;

#ifdef NATIVE_BUILD
    // Legacy native tests retain their original per-frame behavior. Dedicated
    // sweep tests explicitly enable the production timing layer.
    bool testSweepTimingEnabled = false;
#endif

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

    static uint32_t frameEntropy(const CanFrame &frame)
    {
        uint32_t hash = frame.id ^ (static_cast<uint32_t>(frame.dlc) << 24);
        const uint8_t dlc = frame.dlc <= 8 ? frame.dlc : 8;
        for (uint8_t i = 0; i < dlc; ++i)
            hash = (hash * 16777619u) ^ frame.data[i];
        return hash;
    }

    uint32_t nextRandom(uint32_t salt, uint32_t &sequence)
    {
        sequence++;
        return randomWord(nowMs() ^ salt ^ (sequence * 0x85EBCA6Bu));
    }

    bool sweepTimingActive() const
    {
#ifdef NATIVE_BUILD
        return testSweepTimingEnabled;
#else
        return true;
#endif
    }

    static bool deadlineReached(uint32_t now, uint32_t deadline)
    {
        return static_cast<int32_t>(now - deadline) >= 0;
    }

    void resetWriteSchedule()
    {
        writeGateWasOpen = false;
        nextEchoScheduled = false;
        nextEchoAtMs = 0;
        lastSweepDelayMs = 0;
        appliedSweepConfigVersion = (uint32_t)nagSweepConfigVersion;
    }

    void syncRuntimeGateVersion()
    {
        const uint32_t runtimeVersion = nagKillerRuntime.version();
        if (appliedRuntimeGateVersion == runtimeVersion)
            return;
        resetWriteSchedule();
        appliedRuntimeGateVersion = runtimeVersion;
    }

    void scheduleNextEcho(uint32_t now, const CanFrame &frame)
    {
        uint8_t minSeconds = nagSweepMinSecondsValue();
        uint8_t maxSeconds = nagSweepMaxSecondsValue();
        if (minSeconds > maxSeconds)
            std::swap(minSeconds, maxSeconds);

        const uint32_t minMs = static_cast<uint32_t>(minSeconds) * 1000u;
        const uint32_t maxMs = static_cast<uint32_t>(maxSeconds) * 1000u;
        const uint32_t span = maxMs - minMs;
        const uint32_t word = nextRandom(frameEntropy(frame) ^ 0x4F1BBCDCu,
                                         intervalRandomSequence);
        lastSweepDelayMs = minMs + (span == 0 ? 0 : word % (span + 1u));
        nextEchoAtMs = now + lastSweepDelayMs;
        nextEchoScheduled = true;
        appliedSweepConfigVersion = (uint32_t)nagSweepConfigVersion;
    }

    bool shouldSendNow(uint32_t now, const CanFrame &frame)
    {
        if (!writeGateWasOpen)
        {
            writeGateWasOpen = true;
            appliedSweepConfigVersion = (uint32_t)nagSweepConfigVersion;
            return true;
        }

        if (appliedSweepConfigVersion != (uint32_t)nagSweepConfigVersion)
        {
            scheduleNextEcho(now, frame);
            return false;
        }

        if (!nextEchoScheduled)
        {
            scheduleNextEcho(now, frame);
            return false;
        }

        return deadlineReached(now, nextEchoAtMs);
    }

    void prepareAv2TorqueForWrite(uint32_t now)
    {
        if ((uint8_t)nagMode != MODE_A_V2)
            return;

        // The base implementation returns a deterministic random endpoint at
        // each 2000 ms period boundary. Point it at a new period for each real
        // write, turning A_V2 into one fresh random torque per transmitted frame.
        modeStartMs = now - (torqueRandomSequence * kAv2SweepPeriodMs);
        torqueRandomSequence++;
    }

    void setMode(uint8_t mode)
    {
        const uint8_t previous = (uint8_t)nagMode;
        NagHandlerBase::setMode(mode);
        if (previous != (uint8_t)nagMode)
            torqueRandomSequence = 0;
    }

    void restartModeTimer()
    {
        NagHandlerBase::restartModeTimer();
        torqueRandomSequence = 0;
    }

    void setAv2RangeNm(float minNm, float maxNm)
    {
        NagHandlerBase::setAv2RangeNm(minNm, maxNm);
        torqueRandomSequence = 0;
    }

    void setAv2RangeCentiNm(int16_t minCentiNm, int16_t maxCentiNm)
    {
        NagHandlerBase::setAv2RangeCentiNm(minCentiNm, maxCentiNm);
        torqueRandomSequence = 0;
    }

#ifdef NATIVE_BUILD
    void setTestSweepTimingEnabled(bool enabled)
    {
        testSweepTimingEnabled = enabled;
        resetWriteSchedule();
    }

    uint32_t testLastSweepDelayMs() const { return lastSweepDelayMs; }
    uint32_t testNextEchoAtMs() const { return nextEchoAtMs; }
#endif

    void handleMessage(CanFrame &frame, CanDriver &driver) override
    {
        if (!sweepTimingActive())
        {
            NagHandlerBase::handleMessage(frame, driver);
            return;
        }

        if (onFrame)
            onFrame(frame);

        if (frame.id != 880 || frame.dlc < 8)
            return;

        lastObservedCentiNm = rawToCentiNm(readTorqueRaw(frame));
        syncRuntimeGateVersion();

        if (!nagKillerActive || !nagKillerRuntime)
        {
            resetWriteSchedule();
            return;
        }

        if (isOwnEcho(frame))
        {
            nagOwnEchoSkipCount++;
            return;
        }

        const uint32_t now = nowMs();
        if (!shouldSendNow(now, frame))
            return;

        prepareAv2TorqueForWrite(now);

        // The base handler owns the proven frame mutation, counter, checksum,
        // own-echo fingerprint and send path. Avoid counting this frame twice
        // in the dashboard callback while delegating the actual write.
        void (*savedOnFrame)(const CanFrame &) = onFrame;
        onFrame = nullptr;
        NagHandlerBase::handleMessage(frame, driver);
        onFrame = savedOnFrame;

        scheduleNextEcho(now, frame);
    }
};
