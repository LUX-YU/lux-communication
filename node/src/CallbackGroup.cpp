#include "lux/communication/CallbackGroup.hpp"
#include "lux/communication/Executor.hpp"
#include "lux/communication/NodeBase.hpp"

namespace lux::communication {
    CallbackGroup::CallbackGroup(std::shared_ptr<NodeBase> node, CallbackGroupType type)
        : node_(std::move(node)), type_(type) {}
    
    CallbackGroup::~CallbackGroup()
    {
		node_->removeCallbackGroup(id());
    }
    
    CallbackGroupType CallbackGroup::type() const {
        return type_;
    }
    
    void CallbackGroup::addSubscriber(SubscriberSptr sub)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!sub) return;
		if (subscribers_.contains(sub->id())) {
			return; // Subscriber already exists
		}
		subscribers_[sub->id()] = sub;
    }
    
    void CallbackGroup::removeSubscriber(size_t sub_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.erase(sub_id);
    }
    
    bool CallbackGroup::hasReadySubscribers() const
    {
        return has_ready_.load(std::memory_order_acquire);
    }
    
    void CallbackGroup::notify(SubscriberSptr sub)
    {
        if (!sub) return;

        if (!sub->setReadyIfNot())
            return;

        std::shared_ptr<Executor> ex;

        {
            // std::lock_guard lk(mutex_);
            lux::communication::push(ready_list_, std::move(sub));
            ex = executor_.lock();
        }

        bool was_ready = has_ready_.exchange(true, std::memory_order_acq_rel);
        if (!was_ready && ex)
        {
            ex->wakeup();
        }
    }
    
    std::vector<SubscriberSptr> CallbackGroup::collectReadySubscribers()
    {
        has_ready_.store(false, std::memory_order_release);
        std::vector<SubscriberSptr> out;
        SubscriberSptr sub;
        while (ready_list_.try_dequeue(sub))
        {
            out.push_back(std::move(sub));
        }
		return out;
    }
    
    void CallbackGroup::setExecutor(std::shared_ptr<Executor> exec)
    {
        executor_ = exec;
    }
    
    std::shared_ptr<Executor> CallbackGroup::getExecutor() const
    {
        return executor_.lock();
    }

    size_t CallbackGroup::id() const
    {
		return id_;
    }

    void CallbackGroup::setId(size_t id)
    {
		id_ = id;
    }
} // namespace lux::communication
