#pragma once

#include <cstdint>
#include "can_frame_types.h"

enum class NagDasHandsOnState : uint8_t
{
    NOT_REQUIRED = 0,
    REQUIRED_DETECTED = 1,
    REQUIRED_NOT_DETECTED = 2,
    VISUAL = 3,
    CHIME_1 = 4,
    CHIME_2 = 5,
    SLOWING = 6,
    STRUCK_OUT = 7,
    SUSPENDED = 8,
    SNA = 15,
};

enum class NagDasClass : uint8_t
{
    NORMAL = 0,
    CORRECTIVE = 1,
    BLOCKED = 2,
};

class NagDasFeedbackTracker
{
public:
    static constexpr uint32_t kDasCanId = 0x39B;

    static uint8_t readRaw(const CanFrame &frame)
    {
        return static_cast<uint8_t>((frame.data[5] >> 2) & 0x0F);
    }

    bool observe(const CanFrame &frame, uint32_t nowMs)
    {
        if (frame.id != kDasCanId || frame.dlc < 8)
            return false;
        raw_ = readRaw(frame);
        lastAtMs_ = nowMs;
        seen_ = true;
        valid_ = raw_ <= 8;
        if (raw_ == 15)
            valid_ = false;
        class_ = raw_ <= 1 ? NagDasClass::NORMAL
                           : (raw_ <= 7 ? NagDasClass::CORRECTIVE
                                        : NagDasClass::BLOCKED);
        return valid_;
    }

    bool fresh(uint32_t nowMs, uint32_t timeoutMs) const
    {
        return seen_ && valid_ && static_cast<uint32_t>(nowMs - lastAtMs_) <= timeoutMs;
    }

    bool seen() const { return seen_; }
    bool valid() const { return valid_; }
    uint8_t raw() const { return raw_; }
    uint32_t ageMs(uint32_t nowMs) const
    {
        return seen_ ? static_cast<uint32_t>(nowMs - lastAtMs_) : 0xFFFFFFFFu;
    }
    NagDasClass classification() const { return class_; }

private:
    bool seen_ = false;
    bool valid_ = false;
    uint8_t raw_ = 15;
    uint32_t lastAtMs_ = 0;
    NagDasClass class_ = NagDasClass::BLOCKED;
};
