#pragma once

#include "shared_types.h"

#if defined(NAG_KILLER) && !defined(ESP32_DASHBOARD)
inline constexpr bool kNagKillerDefaultEnabled = true;
inline constexpr bool kNagKillerBuildEnabled = true;
#else
inline constexpr bool kNagKillerDefaultEnabled = false;
inline constexpr bool kNagKillerBuildEnabled = false;
#endif

inline Shared<bool> nagKillerRuntime{kNagKillerDefaultEnabled};
