#pragma once
#include <lux/communication/core/Registry.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication
{
	class Domain;
	class NodeBase;
	using node_handle_t = typename Registry<NodeBase>::Handle;

	enum class ENodeType
	{
		INTRAPROCESS,
	};

	class LUX_COMMUNICATION_PUBLIC NodeInterface
	{
		friend class CallbackGroupInterface;
		friend class SubscriberInterface;
		friend class PublisherInterface;
	public:
		explicit NodeInterface(std::string name, ENodeType type);
		NodeInterface(std::string name, Domain& domain, ENodeType type);

		const std::string& name() const;

		ENodeType type() const;

	private:
		node_handle_t node_handle_;
	};
}