#include "lux/communication/CallbackGroup.hpp"
#include "lux/communication/Executor.hpp"
#include "lux/communication/NodeBase.hpp"

namespace lux::communication {
    CallbackGroup::CallbackGroup(std::shared_ptr<NodeBase> node, CallbackGroupType type)
        : node_(std::move(node)), type_(type) {}
    
    CallbackGroup::~CallbackGroup() = default;
    
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
    
    void CallbackGroup::notify(SubscriberSptr sub)
    {
        if (!sub) return;

        if (!sub->setReadyIfNot())
            return;

        auto ex = executor_.lock();
        if (ex)
        {
            ex->enqueueReady(std::move(sub));
        }
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
