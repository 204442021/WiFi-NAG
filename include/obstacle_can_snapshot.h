#pragma once

#include <cstdint>

#include "can_frame_types.h"
#include "shared_types.h"

struct ObstacleCanView
{
    uint8_t raw255[4] = {};
    uint8_t raw12B[4] = {};
    uint32_t last255RxMs = 0;
    uint32_t last12BRxMs = 0;
    uint32_t generation255 = 0;
    uint32_t generation12B = 0;
    bool has255 = false;
    bool has12B = false;
    bool dlc255Valid = false;
    bool dlc12BValid = false;
};

class ObstacleCanSnapshot
{
public:
    void observe(const CanFrame &frame, uint32_t nowMs)
    {
        if (frame.id != 0x255 && frame.id != 0x12B)
            return;

        // Single CAN-task writer. Every published field is atomic; the odd/even
        // generation lets the BLE task retry rather than wait on a lock.
        const uint32_t start = static_cast<uint32_t>(version_);
        version_ = start + 1U;

        const bool valid = frame.dlc == 4;
        const uint32_t packed = valid ? pack(frame.data) : 0U;
        if (frame.id == 0x255)
        {
            raw255_ = packed;
            last255RxMs_ = nowMs;
            generation255_ = static_cast<uint32_t>(generation255_) + 1U;
            has255_ = true;
            dlc255Valid_ = valid;
        }
        else
        {
            raw12B_ = packed;
            last12BRxMs_ = nowMs;
            generation12B_ = static_cast<uint32_t>(generation12B_) + 1U;
            has12B_ = true;
            dlc12BValid_ = valid;
        }

        version_ = start + 2U;
    }

    bool read(ObstacleCanView &out) const
    {
        for (uint8_t attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t before = static_cast<uint32_t>(version_);
            if (before & 1U)
                continue;

            ObstacleCanView candidate;
            unpack(static_cast<uint32_t>(raw255_), candidate.raw255);
            unpack(static_cast<uint32_t>(raw12B_), candidate.raw12B);
            candidate.last255RxMs = static_cast<uint32_t>(last255RxMs_);
            candidate.last12BRxMs = static_cast<uint32_t>(last12BRxMs_);
            candidate.generation255 = static_cast<uint32_t>(generation255_);
            candidate.generation12B = static_cast<uint32_t>(generation12B_);
            candidate.has255 = static_cast<bool>(has255_);
            candidate.has12B = static_cast<bool>(has12B_);
            candidate.dlc255Valid = static_cast<bool>(dlc255Valid_);
            candidate.dlc12BValid = static_cast<bool>(dlc12BValid_);

            const uint32_t after = static_cast<uint32_t>(version_);
            if (before == after && !(after & 1U))
            {
                out = candidate;
                return true;
            }
        }
        return false;
    }

#ifdef NATIVE_BUILD
    void reset()
    {
        version_ = 1;
        raw255_ = 0;
        raw12B_ = 0;
        last255RxMs_ = 0;
        last12BRxMs_ = 0;
        generation255_ = 0;
        generation12B_ = 0;
        has255_ = false;
        has12B_ = false;
        dlc255Valid_ = false;
        dlc12BValid_ = false;
        version_ = 2;
    }
#endif

private:
    static uint32_t pack(const uint8_t data[8])
    {
        return static_cast<uint32_t>(data[0]) |
               (static_cast<uint32_t>(data[1]) << 8) |
               (static_cast<uint32_t>(data[2]) << 16) |
               (static_cast<uint32_t>(data[3]) << 24);
    }

    static void unpack(uint32_t packed, uint8_t out[4])
    {
        out[0] = static_cast<uint8_t>(packed & 0xFFU);
        out[1] = static_cast<uint8_t>((packed >> 8) & 0xFFU);
        out[2] = static_cast<uint8_t>((packed >> 16) & 0xFFU);
        out[3] = static_cast<uint8_t>((packed >> 24) & 0xFFU);
    }

    Shared<uint32_t> version_{0};
    Shared<uint32_t> raw255_{0};
    Shared<uint32_t> raw12B_{0};
    Shared<uint32_t> last255RxMs_{0};
    Shared<uint32_t> last12BRxMs_{0};
    Shared<uint32_t> generation255_{0};
    Shared<uint32_t> generation12B_{0};
    Shared<bool> has255_{false};
    Shared<bool> has12B_{false};
    Shared<bool> dlc255Valid_{false};
    Shared<bool> dlc12BValid_{false};
};

inline ObstacleCanSnapshot obstacleCanSnapshot;
