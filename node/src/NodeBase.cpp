#include <lux/communication/NodeBase.hpp>
#include <lux/communication/PublisherBase.hpp>
#include <lux/communication/SubscriberBase.hpp>

namespace lux::communication
{
	NodeBase::NodeBase(std::string name, Domain* domain, ENodeType type)
		: node_name_(std::move(name)), domain_(domain), type_(type)
	{
		default_callback_group_ = assignCallbackGroup(CallbackGroupType::MutuallyExclusive);
	}

	NodeBase::~NodeBase() = default;

	const std::string& NodeBase::name() const
	{
		return node_name_;
	}

	const Domain* NodeBase::domain() const
	{
		return domain_;
	}

	Domain* NodeBase::domain()
	{
		return domain_;
	}

	subscriber_handle_t NodeBase::assignSubscriber(node_handle_t nh, topic_handle_t th, callback_group_handle_t ch)
	{
		return sub_reg_.emplace(nh, th, ch);
	}

	publisher_handle_t NodeBase::assignPublisher(node_handle_t nh, topic_handle_t th)
	{
		return pub_reg_.emplace(nh, th);
	}

	callback_group_handle_t NodeBase::assignCallbackGroup(CallbackGroupType type)
	{
		return cbg_reg_.emplace(type);
	}
} // namespace lux::communication