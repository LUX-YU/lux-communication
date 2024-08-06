#include "lux/communication/Node.hpp"
#include "lux/communication/NodeImpl.hpp"

namespace lux::communication
{
	Node::Node(std::string_view name, int argc, char* argv[], int thread_num)
	{
		_impl = std::make_unique<NodeImpl>(name, argc, argv, thread_num);
	}

	Node::~Node()
	{

	}
}

