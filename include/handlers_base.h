#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "can_helpers.h"
#include "log_buffer.h"
#include "nag_adaptive_controller.h"
#include "shared_types.h"

#ifndef NATIVE_BUILD
#ifdef ESP_PLATFORM
#include "platform/espidf_runtime.h"
#else
#include <Arduino.h>
#endif
#endif

inline LogRingBuffer logRing;

static inline bool framePayloadChanged(const CanFrame &original, const CanFrame &modified)
{
    if (original.id != modified.id || original.dlc != modified.dlc)
        return true;

    const uint8_t dlc = (original.dlc <= 8) ? original.dlc : 8;
    for (uint8_t i = 0; i < dlc; ++i)
    {
        if (original.data[i] != modified.data[i])
            return true;
    }
    return false;
}

struct CarManagerBase
{
    Shared<bool> enablePrint{false};
    Shared<uint32_t> frameCount{0};
    Shared<uint32_t> framesSent{0};

    void (*onFrame)(const CanFrame &) = nullptr;

    virtual void handleMessage(CanFrame &frame, CanDriver &driver) = 0;
    virtual const uint32_t *filterIds() const = 0;
    virtual uint8_t filterIdCount() const = 0;
    virtual ~CarManagerBase() = default;
};

/**
 * NagHandler - Autosteer nag suppression (counter+1 echo method)
 *
 * - Listens for CAN 880 (0x370) = EPAS3P_sysStatus
 * - Copies the real frame, writes a small torsionBarTorque echo, increments
 *   the low-nibble counter, and recalculates checksum byte 7.
 * - Echo transmission is gated by nagKillerRuntime, which the WebUI ties to
 *   the CAN Write switch for WIFI-NAG builds.
 */
struct NagHandler : public CarManagerBase
{
    enum Mode : uint8_t
    {
        MODE_A = 0,
        MODE_A_V2 = 4,
        MODE_ADAPTIVE = 5,
    };

    Shared<bool> nagKillerActive{true};
    Shared<uint32_t> nagEchoCount{0};
    Shared<uint32_t> nagSendAttemptCount{0};
    Shared<uint32_t> nagSendFailureCount{0};
    Shared<uint32_t> nagOwnEchoSkipCount{0};
    Shared<uint32_t> nagChecksumRejectCount{0};
    Shared<uint32_t> nagInvalidTorqueRejectCount{0};
    Shared<uint8_t> nagMode{MODE_A};
    Shared<int16_t> av2MinCentiNm{150};
    Shared<int16_t> av2MaxCentiNm{180};
    Shared<int16_t> lastObservedCentiNm{0};
    Shared<int16_t> lastInjectedCentiNm{0};
    NagAdaptiveController adaptiveController;

    static constexpr uint32_t kAv2SweepPeriodMs = 2000;
    static constexpr int16_t kTorqueMinCentiNm = -180;
    static constexpr int16_t kTorqueMaxCentiNm = 180;
    static constexpr uint32_t kOwnEchoFingerprintLifetimeMs = 100;
    static constexpr uint8_t kRecentEchoCount = 4;

    uint32_t modeStartMs = 0;
    struct RecentEcho
    {
        CanFrame frame;
        uint32_t sentAtMs = 0;
        bool valid = false;
    };
    RecentEcho recentEchoes[kRecentEchoCount]{};
    uint8_t recentEchoWriteIndex = 0;
#ifdef NATIVE_BUILD
    bool testClockEnabled = false;
    uint32_t testNowMs = 0;
#endif

    const uint32_t *filterIds() const override
    {
        static constexpr uint32_t ids[] = {880};
        return ids;
    }

    uint8_t filterIdCount() const override { return 1; }

    static int16_t clampTorqueCentiNm(int16_t v)
    {
        if (v < kTorqueMinCentiNm)
            return kTorqueMinCentiNm;
        if (v > kTorqueMaxCentiNm)
            return kTorqueMaxCentiNm;
        return v;
    }

    static int16_t nmToCentiNm(float nm)
    {
        float centi = nm * 100.0f;
        int16_t rounded = static_cast<int16_t>(centi >= 0.0f ? centi + 0.5f : centi - 0.5f);
        return clampTorqueCentiNm(rounded);
    }

    static float centiNmToNm(int16_t centiNm)
    {
        return static_cast<float>(centiNm) / 100.0f;
    }

    static uint16_t centiNmToRaw(int16_t centiNm)
    {
        centiNm = clampTorqueCentiNm(centiNm);
        return static_cast<uint16_t>(2050 + centiNm);
    }

    static int16_t rawToCentiNm(uint16_t raw)
    {
        return clampTorqueCentiNm(static_cast<int16_t>(raw) - 2050);
    }

    static int16_t rawToObservedCentiNm(uint16_t raw)
    {
        return static_cast<int16_t>(raw) - 2050;
    }

    static uint16_t readTorqueRaw(const CanFrame &frame)
    {
        return static_cast<uint16_t>(((frame.data[2] & 0x0F) << 8) | frame.data[3]);
    }

    static void writeTorqueRaw(CanFrame &frame, uint16_t raw)
    {
        frame.data[2] = static_cast<uint8_t>((frame.data[2] & 0xF0) | ((raw >> 8) & 0x0F));
        frame.data[3] = static_cast<uint8_t>(raw & 0xFF);
    }

    static bool isReservedTorqueRaw(uint16_t raw)
    {
        return raw == 0 || raw >= 4094;
    }

    static uint16_t readSteeringAngleRaw(const CanFrame &frame)
    {
        return static_cast<uint16_t>(((frame.data[4] & 0x3F) << 8) | frame.data[5]);
    }

    static int16_t rawToSteeringAngleDeciDeg(uint16_t raw)
    {
        return static_cast<int16_t>(raw) - 8192;
    }

    static int16_t readSteeringAngleDeciDeg(const CanFrame &frame)
    {
        return rawToSteeringAngleDeciDeg(readSteeringAngleRaw(frame));
    }

    static void writeSteeringAngleDeciDeg(CanFrame &frame, int16_t angleDeciDeg)
    {
        if (angleDeciDeg < -8192)
            angleDeciDeg = -8192;
        if (angleDeciDeg > 8191)
            angleDeciDeg = 8191;
        const uint16_t raw = static_cast<uint16_t>(static_cast<int32_t>(angleDeciDeg) + 8192);
        frame.data[4] = static_cast<uint8_t>((frame.data[4] & 0xC0) | ((raw >> 8) & 0x3F));
        frame.data[5] = static_cast<uint8_t>(raw & 0xFF);
    }

    static bool verifyChecksum(const CanFrame &frame)
    {
        if (frame.dlc < 8)
            return false;
        uint16_t sum = 0;
        for (uint8_t index = 0; index < 7; ++index)
            sum += frame.data[index];
        return frame.data[7] == static_cast<uint8_t>((sum + 0x73) & 0xFF);
    }

    static uint32_t frameEntropy(const CanFrame &frame)
    {
        uint32_t hash = 2166136261u ^ frame.id;
        for (uint8_t index = 0; index < 8; ++index)
            hash = (hash ^ frame.data[index]) * 16777619u;
        return hash;
    }

    uint32_t nowMs() const
    {
#ifdef NATIVE_BUILD
        return testClockEnabled ? testNowMs : 0;
#else
        return millis();
#endif
    }

#ifdef NATIVE_BUILD
    void setTestNowMs(uint32_t ms)
    {
        testClockEnabled = true;
        testNowMs = ms;
    }
#endif

    static bool isSupportedMode(uint8_t mode)
    {
        return mode == MODE_A || mode == MODE_A_V2 || mode == MODE_ADAPTIVE;
    }

    void setMode(uint8_t mode)
    {
        if (!isSupportedMode(mode))
            mode = MODE_A;
        if ((uint8_t)nagMode != mode)
        {
            nagMode = mode;
            modeStartMs = nowMs();
            adaptiveController.requestReset();
        }
    }

    void restartModeTimer()
    {
        modeStartMs = nowMs();
    }

    void setAv2RangeNm(float minNm, float maxNm)
    {
        setAv2RangeCentiNm(nmToCentiNm(minNm), nmToCentiNm(maxNm));
    }

    void setAv2RangeCentiNm(int16_t minCentiNm, int16_t maxCentiNm)
    {
        minCentiNm = clampTorqueCentiNm(minCentiNm);
        maxCentiNm = clampTorqueCentiNm(maxCentiNm);
        if (minCentiNm > maxCentiNm)
            std::swap(minCentiNm, maxCentiNm);
        av2MinCentiNm = minCentiNm;
        av2MaxCentiNm = maxCentiNm;
    }

    int16_t av2MinCenti() const { return (int16_t)av2MinCentiNm; }
    int16_t av2MaxCenti() const { return (int16_t)av2MaxCentiNm; }
    float av2MinNm() const { return centiNmToNm(av2MinCenti()); }
    float av2MaxNm() const { return centiNmToNm(av2MaxCenti()); }
    int16_t lastObservedCenti() const { return (int16_t)lastObservedCentiNm; }
    float lastObservedNm() const { return centiNmToNm(lastObservedCenti()); }
    int16_t lastInjectedCenti() const { return (int16_t)lastInjectedCentiNm; }
    float lastInjectedNm() const { return centiNmToNm(lastInjectedCenti()); }

    void setAdaptiveConfig(const NagAdaptiveConfig &config)
    {
        adaptiveController.setConfig(config);
    }

    NagAdaptiveConfig adaptiveConfig() const { return adaptiveController.config(); }
    int16_t adaptiveAngleDeciDeg() const { return adaptiveController.lastAngleDeciDeg(); }
    float adaptiveAngleDeg() const { return static_cast<float>(adaptiveAngleDeciDeg()) / 10.0f; }
    int16_t adaptiveTargetCentiNm() const { return adaptiveController.targetTorqueCentiNm(); }
    float adaptiveTargetNm() const { return centiNmToNm(adaptiveTargetCentiNm()); }
    uint32_t adaptivePhaseRemainingMs() const { return adaptiveController.phaseRemainingMs(nowMs()); }

    static uint32_t av2RandomWord(uint32_t period)
    {
        uint32_t x = period + 0x9E3779B9u;
        x ^= x >> 16;
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        return x;
    }

    int16_t av2RandomEndpointCentiNm(uint32_t period) const
    {
        const int16_t minNm = av2MinCenti();
        const int16_t maxNm = av2MaxCenti();
        const uint16_t span = static_cast<uint16_t>(maxNm - minNm);
        if (span == 0)
            return minNm;

        return static_cast<int16_t>(minNm + static_cast<int16_t>(av2RandomWord(period) % (static_cast<uint32_t>(span) + 1)));
    }

    int16_t randomSweepCentiNm(uint32_t elapsedMs) const
    {
        const uint32_t phase = elapsedMs % kAv2SweepPeriodMs;
        const uint32_t period = elapsedMs / kAv2SweepPeriodMs;
        const int16_t start = av2RandomEndpointCentiNm(period);
        const int16_t end = av2RandomEndpointCentiNm(period + 1);
        const int32_t delta = static_cast<int32_t>(end) - static_cast<int32_t>(start);
        return clampTorqueCentiNm(static_cast<int16_t>(static_cast<int32_t>(start) +
                                                       (delta * static_cast<int32_t>(phase)) /
                                                           static_cast<int32_t>(kAv2SweepPeriodMs)));
    }

    int16_t targetTorqueCentiNm() const
    {
        if ((uint8_t)nagMode != MODE_A_V2)
            return kTorqueMaxCentiNm;

        return randomSweepCentiNm(nowMs() - modeStartMs);
    }

    bool isOwnEcho(const CanFrame &frame) const
    {
        const uint32_t now = nowMs();
        for (uint8_t index = 0; index < kRecentEchoCount; ++index)
        {
            const RecentEcho &candidate = recentEchoes[index];
            if (!candidate.valid || now - candidate.sentAtMs > kOwnEchoFingerprintLifetimeMs)
                continue;
            if (candidate.frame.id == frame.id &&
                candidate.frame.dlc == frame.dlc &&
                std::memcmp(candidate.frame.data, frame.data, 8) == 0)
                return true;
        }
        return false;
    }

    void rememberSuccessfulEcho(const CanFrame &frame)
    {
        RecentEcho &slot = recentEchoes[recentEchoWriteIndex];
        slot.frame = frame;
        slot.sentAtMs = nowMs();
        slot.valid = true;
        recentEchoWriteIndex = static_cast<uint8_t>((recentEchoWriteIndex + 1U) % kRecentEchoCount);
    }

    void handleMessage(CanFrame &frame, CanDriver &driver) override
    {
        if (onFrame)
            onFrame(frame);

        if (frame.id != 880 || frame.dlc < 8)
            return;

        const bool adaptiveMode = static_cast<uint8_t>(nagMode) == MODE_ADAPTIVE;
        if (adaptiveMode && !verifyChecksum(frame))
        {
            nagChecksumRejectCount++;
            return;
        }

        if (isOwnEcho(frame))
        {
            nagOwnEchoSkipCount++;
            return;
        }

        const uint16_t observedTorqueRaw = readTorqueRaw(frame);
        lastObservedCentiNm = rawToObservedCentiNm(observedTorqueRaw);

        if (!nagKillerActive || !nagKillerRuntime)
        {
            adaptiveController.disable(nowMs());
            return;
        }

        int16_t torqueCentiNm = targetTorqueCentiNm();
        if (adaptiveMode)
        {
            if (isReservedTorqueRaw(observedTorqueRaw))
            {
                nagInvalidTorqueRejectCount++;
                return;
            }
            const NagAdaptiveDecision decision = adaptiveController.observe(
                nowMs(),
                readSteeringAngleDeciDeg(frame),
                rawToObservedCentiNm(observedTorqueRaw),
                frameEntropy(frame));
            if (!decision.shouldSend)
                return;
            torqueCentiNm = decision.targetTorqueCentiNm;
        }
        else
        {
            adaptiveController.disable(nowMs());
        }

        CanFrame echo = frame;
        echo.id = 880;
        echo.dlc = 8;

        const uint16_t torqueRaw = centiNmToRaw(torqueCentiNm);
        writeTorqueRaw(echo, torqueRaw);

        echo.data[4] = static_cast<uint8_t>((frame.data[4] & 0x3F) | 0x40);

        uint8_t cnt = (frame.data[6] & 0x0F);
        cnt = (cnt + 1) & 0x0F;
        echo.data[6] = (frame.data[6] & 0xF0) | cnt;

        uint16_t sum = echo.data[0] + echo.data[1] + echo.data[2] + echo.data[3] + echo.data[4] + echo.data[5] + echo.data[6];
        echo.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);

        nagSendAttemptCount++;
        if (driver.send(echo))
        {
            framesSent++;
            nagEchoCount++;
            lastInjectedCentiNm = torqueCentiNm;
            rememberSuccessfulEcho(echo);
        }
        else
        {
            nagSendFailureCount++;
        }

        if (enablePrint && (nagEchoCount % 500 == 1))
        {
            char buf[LogRingBuffer::kMaxMsgLen];
            snprintf(buf, sizeof(buf), "NagHandler: echo=%u",
                     (unsigned int)(uint32_t)nagEchoCount);
            logRing.push(buf,
#ifndef NATIVE_BUILD
                         millis()
#else
                         0
#endif
            );
#ifndef NATIVE_BUILD
            Serial.println(buf);
#endif
        }
    }
};
