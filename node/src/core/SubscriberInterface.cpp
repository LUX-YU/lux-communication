#include <lux/communication/core/Domain.hpp>
#include <lux/communication/core/SubscriberInterface.hpp>
#include <lux/communication/core/CallbackGroup.hpp>
#include <lux/communication/core/Node.hpp>
#include <lux/communication/NodeBase.hpp>
#include <lux/communication/TopicBase.hpp>
#include <lux/communication/SubscriberBase.hpp>


namespace lux::communication
{
	SubscriberInterface::SubscriberInterface(const std::string& topic, Node& node)
	{
		auto topic_handle = node.node_handle_->domain()->createOrGetTopic(topic);
		handle_ = node.node_handle_->assignSubscriber(
			node.node_handle_, 
			std::move(topic_handle),
			node.node_handle_->defaultCallbackGroup()
		);
	}

	SubscriberInterface::SubscriberInterface(const std::string& topic, Node& node, CallbackGroup& callback_group)
	{
		auto topic_handle = node.node_handle_->domain()->createOrGetTopic(topic);
		handle_ = node.node_handle_->assignSubscriber(
			node.node_handle_,
			std::move(topic_handle),
			callback_group.handle_
		);
	}

	const std::string& SubscriberInterface::topic() const
	{
		return handle_->topic().name();
	}

	SubscriberInterface::~SubscriberInterface() = default;
}
