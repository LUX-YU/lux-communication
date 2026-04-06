#include "lux/communication/executor/SeqOrderedExecutor.hpp"
#include "lux/communication/SubscriberBase.hpp"

namespace lux::communication 
{
    SeqOrderedExecutor::~SeqOrderedExecutor()
    {
        stop();
    }

    bool SeqOrderedExecutor::checkRunnable()
    {
        return buffer_.pending_size() > 0 || ready_queue_.size_approx() > 0;
    }

    void SeqOrderedExecutor::stop()
    {
        if (spinning_.exchange(false))
        {
            ready_sem_.release();
            notifyCondition();
        }
    }

    // ── Helpers ──

    size_t SeqOrderedExecutor::collectUniqueReady()
    {
        size_t n = 0;
        SubscriberBase* sub = nullptr;
        while (n < kMaxReadyBatch && ready_queue_.try_dequeue(sub))
        {
            (void)ready_sem_.try_acquire_for(std::chrono::milliseconds(0));
            if (!sub) continue;

            // Linear-scan dedup (batch is tiny, typically 2-4 subscribers)
            bool dup = false;
            for (size_t j = 0; j < n; ++j)
            {
                if (ready_batch_[j] == sub) { dup = true; break; }
            }
            if (!dup)
                ready_batch_[n++] = sub;
        }
        return n;
    }

    void SeqOrderedExecutor::drainAndExecute(size_t batch_count)
    {
        // Track per-subscriber high-water mark for adaptive throttle.
        uint64_t sub_max_seq[kMaxReadyBatch] = {};

        bool any_drained = true;
        while (any_drained)
        {
            any_drained = false;
            const uint64_t next = buffer_.next_seq();

            for (size_t i = 0; i < batch_count; ++i)
            {
                // Adaptive: if this subscriber is far ahead, throttle to 1
                // so the gap-filling subscriber(s) can catch up.
                size_t count = kMaxDrainPerSubscriber;
                if (sub_max_seq[i] > next &&
                    (sub_max_seq[i] - next) >= kMaxWindow)
                {
                    count = 1;
                }

                drain_buffer_.clear();
                size_t n = ready_batch_[i]->drainExecSome(
                    drain_buffer_, count);
                if (n > 0)
                {
                    any_drained = true;
                    if (!drain_buffer_.empty())
                        sub_max_seq[i] = drain_buffer_.back().seq;
                    for (auto& e : drain_buffer_)
                        buffer_.put(std::move(e));
                }
            }
            executeConsecutive();

            // Absorb any newly-ready subscribers (handles the race where
            // a subscriber is notified after we started this loop).
            SubscriberBase* sub = nullptr;
            while (batch_count < kMaxReadyBatch &&
                   ready_queue_.try_dequeue(sub))
            {
                (void)ready_sem_.try_acquire_for(
                    std::chrono::milliseconds(0));
                if (!sub) continue;
                bool dup = false;
                for (size_t j = 0; j < batch_count; ++j)
                    if (ready_batch_[j] == sub) { dup = true; break; }
                if (!dup)
                {
                    ready_batch_[batch_count] = sub;
                    sub_max_seq[batch_count] = 0;
                    ++batch_count;
                    any_drained = true;
                }
            }
        }
    }

    // ── Main loops ──

    void SeqOrderedExecutor::spin()
    {
        if (spinning_.exchange(true))
            return;

        drain_buffer_.reserve(kMaxDrainPerSubscriber * 2);

        while (spinning_)
        {
            // Execute consecutive entries first
            if (executeConsecutive() > 0)
                continue;

            // Gap at next_seq — collect unique ready subscribers, then interleaved drain
            size_t batch_count = collectUniqueReady();
            if (batch_count > 0)
            {
                drainAndExecute(batch_count);
                continue;
            }
            
            // No ready subscribers available, block-wait for one
            auto sub = waitOneReady();
            if (sub)
                drainOneSubscriber(sub);
        }
    }

    void SeqOrderedExecutor::spinSome()
    {
        for (;;)
        {
            // Collect unique ready subscribers (deduped)
            size_t batch_count = collectUniqueReady();

            if (batch_count > 0)
            {
                drainAndExecute(batch_count);
                continue;
            }

            // No more ready subscribers — execute any remaining entries
            if (executeConsecutive() == 0)
                break;
        }
    }

    void SeqOrderedExecutor::handleSubscriber(SubscriberBase* sub)
    {
        drainOneSubscriber(sub);
    }

    bool SeqOrderedExecutor::drainOneSubscriber(SubscriberBase* sub)
    {
        if (!sub)
            return false;

        drain_buffer_.clear();
        size_t drained = sub->drainExecSome(drain_buffer_, kMaxDrainPerSubscriber);
        
        for (auto& e : drain_buffer_)
            buffer_.put(std::move(e));
        
        return drained > 0;
    }

    size_t SeqOrderedExecutor::executeConsecutive()
    {
        size_t executed = 0;
        ExecEntry entry;
        
        while (buffer_.try_pop_next(entry))
        {
            entry.invoke(entry.obj, std::move(entry.msg));
            ++executed;
        }
        
        return executed;
    }

} // namespace lux::communication
