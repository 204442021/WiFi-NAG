#pragma once

#include <vector>
#include "../can_frame_types.h"
#include "can_driver.h"

class MockDriver : public CanDriver
{
public:
    static constexpr bool kSupportsISR = false;

    std::vector<CanFrame> sent;
    bool writeEnabled = true;
    bool restartPrepared = false;

    bool init() override { return true; }
    void setFilters(const uint32_t * /*ids*/, uint8_t /*count*/) override {}
    bool setWriteEnabled(bool enabled) override
    {
        if (restartPrepared && enabled)
            return false;
        writeEnabled = enabled;
        return true;
    }
    void prepareForRestart() override
    {
        restartPrepared = true;
        writeEnabled = false;
    }
    bool enableInterrupt(void (* /*onReady*/)()) override { return false; }

    bool read(CanFrame & /*frame*/) override
    {
        return false;
    }

    bool send(const CanFrame &frame) override
    {
        if (!writeEnabled || restartPrepared)
        {
            if (onSendFrame)
                onSendFrame(frame, false);
            return false;
        }
        sent.push_back(frame);
        if (onSendFrame)
            onSendFrame(frame, true);
        return true;
    }

    void reset()
    {
        sent.clear();
    }
};
