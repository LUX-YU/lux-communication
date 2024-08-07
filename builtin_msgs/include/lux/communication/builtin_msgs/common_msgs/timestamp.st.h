#pragma once
#include <cstdint>
#include <lux/communication/visibility.h>

namespace lux::communication::builtin_msgs::common_msgs
{
	struct TimestampS
	{
		uint64_t secs;
		uint64_t nsecs;
	};

	LUX_COMMUNICATION_PUBLIC void timestamp_from_ns(common_msgs::TimestampS& timestamp, uint64_t time_ns);
	LUX_COMMUNICATION_PUBLIC uint64_t timestamp_to_ns(const common_msgs::TimestampS& timestamp);
}
