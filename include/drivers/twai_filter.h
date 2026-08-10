#pragma once

#include <cstdint>

struct TwaiFilterResult
{
    uint32_t acceptance_code = 0;
    uint32_t acceptance_mask = 0;
};

inline TwaiFilterResult computeTwaiFilter(const uint32_t *ids, uint8_t count)
{
    TwaiFilterResult result;
    if (!ids || count == 0)
        return result;

    uint32_t differ = 0;
    for (uint8_t index = 1; index < count; ++index)
        differ |= ids[0] ^ ids[index];

    const uint32_t base = ids[0] & ~differ;
    result.acceptance_code = base << 21;
    result.acceptance_mask = (differ << 21) | 0x001FFFFFU;
    return result;
}

inline bool twaiHardwareFilterAccepts(const TwaiFilterResult &filter,
                                      uint32_t id)
{
    const uint32_t received = id << 21;
    return (received & ~filter.acceptance_mask) ==
           (filter.acceptance_code & ~filter.acceptance_mask);
}

inline bool exactCanIdMatches(const uint32_t *ids, uint8_t count,
                              uint32_t id)
{
    if (count == 0)
        return true;
    if (!ids)
        return false;
    for (uint8_t index = 0; index < count; ++index)
    {
        if (ids[index] == id)
            return true;
    }
    return false;
}
