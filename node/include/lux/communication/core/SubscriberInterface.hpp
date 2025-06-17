#pragma once
#include <lux/communication/core/Registry.hpp>
#include <lux/communication/visibility.h>

namespace lux::communication
{
	class Node;
	class CallbackGroup;

	class SubscriberBase;
	using subscriber_handle_t = typename Registry<SubscriberBase>::Handle;

	class LUX_COMMUNICATION_PUBLIC SubscriberInterface
	{
	private:
		SubscriberInterface(const std::string& topic, Node& node);
		SubscriberInterface(const std::string& topic, Node& node, CallbackGroup& callback_group);

		~SubscriberInterface();

		const std::string& topic() const;

	private:
		subscriber_handle_t handle_;
	};

	struct TimeExecEntry
	{
		uint64_t				timestamp_ns;
		std::function<void()>	invoker;

		bool operator<(const TimeExecEntry& rhs) const
		{
			return timestamp_ns > rhs.timestamp_ns;
		}
	};
}