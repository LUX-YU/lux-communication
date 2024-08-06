#pragma once
#include <memory>
#include <string>
#include <lux/communication/visibility.h>

namespace lux::communication
{
	class NodeImpl;
	class Node
	{
	public:
		LUX_COMMUNICATION_PUBLIC Node(std::string_view name, int argc, char* argv[], int thread_num = 1);

		LUX_COMMUNICATION_PUBLIC ~Node();

	private:
		std::unique_ptr<NodeImpl> _impl;
	};
}
