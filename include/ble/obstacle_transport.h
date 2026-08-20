#pragma once

#include <cstdint>

enum ObstacleTransportGear : uint8_t
{
    OBSTACLE_TRANSPORT_GEAR_UNKNOWN = 0,
    OBSTACLE_TRANSPORT_GEAR_P = 1,
    OBSTACLE_TRANSPORT_GEAR_R = 2,
    OBSTACLE_TRANSPORT_GEAR_N = 3,
    OBSTACLE_TRANSPORT_GEAR_D = 4,
};

enum ObstacleTransportReason : uint8_t
{
    OBSTACLE_TRANSPORT_REASON_NONE = 0,
    OBSTACLE_TRANSPORT_REASON_D = 1,
    OBSTACLE_TRANSPORT_REASON_R = 2,
};

enum ObstacleTransportUpdate : uint8_t
{
    OBSTACLE_TRANSPORT_NO_CHANGE = 0,
    OBSTACLE_TRANSPORT_CHANGED,
    OBSTACLE_TRANSPORT_TIMED_OUT,
    OBSTACLE_TRANSPORT_REJECTED,
};

struct ObstacleTransportView
{
    bool supported = false;
    bool valid = false;
    bool paused = false;
    bool gearFresh = false;
    ObstacleTransportGear realGear = OBSTACLE_TRANSPORT_GEAR_UNKNOWN;
    ObstacleTransportReason reason = OBSTACLE_TRANSPORT_REASON_NONE;
    uint32_t stateGeneration = 0;
    uint32_t sessionGeneration = 0;
    uint32_t lastRxAgeMs = UINT32_MAX;
    uint32_t badPayloadCount = 0;
    uint32_t timeoutCount = 0;
};

class ObstacleTransportController
{
public:
    static constexpr uint32_t kWatchdogMs = 3000;

    void beginSession(bool supported, uint32_t nowMs)
    {
        supported_ = supported;
        valid_ = false;
        paused_ = false;
        gearFresh_ = false;
        realGear_ = OBSTACLE_TRANSPORT_GEAR_UNKNOWN;
        reason_ = OBSTACLE_TRANSPORT_REASON_NONE;
        stateGeneration_ = 0;
        lastRxMs_ = nowMs;
        sessionGeneration_ = nextNonZero(sessionGeneration_);
    }

    void endSession()
    {
        supported_ = false;
        valid_ = false;
        paused_ = false;
        gearFresh_ = false;
        realGear_ = OBSTACLE_TRANSPORT_GEAR_UNKNOWN;
        reason_ = OBSTACLE_TRANSPORT_REASON_NONE;
        stateGeneration_ = 0;
        lastRxMs_ = 0;
        sessionGeneration_ = nextNonZero(sessionGeneration_);
    }

    ObstacleTransportUpdate ingest(const uint8_t payload[10],
                                   uint32_t nowMs)
    {
        Decoded decoded{};
        if (!supported_ || !decode(payload, decoded))
        {
            ++badPayloadCount_;
            return OBSTACLE_TRANSPORT_REJECTED;
        }
        if (valid_ && decoded.stateGeneration != stateGeneration_ &&
            static_cast<int32_t>(decoded.stateGeneration - stateGeneration_) <= 0)
        {
            ++badPayloadCount_;
            return OBSTACLE_TRANSPORT_REJECTED;
        }
        if (valid_ && decoded.stateGeneration == stateGeneration_ &&
            (paused_ != decoded.paused ||
             gearFresh_ != decoded.gearFresh ||
             realGear_ != decoded.realGear ||
             reason_ != decoded.reason))
        {
            ++badPayloadCount_;
            return OBSTACLE_TRANSPORT_REJECTED;
        }
        const bool changed = !valid_ || paused_ != decoded.paused ||
                             gearFresh_ != decoded.gearFresh ||
                             realGear_ != decoded.realGear ||
                             reason_ != decoded.reason ||
                             stateGeneration_ != decoded.stateGeneration;
        valid_ = true;
        paused_ = decoded.paused;
        gearFresh_ = decoded.gearFresh;
        realGear_ = decoded.realGear;
        reason_ = decoded.reason;
        stateGeneration_ = decoded.stateGeneration;
        lastRxMs_ = nowMs;
        return changed ? OBSTACLE_TRANSPORT_CHANGED
                       : OBSTACLE_TRANSPORT_NO_CHANGE;
    }

    ObstacleTransportUpdate service(uint32_t nowMs)
    {
        if (!supported_ || !valid_ ||
            static_cast<uint32_t>(nowMs - lastRxMs_) < kWatchdogMs)
            return OBSTACLE_TRANSPORT_NO_CHANGE;
        valid_ = false;
        paused_ = false;
        gearFresh_ = false;
        realGear_ = OBSTACLE_TRANSPORT_GEAR_UNKNOWN;
        reason_ = OBSTACLE_TRANSPORT_REASON_NONE;
        ++timeoutCount_;
        return OBSTACLE_TRANSPORT_TIMED_OUT;
    }

    ObstacleTransportView view(uint32_t nowMs) const
    {
        ObstacleTransportView out;
        out.supported = supported_;
        out.valid = valid_;
        out.paused = valid_ && paused_;
        out.gearFresh = valid_ && gearFresh_;
        out.realGear = valid_ ? realGear_ : OBSTACLE_TRANSPORT_GEAR_UNKNOWN;
        out.reason = valid_ ? reason_ : OBSTACLE_TRANSPORT_REASON_NONE;
        out.stateGeneration = stateGeneration_;
        out.sessionGeneration = sessionGeneration_;
        out.lastRxAgeMs = valid_
                              ? static_cast<uint32_t>(nowMs - lastRxMs_)
                              : UINT32_MAX;
        out.badPayloadCount = badPayloadCount_;
        out.timeoutCount = timeoutCount_;
        return out;
    }

private:
    struct Decoded
    {
        bool paused = false;
        bool gearFresh = false;
        ObstacleTransportGear realGear = OBSTACLE_TRANSPORT_GEAR_UNKNOWN;
        ObstacleTransportReason reason = OBSTACLE_TRANSPORT_REASON_NONE;
        uint32_t stateGeneration = 0;
    };

    static uint32_t readLe32(const uint8_t *p)
    {
        return static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    }

    static bool decode(const uint8_t payload[10], Decoded &out)
    {
        if (!payload || payload[0] != 1 || (payload[1] & 0xFCU) != 0 ||
            payload[2] > OBSTACLE_TRANSPORT_GEAR_D ||
            payload[3] > OBSTACLE_TRANSPORT_REASON_R ||
            payload[8] != 0 || payload[9] != 0)
            return false;
        out.paused = (payload[1] & 0x01U) != 0;
        out.gearFresh = (payload[1] & 0x02U) != 0;
        out.realGear = static_cast<ObstacleTransportGear>(payload[2]);
        out.reason = static_cast<ObstacleTransportReason>(payload[3]);
        out.stateGeneration = readLe32(payload + 4);
        if (out.stateGeneration == 0)
            return false;
        if (!out.paused)
            return out.reason == OBSTACLE_TRANSPORT_REASON_NONE;
        if (!out.gearFresh)
            return false;
        return (out.realGear == OBSTACLE_TRANSPORT_GEAR_D &&
                out.reason == OBSTACLE_TRANSPORT_REASON_D) ||
               (out.realGear == OBSTACLE_TRANSPORT_GEAR_R &&
                out.reason == OBSTACLE_TRANSPORT_REASON_R);
    }

    static uint32_t nextNonZero(uint32_t value)
    {
        const uint32_t next = static_cast<uint32_t>(value + 1U);
        return next == 0 ? 1U : next;
    }

    bool supported_ = false;
    bool valid_ = false;
    bool paused_ = false;
    bool gearFresh_ = false;
    ObstacleTransportGear realGear_ = OBSTACLE_TRANSPORT_GEAR_UNKNOWN;
    ObstacleTransportReason reason_ = OBSTACLE_TRANSPORT_REASON_NONE;
    uint32_t stateGeneration_ = 0;
    uint32_t sessionGeneration_ = 0;
    uint32_t lastRxMs_ = 0;
    uint32_t badPayloadCount_ = 0;
    uint32_t timeoutCount_ = 0;
};

inline ObstacleTransportController obstacleTransportController;
