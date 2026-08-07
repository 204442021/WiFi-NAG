#pragma once

#include <cstdint>
#include "shared_types.h"

#if defined(NAG_KILLER) && !defined(ESP32_DASHBOARD)
inline constexpr bool kNagKillerDefaultEnabled = true;
inline constexpr bool kNagKillerBuildEnabled = true;
#else
inline constexpr bool kNagKillerDefaultEnabled = false;
inline constexpr bool kNagKillerBuildEnabled = false;
#endif

// Tracks both the current runtime gate and every OFF/ON transition.  The
// transition counter lets the CAN handler detect an OFF -> ON edge even when
// no CAN frame arrived while writing was disabled, so the next real frame can
// still be written immediately as requested.
struct NagRuntimeGate
{
    Shared<bool> enabled{kNagKillerDefaultEnabled};
    Shared<uint32_t> generation{0};

    NagRuntimeGate &operator=(bool value)
    {
        const bool previous = (bool)enabled;
        enabled = value;
        if (previous != value)
            generation++;
        return *this;
    }

    operator bool() const { return (bool)enabled; }
    uint32_t version() const { return (uint32_t)generation; }
};

inline NagRuntimeGate nagKillerRuntime;
