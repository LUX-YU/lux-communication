#include "lux/communication/executor/TimeOrderedExecutor.hpp"
#include "lux/communication/SubscriberBase.hpp"

namespace lux::communication 
{
    TimeOrderedExecutor::TimeOrderedExecutor(std::chrono::nanoseconds time_offset)
        : time_offset_(time_offset)
    {
        spinning_.store(false);
    }

    TimeOrderedExecutor::~TimeOrderedExecutor()
    {
        stop();
    }

    bool TimeOrderedExecutor::spinSome()
    {
        if (!spinning_)
        {
            return false;
        }
        auto sub = waitOneReadyTimeout(std::chrono::milliseconds(1));
        if (sub)
        {
            handleSubscriber(sub);
        }
        processReadyEntries();
        return spinning_;
    }

    void TimeOrderedExecutor::spin()
    {
        if (spinning_.exchange(true))
            return;

        while (spinning_)
        {
            spinSome();
            if (!spinning_)
                break;
            doWait();
        }
    }

    void TimeOrderedExecutor::stop()
    {
        if (spinning_.exchange(false))
        {
            notifyCondition();
        }
    }

    void TimeOrderedExecutor::setTimeOffset(std::chrono::nanoseconds offset)
    {
        time_offset_ = offset;
    }

    std::chrono::nanoseconds TimeOrderedExecutor::getTimeOffset() const
    {
        return time_offset_;
    }

    bool TimeOrderedExecutor::checkRunnable()
    {
        return !buffer_.empty();
    }

    void TimeOrderedExecutor::handleSubscriber(SubscriberBase* sub)
    {
        if (!sub)
            return;

        std::vector<TimeExecEntry> entries;
        entries.reserve(16);
        sub->drainAll(entries);
        for (auto& e : entries)
        {
            if (e.timestamp_ns > max_timestamp_seen_)
            {
                max_timestamp_seen_ = e.timestamp_ns;
            }
            buffer_.push(std::move(e));
        }
    }

    void TimeOrderedExecutor::processReadyEntries()
    {
        uint64_t cutoff = 0;
        if (time_offset_.count() == 0)
        {
            cutoff = UINT64_MAX;
        }
        else
        {
            if (max_timestamp_seen_ > static_cast<uint64_t>(time_offset_.count()))
            {
                cutoff = max_timestamp_seen_ - static_cast<uint64_t>(time_offset_.count());
            }
            else
            {
                cutoff = 0;
            }
        }

        while (!buffer_.empty() && buffer_.top().timestamp_ns <= cutoff)
        {
            // Fix UB: copy the entry before pop to avoid dangling reference
            TimeExecEntry entry = std::move(const_cast<TimeExecEntry&>(buffer_.top()));
            buffer_.pop();
            if (entry.invoker) entry.invoker();
        }
    }

    void TimeOrderedExecutor::doWait()
    {
        return;
    }

} // namespace lux::communication
