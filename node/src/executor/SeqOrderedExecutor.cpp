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

    void SeqOrderedExecutor::spin()
    {
        if (spinning_.exchange(true))
            return;

        // Pre-allocate drain buffer
        drain_buffer_.reserve(kMaxDrainPerSubscriber * 2);

        while (spinning_)
        {
            // Strategy: "Execute first, drain on gap"
            // Try to execute consecutive entries first
            size_t executed = executeConsecutive();
            
            if (executed > 0)
            {
                // Made progress executing, continue without waiting
                continue;
            }
            
            // Can't execute (gap at next_seq), need to drain more subscribers
            // Try non-blocking drain first
            SubscriberBase* sub = nullptr;
            if (ready_sem_.try_acquire_for(std::chrono::milliseconds(0)))
            {
                if (ready_queue_.try_dequeue(sub) && sub)
                {
                    drainOneSubscriber(sub);
                    continue;  // Try executing again
                }
            }
            
            // No ready subscribers available, wait with timeout
            sub = waitOneReadyTimeout(std::chrono::milliseconds(1));
            if (sub)
            {
                drainOneSubscriber(sub);
            }
        }
    }

    bool SeqOrderedExecutor::spinSome()
    {
        if (!spinning_)
            return false;

        bool made_progress = false;

        // Strategy: "Execute first, drain on gap"
        // 1. Try to execute as many consecutive entries as possible
        size_t executed = executeConsecutive();
        if (executed > 0)
        {
            made_progress = true;
        }

        // 2. If we can't execute (gap), drain one ready subscriber
        SubscriberBase* sub = nullptr;
        if (ready_sem_.try_acquire_for(std::chrono::milliseconds(0)))
        {
            if (ready_queue_.try_dequeue(sub) && sub)
            {
                if (drainOneSubscriber(sub))
                {
                    made_progress = true;
                }
                
                // After draining, try executing again
                executed = executeConsecutive();
                if (executed > 0)
                {
                    made_progress = true;
                }
            }
        }

        // 3. If no progress, wait briefly for new ready subscriber
        if (!made_progress)
        {
            sub = waitOneReadyTimeout(std::chrono::milliseconds(1));
            if (sub)
            {
                drainOneSubscriber(sub);
                executeConsecutive();
                made_progress = true;
            }
        }

        return spinning_;
    }

    void SeqOrderedExecutor::handleSubscriber(SubscriberBase* sub)
    {
        // Delegate to bounded drain
        drainOneSubscriber(sub);
    }

    bool SeqOrderedExecutor::drainOneSubscriber(SubscriberBase* sub)
    {
        if (!sub)
            return false;

        // Clear and reuse drain buffer
        drain_buffer_.clear();
        
        // Bounded drain: only drain a small batch to enable round-robin
        size_t drained = sub->drainExecSome(drain_buffer_, kMaxDrainPerSubscriber);
        
        // Put all drained entries into reorder buffer
        for (auto& e : drain_buffer_)
        {
            buffer_.put(std::move(e));
        }
        
        return drained > 0;
    }

    size_t SeqOrderedExecutor::executeConsecutive()
    {
        size_t executed = 0;
        ExecEntry entry;
        
        // Execute as many consecutive entries as available
        while (buffer_.try_pop_next(entry))
        {
            entry.invoke(entry.obj, entry.msg);
            ++executed;
        }
        
        return executed;
    }

} // namespace lux::communication
