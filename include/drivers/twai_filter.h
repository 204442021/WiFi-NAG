#pragma once

#include <cstdint>

struct TwaiFilterResult
{
    uint32_t acceptance_code = 0;
    uint32_t acceptance_mask = 0;
    bool single_filter = true;
};

inline uint8_t twaiFilterPopcount11(uint32_t value)
{
    value &= 0x7FFU;
    uint8_t count = 0;
    while (value)
    {
        count = static_cast<uint8_t>(count + (value & 1U));
        value >>= 1U;
    }
    return count;
}

inline uint16_t twaiDualFilterCode(uint32_t id, uint32_t compareMask)
{
    return static_cast<uint16_t>((id & compareMask & 0x7FFU) << 5U);
}

inline uint16_t twaiDualFilterIgnoreMask(uint32_t compareMask)
{
    // Legacy TWAI acceptance masks use 1 for "do not compare". The lower
    // five bits contain non-ID fields in dual-filter mode and are left open;
    // read() rejects RTR/extended frames before they reach the application.
    return static_cast<uint16_t>(((~compareMask) & 0x7FFU) << 5U) | 0x001FU;
}

inline TwaiFilterResult computeTwaiDualFilter(const uint32_t *ids,
                                               uint8_t singletonIndex)
{
    const uint8_t pairA = singletonIndex == 0 ? 1 : 0;
    const uint8_t pairB = singletonIndex == 2 ? 1 : 2;
    const uint32_t singletonMask = 0x7FFU;
    const uint32_t pairCompareMask =
        (~(ids[pairA] ^ ids[pairB])) & 0x7FFU;

    const uint16_t codeA =
        twaiDualFilterCode(ids[singletonIndex], singletonMask);
    const uint16_t maskA = twaiDualFilterIgnoreMask(singletonMask);
    const uint16_t codeB = twaiDualFilterCode(ids[pairA], pairCompareMask);
    const uint16_t maskB = twaiDualFilterIgnoreMask(pairCompareMask);

    TwaiFilterResult result;
    result.acceptance_code =
        (static_cast<uint32_t>(codeA) << 16U) | codeB;
    result.acceptance_mask =
        (static_cast<uint32_t>(maskA) << 16U) | maskB;
    result.single_filter = false;
    return result;
}

inline TwaiFilterResult computeTwaiFilter(const uint32_t *ids, uint8_t count)
{
    TwaiFilterResult result;
    if (!ids || count == 0)
        return result;

    // Three unrelated standard IDs cannot be represented exactly by the
    // ESP32-S3's one mask filter. Use its two 16-bit filters to keep one ID
    // exact and group the closest pair. A software whitelist remains the
    // authoritative exact filter.
    if (count == 3)
    {
        uint8_t bestSingleton = 0;
        uint8_t bestWildcardBits = 12;
        for (uint8_t singleton = 0; singleton < 3; ++singleton)
        {
            const uint8_t pairA = singleton == 0 ? 1 : 0;
            const uint8_t pairB = singleton == 2 ? 1 : 2;
            const uint8_t wildcardBits =
                twaiFilterPopcount11(ids[pairA] ^ ids[pairB]);
            if (wildcardBits < bestWildcardBits)
            {
                bestWildcardBits = wildcardBits;
                bestSingleton = singleton;
            }
        }
        return computeTwaiDualFilter(ids, bestSingleton);
    }

    uint32_t differ = 0;
    for (uint8_t index = 1; index < count; ++index)
        differ |= ids[0] ^ ids[index];

    const uint32_t base = ids[0] & ~differ;
    result.acceptance_code = base << 21;
    result.acceptance_mask = (differ << 21) | 0x001FFFFFU;
    result.single_filter = true;
    return result;
}

inline bool twaiHardwareFilterAccepts(const TwaiFilterResult &filter,
                                      uint32_t id)
{
    if (!filter.single_filter)
    {
        const uint16_t received =
            static_cast<uint16_t>((id & 0x7FFU) << 5U);
        const uint16_t codeA =
            static_cast<uint16_t>(filter.acceptance_code >> 16U);
        const uint16_t codeB =
            static_cast<uint16_t>(filter.acceptance_code & 0xFFFFU);
        const uint16_t maskA =
            static_cast<uint16_t>(filter.acceptance_mask >> 16U);
        const uint16_t maskB =
            static_cast<uint16_t>(filter.acceptance_mask & 0xFFFFU);
        const bool acceptedA =
            (received & static_cast<uint16_t>(~maskA)) ==
            (codeA & static_cast<uint16_t>(~maskA));
        const bool acceptedB =
            (received & static_cast<uint16_t>(~maskB)) ==
            (codeB & static_cast<uint16_t>(~maskB));
        return acceptedA || acceptedB;
    }

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
