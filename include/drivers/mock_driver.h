#pragma once

#include <vector>
#include "../can_frame_types.h"
#include "can_driver.h"

class MockDriver : public CanDriver
{
public:
    static constexpr bool kSupportsISR = false;

    std::vector<CanFrame> sent;
    bool sendResult = true;

    bool init() override { return true; }
    void setFilters(const uint32_t * /*ids*/, uint8_t /*count*/) override {}
    bool enableInterrupt(void (* /*onReady*/)()) override { return false; }

    bool read(CanFrame & /*frame*/) override
    {
        return false;
    }

    bool send(const CanFrame &frame) override
    {
        if (sendResult)
            sent.push_back(frame);
        if (onSendFrame)
            onSendFrame(frame, sendResult);
        return sendResult;
    }

    void reset()
    {
        sent.clear();
        sendResult = true;
    }
};
