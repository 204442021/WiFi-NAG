#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "can_helpers.h"
#include "log_buffer.h"
#include "nag_adaptive_controller.h"
#include "nag_adaptive_exchange.h"
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
    Shared<uint32_t> nagCounterCollisionCount{0};
    Shared<uint32_t> nagLastCounterCollisionGapUs{0};
    Shared<uint32_t> nagDasFrameCount{0};
    Shared<uint32_t> nagOemEpasFrameCount{0};
    Shared<uint32_t> nagLastOemEpasAtMs{0};
    Shared<uint8_t> nagLastOemEpasCounter{0};
    Shared<uint8_t> nagMode{MODE_A};
    Shared<int16_t> lastObservedCentiNm{0};
    Shared<int16_t> lastInjectedCentiNm{0};
    Shared<bool> lastInjectedValid{false};
    Shared<uint32_t> lastInjectedAtMs{0};
    NagAdaptiveController adaptiveController;
    NagAdaptiveExchange adaptiveExchange;
    uint32_t lastAppliedAdaptiveCommandGeneration = 0;

    static constexpr uint32_t kInjectedFreshMs = 200;
    static constexpr int16_t kTorqueMinCentiNm = -180;
    static constexpr int16_t kTorqueMaxCentiNm = 180;
    static constexpr int16_t kCorrectiveTorqueMinCentiNm = -200;
    static constexpr int16_t kCorrectiveTorqueMaxCentiNm = 200;
    static constexpr uint32_t kOwnEchoFingerprintLifetimeMs = 100;
    static constexpr uint64_t kCounterCollisionWindowUs = 100000;
    static constexpr uint8_t kRecentEchoCount = 4;

    struct RecentEcho
    {
        CanFrame frame;
        uint32_t sentAtMs = 0;
        bool valid = false;
    };
    RecentEcho recentEchoes[kRecentEchoCount]{};
    uint8_t recentEchoWriteIndex = 0;
    uint8_t lastSuccessfulEchoCounter = 0;
    uint64_t lastSuccessfulEchoAtUs = 0;
    bool lastSuccessfulEchoCounterValid = false;
    Shared<uint8_t> lastOemHandsOnRaw{0};
    Shared<uint8_t> lastOemHandsOnTier{1};
    Shared<int16_t> lastOemSteeringAngleDeciDeg{0};
    bool lastEffectiveRuntimeEnabled = true;
#ifdef NATIVE_BUILD
    bool testClockEnabled = false;
    uint32_t testNowMs = 0;
    bool testUsClockEnabled = false;
    uint64_t testNowUs = 0;
#endif

    const uint32_t *filterIds() const override
    {
        static constexpr uint32_t ids[] = {0x370, 0x39B};
        return ids;
    }

    uint8_t filterIdCount() const override { return 2; }

    static int16_t clampTorqueCentiNm(int16_t v)
    {
        if (v < kTorqueMinCentiNm)
            return kTorqueMinCentiNm;
        if (v > kTorqueMaxCentiNm)
            return kTorqueMaxCentiNm;
        return v;
    }

    static int16_t clampCorrectiveTorqueCentiNm(int16_t v)
    {
        if (v < kCorrectiveTorqueMinCentiNm)
            return kCorrectiveTorqueMinCentiNm;
        if (v > kCorrectiveTorqueMaxCentiNm)
            return kCorrectiveTorqueMaxCentiNm;
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
        centiNm = clampCorrectiveTorqueCentiNm(centiNm);
        return static_cast<uint16_t>(2050 + centiNm);
    }

    static int16_t rawToCentiNm(uint16_t raw)
    {
        return clampCorrectiveTorqueCentiNm(static_cast<int16_t>(raw) - 2050);
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

    uint64_t nowUs() const
    {
#ifdef NATIVE_BUILD
        return testUsClockEnabled ? testNowUs : static_cast<uint64_t>(nowMs()) * 1000ULL;
#else
#ifdef ESP_PLATFORM
        return static_cast<uint64_t>(esp_timer_get_time());
#else
        return static_cast<uint64_t>(micros());
#endif
#endif
    }

#ifdef NATIVE_BUILD
    void setTestNowMs(uint32_t ms)
    {
        testClockEnabled = true;
        testNowMs = ms;
    }


    void setTestNowUs(uint64_t us)
    {
        testUsClockEnabled = true;
        testNowUs = us;
    }
#endif

    static bool isSupportedMode(uint8_t mode)
    {
        return mode == MODE_A || mode == MODE_ADAPTIVE;
    }

    void setMode(uint8_t mode)
    {
        if (!isSupportedMode(mode))
            mode = MODE_A;
        const NagAdaptivePendingCommand desired = adaptiveExchange.desiredCommand();
        if (desired.mode != mode)
            publishAdaptiveCommand(desired.config, mode, true);
    }

    static int16_t clampMagnitudeCentiNm(int16_t value)
    {
        int32_t magnitude = value;
        if (magnitude < 0)
            magnitude = -magnitude;
        if (magnitude < 10)
            return 10;
        if (magnitude > kTorqueMaxCentiNm)
            return kTorqueMaxCentiNm;
        return static_cast<int16_t>(magnitude);
    }

    void setHandsOnRangeCentiNm(uint8_t tier, int8_t sign,
                                int16_t minCentiNm, int16_t maxCentiNm)
    {
        minCentiNm = clampMagnitudeCentiNm(minCentiNm);
        maxCentiNm = clampMagnitudeCentiNm(maxCentiNm);
        if (minCentiNm > maxCentiNm)
            std::swap(minCentiNm, maxCentiNm);
        NagAdaptiveConfig config = adaptiveConfig();
        if (tier == 2)
        {
            if (sign < 0)
            {
                config.correctiveNegativeMinCentiNm = minCentiNm;
                config.correctiveNegativeMaxCentiNm = maxCentiNm;
            }
            else
            {
                config.correctivePositiveMinCentiNm = minCentiNm;
                config.correctivePositiveMaxCentiNm = maxCentiNm;
            }
        }
        else if (sign < 0)
        {
            config.preventiveNegativeMinCentiNm = minCentiNm;
            config.preventiveNegativeMaxCentiNm = maxCentiNm;
        }
        else
        {
            config.preventivePositiveMinCentiNm = minCentiNm;
            config.preventivePositiveMaxCentiNm = maxCentiNm;
        }
        setAdaptiveConfig(config);
    }

    int16_t handsOnRangeMinCenti(uint8_t tier, int8_t sign) const
    {
        const NagAdaptiveConfig config = adaptiveConfig();
        if (tier == 2)
            return sign < 0 ? config.correctiveNegativeMinCentiNm
                            : config.correctivePositiveMinCentiNm;
        return sign < 0 ? config.preventiveNegativeMinCentiNm
                        : config.preventivePositiveMinCentiNm;
    }

    int16_t handsOnRangeMaxCenti(uint8_t tier, int8_t sign) const
    {
        const NagAdaptiveConfig config = adaptiveConfig();
        if (tier == 2)
            return sign < 0 ? config.correctiveNegativeMaxCentiNm
                            : config.correctivePositiveMaxCentiNm;
        return sign < 0 ? config.preventiveNegativeMaxCentiNm
                        : config.preventivePositiveMaxCentiNm;
    }

    int16_t lastObservedCenti() const { return (int16_t)lastObservedCentiNm; }
    float lastObservedNm() const { return centiNmToNm(lastObservedCenti()); }
    int16_t lastInjectedCenti() const { return (int16_t)lastInjectedCentiNm; }
    float lastInjectedNm() const { return centiNmToNm(lastInjectedCenti()); }
    uint32_t lastInjectedAgeMs() const
    {
        if (!static_cast<bool>(lastInjectedValid))
            return 0xFFFFFFFFu;
        return nowMs() - static_cast<uint32_t>(lastInjectedAtMs);
    }
    bool injectedTorqueIsValid() const
    {
        return static_cast<bool>(lastInjectedValid) &&
               static_cast<bool>(nagKillerActive) && nagKillerRuntime &&
               lastInjectedAgeMs() <= kInjectedFreshMs;
    }
    uint8_t handsOnRaw() const { return static_cast<uint8_t>(lastOemHandsOnRaw); }
    uint8_t handsOnTier() const { return static_cast<uint8_t>(lastOemHandsOnTier); }

    static bool adaptiveConfigEqual(const NagAdaptiveConfig &left,
                                    const NagAdaptiveConfig &right)
    {
        return left.maintenanceEnabled == right.maintenanceEnabled &&
               left.preventiveNegativeMinCentiNm == right.preventiveNegativeMinCentiNm &&
               left.preventiveNegativeMaxCentiNm == right.preventiveNegativeMaxCentiNm &&
               left.preventivePositiveMinCentiNm == right.preventivePositiveMinCentiNm &&
               left.preventivePositiveMaxCentiNm == right.preventivePositiveMaxCentiNm &&
               left.correctiveNegativeMinCentiNm == right.correctiveNegativeMinCentiNm &&
               left.correctiveNegativeMaxCentiNm == right.correctiveNegativeMaxCentiNm &&
               left.correctivePositiveMinCentiNm == right.correctivePositiveMinCentiNm &&
               left.correctivePositiveMaxCentiNm == right.correctivePositiveMaxCentiNm &&
               left.activityMinMs == right.activityMinMs &&
               left.activityMaxMs == right.activityMaxMs &&
               left.releaseMinMs == right.releaseMinMs &&
               left.releaseMaxMs == right.releaseMaxMs &&
               left.restMinMs == right.restMinMs &&
               left.restMaxMs == right.restMaxMs &&
               left.correctiveSendMinMs == right.correctiveSendMinMs &&
               left.correctiveSendMaxMs == right.correctiveSendMaxMs &&
               left.correctivePauseMinMs == right.correctivePauseMinMs &&
               left.correctivePauseMaxMs == right.correctivePauseMaxMs &&
               left.correctiveFrameIntervalMs == right.correctiveFrameIntervalMs &&
               left.dasFreshTimeoutMs == right.dasFreshTimeoutMs;
    }

    void publishAdaptiveCommand(const NagAdaptiveConfig &config, uint8_t mode,
                                bool resetRequested)
    {
        if (!isSupportedMode(mode))
            mode = MODE_A;
        adaptiveExchange.publishCommand(NagAdaptiveController::normalizeConfig(config),
                                        mode, resetRequested);
    }

    uint8_t requestedMode() const
    {
        return adaptiveExchange.desiredCommand().mode;
    }

    NagAdaptivePendingCommand desiredAdaptiveCommand() const
    {
        return adaptiveExchange.desiredCommand();
    }

    void consumePendingAdaptiveCommandAtFrameBoundary()
    {
        NagAdaptivePendingCommand command;
        if (!adaptiveExchange.consumeCommand(lastAppliedAdaptiveCommandGeneration, command))
            return;
        const bool configChanged = !adaptiveConfigEqual(adaptiveController.config(), command.config);
        const bool modeChanged = static_cast<uint8_t>(nagMode) != command.mode;
        if (configChanged)
            adaptiveController.setConfig(command.config);
        if (modeChanged)
            nagMode = command.mode;
        if (command.resetRequested && !configChanged)
            adaptiveController.requestReset();
        else if (modeChanged)
            adaptiveController.requestReset();
    }

    void publishAdaptiveSnapshot(uint32_t now)
    {
        adaptiveExchange.publishSnapshot(adaptiveController.snapshot(now));
    }

    void setAdaptiveConfig(const NagAdaptiveConfig &config)
    {
        const NagAdaptivePendingCommand desired = adaptiveExchange.desiredCommand();
        publishAdaptiveCommand(config, desired.mode, true);
    }

    NagAdaptiveConfig adaptiveConfig() const { return adaptiveExchange.desiredCommand().config; }
    NagAdaptiveSnapshot adaptiveSnapshot() const { return adaptiveExchange.readSnapshot(); }
    int16_t adaptiveAngleDeciDeg() const { return static_cast<int16_t>(lastOemSteeringAngleDeciDeg); }
    float adaptiveAngleDeg() const { return static_cast<float>(adaptiveAngleDeciDeg()) / 10.0f; }
    int16_t adaptiveTargetCentiNm() const { return adaptiveSnapshot().targetTorqueCentiNm; }
    float adaptiveTargetNm() const { return centiNmToNm(adaptiveTargetCentiNm()); }
    uint32_t adaptivePhaseRemainingMs() const { return adaptiveSnapshot().phaseRemainingMs; }

    int16_t targetTorqueCentiNm() const
    {
        return kTorqueMaxCentiNm;
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
        const uint32_t now = nowMs();
        consumePendingAdaptiveCommandAtFrameBoundary();
        struct SnapshotPublishGuard
        {
            NagHandler &handler;
            uint32_t now;
            ~SnapshotPublishGuard() { handler.publishAdaptiveSnapshot(now); }
        } snapshotPublishGuard{*this, now};

        if (onFrame)
            onFrame(frame);
        const bool effectiveRuntimeEnabled = static_cast<bool>(nagKillerActive) &&
                                             nagKillerRuntime;
        if (effectiveRuntimeEnabled && !lastEffectiveRuntimeEnabled &&
            static_cast<uint8_t>(nagMode) == MODE_ADAPTIVE)
            adaptiveController.requestReset();
        lastEffectiveRuntimeEnabled = effectiveRuntimeEnabled;

        if (frame.id == NagDasFeedbackTracker::kDasCanId)
        {
            if (frame.dlc >= 8)
            {
                nagDasFrameCount++;
                adaptiveController.observeDas(frame, now);
            }
            return;
        }

        if (frame.id != 0x370 || frame.dlc < 8)
            return;

        if (isOwnEcho(frame))
        {
            nagOwnEchoSkipCount++;
            return;
        }

        const bool adaptiveMode = static_cast<uint8_t>(nagMode) == MODE_ADAPTIVE;
        if (adaptiveMode && !verifyChecksum(frame))
        {
            nagChecksumRejectCount++;
            return;
        }

        const uint16_t observedTorqueRaw = readTorqueRaw(frame);
        if (adaptiveMode && isReservedTorqueRaw(observedTorqueRaw))
        {
            nagInvalidTorqueRejectCount++;
            return;
        }

        const uint64_t receivedAtUs = nowUs();
        const uint8_t oemCounter = static_cast<uint8_t>(frame.data[6] & 0x0F);
        if (lastSuccessfulEchoCounterValid)
        {
            const uint64_t gapUs = receivedAtUs - lastSuccessfulEchoAtUs;
            if (oemCounter == lastSuccessfulEchoCounter &&
                gapUs >= 1 && gapUs <= kCounterCollisionWindowUs)
            {
                nagCounterCollisionCount++;
                nagLastCounterCollisionGapUs = static_cast<uint32_t>(gapUs);
            }
            lastSuccessfulEchoCounterValid = false;
        }
        nagOemEpasFrameCount++;
        nagLastOemEpasAtMs = now;
        nagLastOemEpasCounter = oemCounter;
        lastObservedCentiNm = rawToObservedCentiNm(observedTorqueRaw);
        const uint8_t rawHandsOn = static_cast<uint8_t>((frame.data[4] >> 6) & 0x03);
        lastOemHandsOnRaw = rawHandsOn;
        if (rawHandsOn == 1 || rawHandsOn == 2)
            lastOemHandsOnTier = rawHandsOn;
        lastOemSteeringAngleDeciDeg = readSteeringAngleDeciDeg(frame);

        if (!effectiveRuntimeEnabled)
        {
            adaptiveController.disable(now);
            return;
        }

        int16_t torqueCentiNm = targetTorqueCentiNm();
        NagAdaptiveDecision decision;
        if (adaptiveMode)
        {
            decision = adaptiveController.observeEpas(
                now,
                lastOemSteeringAngleDeciDeg,
                rawToObservedCentiNm(observedTorqueRaw),
                frameEntropy(frame));
            if (!decision.shouldSend)
                return;
            torqueCentiNm = decision.targetTorqueCentiNm;
        }
        else
        {
            adaptiveController.disable(now);
        }

        CanFrame echo = frame;
        echo.id = 880;
        echo.dlc = 8;

        torqueCentiNm = adaptiveMode && decision.corrective
                            ? clampCorrectiveTorqueCentiNm(torqueCentiNm)
                            : clampTorqueCentiNm(torqueCentiNm);
        const uint16_t torqueRaw = centiNmToRaw(torqueCentiNm);
        writeTorqueRaw(echo, torqueRaw);

        echo.data[4] = static_cast<uint8_t>((frame.data[4] & 0x3F) | 0x40);

        uint8_t cnt = (frame.data[6] & 0x0F);
        cnt = (cnt + 1) & 0x0F;
        echo.data[6] = (frame.data[6] & 0xF0) | cnt;

        uint16_t sum = echo.data[0] + echo.data[1] + echo.data[2] + echo.data[3] + echo.data[4] + echo.data[5] + echo.data[6];
        echo.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);

        nagSendAttemptCount++;
        const bool sent = driver.send(echo);
        adaptiveController.onTransmitResult(now, decision, sent);
        if (sent)
        {
            framesSent++;
            nagEchoCount++;
            lastInjectedCentiNm = torqueCentiNm;
            lastInjectedAtMs = now;
            lastInjectedValid = true;
            rememberSuccessfulEcho(echo);
            lastSuccessfulEchoCounter = cnt;
            lastSuccessfulEchoAtUs = nowUs();
            lastSuccessfulEchoCounterValid = true;
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
