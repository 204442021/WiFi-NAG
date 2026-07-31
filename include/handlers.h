#pragma once

#include <algorithm>
#include <cstdio>
#include "can_frame_types.h"
#include "drivers/can_driver.h"
#include "can_helpers.h"
#include "shared_types.h"
#include "log_buffer.h"

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
    };

    Shared<bool> nagKillerActive{true};
    Shared<uint32_t> nagEchoCount{0};
    Shared<uint32_t> nagTxDropCount{0};
    Shared<uint32_t> nagOwnEchoSkipCount{0};
    Shared<uint8_t> nagMode{MODE_A};
    Shared<int16_t> av2MinCentiNm{150};
    Shared<int16_t> av2MaxCentiNm{180};
    Shared<int16_t> lastObservedCentiNm{0};
    Shared<int16_t> lastInjectedCentiNm{0};

    static constexpr uint32_t kAv2SweepPeriodMs = 2000;
    static constexpr int16_t kTorqueMinCentiNm = -180;
    static constexpr int16_t kTorqueMaxCentiNm = 180;

    uint32_t modeStartMs = 0;
    uint32_t aModeActiveEndsMs = 0; // 0 = BLE-gated A-mode output inactive
    bool hasLastInjected = false;
    uint16_t lastInjectedRaw = 0;
    uint8_t lastInjectedCounter = 0;
    uint8_t lastInjectedByte4 = 0;
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

    static uint16_t readTorqueRaw(const CanFrame &frame)
    {
        return static_cast<uint16_t>(((frame.data[2] & 0x0F) << 8) | frame.data[3]);
    }

    static void writeTorqueRaw(CanFrame &frame, uint16_t raw)
    {
        frame.data[2] = static_cast<uint8_t>((frame.data[2] & 0xF0) | ((raw >> 8) & 0x0F));
        frame.data[3] = static_cast<uint8_t>(raw & 0xFF);
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
        return mode == MODE_A || mode == MODE_A_V2;
    }

    void setMode(uint8_t mode)
    {
        if (!isSupportedMode(mode))
            mode = MODE_A;
        if ((uint8_t)nagMode != mode)
        {
            nagMode = mode;
            modeStartMs = nowMs();
        }
    }

    void restartModeTimer()
    {
        modeStartMs = nowMs();
    }

    // BLE-triggered A-mode activation window. MODE_A is idle unless this
    // window is active; MODE_A_V2 retains its existing manual behavior.
    void triggerAModeWindow(uint32_t windowMs)
    {
        setMode(MODE_A);
        aModeActiveEndsMs = nowMs() + windowMs;
    }

    void cancelAModeWindow()
    {
        aModeActiveEndsMs = 0;
    }

    bool aModeActive() const
    {
        return aModeActiveEndsMs != 0 &&
               static_cast<int32_t>(aModeActiveEndsMs - nowMs()) > 0;
    }

    uint32_t aModeRemainingMs() const
    {
        if (!aModeActive())
            return 0;
        return aModeActiveEndsMs - nowMs();
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
        if (!hasLastInjected)
            return false;
        return readTorqueRaw(frame) == lastInjectedRaw &&
               (frame.data[6] & 0x0F) == lastInjectedCounter &&
               frame.data[4] == lastInjectedByte4;
    }

    void handleMessage(CanFrame &frame, CanDriver &driver) override
    {
        if (onFrame)
            onFrame(frame);

        if (frame.id != 880 || frame.dlc < 8)
            return;

        lastObservedCentiNm = rawToCentiNm(readTorqueRaw(frame));

        if (!nagKillerActive || !nagKillerRuntime)
            return;

        // Fixed A mode is never active by default. A new validated BLE rising
        // edge opens the only permitted time-bounded output window.
        if ((uint8_t)nagMode == MODE_A && !aModeActive())
            return;

        if (isOwnEcho(frame))
        {
            nagOwnEchoSkipCount++;
            return;
        }

        CanFrame echo = frame;
        echo.id = 880;
        echo.dlc = 8;

        const int16_t torqueCentiNm = targetTorqueCentiNm();
        const uint16_t torqueRaw = centiNmToRaw(torqueCentiNm);
        writeTorqueRaw(echo, torqueRaw);

        echo.data[4] = static_cast<uint8_t>((frame.data[4] & 0x3F) | 0x40);

        uint8_t cnt = (frame.data[6] & 0x0F);
        cnt = (cnt + 1) & 0x0F;
        echo.data[6] = (frame.data[6] & 0xF0) | cnt;

        uint16_t sum = echo.data[0] + echo.data[1] + echo.data[2] + echo.data[3] + echo.data[4] + echo.data[5] + echo.data[6];
        echo.data[7] = static_cast<uint8_t>((sum + 0x73) & 0xFF);

        if (!driver.send(echo))
        {
            nagTxDropCount++;
            return;
        }

        framesSent++;
        nagEchoCount++;
        lastInjectedCentiNm = torqueCentiNm;
        lastInjectedRaw = torqueRaw;
        lastInjectedCounter = cnt;
        lastInjectedByte4 = echo.data[4];
        hasLastInjected = true;

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
