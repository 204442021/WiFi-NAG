#pragma once

#include <cstring>
#include "../can_frame_types.h"
#include "can_driver.h"
#include "twai_filter.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <driver/twai.h>
#pragma GCC diagnostic pop
#include <driver/gpio.h>
#include <esp_intr_alloc.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#ifndef TWAI_RX_QUEUE_LEN
#define TWAI_RX_QUEUE_LEN 32
#endif
#ifndef TWAI_TX_QUEUE_LEN
#define TWAI_TX_QUEUE_LEN 16
#endif
#ifndef TWAI_READ_DRAIN_BUDGET
#define TWAI_READ_DRAIN_BUDGET TWAI_RX_QUEUE_LEN
#endif

class TWAIDriver : public CanDriver
{
public:
    static constexpr bool kSupportsISR = false;

    TWAIDriver(gpio_num_t txPin, gpio_num_t rxPin)
        : txPin_(txPin), rxPin_(rxPin)
    {
        t_config_ = TWAI_TIMING_CONFIG_500KBITS();
        f_config_ = TWAI_FILTER_CONFIG_ACCEPT_ALL();
        // Keep the transceiver recessive during the startup delay and before
        // the TWAI peripheral claims the TX GPIO.
        forceTxRecessiveLocked();
    }

    bool init() override
    {
        if (!mutex_)
            mutex_ = xSemaphoreCreateMutex();
        if (!mutex_)
            return false;

        lock();
        shutdown_ = false;
        writeEnabled_ = false; // Safe boot: real hardware listen-only first.
        driverOK_ = installAndStartLocked();
        unlock();
        return driverOK_;
    }

    void setFilters(const uint32_t *ids, uint8_t count) override
    {
        if (!ids || count == 0)
            return;

        const TwaiFilterResult nextFilter = computeTwaiFilter(ids, count);
        const uint8_t nextExactCount = (count < kMaxExactFilters) ? count : kMaxExactFilters;

        lock();
        const bool sameFilter = filterConfigured_ &&
                                f_config_.acceptance_code == nextFilter.acceptance_code &&
                                f_config_.acceptance_mask == nextFilter.acceptance_mask &&
                                f_config_.single_filter &&
                                exactFilterListMatchesLocked(ids, nextExactCount);
        if (sameFilter)
        {
            unlock();
            return;
        }

        const bool reinstall = driverInstalled_ && !shutdown_;
        if (reinstall)
        {
            waitForBusIdleLocked();
            stopAndUninstallLocked();
        }

        exactFilterCount_ = nextExactCount;
        for (uint8_t i = 0; i < exactFilterCount_; i++)
            exactFilterIds_[i] = ids[i];
        f_config_.acceptance_code = nextFilter.acceptance_code;
        f_config_.acceptance_mask = nextFilter.acceptance_mask;
        f_config_.single_filter = true;
        filterConfigured_ = true;

        if (reinstall)
            driverOK_ = installAndStartLocked();
        unlock();
    }

    bool setWriteEnabled(bool enabled) override
    {
        lock();
        if (shutdown_)
        {
            const bool safeStateRequested = !enabled;
            unlock();
            return safeStateRequested;
        }

        if (driverInstalled_ && driverOK_ && writeEnabled_ == enabled)
        {
            unlock();
            return true;
        }

        if (driverInstalled_)
        {
            waitForBusIdleLocked();
            stopAndUninstallLocked();
        }

        writeEnabled_ = enabled;
        driverOK_ = installAndStartLocked();
        const bool ok = driverOK_;
        unlock();
        return ok;
    }

    void prepareForRestart() override
    {
        lock();
        if (!shutdown_)
        {
            shutdown_ = true;
            writeEnabled_ = false;
            if (driverInstalled_)
            {
                waitForBusIdleLocked();
                stopAndUninstallLocked();
            }
        }
        // Idempotent fallback: keep TX recessive even if the driver was never
        // installed, failed, or this function is called more than once.
        forceTxRecessiveLocked();
        unlock();
    }

    bool enableInterrupt(void (* /*onReady*/)()) override { return false; }

    bool read(CanFrame &frame) override
    {
        for (uint16_t attempt = 0; attempt < kReadDrainBudget; attempt++)
        {
            lock();
            if (shutdown_)
            {
                unlock();
                return false;
            }
            if (!driverOK_)
            {
                tryRecover();
                unlock();
                return false;
            }

            twai_message_t msg;
            if (twai_receive(&msg, 0) != ESP_OK)
            {
                if (isBusOff())
                    recoverWithCooldown();
                unlock();
                return false;
            }
            const bool accepted = !msg.extd && !msg.rtr &&
                                  exactFilterMatchesLocked(msg.identifier);
            unlock();

            if (!accepted)
                continue;

            frame.id = msg.identifier;
            frame.dlc = (msg.data_length_code <= 8) ? msg.data_length_code : 8;
            std::memset(frame.data, 0, 8);
            std::memcpy(frame.data, msg.data, frame.dlc);
            return true;
        }

        return false;
    }

    bool send(const CanFrame &frame) override
    {
        lock();
        if (!driverOK_ || !writeEnabled_ || shutdown_)
        {
            unlock();
            if (onSendFrame)
                onSendFrame(frame, false);
            return false;
        }

        twai_message_t msg = {};
        const uint8_t dlc = (frame.dlc <= 8) ? frame.dlc : 8;
        msg.identifier = frame.id;
        msg.data_length_code = dlc;
        std::memcpy(msg.data, frame.data, dlc);

        // Short timeout (2ms): modified frames should not be dropped, but
        // long blocks risk overflowing the RX queue.
        const bool ok = twai_transmit(&msg, pdMS_TO_TICKS(2)) == ESP_OK;
        if (!ok && isBusOff())
            recoverWithCooldown();
        unlock();
        if (onSendFrame)
            onSendFrame(frame, ok);
        return ok;
    }

private:
    static constexpr uint8_t kMaxExactFilters = 32;
    static constexpr uint16_t kReadDrainBudget = TWAI_READ_DRAIN_BUDGET;
    static constexpr uint32_t kBusOffCooldownMs = 1000;
    // At 500 kbit/s a bit is 2 us. Six consecutive recessive bits cannot
    // occur in the stuffed payload, so this detects EOF/intermission/idle.
    static constexpr int64_t kBusIdleStableUs = 12;
    static constexpr int64_t kBusIdleTimeoutUs = 5000;

    bool exactFilterListMatchesLocked(const uint32_t *ids, uint8_t count) const
    {
        if (exactFilterCount_ != count)
            return false;
        for (uint8_t i = 0; i < count; i++)
        {
            if (exactFilterIds_[i] != ids[i])
                return false;
        }
        return true;
    }

    bool exactFilterMatchesLocked(uint32_t id) const
    {
        return exactCanIdMatches(exactFilterIds_, exactFilterCount_, id);
    }

    bool waitForBusIdleLocked() const
    {
        const int64_t started = esp_timer_get_time();
        int64_t recessiveSince = -1;
        while (esp_timer_get_time() - started < kBusIdleTimeoutUs)
        {
            const int64_t now = esp_timer_get_time();
            if (gpio_get_level(rxPin_) != 0)
            {
                if (recessiveSince < 0)
                    recessiveSince = now;
                if (now - recessiveSince >= kBusIdleStableUs)
                    return true;
            }
            else
            {
                recessiveSince = -1;
            }
        }
        return false;
    }

    void forceTxRecessiveLocked() const
    {
        gpio_reset_pin(txPin_);
        gpio_pullup_en(txPin_);
        // Latch HIGH before enabling output to avoid a dominant edge.
        gpio_set_level(txPin_, 1);
        gpio_set_direction(txPin_, GPIO_MODE_OUTPUT);
        gpio_set_level(txPin_, 1);
    }

    bool isBusOff()
    {
        if (!driverInstalled_)
            return false;
        twai_status_info_t status;
        if (twai_get_status_info(&status) != ESP_OK)
            return false;
        return status.state == TWAI_STATE_BUS_OFF;
    }

    void recoverWithCooldown()
    {
        if (shutdown_)
            return;
        const uint32_t now = millis();
        if (now - lastRecovery_ < kBusOffCooldownMs)
            return;
        lastRecovery_ = now;

        stopAndUninstallLocked();
        driverOK_ = installAndStartLocked();
    }

    void tryRecover()
    {
        if (shutdown_)
            return;
        const uint32_t now = millis();
        if (now - lastRecovery_ < kBusOffCooldownMs * 10)
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

    void configureGeneralLocked()
    {
        const twai_mode_t mode = writeEnabled_ ? TWAI_MODE_NORMAL : TWAI_MODE_LISTEN_ONLY;
        g_config_ = TWAI_GENERAL_CONFIG_DEFAULT(txPin_, rxPin_, mode);
        g_config_.rx_queue_len = TWAI_RX_QUEUE_LEN;
        g_config_.tx_queue_len = writeEnabled_ ? TWAI_TX_QUEUE_LEN : 0;
        g_config_.intr_flags |= ESP_INTR_FLAG_IRAM;
    }

    bool installAndStartLocked()
    {
        if (shutdown_)
            return false;

        configureGeneralLocked();
        if (twai_driver_install(&g_config_, &t_config_, &f_config_) != ESP_OK)
        {
            driverInstalled_ = false;
            forceTxRecessiveLocked();
            return false;
        }
        driverInstalled_ = true;
        if (twai_start() != ESP_OK)
        {
            twai_driver_uninstall();
            driverInstalled_ = false;
            forceTxRecessiveLocked();
            return false;
        }
        return true;
    }

    void stopAndUninstallLocked()
    {
        if (driverInstalled_)
        {
            twai_stop();
            twai_driver_uninstall();
        }
        driverInstalled_ = false;
        driverOK_ = false;
        forceTxRecessiveLocked();
    }

    gpio_num_t txPin_;
    gpio_num_t rxPin_;
    twai_general_config_t g_config_ = {};
    twai_timing_config_t t_config_ = {};
    twai_filter_config_t f_config_ = {};
    SemaphoreHandle_t mutex_ = nullptr;
    bool driverInstalled_ = false;
    bool driverOK_ = false;
    bool writeEnabled_ = false;
    bool shutdown_ = false;
    bool filterConfigured_ = false;
    uint32_t lastRecovery_ = 0;
    uint32_t exactFilterIds_[kMaxExactFilters] = {};
    uint8_t exactFilterCount_ = 0;
};
