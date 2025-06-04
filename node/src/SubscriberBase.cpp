#include "lux/communication/SubscriberBase.hpp"
#include "lux/communication/CallbackGroup.hpp"
#include "lux/communication/ITopicHolder.hpp"
#include "lux/communication/NodeBase.hpp"

namespace lux::communication {

    bool TimeExecEntry::operator<(const TimeExecEntry &rhs) const
    {
        return timestamp_ns > rhs.timestamp_ns;
    }

    const ITopicHolder& SubscriberBase::topic() const
    {
        return *topic_;
    }

    const CallbackGroup& SubscriberBase::callbackGroup() const
    {
        return *callback_group_;
    }

    ITopicHolder& SubscriberBase::topic()
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
