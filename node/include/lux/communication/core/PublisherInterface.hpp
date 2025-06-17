#pragma once
#include <lux/communication/core/Registry.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication
{
	class Node;

	class NodeBase;
	class PublisherBase;

	using publisher_handle_t  = typename Registry<PublisherBase>::Handle;
	using node_handle_t		  = typename Registry<NodeBase>::Handle;

	class LUX_COMMUNICATION_PUBLIC PublisherInterface
	{
	private:
		PublisherInterface(const std::string& topic, Node& node);

	private:
		publisher_handle_t handle_;
	};
}