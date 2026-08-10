#pragma once

#include <cstdint>

#ifdef NATIVE_BUILD
#include <mutex>
#else
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

#include "ble/bridge_protocol.h"
#include "shared_types.h"

enum BrakeRealGear : uint8_t
{
    BRAKE_GEAR_UNKNOWN = 0,
    BRAKE_GEAR_P = 1,
    BRAKE_GEAR_R = 2,
    BRAKE_GEAR_N = 3,
    BRAKE_GEAR_D = 4,
};

enum BrakeStateReason : uint8_t
{
    BRAKE_REASON_ACTIVE = 0,
    BRAKE_REASON_RELEASE_CONFIRMED = 1,
    BRAKE_REASON_STALE = 2,
    BRAKE_REASON_CONFLICT = 3,
    BRAKE_REASON_FALLBACK = 4,
    BRAKE_REASON_HOLD_PENDING = 5,
    BRAKE_REASON_GEAR_STALE = 6,
    BRAKE_REASON_GEAR_NOT_DR = 7,
    BRAKE_REASON_SPEED_STALE = 8,
    BRAKE_REASON_MOVING = 9,
    BRAKE_REASON_DISABLED = 10,
    BRAKE_REASON_SESSION_RESYNC = 11,
    BRAKE_REASON_UNKNOWN = 0xFF,
};

struct BrakeStateData
{
    bool brakePressed = false;
    bool releaseConfirmed = false;
    bool physicalKnown = false;
    bool physicalFresh = false;
    bool physicalPressed = false;
    bool systemKnown = false;
    bool systemFresh = false;
    bool systemPressed = false;
    BrakeRealGear realGear = BRAKE_GEAR_UNKNOWN;
    bool gearFresh = false;
    bool sourcesAgree = false;
    bool fallbackActive = false;
    bool speedFresh = false;
    bool stationaryConfirmed = false;
    bool drConfirmed = false;
    bool brakeHoldReady = false;
    bool activeEligible = false;
    bool releaseTail = false;
    bool senderEnabled = false;
    bool normalRuntime = false;
    uint8_t gearSource = 0;
    BrakeStateReason reason = BRAKE_REASON_UNKNOWN;
    uint16_t brakeHoldMs = 0;
    int16_t speedDeciKph = 0;
};

inline bool decodeBrakeStatePayload(const uint8_t *payload, BrakeStateData &out)
{
    if (!payload || payload[0] != 0x01U || (payload[3] & 0xC0U) != 0U)
        return false;

    const uint8_t gear = payload[2] & 0x07U;
    if (gear > BRAKE_GEAR_D || payload[4] > 2U ||
        payload[5] > BRAKE_REASON_SESSION_RESYNC)
        return false;

    BrakeStateData decoded;
    decoded.brakePressed = (payload[1] & 0x01U) != 0U;
    decoded.releaseConfirmed = (payload[1] & 0x02U) != 0U;
    decoded.physicalKnown = (payload[1] & 0x04U) != 0U;
    decoded.physicalFresh = (payload[1] & 0x08U) != 0U;
    decoded.physicalPressed = (payload[1] & 0x10U) != 0U;
    decoded.systemKnown = (payload[1] & 0x20U) != 0U;
    decoded.systemFresh = (payload[1] & 0x40U) != 0U;
    decoded.systemPressed = (payload[1] & 0x80U) != 0U;

    decoded.realGear = static_cast<BrakeRealGear>(gear);
    decoded.gearFresh = (payload[2] & 0x08U) != 0U;
    decoded.sourcesAgree = (payload[2] & 0x10U) != 0U;
    decoded.fallbackActive = (payload[2] & 0x20U) != 0U;
    decoded.speedFresh = (payload[2] & 0x40U) != 0U;
    decoded.stationaryConfirmed = (payload[2] & 0x80U) != 0U;

    decoded.drConfirmed = (payload[3] & 0x01U) != 0U;
    decoded.brakeHoldReady = (payload[3] & 0x02U) != 0U;
    decoded.activeEligible = (payload[3] & 0x04U) != 0U;
    decoded.releaseTail = (payload[3] & 0x08U) != 0U;
    decoded.senderEnabled = (payload[3] & 0x10U) != 0U;
    decoded.normalRuntime = (payload[3] & 0x20U) != 0U;
    decoded.gearSource = payload[4];
    decoded.reason = static_cast<BrakeStateReason>(payload[5]);
    decoded.brakeHoldMs = BleBridgeProtocol::readLe16(payload + 6);
    decoded.speedDeciKph = static_cast<int16_t>(
        BleBridgeProtocol::readLe16(payload + 8));

    if (decoded.brakeHoldReady && decoded.brakeHoldMs < 150U)
        return false;
    if (decoded.stationaryConfirmed)
    {
        const int32_t speed = static_cast<int32_t>(decoded.speedDeciKph);
        const int32_t magnitude = speed < 0 ? -speed : speed;
        if (!decoded.speedFresh || magnitude > 2)
            return false;
    }
    if (decoded.gearFresh &&
        (decoded.realGear == BRAKE_GEAR_UNKNOWN || decoded.gearSource == 0U))
        return false;
    if (decoded.realGear == BRAKE_GEAR_UNKNOWN && decoded.gearSource != 0U)
        return false;
    if (decoded.releaseConfirmed && decoded.brakePressed)
        return false;
    if (decoded.brakePressed &&
        (decoded.reason != BRAKE_REASON_ACTIVE || decoded.releaseTail))
        return false;
    if (!decoded.brakePressed && decoded.reason == BRAKE_REASON_ACTIVE)
        return false;
    const bool confirmedReleaseReason =
        decoded.reason == BRAKE_REASON_RELEASE_CONFIRMED ||
        decoded.reason == BRAKE_REASON_SESSION_RESYNC;
    if (decoded.releaseConfirmed &&
        (!confirmedReleaseReason || !decoded.releaseTail))
        return false;
    if (!decoded.releaseConfirmed && confirmedReleaseReason)
        return false;
    if (decoded.releaseConfirmed &&
        !(decoded.physicalKnown && decoded.physicalFresh &&
          !decoded.physicalPressed && decoded.systemKnown &&
          decoded.systemFresh && !decoded.systemPressed &&
          decoded.sourcesAgree && !decoded.fallbackActive))
        return false;
    if (decoded.brakePressed &&
        !(decoded.physicalKnown && decoded.physicalFresh &&
          decoded.physicalPressed && decoded.systemKnown &&
          decoded.systemFresh && decoded.systemPressed &&
          decoded.sourcesAgree && !decoded.fallbackActive &&
          decoded.drConfirmed && decoded.brakeHoldReady &&
          decoded.activeEligible && decoded.senderEnabled &&
          decoded.normalRuntime && decoded.gearFresh &&
          (decoded.realGear == BRAKE_GEAR_D ||
           decoded.realGear == BRAKE_GEAR_R) &&
          decoded.speedFresh && decoded.stationaryConfirmed))
        return false;

    out = decoded;
    return true;
}

struct BrakeStateView
{
    BrakeStateData data;
    uint32_t sessionGeneration = 0;
    uint32_t peerBootId = 0;
    uint32_t sequence = 0;
    uint32_t lastRxMs = 0;
    bool linkReady = false;
    bool capabilitySupported = false;
    bool hasState = false;
};

class BrakeStateMailbox
{
public:
    void beginSession(uint32_t peerBootId, bool supported, uint32_t nowMs)
    {
        (void)nowMs;
        lockWriter();
        const uint32_t start = static_cast<uint32_t>(version_);
        version_ = start + 1U;
        sessionGeneration_ = static_cast<uint32_t>(sessionGeneration_) + 1U;
        peerBootId_ = peerBootId;
        sequence_ = 0;
        lastRxMs_ = 0;
        payload0_ = payload1_ = payload2_ = 0;
        linkReady_ = true;
        capabilitySupported_ = supported;
        hasState_ = false;
        version_ = start + 2U;
        unlockWriter();
    }

    void endSession()
    {
        lockWriter();
        if (!static_cast<bool>(linkReady_) && !static_cast<bool>(hasState_))
        {
            unlockWriter();
            return;
        }

        const uint32_t start = static_cast<uint32_t>(version_);
        version_ = start + 1U;
        sessionGeneration_ = static_cast<uint32_t>(sessionGeneration_) + 1U;
        peerBootId_ = 0;
        sequence_ = 0;
        lastRxMs_ = 0;
        payload0_ = payload1_ = payload2_ = 0;
        linkReady_ = false;
        capabilitySupported_ = false;
        hasState_ = false;
        version_ = start + 2U;
        unlockWriter();
    }

    bool publish(const uint8_t payload[10], uint32_t sequence, uint32_t nowMs)
    {
        BrakeStateData decoded;
        if (!decodeBrakeStatePayload(payload, decoded))
            return false;

        lockWriter();
        if (!static_cast<bool>(linkReady_) ||
            !static_cast<bool>(capabilitySupported_))
        {
            unlockWriter();
            return false;
        }
        const uint32_t start = static_cast<uint32_t>(version_);
        version_ = start + 1U;
        payload0_ = pack4(payload);
        payload1_ = pack4(payload + 4);
        payload2_ = static_cast<uint32_t>(payload[8]) |
                    (static_cast<uint32_t>(payload[9]) << 8);
        sequence_ = sequence;
        lastRxMs_ = nowMs;
        hasState_ = true;
        version_ = start + 2U;
        unlockWriter();
        return true;
    }

    bool read(BrakeStateView &out) const
    {
        for (uint8_t attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t before = static_cast<uint32_t>(version_);
            if (before & 1U)
                continue;

            BrakeStateView candidate;
            candidate.sessionGeneration = static_cast<uint32_t>(sessionGeneration_);
            candidate.peerBootId = static_cast<uint32_t>(peerBootId_);
            candidate.sequence = static_cast<uint32_t>(sequence_);
            candidate.lastRxMs = static_cast<uint32_t>(lastRxMs_);
            candidate.linkReady = static_cast<bool>(linkReady_);
            candidate.capabilitySupported = static_cast<bool>(capabilitySupported_);
            candidate.hasState = static_cast<bool>(hasState_);

            uint8_t payload[10] = {};
            unpack4(static_cast<uint32_t>(payload0_), payload);
            unpack4(static_cast<uint32_t>(payload1_), payload + 4);
            const uint32_t tail = static_cast<uint32_t>(payload2_);
            payload[8] = static_cast<uint8_t>(tail & 0xFFU);
            payload[9] = static_cast<uint8_t>((tail >> 8) & 0xFFU);

            const uint32_t after = static_cast<uint32_t>(version_);
            if (before != after || (after & 1U))
                continue;
            if (candidate.hasState &&
                !decodeBrakeStatePayload(payload, candidate.data))
                return false;
            out = candidate;
            return true;
        }
        return false;
    }

#ifdef NATIVE_BUILD
    void reset()
    {
        lockWriter();
        const uint32_t start = static_cast<uint32_t>(version_);
        version_ = start + 1U;
        sessionGeneration_ = 0;
        peerBootId_ = 0;
        sequence_ = 0;
        lastRxMs_ = 0;
        payload0_ = payload1_ = payload2_ = 0;
        linkReady_ = false;
        capabilitySupported_ = false;
        hasState_ = false;
        version_ = start + 2U;
        unlockWriter();
    }
#endif

private:
    void lockWriter()
    {
#ifdef NATIVE_BUILD
        writerMutex_.lock();
#else
        portENTER_CRITICAL(&writerMux_);
#endif
    }

    void unlockWriter()
    {
#ifdef NATIVE_BUILD
        writerMutex_.unlock();
#else
        portEXIT_CRITICAL(&writerMux_);
#endif
    }

    static uint32_t pack4(const uint8_t *data)
    {
        return static_cast<uint32_t>(data[0]) |
               (static_cast<uint32_t>(data[1]) << 8) |
               (static_cast<uint32_t>(data[2]) << 16) |
               (static_cast<uint32_t>(data[3]) << 24);
    }

    static void unpack4(uint32_t packed, uint8_t *out)
    {
        out[0] = static_cast<uint8_t>(packed & 0xFFU);
        out[1] = static_cast<uint8_t>((packed >> 8) & 0xFFU);
        out[2] = static_cast<uint8_t>((packed >> 16) & 0xFFU);
        out[3] = static_cast<uint8_t>((packed >> 24) & 0xFFU);
    }

    Shared<uint32_t> version_{0};
    Shared<uint32_t> sessionGeneration_{0};
    Shared<uint32_t> peerBootId_{0};
    Shared<uint32_t> sequence_{0};
    Shared<uint32_t> lastRxMs_{0};
    Shared<uint32_t> payload0_{0};
    Shared<uint32_t> payload1_{0};
    Shared<uint32_t> payload2_{0};
    Shared<bool> linkReady_{false};
    Shared<bool> capabilitySupported_{false};
    Shared<bool> hasState_{false};
#ifdef NATIVE_BUILD
    std::mutex writerMutex_;
#else
    portMUX_TYPE writerMux_ = portMUX_INITIALIZER_UNLOCKED;
#endif
};

inline BrakeStateMailbox brakeStateMailbox;
