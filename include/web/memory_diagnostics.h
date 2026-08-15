#pragma once

#if defined(ESP_PLATFORM) && !defined(NATIVE_BUILD)

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

enum DashDiagEventCode : uint8_t
{
    DASH_DIAG_EVENT_BOOT = 1,
    DASH_DIAG_EVENT_WIFI_STATE,
    DASH_DIAG_EVENT_AP_CLIENTS,
    DASH_DIAG_EVENT_BLE_STATE,
    DASH_DIAG_EVENT_OTA_START,
    DASH_DIAG_EVENT_OTA_END,
    DASH_DIAG_EVENT_OTA_FAIL,
    DASH_DIAG_EVENT_TASK_COUNT,
    DASH_DIAG_EVENT_MEMORY_WARNING,
    DASH_DIAG_EVENT_MEMORY_CRITICAL,
    DASH_DIAG_EVENT_MEMORY_RECOVERED,
    DASH_DIAG_EVENT_ALLOC_FAILED,
    DASH_DIAG_EVENT_MANUAL_MARK,
    DASH_DIAG_EVENT_RECORDS_CLEARED,
    DASH_DIAG_EVENT_RESTART_REQUESTED,
};

struct DashDiagRuntimeState
{
    uint8_t wifiMode = 0;
    uint8_t wifiStatus = 0;
    uint8_t apClients = 0;
    int8_t wifiRssi = -127;
    uint8_t bleProtocol = 0;
    uint8_t bleFlags = 0;
    int8_t bleRssi = -127;
    uint16_t bleDisconnectReason = 0;
    uint32_t bleReconnectCount = 0;
    uint32_t bleDisconnectCount = 0;
    uint16_t gatewayPending = 0;
    uint16_t gatewayPendingMax = 0;
    uint32_t gatewayPendingFull = 0;
    uint32_t gatewayTimeouts = 0;
    uint32_t rxCount = 0;
    uint32_t txCount = 0;
    uint32_t txErrorCount = 0;
    bool otaRunning = false;
    bool canOnline = false;
    bool canWriteEnabled = false;
};

struct DashDiagHeapRegion
{
    uint32_t total = 0;
    uint32_t free = 0;
    uint32_t minimum = 0;
    uint32_t largest = 0;
    uint32_t allocated = 0;
    uint32_t allocatedBlocks = 0;
    uint32_t freeBlocks = 0;
    uint32_t totalBlocks = 0;
};

struct DashDiagHeapSnapshot
{
    DashDiagHeapRegion internal;
    DashDiagHeapRegion dma;
    DashDiagHeapRegion psram;
    DashDiagHeapRegion combined;
};

struct DashDiagSample
{
    uint32_t uptimeMs = 0;
    uint32_t internalFree = 0;
    uint32_t internalMinimum = 0;
    uint32_t internalLargest = 0;
    uint32_t internalAllocated = 0;
    uint32_t dmaFree = 0;
    uint32_t dmaLargest = 0;
    uint32_t psramFree = 0;
    uint32_t psramLargest = 0;
    uint32_t rxCount = 0;
    uint32_t txCount = 0;
    uint32_t txErrorCount = 0;
    uint32_t bleReconnectCount = 0;
    uint32_t bleDisconnectCount = 0;
    uint32_t gatewayPendingFull = 0;
    uint32_t gatewayTimeouts = 0;
    uint16_t taskCount = 0;
    uint16_t internalAllocatedBlocks = 0;
    uint16_t internalFreeBlocks = 0;
    uint16_t gatewayPending = 0;
    uint16_t gatewayPendingMax = 0;
    uint16_t bleDisconnectReason = 0;
    uint8_t wifiMode = 0;
    uint8_t wifiStatus = 0;
    uint8_t apClients = 0;
    uint8_t bleProtocol = 0;
    uint8_t bleFlags = 0;
    int8_t wifiRssi = -127;
    int8_t bleRssi = -127;
    uint8_t runtimeFlags = 0;
};

struct DashDiagEvent
{
    uint32_t uptimeMs = 0;
    uint32_t internalFree = 0;
    uint32_t internalLargest = 0;
    int32_t deltaFromPrevious = 0;
    uint32_t argument = 0;
    uint16_t taskCount = 0;
    uint8_t code = 0;
    uint8_t wifiStatus = 0;
    uint8_t apClients = 0;
    uint8_t bleProtocol = 0;
    uint8_t runtimeFlags = 0;
};

struct DashDiagAllocationFailure
{
    uint32_t sequence = 0;
    uint32_t uptimeMs = 0;
    uint32_t requestedSize = 0;
    uint32_t caps = 0;
    uint32_t internalFree = 0;
    uint32_t internalLargest = 0;
    char functionName[24] = {};
};

struct DashDiagPreviousBoot
{
    uint32_t magic = 0;
    uint32_t uptimeMs = 0;
    uint32_t internalFree = 0;
    uint32_t internalMinimum = 0;
    uint32_t internalLargest = 0;
    uint32_t taskCount = 0;
    uint32_t runtimeFlags = 0;
    uint32_t lastEvent = 0;
    uint32_t checksum = 0;
};

static constexpr uint32_t kDashDiagRtcMagic = 0x574E4433UL; // "WND3"
RTC_NOINIT_ATTR static DashDiagPreviousBoot dashDiagRtcSnapshot;
static volatile DashDiagAllocationFailure dashDiagLastAllocationFailure;

static void dashDiagAllocationFailedHook(size_t requestedSize,
                                         uint32_t caps,
                                         const char *functionName)
{
    __atomic_add_fetch(&dashDiagLastAllocationFailure.sequence, 1U, __ATOMIC_RELAXED);
    dashDiagLastAllocationFailure.uptimeMs = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
    dashDiagLastAllocationFailure.requestedSize = static_cast<uint32_t>(requestedSize);
    dashDiagLastAllocationFailure.caps = caps;
    dashDiagLastAllocationFailure.internalFree = static_cast<uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    dashDiagLastAllocationFailure.internalLargest = static_cast<uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    size_t i = 0;
    if (functionName)
    {
        for (; i + 1 < sizeof(dashDiagLastAllocationFailure.functionName) && functionName[i]; i++)
            dashDiagLastAllocationFailure.functionName[i] = functionName[i];
    }
    dashDiagLastAllocationFailure.functionName[i] = '\0';
    __atomic_add_fetch(&dashDiagLastAllocationFailure.sequence, 1U, __ATOMIC_RELEASE);
}

class DashMemoryDiagnostics
{
public:
    static constexpr uint32_t kSampleIntervalMs = 5000;
    static constexpr size_t kSampleCapacity = 1440; // two hours at five seconds
    static constexpr size_t kEventCapacity = 192;
    static constexpr uint32_t kWarningBytes = 80U * 1024U;
    static constexpr uint32_t kCriticalBytes = 60U * 1024U;

    bool begin()
    {
        if (dashDiagPreviousBootValid(dashDiagRtcSnapshot))
        {
            previousBoot_ = dashDiagRtcSnapshot;
            hasPreviousBoot_ = true;
        }
        samples_ = static_cast<DashDiagSample *>(
            heap_caps_calloc(kSampleCapacity, sizeof(DashDiagSample),
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        events_ = static_cast<DashDiagEvent *>(
            heap_caps_calloc(kEventCapacity, sizeof(DashDiagEvent),
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        heap_caps_register_failed_alloc_callback(dashDiagAllocationFailedHook);
        initialized_ = samples_ != nullptr;
        return initialized_;
    }

    bool initialized() const { return initialized_; }
    size_t sampleCount() const { return sampleCount_; }
    size_t sampleCapacity() const { return samples_ ? kSampleCapacity : 0; }
    size_t eventCount() const { return eventCount_; }
    size_t eventCapacity() const { return events_ ? kEventCapacity : 0; }
    bool hasPreviousBoot() const { return hasPreviousBoot_; }
    const DashDiagPreviousBoot &previousBoot() const { return previousBoot_; }

    static uint8_t fragmentationPercent(uint32_t freeBytes, uint32_t largestBlock)
    {
        if (freeBytes == 0 || largestBlock >= freeBytes)
            return 0;
        return static_cast<uint8_t>(100U -
                                    static_cast<uint32_t>((static_cast<uint64_t>(largestBlock) * 100U) /
                                                          freeBytes));
    }

    DashDiagHeapSnapshot heapSnapshot() const
    {
        DashDiagHeapSnapshot snapshot;
        fillRegion(snapshot.internal, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        fillRegion(snapshot.dma, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        fillRegion(snapshot.psram, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        fillRegion(snapshot.combined, MALLOC_CAP_8BIT);
        return snapshot;
    }

    void poll(const DashDiagRuntimeState &state, bool force = false)
    {
        if (!initialized_)
            return;
        const uint32_t now = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (!force && sampleCount_ > 0 && now - lastSampleMs_ < kSampleIntervalMs)
            return;

        const DashDiagSample sample = captureSample(now, state);
        if (!haveRuntimeState_)
        {
            storeEvent(DASH_DIAG_EVENT_BOOT, sample, 0);
        }
        else
        {
            if (state.wifiMode != lastRuntimeState_.wifiMode ||
                state.wifiStatus != lastRuntimeState_.wifiStatus)
                storeEvent(DASH_DIAG_EVENT_WIFI_STATE, sample, state.wifiStatus);
            if (state.apClients != lastRuntimeState_.apClients)
                storeEvent(DASH_DIAG_EVENT_AP_CLIENTS, sample, state.apClients);
            if (state.bleProtocol != lastRuntimeState_.bleProtocol ||
                state.bleFlags != lastRuntimeState_.bleFlags)
                storeEvent(DASH_DIAG_EVENT_BLE_STATE, sample, state.bleProtocol);
            if (state.otaRunning != lastRuntimeState_.otaRunning)
                storeEvent(state.otaRunning ? DASH_DIAG_EVENT_OTA_START : DASH_DIAG_EVENT_OTA_END,
                           sample, 0);
            if (sample.taskCount != lastTaskCount_)
                storeEvent(DASH_DIAG_EVENT_TASK_COUNT, sample, sample.taskCount);
        }

        const uint8_t nextPressure = sample.internalFree < kCriticalBytes
                                         ? 2
                                         : (sample.internalFree < kWarningBytes ? 1 : 0);
        if (nextPressure != memoryPressure_)
        {
            storeEvent(nextPressure == 2   ? DASH_DIAG_EVENT_MEMORY_CRITICAL
                       : nextPressure == 1 ? DASH_DIAG_EVENT_MEMORY_WARNING
                                           : DASH_DIAG_EVENT_MEMORY_RECOVERED,
                       sample, sample.internalFree);
            memoryPressure_ = nextPressure;
        }

        DashDiagAllocationFailure failure = allocationFailure();
        if (failure.sequence != 0 && failure.sequence != lastAllocationFailureSequence_)
        {
            storeEvent(DASH_DIAG_EVENT_ALLOC_FAILED, sample, failure.requestedSize);
            lastAllocationFailureSequence_ = failure.sequence;
        }

        appendSample(sample);
        lastRuntimeState_ = state;
        haveRuntimeState_ = true;
        lastTaskCount_ = sample.taskCount;
        lastSampleMs_ = now;
        updateRtc(sample, lastEventCode_);
    }

    void recordEvent(DashDiagEventCode code,
                     const DashDiagRuntimeState &state,
                     uint32_t argument = 0)
    {
        if (!initialized_)
            return;
        const uint32_t now = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
        const DashDiagSample sample = captureSample(now, state);
        storeEvent(code, sample, argument);
        updateRtc(sample, code);
    }

    void clear(const DashDiagRuntimeState &state)
    {
        sampleHead_ = 0;
        sampleCount_ = 0;
        eventHead_ = 0;
        eventCount_ = 0;
        haveRuntimeState_ = false;
        memoryPressure_ = 0;
        lastSampleMs_ = 0;
        poll(state, true);
        recordEvent(DASH_DIAG_EVENT_RECORDS_CLEARED, state, 0);
    }

    const DashDiagSample *sampleAt(size_t chronologicalIndex) const
    {
        if (!samples_ || chronologicalIndex >= sampleCount_)
            return nullptr;
        const size_t start = sampleCount_ < kSampleCapacity ? 0 : sampleHead_;
        return &samples_[(start + chronologicalIndex) % kSampleCapacity];
    }

    const DashDiagEvent *eventAt(size_t chronologicalIndex) const
    {
        if (!events_ || chronologicalIndex >= eventCount_)
            return nullptr;
        const size_t start = eventCount_ < kEventCapacity ? 0 : eventHead_;
        return &events_[(start + chronologicalIndex) % kEventCapacity];
    }

    const DashDiagSample *latestSample() const
    {
        return sampleCount_ ? sampleAt(sampleCount_ - 1) : nullptr;
    }

    int32_t startupInternalDelta() const
    {
        const DashDiagSample *first = sampleAt(0);
        const DashDiagSample *last = latestSample();
        return first && last ? static_cast<int32_t>(last->internalFree) -
                                   static_cast<int32_t>(first->internalFree)
                             : 0;
    }

    int32_t tenMinuteInternalDelta() const
    {
        const DashDiagSample *last = latestSample();
        if (!last)
            return 0;
        const uint32_t target = last->uptimeMs > 600000U ? last->uptimeMs - 600000U : 0;
        const DashDiagSample *base = sampleAt(0);
        for (size_t i = 0; i < sampleCount_; i++)
        {
            const DashDiagSample *candidate = sampleAt(i);
            if (candidate && candidate->uptimeMs >= target)
            {
                base = candidate;
                break;
            }
        }
        return base ? static_cast<int32_t>(last->internalFree) -
                          static_cast<int32_t>(base->internalFree)
                    : 0;
    }

    DashDiagAllocationFailure allocationFailure() const
    {
        DashDiagAllocationFailure copy;
        for (uint8_t attempt = 0; attempt < 3; attempt++)
        {
            const uint32_t before = __atomic_load_n(&dashDiagLastAllocationFailure.sequence,
                                                     __ATOMIC_ACQUIRE);
            if (before & 1U)
                continue;
            copy.sequence = before;
            copy.uptimeMs = dashDiagLastAllocationFailure.uptimeMs;
            copy.requestedSize = dashDiagLastAllocationFailure.requestedSize;
            copy.caps = dashDiagLastAllocationFailure.caps;
            copy.internalFree = dashDiagLastAllocationFailure.internalFree;
            copy.internalLargest = dashDiagLastAllocationFailure.internalLargest;
            for (size_t i = 0; i < sizeof(copy.functionName); i++)
                copy.functionName[i] = dashDiagLastAllocationFailure.functionName[i];
            const uint32_t after = __atomic_load_n(&dashDiagLastAllocationFailure.sequence,
                                                    __ATOMIC_ACQUIRE);
            if (before == after && !(after & 1U))
                return copy;
        }
        return {};
    }

private:
    static void fillRegion(DashDiagHeapRegion &region, uint32_t caps)
    {
        multi_heap_info_t info = {};
        heap_caps_get_info(&info, caps);
        region.total = static_cast<uint32_t>(info.total_allocated_bytes + info.total_free_bytes);
        region.free = static_cast<uint32_t>(info.total_free_bytes);
        region.minimum = static_cast<uint32_t>(info.minimum_free_bytes);
        region.largest = static_cast<uint32_t>(info.largest_free_block);
        region.allocated = static_cast<uint32_t>(info.total_allocated_bytes);
        region.allocatedBlocks = static_cast<uint32_t>(info.allocated_blocks);
        region.freeBlocks = static_cast<uint32_t>(info.free_blocks);
        region.totalBlocks = static_cast<uint32_t>(info.total_blocks);
    }

    DashDiagSample captureSample(uint32_t now, const DashDiagRuntimeState &state) const
    {
        const DashDiagHeapSnapshot heap = heapSnapshot();
        DashDiagSample sample;
        sample.uptimeMs = now;
        sample.internalFree = heap.internal.free;
        sample.internalMinimum = heap.internal.minimum;
        sample.internalLargest = heap.internal.largest;
        sample.internalAllocated = heap.internal.allocated;
        sample.dmaFree = heap.dma.free;
        sample.dmaLargest = heap.dma.largest;
        sample.psramFree = heap.psram.free;
        sample.psramLargest = heap.psram.largest;
        sample.rxCount = state.rxCount;
        sample.txCount = state.txCount;
        sample.txErrorCount = state.txErrorCount;
        sample.bleReconnectCount = state.bleReconnectCount;
        sample.bleDisconnectCount = state.bleDisconnectCount;
        sample.gatewayPendingFull = state.gatewayPendingFull;
        sample.gatewayTimeouts = state.gatewayTimeouts;
        sample.taskCount = static_cast<uint16_t>(uxTaskGetNumberOfTasks());
        sample.internalAllocatedBlocks = static_cast<uint16_t>(heap.internal.allocatedBlocks > UINT16_MAX
                                                                    ? UINT16_MAX
                                                                    : heap.internal.allocatedBlocks);
        sample.internalFreeBlocks = static_cast<uint16_t>(heap.internal.freeBlocks > UINT16_MAX
                                                               ? UINT16_MAX
                                                               : heap.internal.freeBlocks);
        sample.gatewayPending = state.gatewayPending;
        sample.gatewayPendingMax = state.gatewayPendingMax;
        sample.bleDisconnectReason = state.bleDisconnectReason;
        sample.wifiMode = state.wifiMode;
        sample.wifiStatus = state.wifiStatus;
        sample.apClients = state.apClients;
        sample.bleProtocol = state.bleProtocol;
        sample.bleFlags = state.bleFlags;
        sample.wifiRssi = state.wifiRssi;
        sample.bleRssi = state.bleRssi;
        sample.runtimeFlags = (state.otaRunning ? 0x01U : 0U) |
                              (state.canOnline ? 0x02U : 0U) |
                              (state.canWriteEnabled ? 0x04U : 0U);
        return sample;
    }

    void appendSample(const DashDiagSample &sample)
    {
        samples_[sampleHead_] = sample;
        sampleHead_ = (sampleHead_ + 1) % kSampleCapacity;
        if (sampleCount_ < kSampleCapacity)
            sampleCount_++;
    }

    void storeEvent(DashDiagEventCode code,
                    const DashDiagSample &sample,
                    uint32_t argument)
    {
        lastEventCode_ = code;
        if (!events_)
            return;
        DashDiagEvent event;
        event.uptimeMs = sample.uptimeMs;
        event.internalFree = sample.internalFree;
        event.internalLargest = sample.internalLargest;
        const DashDiagSample *previous = latestSample();
        event.deltaFromPrevious = previous
                                      ? static_cast<int32_t>(sample.internalFree) -
                                            static_cast<int32_t>(previous->internalFree)
                                      : 0;
        event.argument = argument;
        event.taskCount = sample.taskCount;
        event.code = code;
        event.wifiStatus = sample.wifiStatus;
        event.apClients = sample.apClients;
        event.bleProtocol = sample.bleProtocol;
        event.runtimeFlags = sample.runtimeFlags;
        events_[eventHead_] = event;
        eventHead_ = (eventHead_ + 1) % kEventCapacity;
        if (eventCount_ < kEventCapacity)
            eventCount_++;
    }

    static uint32_t previousBootChecksum(const DashDiagPreviousBoot &snapshot)
    {
        return snapshot.uptimeMs ^ snapshot.internalFree ^ snapshot.internalMinimum ^
               snapshot.internalLargest ^ snapshot.taskCount ^ snapshot.runtimeFlags ^
               snapshot.lastEvent ^ 0xA35C9E17UL;
    }

    static bool dashDiagPreviousBootValid(const DashDiagPreviousBoot &snapshot)
    {
        return snapshot.magic == kDashDiagRtcMagic &&
               snapshot.checksum == previousBootChecksum(snapshot);
    }

    static void updateRtc(const DashDiagSample &sample, uint32_t lastEvent)
    {
        dashDiagRtcSnapshot.magic = 0;
        dashDiagRtcSnapshot.uptimeMs = sample.uptimeMs;
        dashDiagRtcSnapshot.internalFree = sample.internalFree;
        dashDiagRtcSnapshot.internalMinimum = sample.internalMinimum;
        dashDiagRtcSnapshot.internalLargest = sample.internalLargest;
        dashDiagRtcSnapshot.taskCount = sample.taskCount;
        dashDiagRtcSnapshot.runtimeFlags = sample.runtimeFlags;
        dashDiagRtcSnapshot.lastEvent = lastEvent;
        dashDiagRtcSnapshot.checksum = previousBootChecksum(dashDiagRtcSnapshot);
        dashDiagRtcSnapshot.magic = kDashDiagRtcMagic;
    }

    DashDiagSample *samples_ = nullptr;
    DashDiagEvent *events_ = nullptr;
    size_t sampleHead_ = 0;
    size_t sampleCount_ = 0;
    size_t eventHead_ = 0;
    size_t eventCount_ = 0;
    uint32_t lastSampleMs_ = 0;
    uint32_t lastAllocationFailureSequence_ = 0;
    uint16_t lastTaskCount_ = 0;
    uint8_t memoryPressure_ = 0;
    DashDiagEventCode lastEventCode_ = DASH_DIAG_EVENT_BOOT;
    DashDiagRuntimeState lastRuntimeState_ = {};
    DashDiagPreviousBoot previousBoot_ = {};
    bool initialized_ = false;
    bool haveRuntimeState_ = false;
    bool hasPreviousBoot_ = false;
};

#endif
