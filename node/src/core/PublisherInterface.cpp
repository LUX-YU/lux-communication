#include <lux/communication/core/Domain.hpp>
#include <lux/communication/core/PublisherInterface.hpp>
#include <lux/communication/core/Node.hpp>
#include <lux/communication/NodeBase.hpp>
#include <lux/communication/PublisherBase.hpp>

namespace lux::communication
{
	PublisherInterface::PublisherInterface(const std::string& topic, Node& node)
	{
		auto topic_handle = node.node_handle_->domain()->createOrGetTopic(topic);
		handle_	          = node.node_handle_->assignPublisher(node.node_handle_, std::move(topic_handle));
	}
}
