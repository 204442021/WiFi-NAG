#pragma once

#include "../can_frame_types.h"
#include "can_driver.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <driver/twai.h>
#pragma GCC diagnostic pop
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#ifndef TWAI_RX_QUEUE_LEN
#define TWAI_RX_QUEUE_LEN 32
#endif
#ifndef TWAI_TX_QUEUE_LEN
#define TWAI_TX_QUEUE_LEN 1
#endif
#ifndef TWAI_READ_DRAIN_BUDGET
#define TWAI_READ_DRAIN_BUDGET TWAI_RX_QUEUE_LEN
#endif

class TWAIDriver : public CanDriver
{
public:
    static constexpr bool kSupportsISR = false;

    TWAIDriver(gpio_num_t txPin, gpio_num_t rxPin)
        : txPin_(txPin), rxPin_(rxPin) {}

    bool init() override
    {
        if (!mutex_)
            mutex_ = xSemaphoreCreateMutex();
        if (!mutex_)
            return false;

        g_config_ = TWAI_GENERAL_CONFIG_DEFAULT(txPin_, rxPin_, TWAI_MODE_NORMAL);
        g_config_.rx_queue_len = TWAI_RX_QUEUE_LEN;
        g_config_.tx_queue_len = TWAI_TX_QUEUE_LEN;
        g_config_.alerts_enabled = kDiagnosticAlerts;

        t_config_ = TWAI_TIMING_CONFIG_500KBITS();
        f_config_ = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        lock();
        driverOK_ = installAndStartLocked();
        unlock();
        return driverOK_;
    }

    void setFilters(const uint32_t *ids, uint8_t count) override
    {
        if (count == 0)
            return;

        uint32_t differ = 0;
        for (uint8_t i = 1; i < count; i++)
        {
            differ |= ids[0] ^ ids[i];
        }

        uint32_t base = ids[0] & ~differ;
        twai_filter_config_t nextFilter = f_config_;
        nextFilter.acceptance_code = base << 21;
        nextFilter.acceptance_mask = (differ << 21) | 0x001FFFFF;
        nextFilter.single_filter = true;

        lock();
        // TWAI only has a mask filter; sparse ID sets can pass false positives.
        exactFilterCount_ = (count < kMaxExactFilters) ? count : kMaxExactFilters;
        for (uint8_t i = 0; i < exactFilterCount_; i++)
            exactFilterIds_[i] = ids[i];
        f_config_ = nextFilter;
        stopAndUninstallLocked();
        driverOK_ = installAndStartLocked();
        unlock();
    }

    bool enableInterrupt(void (* /*onReady*/)()) override { return false; }

    bool read(CanFrame &frame) override
    {
        for (uint16_t attempt = 0; attempt < kReadDrainBudget; attempt++)
        {
            lock();
            if (!driverOK_)
            {
                tryRecover();
                unlock();
                return false;
            }

            collectAlertsLocked();

            twai_message_t msg;
            if (twai_receive(&msg, 0) != ESP_OK)
            {
                if (isBusOff())
                    recoverWithCooldown();
                unlock();
                return false;
            }
            bool accepted = exactFilterMatchesLocked(msg.identifier);
            unlock();

            if (!accepted)
                continue;

            frame.id = msg.identifier;
            frame.dlc = (msg.data_length_code <= 8) ? msg.data_length_code : 8;
            memset(frame.data, 0, 8);
            memcpy(frame.data, msg.data, frame.dlc);
            return true;
        }

        return false;
    }

    bool send(const CanFrame &frame) override
    {
        lock();
        if (!driverOK_)
        {
            unlock();
            if (onSendFrame)
                onSendFrame(frame, false);
            return false;
        }

        collectAlertsLocked();
        if (safetyTripped_)
        {
            unlock();
            return false;
        }

        twai_status_info_t status = {};
        if (readStatusLocked(status))
        {
            evaluateSafetyLocked(status);
            if (safetyTripped_)
            {
                unlock();
                return false;
            }

            // Do not queue a new Nag echo behind an older frame that is still
            // waiting for arbitration or transmission completion.
            if (status.msgs_to_tx > 0)
            {
                staleDropCount_++;
                unlock();
                return false;
            }
        }

        twai_message_t msg = {};
        uint8_t dlc = (frame.dlc <= 8) ? frame.dlc : 8;
        msg.identifier = frame.id;
        msg.data_length_code = dlc;
        memcpy(msg.data, frame.data, dlc);

        // A delayed echo is less useful than a dropped echo. Never block the
        // CAN receive task waiting for TX queue space.
        bool ok = twai_transmit(&msg, 0) == ESP_OK;
        if (!ok)
        {
            if (isBusOff())
                recoverWithCooldown();
        }
        unlock();
        if (onSendFrame)
            onSendFrame(frame, ok);
        return ok;
    }

    bool getDiagnostics(CanDriverDiagnostics &out) override
    {
        lock();
        out = {};
        out.busOffCount = counterDelta(busOffCount_, baselineBusOffCount_);
        out.recoveryCount = counterDelta(recoveryCount_, baselineRecoveryCount_);
        out.errorWarningCount = counterDelta(errorWarningCount_, baselineErrorWarningCount_);
        out.errorPassiveCount = counterDelta(errorPassiveCount_, baselineErrorPassiveCount_);
        out.staleDropCount = counterDelta(staleDropCount_, baselineStaleDropCount_);
        out.safetyTripped = safetyTripped_;
        out.safetyReason = safetyReason_;
        out.safetyTripCount = counterDelta(safetyTripCount_, baselineSafetyTripCount_);
        out.txFailedCount = counterDelta(accumulatedTxFailedCount_, baselineTxFailedCount_);
        out.rxMissedCount = counterDelta(accumulatedRxMissedCount_, baselineRxMissedCount_);
        out.rxOverrunCount = counterDelta(accumulatedRxOverrunCount_, baselineRxOverrunCount_);
        out.arbLostCount = counterDelta(accumulatedArbLostCount_, baselineArbLostCount_);
        out.busErrorCount = counterDelta(accumulatedBusErrorCount_, baselineBusErrorCount_);

        collectAlertsLocked(true);

        twai_status_info_t status = {};
        bool ok = readStatusLocked(status);
        if (ok)
        {
            out.available = true;
            out.state = mapState(status.state);
            out.msgsToTx = status.msgs_to_tx;
            out.msgsToRx = status.msgs_to_rx;
            out.txErrorCounter = status.tx_error_counter;
            out.rxErrorCounter = status.rx_error_counter;
            out.txFailedCount = counterDelta(accumulatedTxFailedCount_ + status.tx_failed_count,
                                             baselineTxFailedCount_);
            out.rxMissedCount = counterDelta(accumulatedRxMissedCount_ + status.rx_missed_count,
                                             baselineRxMissedCount_);
            out.rxOverrunCount = counterDelta(accumulatedRxOverrunCount_ + status.rx_overrun_count,
                                              baselineRxOverrunCount_);
            out.arbLostCount = counterDelta(accumulatedArbLostCount_ + status.arb_lost_count,
                                            baselineArbLostCount_);
            out.busErrorCount = counterDelta(accumulatedBusErrorCount_ + status.bus_error_count,
                                             baselineBusErrorCount_);
            out.busOffCount = counterDelta(busOffCount_, baselineBusOffCount_);
        }
        out.busOffCount = counterDelta(busOffCount_, baselineBusOffCount_);
        out.recoveryCount = counterDelta(recoveryCount_, baselineRecoveryCount_);
        out.errorWarningCount = counterDelta(errorWarningCount_, baselineErrorWarningCount_);
        out.errorPassiveCount = counterDelta(errorPassiveCount_, baselineErrorPassiveCount_);
        out.staleDropCount = counterDelta(staleDropCount_, baselineStaleDropCount_);
        out.safetyTripped = safetyTripped_;
        out.safetyReason = safetyReason_;
        out.safetyTripCount = counterDelta(safetyTripCount_, baselineSafetyTripCount_);
        unlock();
        return ok;
    }

    bool resetDiagnostics() override
    {
        lock();
        collectAlertsLocked(true);

        twai_status_info_t status = {};
        bool hasStatus = readStatusLocked(status);
        baselineTxFailedCount_ = accumulatedTxFailedCount_ + (hasStatus ? status.tx_failed_count : 0);
        baselineRxMissedCount_ = accumulatedRxMissedCount_ + (hasStatus ? status.rx_missed_count : 0);
        baselineRxOverrunCount_ = accumulatedRxOverrunCount_ + (hasStatus ? status.rx_overrun_count : 0);
        baselineArbLostCount_ = accumulatedArbLostCount_ + (hasStatus ? status.arb_lost_count : 0);
        baselineBusErrorCount_ = accumulatedBusErrorCount_ + (hasStatus ? status.bus_error_count : 0);
        baselineBusOffCount_ = busOffCount_;
        baselineRecoveryCount_ = recoveryCount_;
        baselineErrorWarningCount_ = errorWarningCount_;
        baselineErrorPassiveCount_ = errorPassiveCount_;
        baselineStaleDropCount_ = staleDropCount_;
        baselineSafetyTripCount_ = safetyTripCount_;
        unlock();
        return true;
    }

    bool clearSafetyLatch() override
    {
        lock();
        twai_status_info_t status = {};
        bool hasStatus = readStatusLocked(status);
        bool needsRestart = safetyTripped_ &&
                            (!hasStatus ||
                             status.state != TWAI_STATE_RUNNING ||
                             status.tx_error_counter >= kErrorCounterTripThreshold ||
                             status.rx_error_counter >= kErrorCounterTripThreshold);

        if (needsRestart)
        {
            stopAndUninstallLocked();
            driverOK_ = installAndStartLocked();
            status = {};
            hasStatus = driverOK_ && readStatusLocked(status);
        }

        bool safe = hasStatus &&
                    status.state == TWAI_STATE_RUNNING &&
                    status.tx_error_counter < kErrorCounterTripThreshold &&
                    status.rx_error_counter < kErrorCounterTripThreshold;
        if (safe)
        {
            safetyTripped_ = false;
            safetyReason_ = CanSafetyReason::None;
            resetSafetyWindowLocked(status, millis());
        }
        unlock();
        return safe;
    }

private:
    static constexpr uint8_t kMaxExactFilters = 32;
    static constexpr uint16_t kReadDrainBudget = TWAI_READ_DRAIN_BUDGET;
    static constexpr uint32_t BUSOFF_COOLDOWN_MS = 1000;
    static constexpr uint32_t kAlertPollIntervalMs = 100;
    static constexpr uint32_t kSafetyWindowMs = 1000;
    static constexpr uint32_t kErrorCounterTripThreshold = 96;
    static constexpr uint32_t kBusErrorBurstLimit = 10;
    static constexpr uint32_t kTxFailureBurstLimit = 5;
    static constexpr uint32_t kDiagnosticAlerts =
        TWAI_ALERT_BUS_OFF |
        TWAI_ALERT_BUS_RECOVERED |
        TWAI_ALERT_ABOVE_ERR_WARN |
        TWAI_ALERT_ERR_PASS |
        TWAI_ALERT_BUS_ERROR |
        TWAI_ALERT_TX_FAILED |
        TWAI_ALERT_ARB_LOST |
        TWAI_ALERT_RX_QUEUE_FULL |
        TWAI_ALERT_RX_FIFO_OVERRUN;

    static uint32_t counterDelta(uint32_t value, uint32_t baseline)
    {
        return value >= baseline ? value - baseline : value;
    }

    bool exactFilterMatchesLocked(uint32_t id) const
    {
        if (exactFilterCount_ == 0)
            return true;
        for (uint8_t i = 0; i < exactFilterCount_; i++)
        {
            if (exactFilterIds_[i] == id)
                return true;
        }
        return false;
    }

    static CanDriverState mapState(twai_state_t state)
    {
        switch (state)
        {
        case TWAI_STATE_STOPPED:
            return CanDriverState::Stopped;
        case TWAI_STATE_RUNNING:
            return CanDriverState::Running;
        case TWAI_STATE_BUS_OFF:
            return CanDriverState::BusOff;
        case TWAI_STATE_RECOVERING:
            return CanDriverState::Recovering;
        default:
            return CanDriverState::Unavailable;
        }
    }

    void noteBusOffLocked()
    {
        if (busOffSeenThisInstall_)
            return;
        busOffSeenThisInstall_ = true;
        busOffCount_++;
        tripSafetyLocked(CanSafetyReason::BusOff);
    }

    bool readStatusLocked(twai_status_info_t &status)
    {
        if (!driverInstalled_)
            return false;
        if (twai_get_status_info(&status) != ESP_OK)
            return false;
        if (status.state == TWAI_STATE_BUS_OFF)
            noteBusOffLocked();
        return true;
    }

    bool isBusOff()
    {
        twai_status_info_t status = {};
        if (!readStatusLocked(status))
            return false;
        return status.state == TWAI_STATE_BUS_OFF;
    }

    void tripSafetyLocked(CanSafetyReason reason)
    {
        if (safetyTripped_)
            return;
        safetyTripped_ = true;
        safetyReason_ = reason;
        safetyTripCount_++;
        if (onSafetyTrip)
            onSafetyTrip(reason);
    }

    void resetSafetyWindowLocked(const twai_status_info_t &status, uint32_t now)
    {
        safetyWindowStartedAt_ = now;
        safetyWindowBusErrors_ = accumulatedBusErrorCount_ + status.bus_error_count;
        safetyWindowTxFailures_ = accumulatedTxFailedCount_ + status.tx_failed_count;
    }

    void evaluateSafetyLocked(const twai_status_info_t &status)
    {
        if (status.state == TWAI_STATE_BUS_OFF)
        {
            noteBusOffLocked();
            return;
        }
        if (status.tx_error_counter >= kErrorCounterTripThreshold)
        {
            tripSafetyLocked(CanSafetyReason::TxErrorCounter);
            return;
        }
        if (status.rx_error_counter >= kErrorCounterTripThreshold)
        {
            tripSafetyLocked(CanSafetyReason::RxErrorCounter);
            return;
        }

        uint32_t now = millis();
        uint32_t busErrors = accumulatedBusErrorCount_ + status.bus_error_count;
        uint32_t txFailures = accumulatedTxFailedCount_ + status.tx_failed_count;
        if (safetyWindowStartedAt_ == 0 ||
            now - safetyWindowStartedAt_ >= kSafetyWindowMs ||
            busErrors < safetyWindowBusErrors_ ||
            txFailures < safetyWindowTxFailures_)
        {
            resetSafetyWindowLocked(status, now);
            return;
        }

        if (busErrors - safetyWindowBusErrors_ >= kBusErrorBurstLimit)
        {
            tripSafetyLocked(CanSafetyReason::BusErrorBurst);
            return;
        }
        if (txFailures - safetyWindowTxFailures_ >= kTxFailureBurstLimit)
            tripSafetyLocked(CanSafetyReason::TxFailureBurst);
    }

    void collectAlertsLocked(bool force = false)
    {
        if (!driverInstalled_)
            return;
        uint32_t now = millis();
        if (!force && now - lastAlertPollMs_ < kAlertPollIntervalMs)
            return;
        lastAlertPollMs_ = now;

        uint32_t alerts = 0;
        if (twai_read_alerts(&alerts, 0) != ESP_OK)
            return;
        if (alerts & TWAI_ALERT_BUS_OFF)
            noteBusOffLocked();
        if (alerts & TWAI_ALERT_ABOVE_ERR_WARN)
            errorWarningCount_++;
        if (alerts & TWAI_ALERT_ERR_PASS)
            errorPassiveCount_++;
    }

    void accumulateStatusLocked()
    {
        twai_status_info_t status = {};
        if (!readStatusLocked(status))
            return;
        accumulatedTxFailedCount_ += status.tx_failed_count;
        accumulatedRxMissedCount_ += status.rx_missed_count;
        accumulatedRxOverrunCount_ += status.rx_overrun_count;
        accumulatedArbLostCount_ += status.arb_lost_count;
        accumulatedBusErrorCount_ += status.bus_error_count;
    }

    void recoverWithCooldown()
    {
        uint32_t now = millis();
        if (now - lastRecovery_ < BUSOFF_COOLDOWN_MS)
            return;
        lastRecovery_ = now;

        collectAlertsLocked(true);
        noteBusOffLocked();
        stopAndUninstallLocked();
        driverOK_ = installAndStartLocked();
        if (driverOK_)
            recoveryCount_++;
    }

    void tryRecover()
    {
        uint32_t now = millis();
        if (now - lastRecovery_ < BUSOFF_COOLDOWN_MS * 10)
            return;
        lastRecovery_ = now;

        stopAndUninstallLocked();
        driverOK_ = installAndStartLocked();
    }

    void lock()
    {
        if (mutex_)
            xSemaphoreTake(mutex_, portMAX_DELAY);
    }

    void unlock()
    {
        if (mutex_)
            xSemaphoreGive(mutex_);
    }

    bool installAndStartLocked()
    {
        if (twai_driver_install(&g_config_, &t_config_, &f_config_) != ESP_OK)
        {
            driverInstalled_ = false;
            return false;
        }
        driverInstalled_ = true;
        busOffSeenThisInstall_ = false;
        lastAlertPollMs_ = 0;
        safetyWindowStartedAt_ = 0;
        if (twai_start() != ESP_OK)
        {
            twai_driver_uninstall();
            driverInstalled_ = false;
            return false;
        }
        return true;
    }

    void stopAndUninstallLocked()
    {
        if (!driverInstalled_)
            return;
        collectAlertsLocked(true);
        accumulateStatusLocked();
        twai_stop();
        twai_driver_uninstall();
        driverInstalled_ = false;
        driverOK_ = false;
    }

    gpio_num_t txPin_;
    gpio_num_t rxPin_;
    twai_general_config_t g_config_;
    twai_timing_config_t t_config_;
    twai_filter_config_t f_config_;
    SemaphoreHandle_t mutex_ = nullptr;
    bool driverInstalled_ = false;
    bool driverOK_ = false;
    uint32_t lastRecovery_ = 0;
    uint32_t exactFilterIds_[kMaxExactFilters] = {};
    uint8_t exactFilterCount_ = 0;
    uint32_t lastAlertPollMs_ = 0;
    bool busOffSeenThisInstall_ = false;
    uint32_t accumulatedTxFailedCount_ = 0;
    uint32_t accumulatedRxMissedCount_ = 0;
    uint32_t accumulatedRxOverrunCount_ = 0;
    uint32_t accumulatedArbLostCount_ = 0;
    uint32_t accumulatedBusErrorCount_ = 0;
    uint32_t busOffCount_ = 0;
    uint32_t recoveryCount_ = 0;
    uint32_t errorWarningCount_ = 0;
    uint32_t errorPassiveCount_ = 0;
    uint32_t staleDropCount_ = 0;
    bool safetyTripped_ = false;
    CanSafetyReason safetyReason_ = CanSafetyReason::None;
    uint32_t safetyTripCount_ = 0;
    uint32_t safetyWindowStartedAt_ = 0;
    uint32_t safetyWindowBusErrors_ = 0;
    uint32_t safetyWindowTxFailures_ = 0;
    uint32_t baselineTxFailedCount_ = 0;
    uint32_t baselineRxMissedCount_ = 0;
    uint32_t baselineRxOverrunCount_ = 0;
    uint32_t baselineArbLostCount_ = 0;
    uint32_t baselineBusErrorCount_ = 0;
    uint32_t baselineBusOffCount_ = 0;
    uint32_t baselineRecoveryCount_ = 0;
    uint32_t baselineErrorWarningCount_ = 0;
    uint32_t baselineErrorPassiveCount_ = 0;
    uint32_t baselineStaleDropCount_ = 0;
    uint32_t baselineSafetyTripCount_ = 0;
};
