#pragma once

#include <cstdint>

#ifdef NATIVE_BUILD
#include <mutex>
#else
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

#include "nag_adaptive_controller.h"

struct NagAdaptivePendingCommand
{
    NagAdaptiveConfig config{};
    uint8_t mode = 0;
    bool resetRequested = false;
    uint32_t generation = 0;
};

class NagAdaptiveExchange
{
public:
    NagAdaptiveExchange() = default;

    NagAdaptiveExchange(const NagAdaptiveExchange &other)
        : pendingCommand_(other.desiredCommand()),
          publishedSnapshot_(other.readSnapshot())
    {
    }

    NagAdaptiveExchange(NagAdaptiveExchange &&other)
        : NagAdaptiveExchange(static_cast<const NagAdaptiveExchange &>(other))
    {
    }

    NagAdaptiveExchange &operator=(const NagAdaptiveExchange &other)
    {
        if (this == &other)
            return *this;

        const NagAdaptivePendingCommand command = other.desiredCommand();
        const NagAdaptiveSnapshot snapshot = other.readSnapshot();

        lockCommand();
        pendingCommand_ = command;
        unlockCommand();

        lockSnapshot();
        publishedSnapshot_ = snapshot;
        unlockSnapshot();
        return *this;
    }

    NagAdaptiveExchange &operator=(NagAdaptiveExchange &&other)
    {
        return operator=(static_cast<const NagAdaptiveExchange &>(other));
    }

    void publishCommand(const NagAdaptiveConfig &config, uint8_t mode,
                        bool resetRequested)
    {
        lockCommand();
        uint32_t generation = pendingCommand_.generation + 1U;
        if (generation == 0)
            generation = 1;
        pendingCommand_.config = config;
        pendingCommand_.mode = mode;
        pendingCommand_.resetRequested = resetRequested;
        pendingCommand_.generation = generation;
        unlockCommand();
    }

    NagAdaptivePendingCommand desiredCommand() const
    {
        lockCommand();
        const NagAdaptivePendingCommand command = pendingCommand_;
        unlockCommand();
        return command;
    }

    bool consumeCommand(uint32_t &consumedGeneration,
                        NagAdaptivePendingCommand &out) const
    {
        lockCommand();
        if (pendingCommand_.generation == consumedGeneration)
        {
            unlockCommand();
            return false;
        }
        out = pendingCommand_;
        consumedGeneration = pendingCommand_.generation;
        unlockCommand();
        return true;
    }

    void publishSnapshot(const NagAdaptiveSnapshot &snapshot)
    {
        lockSnapshot();
        publishedSnapshot_ = snapshot;
        unlockSnapshot();
    }

    NagAdaptiveSnapshot readSnapshot() const
    {
        lockSnapshot();
        const NagAdaptiveSnapshot snapshot = publishedSnapshot_;
        unlockSnapshot();
        return snapshot;
    }

private:
    void lockCommand() const
    {
#ifdef NATIVE_BUILD
        commandMutex_.lock();
#else
        portENTER_CRITICAL(&commandMux_);
#endif
    }

    void unlockCommand() const
    {
#ifdef NATIVE_BUILD
        commandMutex_.unlock();
#else
        portEXIT_CRITICAL(&commandMux_);
#endif
    }

    void lockSnapshot() const
    {
#ifdef NATIVE_BUILD
        snapshotMutex_.lock();
#else
        portENTER_CRITICAL(&snapshotMux_);
#endif
    }

    void unlockSnapshot() const
    {
#ifdef NATIVE_BUILD
        snapshotMutex_.unlock();
#else
        portEXIT_CRITICAL(&snapshotMux_);
#endif
    }

    NagAdaptivePendingCommand pendingCommand_{};
    NagAdaptiveSnapshot publishedSnapshot_{};
#ifdef NATIVE_BUILD
    mutable std::mutex commandMutex_;
    mutable std::mutex snapshotMutex_;
#else
    mutable portMUX_TYPE commandMux_ = portMUX_INITIALIZER_UNLOCKED;
    mutable portMUX_TYPE snapshotMux_ = portMUX_INITIALIZER_UNLOCKED;
#endif
};
