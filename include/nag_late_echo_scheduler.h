#pragma once

#include <cstdint>

class NagLateEchoScheduler
{
public:
    enum PollResult : uint8_t
    {
        POLL_IDLE = 0,
        POLL_WAITING = 1,
        POLL_DUE = 2,
        POLL_EXPIRED = 3,
    };

    static constexpr uint32_t kMinimumSampleUs = 5000U;
    static constexpr uint32_t kMaximumSampleUs = 20000U;
    static constexpr uint8_t kReadySampleCount = 8U;
    static constexpr uint32_t kLeadUs = 1000U;
    static constexpr uint32_t kExpiryGraceUs = 500U;

    void reset()
    {
        enabled_ = false;
        clearTiming();
        scheduledCount_ = 0;
        earlyCancelCount_ = 0;
        expiredCount_ = 0;
        lastLeadUs_ = 0;
    }

    void setEnabled(bool enabled)
    {
        if (enabled_ == enabled)
            return;
        enabled_ = enabled;
        clearTiming();
        lastLeadUs_ = 0;
    }

    void restartTiming()
    {
        clearTiming();
        lastLeadUs_ = 0;
    }

    bool enabled() const { return enabled_; }
    bool ready() const { return enabled_ && validSampleCount_ >= kReadySampleCount; }
    bool pending() const { return pending_; }
    uint32_t estimatedPeriodUs() const { return estimatedPeriodUs_; }
    uint64_t lastOemUs() const { return lastOemUs_; }
    uint64_t dueUs() const { return dueUs_; }
    uint32_t scheduledCount() const { return scheduledCount_; }
    uint32_t earlyCancelCount() const { return earlyCancelCount_; }
    uint32_t expiredCount() const { return expiredCount_; }
    uint32_t lastLeadUs() const { return lastLeadUs_; }

    void observeOem(uint64_t nowUs)
    {
        if (!enabled_)
            return;

        if (pending_)
        {
            pending_ = false;
            earlyCancelCount_++;
        }

        if (lastOemSeen_)
        {
            const uint64_t wideSample = nowUs - lastOemUs_;
            if (wideSample >= kMinimumSampleUs && wideSample <= kMaximumSampleUs)
            {
                const uint32_t sampleUs = static_cast<uint32_t>(wideSample);
                if (validSampleCount_ == 0U)
                    estimatedPeriodUs_ = sampleUs;
                else
                    estimatedPeriodUs_ =
                        static_cast<uint32_t>((static_cast<uint64_t>(estimatedPeriodUs_) * 7U +
                                               sampleUs) /
                                              8U);
                if (validSampleCount_ < 0xFFU)
                    validSampleCount_++;
            }
        }

        lastOemSeen_ = true;
        lastOemUs_ = nowUs;
    }

    bool arm(uint64_t nowUs)
    {
        (void)nowUs;
        if (!ready() || !lastOemSeen_ || pending_ || estimatedPeriodUs_ <= kLeadUs)
            return false;
        pending_ = true;
        dueUs_ = lastOemUs_ + estimatedPeriodUs_ - kLeadUs;
        expectedNextOemUs_ = lastOemUs_ + estimatedPeriodUs_;
        expiryUs_ = expectedNextOemUs_ + kExpiryGraceUs;
        scheduledCount_++;
        return true;
    }

    PollResult poll(uint64_t nowUs)
    {
        if (!pending_)
            return POLL_IDLE;
        if (nowUs > expiryUs_)
        {
            pending_ = false;
            expiredCount_++;
            return POLL_EXPIRED;
        }
        if (nowUs < dueUs_)
            return POLL_WAITING;

        pending_ = false;
        lastLeadUs_ = nowUs < expectedNextOemUs_
                          ? static_cast<uint32_t>(expectedNextOemUs_ - nowUs)
                          : 0U;
        return POLL_DUE;
    }

    void cancel()
    {
        pending_ = false;
    }

private:
    void clearTiming()
    {
        lastOemSeen_ = false;
        pending_ = false;
        validSampleCount_ = 0;
        estimatedPeriodUs_ = 0;
        lastOemUs_ = 0;
        dueUs_ = 0;
        expectedNextOemUs_ = 0;
        expiryUs_ = 0;
    }

    bool enabled_ = false;
    bool lastOemSeen_ = false;
    bool pending_ = false;
    uint8_t validSampleCount_ = 0;
    uint32_t estimatedPeriodUs_ = 0;
    uint64_t lastOemUs_ = 0;
    uint64_t dueUs_ = 0;
    uint64_t expectedNextOemUs_ = 0;
    uint64_t expiryUs_ = 0;
    uint32_t scheduledCount_ = 0;
    uint32_t earlyCancelCount_ = 0;
    uint32_t expiredCount_ = 0;
    uint32_t lastLeadUs_ = 0;
};
