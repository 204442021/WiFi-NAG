#pragma once

#include "../can_frame_types.h"

enum class CanDriverState : uint8_t
{
    Unavailable = 0,
    Stopped,
    Running,
    BusOff,
    Recovering,
};

enum class CanSafetyReason : uint8_t
{
    None = 0,
    BusOff,
    TxErrorCounter,
    RxErrorCounter,
    BusErrorBurst,
    TxFailureBurst,
};

struct CanDriverDiagnostics
{
    bool available = false;
    CanDriverState state = CanDriverState::Unavailable;
    uint32_t msgsToTx = 0;
    uint32_t msgsToRx = 0;
    uint32_t txErrorCounter = 0;
    uint32_t rxErrorCounter = 0;
    uint32_t txFailedCount = 0;
    uint32_t rxMissedCount = 0;
    uint32_t rxOverrunCount = 0;
    uint32_t arbLostCount = 0;
    uint32_t busErrorCount = 0;
    uint32_t busOffCount = 0;
    uint32_t recoveryCount = 0;
    uint32_t errorWarningCount = 0;
    uint32_t errorPassiveCount = 0;
    uint32_t staleDropCount = 0;
    bool safetyTripped = false;
    CanSafetyReason safetyReason = CanSafetyReason::None;
    uint32_t safetyTripCount = 0;
};

struct CanDriver
{
    void (*onSendFrame)(const CanFrame &, bool ok) = nullptr;
    void (*onSafetyTrip)(CanSafetyReason reason) = nullptr;

    virtual bool init() = 0;
    virtual void setFilters(const uint32_t *ids, uint8_t count) = 0;
    virtual bool enableInterrupt(void (*onReady)()) = 0;
    virtual bool read(CanFrame &frame) = 0;
    virtual bool send(const CanFrame &frame) = 0;
    virtual bool getDiagnostics(CanDriverDiagnostics &out)
    {
        out = {};
        return false;
    }
    virtual bool resetDiagnostics() { return false; }
    virtual bool clearSafetyLatch() { return true; }
    virtual ~CanDriver() = default;
};
