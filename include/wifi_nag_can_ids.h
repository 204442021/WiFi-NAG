#pragma once

#include <cstdint>

inline constexpr uint32_t kWifiNagObservedIds[] = {0x370, 0x255, 0x12B};
inline constexpr uint8_t kWifiNagObservedIdCount =
    static_cast<uint8_t>(sizeof(kWifiNagObservedIds) /
                         sizeof(kWifiNagObservedIds[0]));
