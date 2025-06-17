#include <lux/communication/core/CallbackGroupInterface.hpp>
#include <lux/communication/core/NodeInterface.hpp>
#include <lux/communication/NodeBase.hpp>

namespace lux::communication
{
	CallbackGroupInterface::CallbackGroupInterface(CallbackGroupType type, NodeInterface& node)
	{
		handle_ = node.node_handle_->assignCallbackGroup(type);
	}
}