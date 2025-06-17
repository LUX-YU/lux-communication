#include "lux/communication/SubscriberBase.hpp"
#include "lux/communication/CallbackGroup.hpp"
#include "lux/communication/TopicBase.hpp"
#include "lux/communication/NodeBase.hpp"

namespace lux::communication {

    const TopicBase& SubscriberBase::topic() const
    {
        return *topic_;
    }

    const CallbackGroup& SubscriberBase::callbackGroup() const
    {
        return *callback_group_;
    }

    TopicBase& SubscriberBase::topic()
    {
        return *topic_;
    }

    CallbackGroup& SubscriberBase::callbackGroup()
    {
        return *callback_group_;
    }
    
    SubscriberBase::SubscriberBase(NodeBaseSptr node, TopicHolderSptr topic, CallbackGroupSptr callback_group)
		: topic_(std::move(topic)),
		callback_group_(std::move(callback_group)),
		node_(std::move(node)){}

    SubscriberBase::~SubscriberBase()
    {
		callback_group_->removeSubscriber(id_);
		node_->removeSubscriber(id_);
    }
    
    size_t SubscriberBase::id() const 
    { 
        return id_; 
    }

    void SubscriberBase::setId(size_t id)
    {
		id_ = id;
    }
} // namespace lux::communication
