#include "lux/communication/executor/TimeOrderedExecutor.hpp"
#include "lux/communication/SubscriberBase.hpp"

namespace lux::communication 
{
    TimeOrderedExecutor::TimeOrderedExecutor(std::chrono::nanoseconds time_offset)
        : time_offset_(time_offset)
    {
        spinning_.store(false);
        drain_buffer_.reserve(64);
    }

    TimeOrderedExecutor::~TimeOrderedExecutor()
    {
        stop();
    }

    void TimeOrderedExecutor::spinSome()
    {
        // Drain all currently ready subscribers (non-blocking).
        SubscriberBase* sub = nullptr;
        while (ready_queue_.try_dequeue(sub))
        {
            (void)ready_sem_.try_acquire_for(std::chrono::milliseconds(0));
            if (sub)
                handleSubscriber(sub);
        }
        processReadyEntries();
    }

    void TimeOrderedExecutor::spin()
    {
        if (spinning_.exchange(true))
            return;

        while (spinning_)
        {
            // Execute any buffered entries that are ready
            processReadyEntries();

            // Try to drain ready subscribers without blocking
            SubscriberBase* sub = nullptr;
            bool got_work = false;
            while (ready_queue_.try_dequeue(sub))
            {
                (void)ready_sem_.try_acquire_for(std::chrono::milliseconds(0));
                if (sub)
                {
                    handleSubscriber(sub);
                    got_work = true;
                }
            }

            if (got_work)
                continue;

            // No work available — block-wait for the next ready subscriber.
            // This avoids 100% CPU polling when idle.
            sub = waitOneReady();
            if (!spinning_)
                break;
            if (sub)
                handleSubscriber(sub);
        }
    }

    void TimeOrderedExecutor::stop()
    {
        if (spinning_.exchange(false))
        {
            ready_sem_.release();
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

        // offset=0 fast path: invoke callbacks directly, no reorder needed.
        // This avoids std::function allocation overhead from drainAll().
        if (time_offset_.count() == 0)
        {
            sub->takeAll();
            return;
        }

        drain_buffer_.clear();
        sub->drainAll(drain_buffer_);

        for (auto& e : drain_buffer_)
        {
            if (e.timestamp_ns > max_timestamp_seen_)
                max_timestamp_seen_ = e.timestamp_ns;
            buffer_.push(std::move(e));
        }
    }

    void TimeOrderedExecutor::processReadyEntries()
    {
        if (time_offset_.count() == 0)
            return;  // offset=0 path executes directly in handleSubscriber

        uint64_t cutoff = 0;
        if (max_timestamp_seen_ > static_cast<uint64_t>(time_offset_.count()))
            cutoff = max_timestamp_seen_ - static_cast<uint64_t>(time_offset_.count());

        while (!buffer_.empty() && buffer_.top().timestamp_ns <= cutoff)
        {
            TimeExecEntry entry = std::move(const_cast<TimeExecEntry&>(buffer_.top()));
            buffer_.pop();
            if (entry.invoker) entry.invoker();
        }
    }

} // namespace lux::communication
