#include "lux/communication/CallbackGroup.hpp"
#include "lux/communication/Executor.hpp"

namespace lux::communication {
    CallbackGroup::CallbackGroup(CallbackGroupType type)
        : type_(type) {}
    
    CallbackGroup::~CallbackGroup() = default;
    
    CallbackGroupType CallbackGroup::getType() const {
        return type_;
    }
    
    void CallbackGroup::addSubscriber(ISubscriberBase* sub)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!sub) return;
        subscribers_.insert(sub->getId(), sub);
    }
    
    void CallbackGroup::removeSubscriber(ISubscriberBase* sub)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!sub) return;
        subscribers_.erase(sub->getId());
        auto it = std::find(ready_list_.begin(), ready_list_.end(), sub);
        if (it != ready_list_.end())
        {
            ready_list_.erase(it);
        }
    }
    
    bool CallbackGroup::hasReadySubscribers() const
    {
        return has_ready_.load(std::memory_order_acquire);
    }
    
    void CallbackGroup::notify(ISubscriberBase* sub)
    {
        std::shared_ptr<Executor> ex;
        {
            std::lock_guard lk(mutex_);
            if (sub && sub->setReadyIfNot())
                ready_list_.push_back(sub);
            has_ready_.store(true, std::memory_order_release);
            ex = executor_.lock();
        }
        if (ex) ex->wakeup();
    }
    
    std::vector<ISubscriberBase*> CallbackGroup::collectReadySubscribers()
    {
        std::lock_guard lk(mutex_);
        has_ready_.store(false, std::memory_order_release);
        std::vector<ISubscriberBase*> out;
        out.swap(ready_list_);
        return out;
    }
    
    std::vector<ISubscriberBase*> CallbackGroup::collectAllSubscribers()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<ISubscriberBase*> result;
        result.reserve(subscribers_.size());
        for (auto& sb : subscribers_.values())
        {
            result.push_back(sb);
        }
        return result;
    }
    
    void CallbackGroup::setExecutor(std::shared_ptr<Executor> exec)
    {
        executor_ = exec;
    }
    
    std::shared_ptr<Executor> CallbackGroup::getExecutor() const
    {
        return executor_.lock();
    }
} // namespace lux::communication
