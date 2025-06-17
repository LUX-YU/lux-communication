#pragma once
#include <memory>
#include <mutex>
#include <lux/communication/Registry.hpp>
#include <lux/communication/Node.hpp>
#include <lux/communication/CallbackGroup.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication
{
	class Domain;
	class NodeBase;
	class TopicBase;
	class PublisherBase;
	class SubscriberBase;
	class CallbackGroupBase;

	using node_registry_t			= Registry<NodeBase>;
	using topic_registry_t			= QueryableRegistry<TopicBase>;
	using subscriber_registry_t		= Registry<SubscriberBase>;
	using publisher_registry_t		= Registry<PublisherBase>;
	using callback_group_registry_t = Registry<CallbackGroupBase>;

	using node_handle_t				= typename node_registry_t::Handle;
	using topic_handle_t			= typename topic_registry_t::Handle;
	using subscriber_handle_t       = typename subscriber_registry_t::Handle;
	using publisher_handle_t		= typename publisher_registry_t::Handle;
	using callback_group_handle_t	= typename callback_group_registry_t::Handle;

	class NodeBaseInterface;
	class LUX_COMMUNICATION_PUBLIC NodeBase
	{
	public:
		NodeBase(std::string name, Domain* domain, ENodeType type);

		~NodeBase() = default;

		const std::string& name() const;

		const Domain* domain() const;
		Domain* domain();

		subscriber_handle_t      assignSubscriber(node_handle_t, topic_handle_t, callback_group_handle_t);
		publisher_handle_t		 assignPublisher(node_handle_t, topic_handle_t);
		callback_group_handle_t  assignCallbackGroup(CallbackGroupType type);

		ENodeType type() const
		{
			return type_;
		}

		callback_group_handle_t  defaultCallbackGroup()
		{
			return default_callback_group_;
		}

	private:
		callback_group_handle_t     default_callback_group_;
		std::string					node_name_;
		Domain*						domain_;
		ENodeType					type_;
		subscriber_registry_t		sub_reg_;
		publisher_registry_t        pub_reg_;
		callback_group_registry_t	cbg_reg_;
	};
}