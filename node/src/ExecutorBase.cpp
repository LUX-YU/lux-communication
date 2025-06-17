#include "lux/communication/ExecutorBase.hpp"
#include "lux/communication/intraprocess/Node.hpp"

namespace lux::communication {
    ExecutorBase::ExecutorBase() : running_(false) {}
    ExecutorBase::~ExecutorBase() { stop(); }
    
    void ExecutorBase::addNode(NodeBase* node)
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        auto idx = nodes_.insert(node);
        node->setIdInExecutor(idx);
    }
    
    void ExecutorBase::removeNode(NodeBase* node)
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        if (nodes_.erase(node->idInExecutor()))
        {
            node->setIdInExecutor(std::numeric_limits<size_t>::max());
        }
    }
    
    SubscriberBase* ExecutorBase::waitOneReady()
    {
        ready_sem_.acquire();
        SubscriberBase* sub;
        ready_queue_.try_dequeue(sub);
        return sub;
    }

    void ExecutorBase::enqueueReady(SubscriberBase* sub)
    {
        if (sub)
        {
            ready_queue_.enqueue(std::move(sub));
            ready_sem_.release();
        }
    }

    void ExecutorBase::spin()
    {
        if (running_)
            return;
        running_ = true;

        while (running_)
        {
            auto sub = waitOneReady();
            if (!running_)
                break;
            if (sub)
            {
                handleSubscriber(sub);
            }
        }
    }
    
    void ExecutorBase::stop()
    {
        if (running_.exchange(false))
        {
            ready_sem_.release();
            notifyCondition();
        }
    }
    
    void ExecutorBase::wakeup()
    {
        ready_sem_.release();
    }
    
    void ExecutorBase::waitCondition()
    {
        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait(
            lock,
            [this]
            {
                return !running_.load() || checkRunnable();
            }
        );
    }
    
    void ExecutorBase::notifyCondition()
    {
        std::lock_guard lk(cv_mutex_);
        cv_.notify_all();
    }
    
    bool ExecutorBase::checkRunnable()
    {
        return ready_queue_.size_approx() > 0;
    }
    
    SingleThreadedExecutor::~SingleThreadedExecutor()
    {
        stop();
    }
    
    void SingleThreadedExecutor::spinSome()
    {
        auto sub = waitOneReady();
        if (!running_)
            return;
        if (sub)
        {
            handleSubscriber(std::move(sub));
        }
    }

    void SingleThreadedExecutor::handleSubscriber(std::shared_ptr<SubscriberBase> sub)
    {
        if (sub)
        {
            sub->takeAll();
        }
    }
    
    MultiThreadedExecutor::MultiThreadedExecutor(size_t threadNum)
        : thread_pool_(threadNum)
    {
    }
    
    MultiThreadedExecutor::~MultiThreadedExecutor()
    {
        stop();
    }
    
    void MultiThreadedExecutor::spinSome()
    {
        auto sub = waitOneReady();
        if (!running_)
            return;
        if (sub)
        {
            handleSubscriber(std::move(sub));
        }
    }

    void MultiThreadedExecutor::handleSubscriber(SubscriberBase* sub)
    {
        if (!sub)
            return;

        auto& group = sub->callbackGroup();
        if (group.type() == CallbackGroupType::MutuallyExclusive)
        {
            sub->takeAll();
        }
        else
        {
            thread_pool_.submit([sub]{ sub->takeAll(); });
        }
    }
    
    void MultiThreadedExecutor::stop()
    {
        if (running_.exchange(false))
        {
            ready_sem_.release();
            notifyCondition();
        }
        thread_pool_.close();
    }
    
    TimeOrderedExecutor::TimeOrderedExecutor(std::chrono::nanoseconds time_offset)
        : time_offset_(time_offset)
    {
        running_.store(false);
    }
    
    TimeOrderedExecutor::~TimeOrderedExecutor()
    {
        stop();
    }
    
    void TimeOrderedExecutor::spinSome()
    {
        auto sub = waitOneReady();
        if (sub)
        {
            handleSubscriber(sub);
        }
        processReadyEntries();
    }
    
    void TimeOrderedExecutor::spin()
    {
        if (running_.exchange(true))
            return;
    
        while (running_)
        {
            spinSome();
            if (!running_)
                break;
            doWait();
        }
    }
    
    void TimeOrderedExecutor::stop()
    {
        if (running_.exchange(false))
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
            buffer_.push(std::move(e));
        }
    }

    void TimeOrderedExecutor::processReadyEntries()
    {
        auto now_tp = std::chrono::steady_clock::now();
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now_tp.time_since_epoch()).count();

        uint64_t cutoff = 0;
        if (time_offset_.count() == 0)
        {
            cutoff = UINT64_MAX;
        }
        else
        {
            cutoff = now_ns - static_cast<uint64_t>(time_offset_.count());
        }

        while (!buffer_.empty() && buffer_.top().timestamp_ns <= cutoff)
        {
            auto& entry = buffer_.top();
            buffer_.pop();
            entry.invoker();
        }
    }

    void TimeOrderedExecutor::doWait()
    {
        if (buffer_.empty())
        {
            waitCondition();
            return;
        }
    
        const auto& top = buffer_.top();
        auto now_tp = std::chrono::steady_clock::now();
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now_tp.time_since_epoch()).count();
    
        uint64_t cutoff;
        if (time_offset_.count() == 0)
        {
            cutoff = UINT64_MAX;
        }
        else
        {
            cutoff = now_ns - static_cast<uint64_t>(time_offset_.count());
        }
    
        if (top.timestamp_ns <= cutoff)
        {
            return;
        }
    
        uint64_t earliest_ns = 0;
        if (time_offset_.count() == 0)
        {
            earliest_ns = top.timestamp_ns - now_ns;
        }
        else
        {
            earliest_ns = (top.timestamp_ns + static_cast<uint64_t>(time_offset_.count())) - now_ns;
        }
    
        auto wait_dur = std::chrono::nanoseconds(earliest_ns);
        if (wait_dur >= std::chrono::steady_clock::duration::max())
        {
            waitCondition();
            return;
        }
    
        std::unique_lock<std::mutex> lk(cv_mutex_);
        cv_.wait_for(lk, wait_dur,
            [this] { return !running_.load() || !buffer_.empty(); }
        );
    }
} // namespace lux::communication
