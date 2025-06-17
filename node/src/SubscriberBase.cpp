#include "lux/communication/SubscriberBase.hpp"
#include "lux/communication/CallbackGroupBase.hpp"
#include "lux/communication/TopicBase.hpp"
#include "lux/communication/NodeBase.hpp"

namespace lux::communication 
{
    bool TimeExecEntry::operator<(const TimeExecEntry &rhs) const
    {
        return timestamp_ns > rhs.timestamp_ns;
    }
    
    SubscriberBase::SubscriberBase(TopicSptr topic, NodeBase* node, CallbackGroupBase* cgb)
		: topic_(std::move(topic)), callback_group_(cgb), node_(node)
    {
        node_->addSubscriber(this);
        topic_->addSubscriber(this);
        callback_group_->addSubscriber(this);
    }

    SubscriberBase::~SubscriberBase()
    {
        callback_group_->removeSubscriber(this);
        topic_->removeSubscriber(this);
        node_->removeSubscriber(this);
    }
} // namespace lux::communication
