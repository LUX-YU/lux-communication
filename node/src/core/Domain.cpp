#include "lux/communication/core/Domain.hpp"
#include <lux/communication/TopicBase.hpp>
#include <lux/communication/NodeBase.hpp>

namespace lux::communication 
{
	topic_handle_t Domain::createOrGetTopic(const std::string& name)
	{
		return topic_reg_.emplace(name);
	}

	node_handle_t Domain::assignNode(const std::string& name)
	{
		auto handle = node_reg_.emplace(name, this);
	}
} // namespace lux::communication
