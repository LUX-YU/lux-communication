#include <lux/communication/NodeInterface.hpp>
#include <lux/communication/NodeBase.hpp>
#include <lux/communication/Domain.hpp>

namespace lux::communication
{
	NodeInterface::NodeInterface(std::string name, ENodeType type)
	{
		auto& domain = default_domain();
		node_handle_ = domain.assignNode(name);
	}

	NodeInterface::NodeInterface(std::string name, Domain& domain, ENodeType type)
	{
		node_handle_ = domain.assignNode(name);
	}

	const std::string& NodeInterface::name() const
	{
		return node_handle_->name();
	}

	ENodeType NodeInterface::type() const
	{
		return node_handle_->type();
	}
}