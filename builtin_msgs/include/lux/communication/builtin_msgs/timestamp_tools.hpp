#pragma once
#include <cstdint>
#include <lux/communication/visibility.h>

namespace lux::communication::builtin_msgs
{
	namespace common_msgs
	{
		class Timestamp;
	}

	LUX_COMMUNICATION_PUBLIC void timestamp_from_ns(common_msgs::Timestamp& timestamp, uint64_t time_ns);

	LUX_COMMUNICATION_PUBLIC uint64_t timestamp_to_ns(const common_msgs::Timestamp& timestamp);

	template<typename T>
	uint64_t message_get_ns_ts(const T& message)
	{
		return timestamp_to_ns(message.timestamp());
	}
}
