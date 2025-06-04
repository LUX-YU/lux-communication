#include "lux/communication/Executor.hpp"
#include "lux/communication/intraprocess/Node.hpp"

namespace lux::communication {
    Executor::Executor() : running_(false) {}
    Executor::~Executor() { stop(); }
    
    void Executor::addNode(std::shared_ptr<NodeBase> node)
    {
        if (!node) return;
        auto& groups = node->callbackGroups();
        std::lock_guard<std::mutex> lock(callback_groups_mutex_);
        for (auto& g : groups)
        {
            callback_groups_.push_back(g);
            g.lock()->setExecutor(shared_from_this());
        }
    }
    
    void Executor::removeNode(std::shared_ptr<NodeBase> node)
    {
        if (!node) return;
        // const auto& groups = node->callbackGroups();
        // std::lock_guard<std::mutex> lock(callback_groups_mutex_);
		// auto iter = std::find(
        //     callback_groups_.begin(), 
        //     callback_groups_.end(), 
        //     node->default_callback_group()
        // );
        // 
		// callback_groups_.erase(iter, callback_groups_.end());
    }
    
    void Executor::spin()
    {
        if (running_)
            return;
        running_ = true;
    
        while (running_)
        {
            spinSome();
            if (running_)
            {
                waitCondition();
            }
        }
    }
    
    void Executor::stop()
    {
        running_ = false;
        notifyCondition();
    }
    
    void Executor::wakeup()
    {
        notifyCondition();
    }
    
    void Executor::waitCondition()
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
    
    void Executor::notifyCondition()
    {
        std::lock_guard lk(cv_mutex_);
        cv_.notify_all();
    }
    
    bool Executor::checkRunnable()
    {
        std::lock_guard<std::mutex> lock(callback_groups_mutex_);
        for (auto& g : callback_groups_)
        {
            if (g.lock()->hasReadySubscribers())
            {
                return true;
            }
        }
        return false;
    }
    
    SingleThreadedExecutor::~SingleThreadedExecutor()
    {
        stop();
    }
    
    void SingleThreadedExecutor::spinSome()
    {
        std::vector<CallbackGroupSptr> groups_copy;
        {
            std::lock_guard<std::mutex> lock(callback_groups_mutex_);
            groups_copy.reserve(callback_groups_.size());
            for (auto& g : callback_groups_)
            {
                groups_copy.push_back(g.lock());
            }
        }
    
        for (auto& group : groups_copy)
        {
            auto readySubs = group->collectReadySubscribers();
            for (auto& sub : readySubs)
            {
                if (!running_)
                    break;
                sub->takeAll();
            }
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
        std::vector<CallbackGroupSptr> groups_copy;
        {
            std::lock_guard<std::mutex> lock(callback_groups_mutex_);
            groups_copy.reserve(callback_groups_.size());
            for (auto& g : callback_groups_)
            {
                groups_copy.push_back(g.lock());
            }
        }
    
        for (auto& group : groups_copy)
        {
            auto readySubs = group->collectReadySubscribers();
            if (readySubs.empty())
                continue;
    
            if (group->type() == CallbackGroupType::MutuallyExclusive)
            {
                for (auto& sub : readySubs)
                {
                    if (!running_)
                        break;
                    sub->takeAll();
                }
            }
            else
            {
                for (auto& sub : readySubs)
                {
                    if (!running_)
                        break;
                    thread_pool_.submit([sub] { sub->takeAll(); });
                }
            }
        }
    }
    
    void MultiThreadedExecutor::stop()
    {
        if (running_.exchange(false))
        {
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
    
    void TimeOrderedExecutor::addNode(std::shared_ptr<NodeBase> node)
    {
        if (!node) return;
        for (auto& g : node->callbackGroups())
        {
            if (g.lock()->type() == CallbackGroupType::Reentrant)
            {
                throw std::runtime_error("[TimeOrderedExecutor] Reentrant group not supported in single-thread time-order mode!");
            }
        }
        Executor::addNode(std::move(node));
    }
    
    void TimeOrderedExecutor::spinSome()
    {
        fetchReadyEntries();
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
    
    void TimeOrderedExecutor::fetchReadyEntries()
    {
        std::vector<CallbackGroupSptr> groups_copy;
        {
            std::lock_guard<std::mutex> lock(callback_groups_mutex_);
            groups_copy.reserve(callback_groups_.size());
            for (auto& g : callback_groups_)
            {
                groups_copy.push_back(g.lock());
            }
        }
    
        std::vector<TimeExecEntry> newEntries;
        newEntries.reserve(64);
    
        for (auto& group : groups_copy)
        {
            auto readySubs = group->collectReadySubscribers();
            for (auto& sub : readySubs)
            {
                sub->drainAll(newEntries);
            }
        }
    
        for (auto& e : newEntries)
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
