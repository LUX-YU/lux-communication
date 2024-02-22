#pragma once
#include <cstdint>
#include <lux/communication/visibility.h>

namespace lux::communication::builtin_msgs
{
	namespace common_msgs
	{
		class Timestamp;
	}

	LUX_COMMUNICATION_PUBLIC void set_timestamp(common_msgs::Timestamp& timestamp, uint64_t time_ns);
}