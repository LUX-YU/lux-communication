#include "lux/communication/CallbackGroupBase.hpp"
#include "lux/communication/ExecutorBase.hpp"
#include "lux/communication/NodeBase.hpp"
#include <lux/communication/SubscriberBase.hpp>

namespace lux::communication {
    CallbackGroupBase::CallbackGroupBase(NodeBase* node, CallbackGroupType type)
        : node_(std::move(node)), type_(type) 
    {
        node->addCallbackGroup(this);
    }

    CallbackGroupBase::~CallbackGroupBase()
    {
        node_->removeCallbackGroup(this);
    }

    CallbackGroupType CallbackGroupBase::type() const 
    {
        return type_;
    }

    void CallbackGroupBase::notify(SubscriberBase* sub)
    {
        if (!sub->setReadyIfNot())
        {
            return;
        }

        auto executor = sub->callbackGroup()->executor();
        if (executor) {
            executor->enqueueReady(sub);
        }
    }

    void CallbackGroupBase::addSubscriber(SubscriberBase* sub)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!sub) return;
        if (subscribers_.contains(sub->idInCallbackGroup())) {
            return; // Subscriber already exists
        }
        auto idx = subscribers_.insert(sub);
        sub->setIdIInCallbackGroup(idx);
    }

    void CallbackGroupBase::removeSubscriber(SubscriberBase* sub)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (subscribers_.erase(sub->idInCallbackGroup()))
        {
            sub->setIdIInCallbackGroup(std::numeric_limits<size_t>::max());
        }
    }
} // namespace lux::communication
