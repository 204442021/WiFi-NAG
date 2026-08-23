#pragma once

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "nag_adaptive_controller.h"

namespace NagAdaptiveConfigInput
{
enum class Error : uint8_t
{
    NONE = 0,
    EMPTY,
    INVALID,
    ERANGE_VALUE,
    NON_FINITE,
    TRAILING_JUNK,
    OUT_OF_RANGE,
    NOT_INTEGER,
};

enum class ApplyResult : uint8_t
{
    INVALID = 0,
    VALID_UNCHANGED,
    VALID_CHANGED,
};

inline ApplyResult decideApply(bool valid, uint8_t currentMode,
                               uint8_t requestedMode, bool configUnchanged)
{
    if (!valid)
        return ApplyResult::INVALID;
    const bool changed = currentMode != requestedMode || !configUnchanged;
    return changed ? ApplyResult::VALID_CHANGED : ApplyResult::VALID_UNCHANGED;
}

inline bool shouldRejectRequest(ApplyResult result)
{
    return result == ApplyResult::INVALID;
}

inline bool shouldPublishCommand(ApplyResult result)
{
    return result == ApplyResult::VALID_CHANGED;
}

inline const char *message(Error error)
{
    switch (error)
    {
    case Error::EMPTY: return "value is empty";
    case Error::ERANGE_VALUE: return "value is outside the numeric range";
    case Error::NON_FINITE: return "value must be finite";
    case Error::TRAILING_JUNK: return "value contains trailing characters";
    case Error::OUT_OF_RANGE: return "value is outside the allowed range";
    case Error::NOT_INTEGER: return "value must be an integer";
    case Error::INVALID: return "value is not a number";
    default: return "";
    }
}

inline bool parseFiniteDouble(const char *value, double &parsed, Error &error)
{
    if (!value || *value == '\0')
    {
        error = Error::EMPTY;
        return false;
    }
    const char *cursor = value;
    while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor)))
        ++cursor;
    if (*cursor == '\0')
    {
        error = Error::EMPTY;
        return false;
    }

    char *end = nullptr;
    errno = 0;
    parsed = std::strtod(value, &end);
    if (value == end)
    {
        error = Error::INVALID;
        return false;
    }
    if (errno == ERANGE)
    {
        error = Error::ERANGE_VALUE;
        return false;
    }
    if (!std::isfinite(parsed))
    {
        error = Error::NON_FINITE;
        return false;
    }
    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)))
        ++end;
    if (*end == '\0')
    {
        error = Error::NONE;
        return true;
    }
    error = Error::TRAILING_JUNK;
    return false;
}

inline bool parseNmCenti(const char *value, double minimumNm, double maximumNm,
                         int16_t &out, Error &error)
{
    double parsed = 0.0;
    if (!parseFiniteDouble(value, parsed, error))
        return false;
    if (parsed < minimumNm || parsed > maximumNm)
    {
        error = Error::OUT_OF_RANGE;
        return false;
    }
    const double scaled = parsed * 100.0;
    const double rounded = scaled >= 0.0 ? std::floor(scaled + 0.5)
                                         : std::ceil(scaled - 0.5);
    if (!std::isfinite(scaled) ||
        rounded < static_cast<double>(std::numeric_limits<int16_t>::min()) ||
        rounded > static_cast<double>(std::numeric_limits<int16_t>::max()))
    {
        error = Error::OUT_OF_RANGE;
        return false;
    }
    out = static_cast<int16_t>(rounded);
    error = Error::NONE;
    return true;
}

inline bool parseSecondsMs(const char *value, double minimumSeconds,
                           double maximumSeconds, uint32_t &out, Error &error)
{
    double parsed = 0.0;
    if (!parseFiniteDouble(value, parsed, error))
        return false;
    if (parsed < minimumSeconds || parsed > maximumSeconds)
    {
        error = Error::OUT_OF_RANGE;
        return false;
    }
    const double scaled = parsed * 1000.0;
    const double rounded = std::floor(scaled + 0.5);
    if (!std::isfinite(scaled) ||
        rounded > static_cast<double>(std::numeric_limits<uint32_t>::max()))
    {
        error = Error::OUT_OF_RANGE;
        return false;
    }
    out = static_cast<uint32_t>(rounded);
    error = Error::NONE;
    return true;
}

inline bool parseMilliseconds(const char *value, uint32_t minimum,
                              uint32_t maximum, uint32_t &out, Error &error)
{
    double parsed = 0.0;
    if (!parseFiniteDouble(value, parsed, error))
        return false;
    if (parsed != std::floor(parsed))
    {
        error = Error::NOT_INTEGER;
        return false;
    }
    if (parsed < static_cast<double>(minimum) ||
        parsed > static_cast<double>(maximum) ||
        parsed > static_cast<double>(std::numeric_limits<uint32_t>::max()))
    {
        error = Error::OUT_OF_RANGE;
        return false;
    }
    out = static_cast<uint32_t>(parsed);
    error = Error::NONE;
    return true;
}

inline bool parseMode(const char *value, uint8_t &out, Error &error)
{
    uint32_t parsed = 0;
    if (!parseMilliseconds(value, 0, 255, parsed, error))
        return false;
    if (parsed != 0 && parsed != 5)
    {
        error = Error::OUT_OF_RANGE;
        return false;
    }
    out = static_cast<uint8_t>(parsed);
    return true;
}

inline bool parseBoolean(const char *value, bool &out, Error &error)
{
    if (!value || *value == '\0')
    {
        error = Error::EMPTY;
        return false;
    }
    if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
        std::strcmp(value, "on") == 0)
    {
        out = true;
        error = Error::NONE;
        return true;
    }
    if (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 ||
        std::strcmp(value, "off") == 0)
    {
        out = false;
        error = Error::NONE;
        return true;
    }
    error = Error::INVALID;
    return false;
}

inline const char *validate(const NagAdaptiveConfig &config)
{
    if (config.preventiveNegativeMinCentiNm < 150 ||
        config.preventiveNegativeMinCentiNm > 180)
        return "preventiveNegativeMinNm";
    if (config.preventiveNegativeMaxCentiNm < config.preventiveNegativeMinCentiNm ||
        config.preventiveNegativeMaxCentiNm > 180)
        return "preventiveNegativeMaxNm";
    if (config.preventivePositiveMinCentiNm < 150 ||
        config.preventivePositiveMinCentiNm > 180)
        return "preventivePositiveMinNm";
    if (config.preventivePositiveMaxCentiNm < config.preventivePositiveMinCentiNm ||
        config.preventivePositiveMaxCentiNm > 180)
        return "preventivePositiveMaxNm";
    if (config.correctiveNegativeMinCentiNm < 180 ||
        config.correctiveNegativeMinCentiNm > 250)
        return "correctiveNegativeMinNm";
    if (config.correctiveNegativeMaxCentiNm < config.correctiveNegativeMinCentiNm ||
        config.correctiveNegativeMaxCentiNm > 250)
        return "correctiveNegativeMaxNm";
    if (config.correctivePositiveMinCentiNm < 180 ||
        config.correctivePositiveMinCentiNm > 250)
        return "correctivePositiveMinNm";
    if (config.correctivePositiveMaxCentiNm < config.correctivePositiveMinCentiNm ||
        config.correctivePositiveMaxCentiNm > 250)
        return "correctivePositiveMaxNm";
    if ((config.correctiveNegativeFrames != 0 &&
         config.correctiveNegativeFrames < 10) ||
        config.correctiveNegativeFrames > 255)
        return "correctiveNegativeFrames";
    if ((config.correctivePositiveFrames != 0 &&
         config.correctivePositiveFrames < 10) ||
        config.correctivePositiveFrames > 255)
        return "correctivePositiveFrames";
    if (config.activityMinMs < 100)
        return "activityMinSec";
    if (config.activityMaxMs < config.activityMinMs)
        return "activityMaxSec";
    if (config.releaseMinMs < 100 || config.releaseMinMs > 1000)
        return "releaseMinSec";
    if (config.releaseMaxMs < config.releaseMinMs || config.releaseMaxMs > 1000)
        return "releaseMaxSec";
    if (!(config.restMinMs == 0 && config.restMaxMs == 0))
    {
        if (config.restMinMs < 100)
            return "restMinSec";
        if (config.restMaxMs < config.restMinMs)
            return "restMaxSec";
    }
    if (config.correctiveSendMinMs < 100)
        return "correctiveSendMinSec";
    if (config.correctiveSendMaxMs < config.correctiveSendMinMs)
        return "correctiveSendMaxSec";
    if (!(config.correctivePauseMinMs == 0 && config.correctivePauseMaxMs == 0))
    {
        if (config.correctivePauseMinMs < 100)
            return "correctivePauseMinSec";
        if (config.correctivePauseMaxMs < config.correctivePauseMinMs)
            return "correctivePauseMaxSec";
    }
    if (config.dasFreshTimeoutMs < 100 || config.dasFreshTimeoutMs > 2000)
        return "dasFreshTimeoutMs";
    return nullptr;
}
} // namespace NagAdaptiveConfigInput
