#include <lux/communication/builtin_msgs/timestamp_tools.hpp>
#include <lux/communication/builtin_msgs/common_msgs/timestamp.pb.h>

namespace lux::communication::builtin_msgs
{
	namespace common_msgs
	{
		class Timestamp;
	}

#define ONE_E_NINE 1000000000
	void timestamp_from_ns(common_msgs::Timestamp& timestamp, uint64_t time_ns)
	{
		timestamp.set_secs(time_ns / ONE_E_NINE);
		timestamp.set_nsecs(time_ns % ONE_E_NINE);
	}

	uint64_t timestamp_to_ns(const common_msgs::Timestamp& timestamp)
	{
		return timestamp.secs() * ONE_E_NINE + timestamp.nsecs();
	}
}
