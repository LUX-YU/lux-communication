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
    
    void CallbackGroup::notify(size_t sub_id)
    {
        std::shared_ptr<Executor> ex;
        {
            std::lock_guard lk(mutex_);
			auto sub = subscribers_.at(sub_id).lock();
            ex = executor_.lock();
            if (sub && sub->setReadyIfNot()) {
                ready_list_.push_back(sub);
                has_ready_.store(true, std::memory_order_release);
            }
        }
        if (ex)  ex->wakeup();
    }
    
    std::vector<SubscriberSptr> CallbackGroup::collectReadySubscribers()
    {
        std::lock_guard lk(mutex_);
        has_ready_.store(false, std::memory_order_release);
        std::vector<SubscriberSptr> out;
        out.assign(
            std::make_move_iterator(ready_list_.begin()),
            std::make_move_iterator(ready_list_.end())
        );
        ready_list_.clear();
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
